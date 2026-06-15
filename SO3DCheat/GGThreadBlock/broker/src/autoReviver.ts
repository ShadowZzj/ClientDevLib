import fs from "fs";
import path from "path";
import { EventEmitter } from "events";
import { v4 as uuid } from "uuid";
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

export interface ReviveAllResult {
    characterName: string;
    pid: number;
    ok: boolean;
    detail?: string;
}

interface DelayRange {
    delayMinMin: number;
    delayMinMax: number;
}

export interface AutoReviveScheduleWindow extends DelayRange {
    enabled: boolean;
    start: string;
    end: string;
}

export interface AutoReviveConfig {
    characterName: string;
    autoRun: boolean;
    // 死亡后延迟范围(分钟)。pending 时 broker 在 [min, max] 之间随机挑一个具体值
    // 决定 scheduledAt。两个值一样就是固定延迟。
    delayMinMin: number;
    delayMinMax: number;
    pauseOnNearbyGm: boolean;
    // 位置卡住自动复活:存活但坐标连续 stuckReviveMinutes 分钟没变化(超过
    // STUCK_MOVE_EPSILON 才算移动)→ 直接跑一遍复活脚本。前提是 autoRun 开。
    // 死亡场景仍走 delay 排程,两者互不干扰。
    stuckReviveEnabled: boolean;
    stuckReviveMinutes: number;
    scheduleWindows: AutoReviveScheduleWindow[];
    steps: Step[];
    // 定点挂机坐标:作为角色运行时的真实来源。套用脚本模板时从模板复制进来
    // (不同模板可能传送到不同地图)。
    farmX?: number;
    farmY?: number;
    // 勾上才会在脚本跑完后把定点坐标推送给游戏(setStationaryFarm)。默认不推。
    farmPushEnabled: boolean;
}

// 全局共享脚本步骤模板库:存「脚本步骤 + 定点挂机坐标 + 是否推送」。不同模板可能
// 传送到不同地图,所以定点坐标和推送开关都跟着模板走,套用模板时一并带入角色 config。
// 用户给它命名,任意角色都能挑一个套到自己的 steps 上。
export interface StepTemplate {
    id: string;
    name: string;
    steps: Step[];
    farmX?: number;
    farmY?: number;
    farmPushEnabled?: boolean;
}

// 全局共享对话选择模板库:给 sendDialogSelectRaw 的 (npcId, option) 起名,
// 用户在步骤里直接挑名字而不用记两个数字。
export interface DialogTemplate {
    id: string;
    name: string;
    npcId: number;
    option: number;
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
    pausedByGm?: boolean;
    nearbyGmName?: string;
    nearbyGmDistance?: number;
    gmPauseStartedAt?: number;
    gmPauseLastTickAt?: number;
    pausedBySchedule?: boolean;
    schedulePauseLastTickAt?: number;
    // 位置卡住检测的运行时基线(不持久化):上次观测到的坐标 + 上次发生移动的
    // 时刻。lastMoveAt 起就是「未移动」计时起点。
    lastPosX?: number;
    lastPosY?: number;
    lastMoveAt?: number;
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "auto_revive.json"
);

const STATUS_POLL_MS = 5000;     // 角色级状态轮询节奏
const RING_BUF_LINES = 200;       // 每角色日志环形缓冲行数

const GM_NEARBY_MAX_DISTANCE = 600;

// 位置卡住检测:坐标变化超过这个值(单位同 moveTo 的 x/y,主城坐标量级在几百)
// 才算「移动了」,过滤浮点噪声。低于它视为原地不动。
const STUCK_MOVE_EPSILON = 1.0;

interface PersistedDoc {
    configs: AutoReviveConfig[];
    // 排程也持久化:broker 重启后,pending 的角色既不丢死亡时间戳也不会被
    // 重新 roll 一个延迟。 scheduledAt 已经包含 broker 上次 roll 出来的具体值。
    deadAt: Record<string, number>;
    scheduledAt: Record<string, number>;
    // 全局共享模板库:脚本步骤模板 + 对话选择模板。所有角色共用。
    stepTemplates?: StepTemplate[];
    dialogTemplates?: DialogTemplate[];
}

