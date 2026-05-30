import fs from "fs";
import path from "path";
import { EventEmitter } from "events";
import { InstanceRegistry } from "./instances";

// ---------------- step + config 类型 ----------------
// 跟原前端 AutoReviveView.vue 里的 Step / ReviveConfig 同形,迁移直接 1:1 搬。
// 注意 numeric/boolean 字段保留 default,broker 端 JSON 反序列化遇到老格式不报错。

export interface StepBase {
    delayMs: number;
    repeatCount?: number;
    repeatDelayMs?: number;
}
export interface StepReviveToTown extends StepBase {
    type: "reviveToTown";
}
export interface StepMoveTo extends StepBase {
    type: "moveTo";
    x: number;
    y: number;
    action: 1 | 3;
}
export interface StepWaitInTown extends StepBase {
    type: "waitInTown";
    mapId: number;
    timeoutMs: number;
}
export interface StepWaitOutOfTown extends StepBase {
    type: "waitOutOfTown";
    mapId: number;
    timeoutMs: number;
}
export interface StepSendDialogSelectRaw extends StepBase {
    type: "sendDialogSelectRaw";
    npcId: number;
    option: number;
}
export interface StepPressHookedKey extends StepBase {
    type: "pressHookedKey";
    vkey: number;
    alt: boolean;
    ctrl: boolean;
    shift: boolean;
}
export interface StepSleep extends StepBase {
    type: "sleep";
}
export type Step =
    | StepReviveToTown
    | StepMoveTo
    | StepWaitInTown
    | StepWaitOutOfTown
    | StepSendDialogSelectRaw
    | StepPressHookedKey
    | StepSleep;

interface RunScriptOptions {
    ignoreReviveSafetyCheck?: boolean;
    countAsAutoRevive?: boolean;
}

export interface AutoReviveCompletedEvent {
    characterName: string;
    pid: number;
    at: number;
    steps: number;
}

export interface AutoReviveConfig {
    characterName: string;
    autoRun: boolean;
    // 死亡后延迟范围(分钟)。pending 时 broker 在 [min, max] 之间随机挑一个具体值
    // 决定 scheduledAt。两个值一样就是固定延迟。
    delayMinMin: number;
    delayMinMax: number;
    steps: Step[];
}

export type Phase = "idle" | "pending" | "running" | "armed";

export interface AutoReviveState {
    characterName: string;
    phase: Phase;
    deadAt: number;        // 0 = 不在 pending
    scheduledAt: number;   // deadAt + delayMin*60s
    currentStepIdx: number; // running 阶段第几步,0 起;非 running 时 -1
    lastError?: string;
    lastRunAt?: number;
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "auto_revive.json"
);

const STATUS_POLL_MS = 5000;     // 角色级状态轮询节奏
const RING_BUF_LINES = 200;       // 每角色日志环形缓冲行数

interface PersistedDoc {
    configs: AutoReviveConfig[];
    // 排程也持久化:broker 重启后,pending 的角色既不丢死亡时间戳也不会被
    // 重新 roll 一个延迟。 scheduledAt 已经包含 broker 上次 roll 出来的具体值。
    deadAt: Record<string, number>;
    scheduledAt: Record<string, number>;
}

function defaultConfig(name: string): AutoReviveConfig {
    return {
        characterName: name,
        autoRun: false,
        delayMinMin: 5,
        delayMinMax: 5,
        steps: [],
    };
}

// 在 [minMin, maxMin] (分钟) 区间内随机挑一个具体值,返回 ms。
function rollDelayMs(cfg: AutoReviveConfig): number {
    const lo = Math.max(0, Math.min(cfg.delayMinMin, cfg.delayMinMax));
    const hi = Math.max(0, Math.max(cfg.delayMinMin, cfg.delayMinMax));
    const minutes = lo + Math.random() * (hi - lo);
    return Math.round(minutes * 60_000);
}

function normalizeStep(step: any): Step | null {
    if (!step || typeof step.type !== "string") return null;
    const out: any = { ...step };
    out.delayMs = Number.isFinite(out.delayMs) ? Math.max(0, Math.round(out.delayMs)) : 0;
    out.repeatCount = Number.isFinite(out.repeatCount)
        ? Math.max(1, Math.min(999, Math.round(out.repeatCount)))
        : 1;
    out.repeatDelayMs = Number.isFinite(out.repeatDelayMs)
        ? Math.max(0, Math.round(out.repeatDelayMs))
        : 0;
    return out as Step;
}

