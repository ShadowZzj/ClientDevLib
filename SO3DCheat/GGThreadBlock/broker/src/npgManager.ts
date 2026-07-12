import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// npgManager:附近玩家停手 (NPG) 远控。全局一份配置(所有角色共用同一个开关 +
// 保持停手时长),不分角色。配置改了推给所有在线实例;新实例上线时补推一次。
//
// 扫描 / 暂停模块的状态机仍在 DLL 的 NearbyPlayerGuardModule(独立 worker 线程),
// broker 只下发 setNpgConfig,DLL 收下后 ApplyRemoteConfig 立即生效。

export interface NpgConfig {
    enabled: boolean;      // 总开关:关掉后不再扫描、不再停手
    holdSeconds: number;   // 看到人后保持停手的最短秒数;0 = 无人即恢复(旧行为)
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "npg_config.json"
);

// 0 = 立即恢复;上限 2 小时,够覆盖任何挂机场景。
const HOLD_MIN = 0;
const HOLD_MAX = 7200;

export class NpgManager {
    private config: NpgConfig = defaultConfig();
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
                    `[npg] loaded config enabled=${this.config.enabled} holdSeconds=${this.config.holdSeconds}`
                );
            }
        } catch (e: any) {
            console.warn(`[npg] load failed: ${e.message}`);
            this.config = defaultConfig();
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[npg] save failed: ${e.message}`);
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
            .sendCommand(pid, "setNpgConfig", this.config, 5000)
            .then((r) => {
                // DLL 早期连上时 NPG 模块可能还没注册,ack ok=false。撤销标记,
                // 下一次 change(心跳 / 状态变化)会再补推,直到模块就绪。
                if (!r || !r.ok) this.pushedPids.delete(pid);
            })
            .catch((e: any) => {
                this.pushedPids.delete(pid); // 失败回退,下一次 change 再补推
                console.warn(`[npg] push pid=${pid} failed: ${e.message}`);
            });
    }

    private pushAll(): void {
        this.pushedPids.clear();
        for (const inst of this.registry.list()) this.pushTo(inst.pid);
    }

    // ---------------- HTTP 用的 CRUD ----------------

    getConfig(): NpgConfig {
        return this.config;
    }

    setConfig(body: Partial<NpgConfig>): NpgConfig {
        this.config = normalizeConfig({ ...this.config, ...body });
        this.save();
        this.pushAll();
        return this.config;
    }

    destroy(): void {
        this.registry.off("change", this.onChange);
    }
}

function defaultConfig(): NpgConfig {
    return {
        enabled: true,
        holdSeconds: 300,
    };
}

function normalizeConfig(c: any): NpgConfig {
    const d = defaultConfig();
    return {
        enabled: typeof c?.enabled === "boolean" ? c.enabled : d.enabled,
        holdSeconds: clampInt(c?.holdSeconds, d.holdSeconds, HOLD_MIN, HOLD_MAX),
    };
}

function clampInt(v: any, def: number, min: number, max: number): number {
    const n = Math.floor(Number(v));
    if (!Number.isFinite(n)) return def;
    return Math.min(max, Math.max(min, n));
}
