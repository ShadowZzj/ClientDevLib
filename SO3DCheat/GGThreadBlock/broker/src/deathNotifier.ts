import fs from "fs";
import path from "path";
import http from "http";
import https from "https";
import { URL } from "url";
import crypto from "crypto";
import { HttpsProxyAgent } from "https-proxy-agent";
import { InstanceRegistry } from "./instances";

export interface DeathNotifyConfig {
    phone: string;
    enabledCharacters: string[];   // 按角色名启用
    smsEnabled: boolean;           // 短信通道
    discordEnabled: boolean;       // discord 通道
    discordWebhook: string;        // 自定义 webhook;留空则用 DEFAULT_DISCORD_WEBHOOK
    discordProxy: string;          // discord 出站代理 (http://host:port);留空则读环境变量,再无就直连
}

interface PerInstanceState {
    phase: "armed" | "sent";
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "death_notify.json"
);

const SMS_API = "http://api.smsbao.com/sms";
const SMS_USER = "kira0918";
const SMS_PASS = crypto.createHash("md5").update("421d09c188ea42d0a63cf3a7af736200").digest("hex");

// 用户没填 webhook 就回落到这条;通道单独开关控制是否实际发。
const DEFAULT_DISCORD_WEBHOOK =
    "https://discord.com/api/webhooks/1506322199668068382/DKenTMNOmS_GDCmE8iGVYfKkdiDi1a1sMkEyrLB2MnXC6LB_hIEouODaKSCoVnUReq-I";