export class AutoReviver extends EventEmitter {
    private registry: InstanceRegistry;
    private configs = new Map<string, AutoReviveConfig>();
    private states = new Map<string, AutoReviveState>();
    private logs = new Map<string, string[]>(); // 环形缓冲
    private aborts = new Map<string, boolean>();
    private timer: NodeJS.Timeout | null = null;

    constructor(registry: InstanceRegistry) {
        super();
        this.registry = registry;
        this.load();
        this.start();
    }

    // ---------- 持久化 ----------
    private load(): void {
        try {
            if (!fs.existsSync(PERSIST_FILE)) return;
            const raw = fs.readFileSync(PERSIST_FILE, "utf8");
            const doc = JSON.parse(raw) as PersistedDoc;
            for (const c of doc.configs || []) {
                if (!c.characterName) continue;
                this.configs.set(c.characterName, this.normalizeConfig(c));
            }
            const deadAt = doc.deadAt || {};
            const scheduled = doc.scheduledAt || {};
            for (const [name, ts] of Object.entries(deadAt)) {
                const cfg = this.configs.get(name);
                if (!cfg) continue;
                const st = this.ensureState(name);
                if (typeof ts === "number" && ts > 0) {
                    st.deadAt = ts;
                    // 之前 roll 出来的 scheduledAt 优先;没存才 fallback 用 min 端
                    // 重新算(老格式迁移)。
                    const sched = scheduled[name];
                    st.scheduledAt = typeof sched === "number" && sched > 0
                        ? sched
                        : ts + Math.max(0, cfg.delayMinMin) * 60_000;
                    st.phase = "pending";
                }
            }
            console.log(`[autoReviver] loaded ${this.configs.size} configs`);
        } catch (e: any) {
            console.warn(`[autoReviver] load failed: ${e.message}`);
        }
    }

