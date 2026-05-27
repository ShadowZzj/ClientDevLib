import fs from "fs";
import path from "path";
import https from "https";
import http from "http";
import { URL } from "url";
import { EventEmitter } from "events";
import { HttpsProxyAgent } from "https-proxy-agent";
import { InstanceRegistry } from "./instances";

// 从 broker 收到的 chat 事件 payload (instances.ts onChat emit 的形状)
interface ChatEvent {
    pid: number;
    characterName?: string;
    sender: string;
    message: string;
    timestamp: number;
    senderUserId?: number;
    channelId?: number;
}

export interface GmReplierConfig {
    enabled: boolean;
    gmNames: string[];           // GM 角色名列表(精确匹配 sender)
    apiBaseUrl: string;          // 默认 https://timicc.com
    apiKey: string;              // sk-... ; 走 Anthropic 兼容头 x-api-key
    model: string;               // claude-sonnet-4-6
    proxy: string;               // 出站代理(可选)
    cooldownMs: number;          // 同一 GM 同一 pid 两次回复间隔下限,防触发刷屏
    systemPrompt: string;        // 给模型的 system prompt
    timeoutMs: number;           // 单次请求超时
}

export interface GmReplyLogEntry {
    timestamp: number;
    pid: number;
    characterName?: string;
    sender: string;          // GM 名
    incoming: string;        // GM 消息原文
    outgoing?: string;       // 模型答案
    error?: string;
    skipped?: string;        // 命中规则但被冷却/未 @ 自己等原因跳过
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "gm_replier.json"
);

const MAX_LOG_ENTRIES = 200;

const DEFAULT_SYSTEM_PROMPT =
    "你正在扮演一个游戏内挂机玩家,GM 在公屏问你问题以判断你是不是真人。" +
    "请用最短的中文(1~15 字)直接给出答案,不要解释,不要客套,不要用括号,不要加角色名。" +
    "如果是数学题直接给数字;如果是常识题直接答案;如果完全看不懂就回复'在'。";

export class GmReplier extends EventEmitter {
    private config: GmReplierConfig = {
        enabled: false,
        gmNames: [],
        apiBaseUrl: "https://timicc.com",
        apiKey: "",
        model: "claude-sonnet-4-6",
        proxy: "",
        cooldownMs: 8000,
        systemPrompt: DEFAULT_SYSTEM_PROMPT,
        timeoutMs: 20000,
    };
    private registry: InstanceRegistry;
    private logs: GmReplyLogEntry[] = [];
    // (pid, gmName) -> last reply ts ; 避免同一 GM 短时间内被反复触发
    private lastReplyAt = new Map<string, number>();