export class DeathNotifier {
    private config: DeathNotifyConfig = {
        phone: "",
        enabledCharacters: [],
        smsEnabled: true,           // 兼容旧 config:之前唯一通道就是短信,默认开
        discordEnabled: false,
        discordWebhook: "",
        discordProxy: "",
    };
    private states = new Map<number, PerInstanceState>();
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.start();
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                const parsed = JSON.parse(raw);
                this.config = {
                    phone: parsed.phone || "",
                    enabledCharacters: Array.isArray(parsed.enabledCharacters) ? parsed.enabledCharacters : [],
                    // 旧版只有 phone 字段,smsEnabled 默认 true 保持原行为
                    smsEnabled: parsed.smsEnabled !== undefined ? !!parsed.smsEnabled : true,
                    discordEnabled: !!parsed.discordEnabled,
                    discordWebhook: typeof parsed.discordWebhook === "string" ? parsed.discordWebhook : "",
                    discordProxy: typeof parsed.discordProxy === "string" ? parsed.discordProxy : "",
                };
                console.log(
                    `[deathNotifier] loaded: phone=${this.config.phone} sms=${this.config.smsEnabled} ` +
                    `discord=${this.config.discordEnabled} chars=[${this.config.enabledCharacters.join(",")}]`
                );
            }
        } catch (e: any) {
            console.warn(`[deathNotifier] load failed: ${e.message}`);
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[deathNotifier] save failed: ${e.message}`);
        }
    }

    private start(): void {
        this.timer = setInterval(() => this.tick(), 5000);
    }

    private tick(): void {
        if (this.config.enabledCharacters.length === 0) return;
        // 两个通道都关掉就没必要扫
        if (!this.config.smsEnabled && !this.config.discordEnabled) return;

        const online = this.registry.list();
        const activePids = new Set(online.map((i) => i.pid));

        for (const pid of this.states.keys()) {
            if (!activePids.has(pid)) this.states.delete(pid);
        }

        for (const inst of online) {
            if (!inst.characterName || inst.status.hp === undefined) continue;
            if (inst.status.hpKnown !== true || inst.status.clientClosing === true) continue;
            if (!this.config.enabledCharacters.includes(inst.characterName)) continue;

            let st = this.states.get(inst.pid);
            if (!st) {
                st = { phase: "armed" };
                this.states.set(inst.pid, st);
            }

            if (inst.status.hp === 0 && st.phase === "armed") {
                st.phase = "sent";
                console.log(`[deathNotifier] death detected: ${inst.characterName} (pid=${inst.pid})`);

                if (this.config.smsEnabled && this.config.phone) {
                    // 短信走老的"验证码"模板,内容审核才通得过
                    const smsContent = `【短信宝】您的验证码是${inst.characterName}`;
                    this.sendSms(this.config.phone, smsContent);
                }
                if (this.config.discordEnabled) {
                    const webhook = this.config.discordWebhook.trim() || DEFAULT_DISCORD_WEBHOOK;
                    const msg = `您的角色${inst.characterName}已经死亡！`;
                    this.sendDiscord(webhook, msg);
                }
            } else if (inst.status.hp > 0 && st.phase === "sent") {
                st.phase = "armed";
                console.log(`[deathNotifier] re-armed for ${inst.characterName} (pid=${inst.pid})`);
            }
        }
    }

    private sendSms(phone: string, content: string): void {
        const params = new URLSearchParams({
            u: SMS_USER,
            p: SMS_PASS,
            m: phone,
            c: content,
        });
        const url = `${SMS_API}?${params.toString()}`;
        http.get(url, (res) => {
            let body = "";
            res.on("data", (chunk) => (body += chunk));
            res.on("end", () => {
                console.log(`[deathNotifier] SMS result: ${body.trim()}`);
            });
        }).on("error", (e) => {
            console.warn(`[deathNotifier] SMS request failed: ${e.message}`);
        });
    }

    private sendDiscord(webhookUrl: string, content: string): void {
        let parsed: URL;
        try {
            parsed = new URL(webhookUrl);
        } catch {
            console.warn(`[deathNotifier] discord webhook URL invalid: ${webhookUrl}`);
            return;
        }
        const lib = parsed.protocol === "http:" ? http : https;
        const payload = JSON.stringify({ content });

        // 代理优先级: UI 配置 > HTTPS_PROXY / HTTP_PROXY / ALL_PROXY 环境变量。
        // Node 默认无视系统代理(Windows IE/全局代理对 Node 没用),不挂 agent
        // 就是直连,所以国内访问 discord.com 会 ETIMEDOUT。
        const proxyUrl =
            this.config.discordProxy.trim() ||
            process.env.HTTPS_PROXY ||
            process.env.https_proxy ||
            process.env.HTTP_PROXY ||
            process.env.http_proxy ||
            process.env.ALL_PROXY ||
            process.env.all_proxy ||
            "";

        // HttpsProxyAgent 的类型不是 http.Agent 子类(它内部走 CONNECT 隧道),
        // 但运行时 Node 接受任何带 addRequest/createConnection 的对象作为 agent;
        // 这里直接 any 兜底,避免 TS 把 agent 字段卡死成 http.Agent。
        let agent: any;
        if (proxyUrl) {
            try {
                agent = new HttpsProxyAgent(proxyUrl);
                console.log(`[deathNotifier] discord using proxy ${proxyUrl}`);
            } catch (e: any) {
                console.warn(`[deathNotifier] discord proxy URL invalid (${proxyUrl}): ${e.message} — fallback direct`);
            }
        }

        const req = lib.request(
            {
                method: "POST",
                protocol: parsed.protocol,
                hostname: parsed.hostname,
                port: parsed.port || (parsed.protocol === "http:" ? 80 : 443),
                path: parsed.pathname + parsed.search,
                agent,
                headers: {
                    "Content-Type": "application/json",
                    "Content-Length": Buffer.byteLength(payload),
                },
            },
            (res) => {
                let body = "";
                res.on("data", (chunk) => (body += chunk));
                res.on("end", () => {
                    // Discord 成功是 204 No Content,失败会带 json body
                    if (res.statusCode && res.statusCode >= 200 && res.statusCode < 300) {
                        console.log(`[deathNotifier] discord ok (${res.statusCode})`);
                    } else {
                        console.warn(`[deathNotifier] discord ${res.statusCode}: ${body.trim()}`);
                    }
                });
            }
        );
        req.on("error", (e) => {
            console.warn(`[deathNotifier] discord request failed: ${e.message}`);
        });
        req.write(payload);
        req.end();
    }

    getConfig(): DeathNotifyConfig {
        return {
            ...this.config,
            enabledCharacters: [...this.config.enabledCharacters],
        };
    }

    setConfig(cfg: Partial<DeathNotifyConfig>): void {
        if (cfg.phone !== undefined) this.config.phone = cfg.phone;
        if (cfg.enabledCharacters !== undefined) this.config.enabledCharacters = cfg.enabledCharacters;
        if (cfg.smsEnabled !== undefined) this.config.smsEnabled = !!cfg.smsEnabled;
        if (cfg.discordEnabled !== undefined) this.config.discordEnabled = !!cfg.discordEnabled;
        if (cfg.discordWebhook !== undefined) this.config.discordWebhook = String(cfg.discordWebhook);
        if (cfg.discordProxy !== undefined) this.config.discordProxy = String(cfg.discordProxy);
        this.save();
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
    }
}