    private save(): void {
        const deadAt: Record<string, number> = {};
        const scheduledAt: Record<string, number> = {};
        for (const [name, st] of this.states) {
            if (st.deadAt > 0) deadAt[name] = st.deadAt;
            if (st.scheduledAt > 0) scheduledAt[name] = st.scheduledAt;
        }
        const doc: PersistedDoc = {
            configs: Array.from(this.configs.values()),
            deadAt,
            scheduledAt,
        };
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(doc, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[autoReviver] save failed: ${e.message}`);
        }
    }

    private normalizeConfig(c: any): AutoReviveConfig {
        // 老格式兼容:之前是单值 delayMin。迁移到 [delayMinMin, delayMinMax] 区间,
        // 单值视作两端相等。
        let lo: number, hi: number;
        if (Number.isFinite(c.delayMinMin) || Number.isFinite(c.delayMinMax)) {
            lo = Number.isFinite(c.delayMinMin) ? c.delayMinMin : (c.delayMinMax ?? 5);
            hi = Number.isFinite(c.delayMinMax) ? c.delayMinMax : (c.delayMinMin ?? 5);
        } else if (Number.isFinite(c.delayMin)) {
            lo = c.delayMin;
            hi = c.delayMin;
        } else {
            lo = 5;
            hi = 5;
        }
        if (hi < lo) [lo, hi] = [hi, lo];
        return {
            characterName: String(c.characterName),
            autoRun: !!c.autoRun,
            delayMinMin: Math.max(0, lo),
            delayMinMax: Math.max(0, hi),
            steps: Array.isArray(c.steps)
                ? c.steps.map(normalizeStep).filter((s: Step | null): s is Step => !!s)
                : [],
        };
    }

    // ---------- public API ----------
    listConfigs(): AutoReviveConfig[] {
        return Array.from(this.configs.values());
    }

    getConfig(name: string): AutoReviveConfig | null {
        return this.configs.get(name) ?? null;
    }

    setConfig(name: string, cfg: AutoReviveConfig): AutoReviveConfig {
        const norm = this.normalizeConfig({ ...cfg, characterName: name });
        const prev = this.configs.get(name);
        this.configs.set(name, norm);
        // autoRun 被关掉 → 清掉残留 pending,避免再打开就立刻跑。
        if (prev && prev.autoRun && !norm.autoRun) {
            const st = this.states.get(name);
            if (st && st.phase === "pending") {
                this.appendLog(name, `[auto] autoRun 关闭 → 取消排程`);
                st.deadAt = 0;
                st.scheduledAt = 0;
                st.phase = "idle";
                this.emit("state", name, this.snapshotState(name));
            }
        }
        this.save();
        return norm;
    }

    deleteConfig(name: string): boolean {
        const had = this.configs.delete(name);
        // 同时清状态,避免幽灵 deadAt
        const st = this.states.get(name);
        if (st) {
            st.deadAt = 0;
            st.scheduledAt = 0;
            st.phase = "idle";
        }
        if (had) this.save();
        return had;
    }

    getState(name: string): AutoReviveState {
        return this.snapshotState(name);
    }

    listStates(): AutoReviveState[] {
        // 把所有有 config 的都列出来,前端方便一次性渲染状态汇总
        return Array.from(this.configs.keys()).map((n) => this.snapshotState(n));
    }

    getLog(name: string): string[] {
        return [...(this.logs.get(name) || [])];
    }

    cancelPending(name: string): boolean {
        const st = this.states.get(name);
        if (!st || st.phase !== "pending") return false;
        this.appendLog(name, `[auto] 用户取消排程`);
        st.deadAt = 0;
        st.scheduledAt = 0;
        st.phase = "idle";
        this.save();
        this.emit("state", name, this.snapshotState(name));
        return true;
    }

    abortRunning(name: string): boolean {
        const st = this.states.get(name);
        if (!st || st.phase !== "running") return false;
        this.aborts.set(name, true);
        this.appendLog(name, `[auto] 请求中止`);
        return true;
    }

    // 手动触发:立刻按当前角色的脚本跑一次(不看 isDead,主要给调试用)。
    async runNow(name: string): Promise<{ ok: boolean; detail?: string }> {
        const cfg = this.configs.get(name);
        if (!cfg) return { ok: false, detail: "no config" };
        const inst = this.registry
            .list()
            .find((i) => i.characterName === name);
        if (!inst) return { ok: false, detail: "instance offline" };
        const st = this.ensureState(name);
        if (st.phase === "running") return { ok: false, detail: "already running" };
        this.runScript(name, inst.pid, { ignoreReviveSafetyCheck: true }).catch((e) =>
            console.warn(`[autoReviver] runScript ${name} failed: ${e.message}`)
        );
        return { ok: true };
    }

    // ---------- 内部 ----------
    private ensureState(name: string): AutoReviveState {
        let st = this.states.get(name);
        if (!st) {
            st = {
                characterName: name,
                phase: "idle",
                deadAt: 0,
                scheduledAt: 0,
                currentStepIdx: -1,
            };
            this.states.set(name, st);
        }
        return st;
    }

    private snapshotState(name: string): AutoReviveState {
        return { ...this.ensureState(name) };
    }

    private appendLog(name: string, line: string): void {
        const ts = new Date().toLocaleTimeString();
        const full = `[${ts}] ${line}`;
        let buf = this.logs.get(name);
        if (!buf) {
            buf = [];
            this.logs.set(name, buf);
        }
        buf.push(full);
        if (buf.length > RING_BUF_LINES) buf.splice(0, buf.length - RING_BUF_LINES);
        this.emit("log", name, full);
    }

    private start(): void {
        this.timer = setInterval(() => this.tick(), STATUS_POLL_MS);
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }

    // 每个 tick:遍历开了 autoRun 的 config,找对应在线实例,getStatus,
    // 推进状态机。运行中(phase=running) 不再发新 getStatus —— 让 runScript
    // 自己跑;否则一边跑脚本一边重新调度会很乱。
    private async tick(): Promise<void> {
        const online = this.registry.list();
        const nameToPid = new Map<string, number>();
        for (const inst of online) {
            if (inst.characterName) nameToPid.set(inst.characterName, inst.pid);
        }

        const now = Date.now();
        for (const cfg of this.configs.values()) {
            if (!cfg.autoRun) continue;
            const st = this.ensureState(cfg.characterName);
            if (st.phase === "running") continue;
            const pid = nameToPid.get(cfg.characterName);
            if (pid === undefined) continue; // 离线就不动状态机

            // 拉 hp。失败 = 当成 idle (不会误触发自动复活)。
            let isDead = false;
            try {
                const r = await this.registry.sendCommand(pid, "getStatus", {}, 3000);
                if (!r.ok) continue;
                const obj = typeof r.detail === "string" ? JSON.parse(r.detail) : r.detail;
                isDead = !!obj?.isDead;
            } catch {
                continue;
            }

            if (!isDead) {
                if (st.deadAt !== 0) {
                    this.appendLog(cfg.characterName, `[auto] HP 已恢复 → 取消排程`);
                    st.deadAt = 0;
                    st.scheduledAt = 0;
                    st.phase = "idle";
                    this.save();
                    this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
                }
                continue;
            }

            // 死着。首次进入 pending → 记时间戳 + roll 出一个具体延迟。
            // 在 [delayMinMin, delayMinMax] 范围里随机挑(防止固定节奏被识别),
            // 然后 scheduledAt 持久化,broker 重启也不会再 roll 一次。
            if (st.deadAt === 0) {
                const delayMs = rollDelayMs(cfg);
                st.deadAt = now;
                st.scheduledAt = now + delayMs;
                st.phase = "pending";
                const delayS = Math.round(delayMs / 1000);
                if (delayMs > 0) {
                    const rangeStr =
                        cfg.delayMinMin === cfg.delayMinMax
                            ? `${cfg.delayMinMin}min`
                            : `[${cfg.delayMinMin}, ${cfg.delayMinMax}]min`;
                    this.appendLog(cfg.characterName,
                        `[auto] 检测到死亡 → 随机延迟 ${delayS}s (范围 ${rangeStr}) 后执行`);
                } else {
                    this.appendLog(cfg.characterName, `[auto] 检测到死亡 → 立刻执行`);
                }
                this.save();
                this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
            }

            // 到点 → run。先清 deadAt,跑完不会被立刻再次触发;真要再触发,
            // 等下一次"HP 从恢复 → 又掉到 0"的边沿。
            if (now >= st.scheduledAt) {
                this.appendLog(cfg.characterName, `[auto] 延迟到点 → 触发复活脚本`);
                st.deadAt = 0;
                st.scheduledAt = 0;
                this.save();
                this.runScript(cfg.characterName, pid, { countAsAutoRevive: true }).catch((e) =>
                    console.warn(`[autoReviver] runScript ${cfg.characterName} failed: ${e.message}`)
                );
            }
        }
    }

    private async runScript(name: string, pid: number, options: RunScriptOptions = {}): Promise<void> {
        const cfg = this.configs.get(name);
        if (!cfg) return;
        const st = this.ensureState(name);
        st.phase = "running";
        st.currentStepIdx = 0;
        st.lastError = undefined;
        this.aborts.set(name, false);
        this.emit("state", name, this.snapshotState(name));
        this.appendLog(name, `开始执行复活脚本: ${cfg.steps.length} 步`);
        let failed = false;

        try {
            for (let i = 0; i < cfg.steps.length && !failed; ++i) {
                if (this.aborts.get(name)) {
                    this.appendLog(name, `已中止`);
                    break;
                }
                st.currentStepIdx = i;
                this.emit("state", name, this.snapshotState(name));
                const s = cfg.steps[i];
                this.appendLog(name, `步骤 ${i + 1}/${cfg.steps.length}: ${s.type}`);
                const repeatCount = Math.max(1, Math.round(s.repeatCount ?? 1));
                for (let n = 0; n < repeatCount; ++n) {
                    if (this.aborts.get(name)) break;
                    if (repeatCount > 1) {
                        this.appendLog(name, `  循环 ${n + 1}/${repeatCount}`);
                    }
                    const ok = await this.runStep(name, pid, s, options);
                    if (!ok) {
                        st.lastError = `step ${i + 1} (${s.type}) failed`;
                        this.appendLog(name, `步骤失败,中止剩余步骤`);
                        failed = true;
                        break;
                    }
                    if (n + 1 < repeatCount && (s.repeatDelayMs ?? 0) > 0) {
                        this.appendLog(name, `  循环等待 ${s.repeatDelayMs}ms`);
                        await this.sleep(name, s.repeatDelayMs ?? 0);
                    }
                }
                if (s.delayMs > 0) {
                    this.appendLog(name, `  等待 ${s.delayMs}ms`);
                    await this.sleep(name, s.delayMs);
                }
            }
            this.appendLog(name, `脚本执行完毕`);
            const completed = cfg.steps.length > 0 && !failed && this.aborts.get(name) !== true;
            if (completed && options.countAsAutoRevive) {
                this.emit("autoRunComplete", {
                    characterName: name,
                    pid,
                    at: Date.now(),
                    steps: cfg.steps.length,
                } satisfies AutoReviveCompletedEvent);
            }
        } finally {
            st.phase = "idle";
            st.currentStepIdx = -1;
            st.lastRunAt = Date.now();
            this.aborts.delete(name);
            this.emit("state", name, this.snapshotState(name));
        }
    }

    private async sleep(name: string, ms: number): Promise<void> {
        const start = Date.now();
        while (Date.now() - start < ms) {
            if (this.aborts.get(name)) return;
            await new Promise<void>((r) => setTimeout(r, Math.min(100, ms - (Date.now() - start))));
        }
    }

    private async runStep(name: string, pid: number, s: Step, options: RunScriptOptions): Promise<boolean> {
        try {
            switch (s.type) {
                case "reviveToTown": {
                    const r = await this.registry.sendCommand(
                        pid,
                        "reviveToTown",
                        { safetyCheck: !options.ignoreReviveSafetyCheck },
                        5000
                    );
                    this.appendLog(name, `  reviveToTown -> ${r.ok ? "ok" : "FAIL: " + r.detail}`);
                    return !!r.ok;
                }
                case "moveTo": {
                    const r = await this.registry.sendCommand(
                        pid, "moveTo", { x: s.x, y: s.y, action: s.action }, 5000
                    );
                    this.appendLog(name,
                        `  moveTo(${s.x}, ${s.y}, action=${s.action}) -> ${r.ok ? "ok" : "FAIL: " + r.detail}`);
                    return !!r.ok;
                }
                case "sendDialogSelectRaw": {
                    const r = await this.registry.sendCommand(
                        pid, "sendDialogSelectRaw", { npcId: s.npcId, option: s.option }, 5000
                    );
                    this.appendLog(name,
                        `  sendDialogSelectRaw(npcId=${s.npcId}, option=${s.option}) -> ${r.ok ? "ok" : "FAIL: " + r.detail}`);
                    return !!r.ok;
                }
                case "pressHookedKey": {
                    const r = await this.registry.sendCommand(pid, "pressHookedKey", {
                        vkey: s.vkey, alt: s.alt, ctrl: s.ctrl, shift: s.shift,
                    }, 5000);
                    const mods = [s.alt && "Alt", s.ctrl && "Ctrl", s.shift && "Shift"]
                        .filter(Boolean).join("+");
                    this.appendLog(name,
                        `  pressHookedKey(${mods ? mods + "+" : ""}0x${s.vkey.toString(16).toUpperCase()}) -> ${r.ok ? "ok" : "FAIL: " + r.detail}`);
                    return !!r.ok;
                }
                case "waitInTown":
                case "waitOutOfTown": {
                    const wantInside = s.type === "waitInTown";
                    const start = Date.now();
                    this.appendLog(name, `  ${wantInside ? "等待进入" : "等待离开"} mapId=${s.mapId} (timeout ${s.timeoutMs}ms)`);
                    while (Date.now() - start < s.timeoutMs) {
                        if (this.aborts.get(name)) return false;
                        const r = await this.registry.sendCommand(pid, "getStatus", {}, 3000);
                        if (r.ok) {
                            const obj = typeof r.detail === "string" ? JSON.parse(r.detail) : r.detail;
                            const inside = obj?.mapId === s.mapId;
                            if (wantInside === inside) {
                                this.appendLog(name, `  地图 = ${obj?.mapId},条件满足`);
                                return true;
                            }
                        }
                        await this.sleep(name, 500);
                    }
                    this.appendLog(name, `  超时`);
                    return false;
                }
                case "sleep":
                    return true;
            }
        } catch (e: any) {
            this.appendLog(name, `  step exception: ${e.message}`);
            return false;
        }
        this.appendLog(name, `  未知步骤类型`);
        return false;
    }
}