function defaultConfig(name: string): AutoReviveConfig {
    return {
        characterName: name,
        autoRun: false,
        delayMinMin: 5,
        delayMinMax: 5,
        pauseOnNearbyGm: false,
        stuckReviveEnabled: false,
        stuckReviveMinutes: 10,
        scheduleWindows: [],
        steps: [],
        farmPushEnabled: false,
    };
}

// 在 [minMin, maxMin] (分钟) 区间内随机挑一个具体值,返回 ms。
function rollDelayMs(range: DelayRange): number {
    const lo = Math.max(0, Math.min(range.delayMinMin, range.delayMinMax));
    const hi = Math.max(0, Math.max(range.delayMinMin, range.delayMinMax));
    const minutes = lo + Math.random() * (hi - lo);
    return Math.round(minutes * 60_000);
}

function normalizeClock(value: any, fallback: string): string {
    const s = String(value || "").trim();
    const m = /^(\d{1,2}):(\d{2})$/.exec(s);
    if (!m) return fallback;
    const h = Number(m[1]);
    const min = Number(m[2]);
    if (!Number.isInteger(h) || !Number.isInteger(min) || h < 0 || h > 23 || min < 0 || min > 59) {
        return fallback;
    }
    return `${h.toString().padStart(2, "0")}:${min.toString().padStart(2, "0")}`;
}

function parseClockMinutes(value: string): number | null {
    const s = normalizeClock(value, "");
    if (!s) return null;
    const [h, m] = s.split(":").map(Number);
    return h * 60 + m;
}

function isClockRangeActive(win: AutoReviveScheduleWindow, now: Date): boolean {
    const start = parseClockMinutes(win.start);
    const end = parseClockMinutes(win.end);
    if (start === null || end === null || start === end) return false;
    const cur = now.getHours() * 60 + now.getMinutes();
    if (start < end) return cur >= start && cur < end;
    return cur >= start || cur < end;
}

function normalizeScheduleWindow(raw: any): AutoReviveScheduleWindow | null {
    if (!raw || typeof raw !== "object") return null;
    const start = normalizeClock(raw.start, "00:00");
    const end = normalizeClock(raw.end, "23:59");
    const loRaw = Number(raw.delayMinMin);
    const hiRaw = Number(raw.delayMinMax);
    let lo = Number.isFinite(loRaw) ? loRaw : 5;
    let hi = Number.isFinite(hiRaw) ? hiRaw : lo;
    if (hi < lo) [lo, hi] = [hi, lo];
    return {
        enabled: raw.enabled !== false,
        start,
        end,
        delayMinMin: Math.max(0, lo),
        delayMinMax: Math.max(0, hi),
    };
}

function rangeText(range: DelayRange): string {
    return range.delayMinMin === range.delayMinMax
        ? `${range.delayMinMin}min`
        : `[${range.delayMinMin}, ${range.delayMinMax}]min`;
}