    constructor(registry: InstanceRegistry) {
        super();
        this.registry = registry;
        this.load();

        registry.on("chat", (msg: ChatEvent) => {
            // 异步处理,handleChat 内部自己 catch 防止冒上来打死 EventEmitter
            this.handleChat(msg).catch((e) => {
                console.warn(`[gmReplier] handleChat error: ${e?.message ?? e}`);
            });
        });
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                const parsed = JSON.parse(raw);
                this.config = {
                    enabled: !!parsed.enabled,
                    gmNames: Array.isArray(parsed.gmNames) ? parsed.gmNames.filter((s: any) => typeof s === "string" && s) : [],
                    apiBaseUrl: typeof parsed.apiBaseUrl === "string" && parsed.apiBaseUrl ? parsed.apiBaseUrl : "https://timicc.com",
                    apiKey: typeof parsed.apiKey === "string" ? parsed.apiKey : "",
                    model: typeof parsed.model === "string" && parsed.model ? parsed.model : "claude-sonnet-4-6",
                    proxy: typeof parsed.proxy === "string" ? parsed.proxy : "",
                    cooldownMs: typeof parsed.cooldownMs === "number" && parsed.cooldownMs >= 0 ? parsed.cooldownMs : 8000,
                    systemPrompt: typeof parsed.systemPrompt === "string" && parsed.systemPrompt ? parsed.systemPrompt : DEFAULT_SYSTEM_PROMPT,
                    timeoutMs: typeof parsed.timeoutMs === "number" && parsed.timeoutMs > 0 ? parsed.timeoutMs : 20000,
                };
                console.log(
                    `[gmReplier] loaded: enabled=${this.config.enabled} model=${this.config.model} ` +
                    `gms=[${this.config.gmNames.join(",")}]`
                );
            }
        } catch (e: any) {
            console.warn(`[gmReplier] load failed: ${e.message}`);
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[gmReplier] save failed: ${e.message}`);
        }
    }

    private addLog(entry: GmReplyLogEntry): void {
        this.logs.push(entry);
        if (this.logs.length > MAX_LOG_ENTRIES) {
            this.logs.splice(0, this.logs.length - MAX_LOG_ENTRIES);
        }
        this.emit("log", entry);
    }

    private async handleChat(msg: ChatEvent): Promise<void> {
        if (!this.config.enabled) return;
        if (!msg.sender || !msg.message) return;
        if (this.config.gmNames.length === 0) return;
        // 精确匹配 GM 名;原始 sender 可能是 "User#<id>" (AOI 外查不到名),那种永远不会命中
        if (!this.config.gmNames.includes(msg.sender)) return;

        // 必须包含本实例自己的角色名,才算"在问我"。多账号挂机时只让被点到的那个回。
        const myName = msg.characterName || "";
        if (!myName) {
            this.addLog({
                timestamp: Date.now(), pid: msg.pid, characterName: myName,
                sender: msg.sender, incoming: msg.message,
                skipped: "本实例还没认到角色名,跳过",
            });
            return;
        }
        if (!msg.message.includes(myName)) {
            // GM 在问别人,不是问我;静默不打日志(否则 GM 一句话刷一片日志)
            return;
        }

        // 冷却:同 GM 对同 pid 8 秒内只回一次,防止 GM 连发触发 N 次 API + N 条公屏
        const cooldownKey = `${msg.pid}|${msg.sender}`;
        const now = Date.now();
        const last = this.lastReplyAt.get(cooldownKey) || 0;
        if (now - last < this.config.cooldownMs) {
            this.addLog({
                timestamp: now, pid: msg.pid, characterName: myName,
                sender: msg.sender, incoming: msg.message,
                skipped: `冷却中 (剩 ${this.config.cooldownMs - (now - last)}ms)`,
            });
            return;
        }
        // 先占位,防止并发的两条消息都通过冷却检查发出两次
        this.lastReplyAt.set(cooldownKey, now);

        if (!this.config.apiKey) {
            this.addLog({
                timestamp: now, pid: msg.pid, characterName: myName,
                sender: msg.sender, incoming: msg.message,
                error: "未配置 apiKey",
            });
            return;
        }

        let answer: string;
        try {
            answer = await this.callAnthropic(msg.message, myName);
        } catch (e: any) {
            this.addLog({
                timestamp: Date.now(), pid: msg.pid, characterName: myName,
                sender: msg.sender, incoming: msg.message,
                error: `API 失败: ${e?.message ?? e}`,
            });
            return;
        }

        const trimmed = answer.trim();
        if (!trimmed) {
            this.addLog({
                timestamp: Date.now(), pid: msg.pid, characterName: myName,
                sender: msg.sender, incoming: msg.message,
                error: "模型返回空内容",
            });
            return;
        }

        // 公屏字段限长,DLL 端硬上限 200 字节;再加上中文 UTF-8 多 byte,这里裁到 60 字符
        const reply = trimmed.length > 60 ? trimmed.slice(0, 60) : trimmed;

        try {
            const r = await this.registry.sendCommand(msg.pid, "sendPublicChat", { message: reply }, 8000);
            if (!r.ok) {
                this.addLog({
                    timestamp: Date.now(), pid: msg.pid, characterName: myName,
                    sender: msg.sender, incoming: msg.message, outgoing: reply,
                    error: `sendPublicChat 拒绝: ${r.detail || ""}`,
                });
                return;
            }
        } catch (e: any) {
            this.addLog({
                timestamp: Date.now(), pid: msg.pid, characterName: myName,
                sender: msg.sender, incoming: msg.message, outgoing: reply,
                error: `sendPublicChat 异常: ${e?.message ?? e}`,
            });
            return;
        }

        this.addLog({
            timestamp: Date.now(), pid: msg.pid, characterName: myName,
            sender: msg.sender, incoming: msg.message, outgoing: reply,
        });
    }

    // 调 Anthropic /v1/messages。timicc.com 是 Anthropic 兼容代理;
    // 鉴权头跟官方一致 (x-api-key + anthropic-version)。返回纯文本答案。
    private callAnthropic(userMessage: string, myName: string): Promise<string> {
        return new Promise((resolve, reject) => {
            let parsedBase: URL;
            try {
                parsedBase = new URL(this.config.apiBaseUrl);
            } catch (e: any) {
                return reject(new Error(`apiBaseUrl 无效: ${e?.message ?? e}`));
            }
            const lib = parsedBase.protocol === "http:" ? http : https;

            const body = JSON.stringify({
                model: this.config.model,
                max_tokens: 256,
                system: this.config.systemPrompt,
                messages: [
                    {
                        role: "user",
                        content:
                            `我的角色名是「${myName}」,GM 刚在公屏问:「${userMessage}」。\n` +
                            `请直接给出最短的回答(只输出答案本身)。`,
                    },
                ],
            });

            // Anthropic 官方走 /v1/messages;timicc.com 同样路径。
            // 如果 baseUrl 已经带 path (例如 https://x.com/anthropic) 也保留。
            const basePath = parsedBase.pathname.replace(/\/+$/, "");
            const fullPath = basePath + "/v1/messages";

            // 代理:UI 配置 > 环境变量,跟 deathNotifier 一致
            const proxyUrl =
                this.config.proxy.trim() ||
                process.env.HTTPS_PROXY ||
                process.env.https_proxy ||
                process.env.HTTP_PROXY ||
                process.env.http_proxy ||
                "";
            let agent: any;
            if (proxyUrl) {
                try {
                    agent = new HttpsProxyAgent(proxyUrl);
                } catch (e: any) {
                    console.warn(`[gmReplier] proxy URL 无效 (${proxyUrl}): ${e.message} — 直连`);
                }
            }

            const req = lib.request(
                {
                    method: "POST",
                    protocol: parsedBase.protocol,
                    hostname: parsedBase.hostname,
                    port: parsedBase.port || (parsedBase.protocol === "http:" ? 80 : 443),
                    path: fullPath,
                    agent,
                    headers: {
                        "Content-Type": "application/json",
                        "x-api-key": this.config.apiKey,
                        "anthropic-version": "2023-06-01",
                        "Content-Length": Buffer.byteLength(body),
                    },
                },
                (res) => {
                    let buf = "";
                    res.setEncoding("utf8");
                    res.on("data", (chunk) => (buf += chunk));
                    res.on("end", () => {
                        if (!res.statusCode || res.statusCode < 200 || res.statusCode >= 300) {
                            return reject(new Error(`HTTP ${res.statusCode}: ${buf.slice(0, 300)}`));
                        }
                        try {
                            const j = JSON.parse(buf);
                            // Anthropic 标准响应:content 是 array of {type:"text", text:"..."}
                            const text = Array.isArray(j.content)
                                ? j.content
                                      .filter((c: any) => c && c.type === "text" && typeof c.text === "string")
                                      .map((c: any) => c.text)
                                      .join("")
                                : "";
                            if (!text) return reject(new Error(`无 text content: ${buf.slice(0, 300)}`));
                            resolve(text);
                        } catch (e: any) {
                            reject(new Error(`解析响应失败: ${e?.message ?? e}`));
                        }
                    });
                }
            );
            req.setTimeout(this.config.timeoutMs, () => {
                req.destroy(new Error(`请求超时 ${this.config.timeoutMs}ms`));
            });
            req.on("error", reject);
            req.write(body);
            req.end();
        });
    }

    getConfig(): GmReplierConfig {
        // 不把 apiKey 暴露到前端,改成 mask;前端要更新就明文 PUT
        return {
            ...this.config,
            apiKey: this.config.apiKey ? `${this.config.apiKey.slice(0, 6)}***${this.config.apiKey.slice(-4)}` : "",
            gmNames: [...this.config.gmNames],
        };
    }

    setConfig(cfg: Partial<GmReplierConfig>): void {
        if (cfg.enabled !== undefined) this.config.enabled = !!cfg.enabled;
        if (cfg.gmNames !== undefined && Array.isArray(cfg.gmNames)) {
            this.config.gmNames = cfg.gmNames.filter((s: any) => typeof s === "string" && s.trim()).map((s) => s.trim());
        }
        if (cfg.apiBaseUrl !== undefined && cfg.apiBaseUrl) this.config.apiBaseUrl = String(cfg.apiBaseUrl).trim();
        // apiKey: 前端发上来 mask 串(含 ***)就忽略,只接受真正的明文
        if (cfg.apiKey !== undefined && typeof cfg.apiKey === "string" && !cfg.apiKey.includes("***")) {
            this.config.apiKey = cfg.apiKey.trim();
        }
        if (cfg.model !== undefined && cfg.model) this.config.model = String(cfg.model).trim();
        if (cfg.proxy !== undefined) this.config.proxy = String(cfg.proxy);
        if (cfg.cooldownMs !== undefined && Number.isFinite(cfg.cooldownMs) && cfg.cooldownMs >= 0) {
            this.config.cooldownMs = Math.floor(cfg.cooldownMs);
        }
        if (cfg.systemPrompt !== undefined && typeof cfg.systemPrompt === "string" && cfg.systemPrompt.trim()) {
            this.config.systemPrompt = cfg.systemPrompt;
        }
        if (cfg.timeoutMs !== undefined && Number.isFinite(cfg.timeoutMs) && cfg.timeoutMs > 0) {
            this.config.timeoutMs = Math.floor(cfg.timeoutMs);
        }
        this.save();
    }

    getLogs(): GmReplyLogEntry[] {
        return [...this.logs];
    }

    clearLogs(): void {
        this.logs = [];
    }

    destroy(): void {
        // 当前没启 timer,留着 hook 一致
    }
}
