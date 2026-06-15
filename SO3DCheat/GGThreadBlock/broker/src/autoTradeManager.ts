import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// autoTradeManager:自动交易远控。全局一份配置(所有角色共用同一个开关 / 白名单 /
// 延时),不分角色。配置改了推给所有在线实例;新实例上线时把当前配置补推一次。
//
// 交易接受→锁定→确认的 lock-step 状态机仍在 DLL(recv 触发,管道往返跟不上),
// broker 只下发 setAutoTradeConfig,DLL 的 AutoTradeController 收下并执行。

export interface AutoTradeConfig {
    enabled: boolean;
    acceptAll: boolean;          // true = 来者不拒;false = 仅白名单
    autoLockConfirm: boolean;    // 对方锁定后自动锁定 + 确认
    acceptDelayMs: number;       // 收到请求到同意的延时(拟人)
    confirmDelayMs: number;      // 锁定到确认的延时
    whitelist: string[];         // acceptAll=false 时的允许角色名
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "auto_trade.json"
);

// 跟 DLL 的 AutoTradeController::SetConfig 一致的钳位区间。
const ACCEPT_DELAY_MIN = 0;
const ACCEPT_DELAY_MAX = 10000;
const CONFIRM_DELAY_MIN = 100;
const CONFIRM_DELAY_MAX = 5000;

export class AutoTradeManager {
    private config: AutoTradeConfig = defaultConfig();
    private registry: InstanceRegistry;
    // 已经推过当前配置的在线 pid。新 pid 出现就补推,pid 下线就移除。
    private pushedPids = new Set<number>();
    private onChange: () => void;

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.onChange = () => this.syncNewInstances();
        this.registry.on("change", this.onChange);
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                this.config = normalizeConfig(JSON.parse(raw));
                console.log(
                    `[autoTrade] loaded config enabled=${this.config.enabled} acceptAll=${this.config.acceptAll} wl=${this.config.whitelist.length}`
                );
            }
        } catch (e: any) {
            console.warn(`[autoTrade] load failed: ${e.message}`);
            this.config = defaultConfig();
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[autoTrade] save failed: ${e.message}`);
        }
    }

    // 新连上的实例补推当前配置(也清掉已下线 pid 的记录)。
    private syncNewInstances(): void {
        const online = new Set<number>();
        for (const inst of this.registry.list()) online.add(inst.pid);

        for (const pid of this.pushedPids) {
            if (!online.has(pid)) this.pushedPids.delete(pid);
        }
        for (const pid of online) {
            if (!this.pushedPids.has(pid)) this.pushTo(pid);
        }
    }

    private pushTo(pid: number): void {
        this.pushedPids.add(pid); // 乐观标记,避免 change 风暴里重复发
        this.registry
            .sendCommand(pid, "setAutoTradeConfig", this.config, 5000)
            .catch((e: any) => {
                this.pushedPids.delete(pid); // 失败回退,下一次 change 再补推
                console.warn(`[autoTrade] push pid=${pid} failed: ${e.message}`);
            });
    }

    private pushAll(): void {
        this.pushedPids.clear();
        for (const inst of this.registry.list()) this.pushTo(inst.pid);
    }

    // ---------------- HTTP 用的 CRUD ----------------

    getConfig(): AutoTradeConfig {
        return this.config;
    }

    setConfig(body: Partial<AutoTradeConfig>): AutoTradeConfig {
        this.config = normalizeConfig({ ...this.config, ...body });
        this.save();
        this.pushAll();
        return this.config;
    }

    destroy(): void {
        this.registry.off("change", this.onChange);
    }
}

function defaultConfig(): AutoTradeConfig {
    return {
        enabled: false,
        acceptAll: false,
        autoLockConfirm: true,
        acceptDelayMs: 600,
        confirmDelayMs: 300,
        whitelist: [],
    };
}

function normalizeConfig(c: any): AutoTradeConfig {
    const d = defaultConfig();
    const whitelist = Array.isArray(c?.whitelist)
        ? c.whitelist.map((n: unknown) => String(n ?? "").trim()).filter(Boolean)
        : d.whitelist;
    return {
        enabled: typeof c?.enabled === "boolean" ? c.enabled : d.enabled,
        acceptAll: typeof c?.acceptAll === "boolean" ? c.acceptAll : d.acceptAll,
        autoLockConfirm: typeof c?.autoLockConfirm === "boolean" ? c.autoLockConfirm : d.autoLockConfirm,
        acceptDelayMs: clampInt(c?.acceptDelayMs, d.acceptDelayMs, ACCEPT_DELAY_MIN, ACCEPT_DELAY_MAX),
        confirmDelayMs: clampInt(c?.confirmDelayMs, d.confirmDelayMs, CONFIRM_DELAY_MIN, CONFIRM_DELAY_MAX),
        whitelist,
    };
}

function clampInt(v: any, def: number, min: number, max: number): number {
    const n = Math.floor(Number(v));
    if (!Number.isFinite(n)) return def;
    return Math.min(max, Math.max(min, n));
}