function scheduleWindowText(win: AutoReviveScheduleWindow): string {
    return `${win.start}-${win.end}`;
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
    private getGmNames: () => string[];
    // 全局共享模板库(非角色级)。
    private stepTemplates = new Map<string, StepTemplate>();
    private dialogTemplates = new Map<string, DialogTemplate>();

    constructor(registry: InstanceRegistry, getGmNames: () => string[] = () => []) {
        super();
        this.registry = registry;
        this.getGmNames = getGmNames;
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
            for (const t of doc.stepTemplates || []) {
                const norm = this.normalizeStepTemplate(t);
                if (norm) this.stepTemplates.set(norm.id, norm);
            }
            for (const t of doc.dialogTemplates || []) {
                const norm = this.normalizeDialogTemplate(t);
                if (norm) this.dialogTemplates.set(norm.id, norm);
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
            stepTemplates: Array.from(this.stepTemplates.values()),
            dialogTemplates: Array.from(this.dialogTemplates.values()),
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
            pauseOnNearbyGm: !!c.pauseOnNearbyGm,
            stuckReviveEnabled: !!c.stuckReviveEnabled,
            stuckReviveMinutes: Number.isFinite(c.stuckReviveMinutes)
                ? Math.max(0, Math.round(c.stuckReviveMinutes))
                : 10,
            scheduleWindows: Array.isArray(c.scheduleWindows)
                ? c.scheduleWindows
                    .map(normalizeScheduleWindow)
                    .filter((w: AutoReviveScheduleWindow | null): w is AutoReviveScheduleWindow => !!w)
                : [],
            steps: Array.isArray(c.steps)
                ? c.steps.map(normalizeStep).filter((s: Step | null): s is Step => !!s)
                : [],
            ...(Number.isFinite(c.farmX) ? { farmX: Math.round(c.farmX) } : {}),
            ...(Number.isFinite(c.farmY) ? { farmY: Math.round(c.farmY) } : {}),
            farmPushEnabled: !!c.farmPushEnabled,
        };
    }

    private normalizeStepTemplate(t: any): StepTemplate | null {
        if (!t || typeof t !== "object") return null;
        const name = String(t.name || "").trim();
        if (!name) return null;
        const id = String(t.id || "").trim() || uuid();
        const steps = Array.isArray(t.steps)
            ? t.steps.map(normalizeStep).filter((s: Step | null): s is Step => !!s)
            : [];
        return {
            id,
            name,
            steps,
            ...(Number.isFinite(t.farmX) ? { farmX: Math.round(t.farmX) } : {}),
            ...(Number.isFinite(t.farmY) ? { farmY: Math.round(t.farmY) } : {}),
            farmPushEnabled: !!t.farmPushEnabled,
        };
    }

    private normalizeDialogTemplate(t: any): DialogTemplate | null {
        if (!t || typeof t !== "object") return null;
        const name = String(t.name || "").trim();
        if (!name) return null;
        const npcId = Number(t.npcId);
        const option = Number(t.option);
        if (!Number.isFinite(npcId) || !Number.isFinite(option)) return null;
        const id = String(t.id || "").trim() || uuid();
        return { id, name, npcId: Math.round(npcId), option: Math.round(option) };
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
                this.clearGmPause(st);
                this.clearSchedulePause(st);
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
            this.clearGmPause(st);
            this.clearSchedulePause(st);
        }
        if (had) this.save();
        return had;
    }

    // ---------- 全局模板库 CRUD ----------
    listStepTemplates(): StepTemplate[] {
        return Array.from(this.stepTemplates.values());
    }

    // upsert:带 id 且已存在 → 更新;否则新建一个 id。返回规整后的模板。
    saveStepTemplate(input: any): StepTemplate {
        const norm = this.normalizeStepTemplate(input);
        if (!norm) throw new Error("invalid step template (need name)");
        this.stepTemplates.set(norm.id, norm);
        this.save();
        return norm;
    }

    deleteStepTemplate(id: string): boolean {
        const had = this.stepTemplates.delete(id);
        if (had) this.save();
        return had;
    }

    listDialogTemplates(): DialogTemplate[] {
        return Array.from(this.dialogTemplates.values());
    }

    saveDialogTemplate(input: any): DialogTemplate {
        const norm = this.normalizeDialogTemplate(input);
        if (!norm) throw new Error("invalid dialog template (need name, npcId, option)");
        this.dialogTemplates.set(norm.id, norm);
        this.save();
        return norm;
    }

    deleteDialogTemplate(id: string): boolean {
        const had = this.dialogTemplates.delete(id);
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
        this.clearGmPause(st);
        this.clearSchedulePause(st);
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

    async runAllDeadNow(): Promise<{ ok: boolean; results: ReviveAllResult[] }> {
        const results: ReviveAllResult[] = [];
        const online = this.registry.list()
            .filter((inst) => !!inst.characterName && inst.status.hp === 0);

        for (const inst of online) {
            const name = inst.characterName!;
            const cfg = this.configs.get(name);
            const st = this.ensureState(name);
            if (!cfg) {
                results.push({ characterName: name, pid: inst.pid, ok: false, detail: "no config" });
                continue;
            }
            if (st.phase === "running") {
                results.push({ characterName: name, pid: inst.pid, ok: false, detail: "already running" });
                continue;
            }
            st.deadAt = 0;
            st.scheduledAt = 0;
            this.clearGmPause(st);
            this.clearSchedulePause(st);
            this.runScript(name, inst.pid, { ignoreReviveSafetyCheck: true, countAsAutoRevive: true }).catch((e) =>
                console.warn(`[autoReviver] runAllDeadNow ${name} failed: ${e.message}`)
            );
            results.push({ characterName: name, pid: inst.pid, ok: true });
        }

        if (results.length === 0) {
            return { ok: true, results: [] };
        }
        this.save();
        return { ok: results.some((r) => r.ok), results };
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

    private clearGmPause(st: AutoReviveState): void {
        delete st.pausedByGm;
        delete st.nearbyGmName;
        delete st.nearbyGmDistance;
        delete st.gmPauseStartedAt;
        delete st.gmPauseLastTickAt;
    }

    private clearSchedulePause(st: AutoReviveState): void {
        delete st.pausedBySchedule;
        delete st.schedulePauseLastTickAt;
    }

    private hasScheduleWindows(cfg: AutoReviveConfig): boolean {
        return cfg.scheduleWindows.length > 0;
    }

    private getActiveScheduleWindow(cfg: AutoReviveConfig, now: number): AutoReviveScheduleWindow | null {
        const d = new Date(now);
        return cfg.scheduleWindows.find((w) => w.enabled && isClockRangeActive(w, d)) ?? null;
    }

    private async findNearbyGm(pid: number): Promise<{ name: string; distance: number } | null> {
        const names = this.getGmNames()
            .map((s) => String(s || "").trim())
            .filter((s) => s.length > 0);
        if (names.length === 0) return null;

        const gmSet = new Set(names);
        try {
            const r = await this.registry.sendCommand(
                pid,
                "getNearbyPlayers",
                { maxDistance: GM_NEARBY_MAX_DISTANCE },
                3000
            );
            if (!r.ok) return null;
            const arr = typeof r.detail === "string" ? JSON.parse(r.detail) : r.detail;
            if (!Array.isArray(arr)) return null;

            let best: { name: string; distance: number } | null = null;
            for (const p of arr) {
                const name = String(p?.name || "").trim();
                if (!gmSet.has(name)) continue;
                const distance = Number(p?.distance);
                const item = { name, distance: Number.isFinite(distance) ? distance : 0 };
                if (!best || item.distance < best.distance) best = item;
            }
            return best;
        } catch {
            return null;
        }
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

            // 拉 hp + 坐标。失败 = 当成 idle (不会误触发自动复活)。
            let isDead = false;
            let statusObj: any = null;
            try {
                const r = await this.registry.sendCommand(pid, "getStatus", {}, 3000);
                if (!r.ok) continue;
                statusObj = typeof r.detail === "string" ? JSON.parse(r.detail) : r.detail;
                isDead = !!statusObj?.isDead;
            } catch {
                continue;
            }

            if (!isDead) {
                if (st.deadAt !== 0) {
                    this.appendLog(cfg.characterName, `[auto] HP 已恢复 → 取消排程`);
                    st.deadAt = 0;
                    st.scheduledAt = 0;
                    st.phase = "idle";
                    this.clearGmPause(st);
                    this.clearSchedulePause(st);
                    this.save();
                    this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
                }
                // 存活才做位置卡住检测;死亡交给下面的延迟排程。
                await this.tickStuckDetect(cfg, st, pid, statusObj, now);
                continue;
            }

            // 到这里 = 死亡。重置卡住基线,死亡/复活过程中坐标冻结不计入「未移动」。
            st.lastPosX = undefined;
            st.lastPosY = undefined;
            st.lastMoveAt = undefined;

            const hasScheduleWindows = this.hasScheduleWindows(cfg);
            const activeWindow = this.getActiveScheduleWindow(cfg, now);
            if (hasScheduleWindows && !activeWindow) {
                const wasPaused = !!st.pausedBySchedule;
                if (st.deadAt === 0) {
                    st.deadAt = now;
                    st.scheduledAt = 0;
                    st.phase = "pending";
                    this.clearGmPause(st);
                } else if (st.scheduledAt > 0) {
                    const lastTick = st.schedulePauseLastTickAt || now;
                    const delta = Math.max(0, now - lastTick);
                    if (delta > 0) st.scheduledAt += delta;
                }
                st.pausedBySchedule = true;
                st.schedulePauseLastTickAt = now;
                if (!wasPaused) {
                    this.appendLog(cfg.characterName, `[auto] 当前不在启用时间段 → 暂停/等待复活倒计时`);
                }
                this.save();
                this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
                continue;
            }

            if (st.pausedBySchedule) {
                const label = activeWindow ? scheduleWindowText(activeWindow) : "默认";
                this.appendLog(cfg.characterName, `[auto] 进入启用时间段 ${label} → 恢复复活倒计时`);
                this.clearSchedulePause(st);
                this.save();
                this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
            }

            const delayRange: DelayRange = activeWindow ?? cfg;
            const delaySource = activeWindow ? `时间段 ${scheduleWindowText(activeWindow)}` : "默认";

            // 死着。首次进入 pending → 记时间戳 + roll 出一个具体延迟。
            // 在 [delayMinMin, delayMinMax] 范围里随机挑(防止固定节奏被识别),
            // 然后 scheduledAt 持久化,broker 重启也不会再 roll 一次。
            if (st.deadAt === 0 || st.scheduledAt === 0) {
                const delayMs = rollDelayMs(delayRange);
                if (st.deadAt === 0) st.deadAt = now;
                st.scheduledAt = now + delayMs;
                st.phase = "pending";
                this.clearGmPause(st);
                this.clearSchedulePause(st);
                const delayS = Math.round(delayMs / 1000);
                if (delayMs > 0) {
                    this.appendLog(cfg.characterName,
                        `[auto] 检测到死亡 → ${delaySource} 随机延迟 ${delayS}s (范围 ${rangeText(delayRange)}) 后执行`);
                } else {
                    this.appendLog(cfg.characterName, `[auto] 检测到死亡 → ${delaySource} 立刻执行`);
                }
                this.save();
                this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
            }

            // 到点 → run。先清 deadAt,跑完不会被立刻再次触发;真要再触发,
            // 等下一次"HP 从恢复 → 又掉到 0"的边沿。
            if (cfg.pauseOnNearbyGm) {
                const nearbyGm = await this.findNearbyGm(pid);
                if (nearbyGm) {
                    const wasPaused = !!st.pausedByGm;
                    const lastTick = st.gmPauseLastTickAt || now;
                    const delta = Math.max(0, now - lastTick);
                    if (delta > 0) st.scheduledAt += delta;
                    st.pausedByGm = true;
                    st.nearbyGmName = nearbyGm.name;
                    st.nearbyGmDistance = nearbyGm.distance;
                    if (!st.gmPauseStartedAt) st.gmPauseStartedAt = now;
                    st.gmPauseLastTickAt = now;
                    if (!wasPaused) {
                        this.appendLog(
                            cfg.characterName,
                            `[auto] GM ${nearbyGm.name} nearby (${Math.round(nearbyGm.distance)}) -> pause revive timer`
                        );
                    }
                    this.save();
                    this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
                    continue;
                }
                if (st.pausedByGm) {
                    this.appendLog(cfg.characterName, `[auto] GM left -> resume revive timer`);
                    this.clearGmPause(st);
                    this.save();
                    this.emit("state", cfg.characterName, this.snapshotState(cfg.characterName));
                }
            }

            if (now >= st.scheduledAt) {
                this.appendLog(cfg.characterName, `[auto] 延迟到点 → 触发复活脚本`);
                st.deadAt = 0;
                st.scheduledAt = 0;
                this.clearGmPause(st);
                this.clearSchedulePause(st);
                this.save();
                this.runScript(cfg.characterName, pid, { countAsAutoRevive: true }).catch((e) =>
                    console.warn(`[autoReviver] runScript ${cfg.characterName} failed: ${e.message}`)
                );
            }
        }
    }

    // 位置卡住检测(仅在存活 + phase 非 running 时被 tick 调用)。
    // 思路:每个 tick 比对坐标。移动了 → 刷新基线 + lastMoveAt;没动且累计
    // 时长 >= stuckReviveMinutes → 跑一遍复活脚本(同「立即执行(不看死活)」语义,
    // ignoreReviveSafetyCheck)。复用 pauseOnNearbyGm:GM 在旁就先不触发。
    private async tickStuckDetect(
        cfg: AutoReviveConfig,
        st: AutoReviveState,
        pid: number,
        statusObj: any,
        now: number
    ): Promise<void> {
        if (!cfg.stuckReviveEnabled || cfg.stuckReviveMinutes <= 0 || cfg.steps.length === 0) {
            // 功能关闭 / 无脚本 → 清基线,避免下次打开立刻命中旧基线。
            st.lastPosX = undefined;
            st.lastPosY = undefined;
            st.lastMoveAt = undefined;
            return;
        }

        const posX = Number(statusObj?.posX);
        const posY = Number(statusObj?.posY);
        if (!Number.isFinite(posX) || !Number.isFinite(posY)) return;

        const baselineUnset = st.lastPosX === undefined || st.lastPosY === undefined;
        const moved = baselineUnset
            || Math.abs(posX - (st.lastPosX as number)) > STUCK_MOVE_EPSILON
            || Math.abs(posY - (st.lastPosY as number)) > STUCK_MOVE_EPSILON;

        if (moved) {
            st.lastPosX = posX;
            st.lastPosY = posY;
            st.lastMoveAt = now;
            return;
        }

        const stuckMs = now - (st.lastMoveAt ?? now);
        const thresholdMs = cfg.stuckReviveMinutes * 60_000;
        if (stuckMs < thresholdMs) return;

        // 到时间了。GM 在旁边就先不跑(不重置基线,GM 走后下个 tick 立刻触发)。
        if (cfg.pauseOnNearbyGm) {
            const gm = await this.findNearbyGm(pid);
            if (gm) {
                this.appendLog(cfg.characterName,
                    `[stuck] 位置 ${Math.round(stuckMs / 1000)}s 未变化,但 GM ${gm.name} (${Math.round(gm.distance)}) 在旁 → 暂不触发`);
                return;
            }
        }

        this.appendLog(cfg.characterName,
            `[stuck] 位置 ${Math.round(stuckMs / 1000)}s (>=${cfg.stuckReviveMinutes}min) 未变化 → 触发复活流程`);
        // 重置基线:脚本跑完不会因为还在原点而立刻重复触发,需重新累计满 X 分钟。
        st.lastPosX = posX;
        st.lastPosY = posY;
        st.lastMoveAt = now;
        this.runScript(cfg.characterName, pid, {
            ignoreReviveSafetyCheck: true,
            countAsAutoRevive: true,
        }).catch((e) =>
            console.warn(`[autoReviver] stuck runScript ${cfg.characterName} failed: ${e.message}`)
        );
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
            // 脚本顺利跑完(未失败/未中止)且勾了「推送定点坐标」→ 推送定点挂机坐标。
            // 坐标跟着脚本走,由角色 config 持有(套用模板时从模板复制进来)。
            const runOk = !failed && this.aborts.get(name) !== true;
            if (runOk && cfg.farmPushEnabled && Number.isFinite(cfg.farmX as number) && Number.isFinite(cfg.farmY as number)) {
                const x = Math.round(cfg.farmX as number);
                const y = Math.round(cfg.farmY as number);
                try {
                    const r = await this.registry.sendCommand(pid, "setStationaryFarm", { x, y }, 5000);
                    this.appendLog(name, `  推送定点挂机坐标 (${x}, ${y}) -> ${r.ok ? "ok" : "FAIL: " + r.detail}`);
                } catch (e: any) {
                    this.appendLog(name, `  推送定点坐标异常: ${e.message}`);
                }
            }
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
