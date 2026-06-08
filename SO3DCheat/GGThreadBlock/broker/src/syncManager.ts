import fs from "fs";
import path from "path";
import { InstanceRegistry, DialogSelectEvent } from "./instances";

// syncManager:指定一个主角色 + 多个副角色,副角色镜像主角色的动作。
// 只同步两类动作:
//   1. 移动 —— 跟随主角色「当前位置」。复用已有的 status 遥测(主角色每 1.5s 上报
//      posX/posY),follow tick 把主角色当前坐标 moveTo 给每个副角色,引擎自己寻路。
//      主角色没动就不重发(位置去重),免得刷命令。
//   2. NPC 对话 —— 主角色每发一次 CG_NPC_DIALOG_SELECT(411026),DLL 经 NetLog hook
//      上报 dialogSelect 帧;这里立即并发重放 sendDialogSelectRaw 给所有在线副角色,
//      npcId/option/sub 与主角色逐字节一致。不排队,立即扇出。
//
// 跟 buffKeeper 一样:DLL 不在线就跳过,不阻塞。主≠副,主角色不会镜像给自己。

export interface SyncConfig {
    enabled: boolean;
    masterName: string;
    slaveNames: string[];
    followIntervalMs: number;   // 位置跟随轮询间隔
    mirrorPosition: boolean;    // 镜像移动(自动寻路到主角色当前坐标)
    mirrorDialog: boolean;      // 镜像 NPC 对话选择
    posEpsilon: number;         // 主角色移动超过这个距离才重新下发 moveTo
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "sync_config.json"
);

const DEFAULT_FOLLOW_MS = 1500;
const MIN_FOLLOW_MS = 800;
const DEFAULT_POS_EPSILON = 1.0;

export class SyncManager {
    private config: SyncConfig = defaultConfig();
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    // 上一次扇给副角色的主角色坐标,用于去重:主角色没动就不重发 moveTo。
    private lastPushedPos: { x: number; y: number } | null = null;
    private followInFlight = false;
    private onDialogSelect: (ev: DialogSelectEvent) => void;

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.onDialogSelect = (ev) => this.handleDialogSelect(ev);
        this.registry.on("dialogSelect", this.onDialogSelect);
        this.start();
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                const parsed = JSON.parse(raw) as Partial<SyncConfig>;
                this.config = normalizeConfig(parsed);
                console.log(
                    `[sync] loaded config master=${this.config.masterName || "(none)"} slaves=${this.config.slaveNames.length}`
                );
            }
        } catch (e: any) {
            console.warn(`[sync] load failed: ${e.message}`);
            this.config = defaultConfig();
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[sync] save failed: ${e.message}`);
        }
    }

    private start(): void {
        const interval = Math.max(MIN_FOLLOW_MS, this.config.followIntervalMs || DEFAULT_FOLLOW_MS);
        if (this.timer) clearInterval(this.timer);
        this.timer = setInterval(() => this.followTick(), interval);
    }

    // 在线且 characterName 唯一映射到 pid。副角色排除主角色自身。
    private onlineSlavePids(): number[] {
        const master = this.config.masterName;
        const wanted = new Set(this.config.slaveNames.filter((n) => n && n !== master));
        const pids: number[] = [];
        for (const inst of this.registry.list()) {
            if (inst.characterName && wanted.has(inst.characterName)) pids.push(inst.pid);
        }
        return pids;
    }

    private masterInstance() {
        const name = this.config.masterName;
        if (!name) return undefined;
        return this.registry.list().find((i) => i.characterName === name);
    }

    private async followTick(): Promise<void> {
        if (!this.config.enabled || !this.config.mirrorPosition) return;
        if (this.followInFlight) return;

        const master = this.masterInstance();
        if (!master) return;
        const x = master.status.posX;
        const y = master.status.posY;
        if (typeof x !== "number" || typeof y !== "number") return;

        // 主角色没动(在 epsilon 内)就不重发,避免站桩时狂刷 moveTo。
        const eps = this.config.posEpsilon > 0 ? this.config.posEpsilon : DEFAULT_POS_EPSILON;
        if (this.lastPushedPos) {
            const dx = x - this.lastPushedPos.x;
            const dy = y - this.lastPushedPos.y;
            if (dx * dx + dy * dy < eps * eps) return;
        }

        const pids = this.onlineSlavePids();
        if (pids.length === 0) return;

        this.lastPushedPos = { x, y };
        this.followInFlight = true;
        try {
            await Promise.all(
                pids.map((pid) =>
                    this.registry
                        .sendCommand(pid, "moveTo", { x, y, action: 1 }, 4000)
                        .catch((e: any) => {
                            console.warn(`[sync] moveTo pid=${pid} failed: ${e.message}`);
                        })
                )
            );
        } finally {
            this.followInFlight = false;
        }
    }

    private handleDialogSelect(ev: DialogSelectEvent): void {
        if (!this.config.enabled || !this.config.mirrorDialog) return;
        if (!ev.characterName || ev.characterName !== this.config.masterName) return;

        const pids = this.onlineSlavePids();
        if (pids.length === 0) return;

        console.log(
            `[sync] master ${ev.characterName} dialogSelect npc=${ev.npc} opt=${ev.opt} sub=${ev.sub} → ${pids.length} slave(s)`
        );
        // 立即并发重放,不排队。
        for (const pid of pids) {
            this.registry
                .sendCommand(
                    pid,
                    "sendDialogSelectRaw",
                    { npcId: ev.npc, option: ev.opt, sub: ev.sub },
                    5000
                )
                .catch((e: any) => {
                    console.warn(`[sync] dialogSelect replay pid=${pid} failed: ${e.message}`);
                });
        }
    }

    // ---------------- HTTP 用的 CRUD ----------------

    getConfig(): SyncConfig {
        return this.config;
    }

    setConfig(body: Partial<SyncConfig>): SyncConfig {
        const next = normalizeConfig({ ...this.config, ...body });
        const intervalChanged = next.followIntervalMs !== this.config.followIntervalMs;
        this.config = next;
        // 主/副改了就清掉去重缓存,下一拍重新对齐。
        this.lastPushedPos = null;
        this.save();
        if (intervalChanged) this.start();
        return this.config;
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
        this.registry.off("dialogSelect", this.onDialogSelect);
    }
}

function defaultConfig(): SyncConfig {
    return {
        enabled: false,
        masterName: "",
        slaveNames: [],
        followIntervalMs: DEFAULT_FOLLOW_MS,
        mirrorPosition: true,
        mirrorDialog: true,
        posEpsilon: DEFAULT_POS_EPSILON,
    };
}

function normalizeConfig(c: Partial<SyncConfig>): SyncConfig {
    const slaves = Array.isArray(c.slaveNames)
        ? Array.from(new Set(c.slaveNames.filter((n): n is string => typeof n === "string" && !!n)))
        : [];
    const master = typeof c.masterName === "string" ? c.masterName : "";
    return {
        enabled: !!c.enabled,
        masterName: master,
        // 主角色不能同时是副角色。
        slaveNames: slaves.filter((n) => n !== master),
        followIntervalMs: Math.max(MIN_FOLLOW_MS, Number(c.followIntervalMs ?? DEFAULT_FOLLOW_MS) || DEFAULT_FOLLOW_MS),
        mirrorPosition: c.mirrorPosition ?? true,
        mirrorDialog: c.mirrorDialog ?? true,
        posEpsilon: Number(c.posEpsilon ?? DEFAULT_POS_EPSILON) || DEFAULT_POS_EPSILON,
    };
}
