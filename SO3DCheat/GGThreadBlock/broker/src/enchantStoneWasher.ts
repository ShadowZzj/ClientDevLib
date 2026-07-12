import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

const COLOR_COUNT = 4;
const SLOT_COUNT = 12;
const TICK_MS = 150;
const DEFAULT_WAIT_MS = 1500;
const MIN_WAIT_MS = 100;
const MAX_WAIT_MS = 4000;
const OUTCOME_CONFIRM_WAIT_MS = 5000;
const OUTCOME_OBSERVE_TIMEOUT_MS = 5000;
const LOG_LIMIT = 300;
const SAVE_DEBOUNCE_MS = 500;
const SHINING_CRYSTAL_ITEM_ID = 26322;
const RAINBOW_POWDER_ITEM_ID = 26323;

const COLOR_NAMES = ["红色", "橙色", "黄色", "绿色"];

export interface EnchantStoneTarget {
    attrId: number;
    minValue: number;
}

export interface EnchantStoneColorConfig {
    color: number;
    enabled: boolean;
    normalTarget: EnchantStoneTarget;
    specialTarget: EnchantStoneTarget;
}

export interface EnchantStoneWasherConfig {
    characterName: string;
    waitMs: number;
    colors: EnchantStoneColorConfig[];
}

export interface EnchantStoneSlotSnapshot {
    index: number;
    opened: boolean;
    attrId: number;
    value: number;
}

export interface EnchantStoneSnapshot {
    color: number;
    grade: number;
    remaining: number;
    max: number;
    topIndex: number;
    slots: EnchantStoneSlotSnapshot[];
    upgrade?: unknown;
    change?: unknown;
}

export interface EnchantStoneQuerySnapshot {
    stones: EnchantStoneSnapshot[];
    resources?: unknown;
    queriedAt: number;
}

export interface EnchantStoneWasherState {
    characterName: string;
    running: boolean;
    status: string;
    currentColor: number | null;
    currentSlot: number | null;
    lastAction: string;
    lastMessage: string;
    lastErrorCode: string;
    lastError: string;
    actionCount: number;
    optionChangeCount: number;
    upgradeCount: number;
    extendCount: number;
    startedAt: number;
    stoppedAt: number;
    lastOperationStartedAt: number;
    lastConfirmedAt: number;
    nextOperationAt: number;
    awaitingStateChange: boolean;
    updatedAt: number;
    lastSnapshot?: EnchantStoneQuerySnapshot;
}

interface PersistedDocument {
    version: number;
    configs: EnchantStoneWasherConfig[];
    states?: EnchantStoneWasherState[];
}

interface ParsedCommandError {
    code: string;
    message: string;
}

type Decision =
    | { kind: "done" }
    | { kind: "stop"; code: string; message: string; color: number; slotIndex: number }
    | { kind: "extend"; color: number; slotIndex: number }
    | { kind: "upgrade"; color: number; slotIndex: number }
    | { kind: "option-change"; color: number; slotIndex: number; useFixed: boolean };

type QueryResult =
    | { ok: true; snapshot: EnchantStoneQuerySnapshot }
    | { ok: false; error: ParsedCommandError; transport: boolean };

interface PendingOutcome {
    kind: "option-change" | "upgrade" | "extend";
    color: number;
    beforeTopIndex: number;
    beforeRemaining: number;
    afterTopIndex: number;
    afterRemaining: number;
    afterMax: number;
    afterAttrId: number;
    afterValue: number;
    upgradeFailed: boolean;
    expiresAt: number;
}

export class EnchantStoneWasher {
    private readonly registry: InstanceRegistry;
    private readonly persistFile: string;
    private readonly configs = new Map<string, EnchantStoneWasherConfig>();
    private readonly states = new Map<string, EnchantStoneWasherState>();
    private readonly logs = new Map<string, string[]>();
    private readonly inFlight = new Set<string>();
    private readonly epochs = new Map<string, number>();
    private readonly operationDeadlines = new Map<string, number>();
    private readonly pendingOutcomes = new Map<string, PendingOutcome>();
    private timer: NodeJS.Timeout | null = null;
    private saveTimer: NodeJS.Timeout | null = null;
    private destroyed = false;

    constructor(registry: InstanceRegistry, dataDir?: string) {
        this.registry = registry;
        this.persistFile = path.resolve(
            dataDir || process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
            "enchant_stone_washer.json"
        );
        this.load();
        this.timer = setInterval(() => this.tick(), TICK_MS);
    }

    listConfigs(): EnchantStoneWasherConfig[] {
        return Array.from(this.configs.values());
    }

    getConfig(characterName: string): EnchantStoneWasherConfig | undefined {
        return this.configs.get(characterName);
    }

    setConfig(characterName: string, raw: Partial<EnchantStoneWasherConfig>): EnchantStoneWasherConfig {
        const name = String(characterName || "").trim();
        if (!name) throw new Error("characterName is required");
        if (this.states.get(name)?.running) throw new Error("stop the task before editing config");

        const cfg = normalizeConfig(name, raw);
        this.configs.set(name, cfg);
        this.ensureState(name);
        this.saveNow();
        return cfg;
    }

    deleteConfig(characterName: string): boolean {
        const name = String(characterName || "").trim();
        const had = this.configs.delete(name);
        if (!had) return false;

        this.bumpEpoch(name);
        this.states.delete(name);
        this.logs.delete(name);
        this.operationDeadlines.delete(name);
        this.pendingOutcomes.delete(name);
        this.saveNow();
        return true;
    }

    getState(characterName: string): EnchantStoneWasherState | undefined {
        const state = this.states.get(characterName);
        if (state) return state;
        return this.configs.has(characterName) ? this.ensureState(characterName) : undefined;
    }

    getLogs(characterName: string): string[] {
        return [...(this.logs.get(characterName) || [])];
    }

    async refresh(characterName: string): Promise<{
        ok: boolean;
        state?: EnchantStoneWasherState;
        error?: string;
    }> {
        const name = String(characterName || "").trim();
        if (!name) return { ok: false, error: "character name is required" };
        const state = this.ensureState(name);
        if (state.running || this.inFlight.has(name)) return { ok: true, state };
        const instance = this.registry.list().find((entry) => entry.characterName === name);
        if (!instance) return { ok: false, state, error: "character offline" };

        const result = await this.query(instance.pid, true);
        if (!result.ok) return { ok: false, state, error: result.error.message };
        if (!this.isSameInstance(name, instance.pid, instance.connId)) {
            return { ok: false, state, error: "character connection changed" };
        }
        state.lastSnapshot = result.snapshot;
        state.updatedAt = Date.now();
        state.lastErrorCode = "";
        state.lastError = "";
        if (this.configs.has(name)) this.scheduleSave();
        return { ok: true, state };
    }

    start(characterName: string): { ok: boolean; state?: EnchantStoneWasherState; error?: string } {
        const cfg = this.configs.get(characterName);
        if (!cfg) return { ok: false, error: "config not found" };
        if (countEnabledColors(cfg) === 0) return { ok: false, error: "no enabled colors" };

        const current = this.ensureState(characterName);
        if (current.running) return { ok: false, state: current, error: "already running" };

        const now = Date.now();
        const state: EnchantStoneWasherState = {
            ...newState(characterName),
            running: true,
            status: "running",
            lastMessage: "任务已启动",
            startedAt: now,
            lastOperationStartedAt: current.lastOperationStartedAt,
            lastConfirmedAt: current.lastConfirmedAt,
            nextOperationAt: Math.max(
                current.nextOperationAt,
                this.operationDeadlines.get(characterName) || 0
            ),
            awaitingStateChange: this.pendingOutcomes.has(characterName),
            updatedAt: now,
        };
        this.states.set(characterName, state);
        this.bumpEpoch(characterName);
        this.appendLog(characterName, "任务启动");
        this.saveNow();
        this.tick();
        return { ok: true, state };
    }

    stop(characterName: string): { ok: boolean; state?: EnchantStoneWasherState; error?: string } {
        if (!this.configs.has(characterName)) return { ok: false, error: "config not found" };
        const state = this.ensureState(characterName);
        if (!state.running) return { ok: true, state };

        this.bumpEpoch(characterName);
        state.running = false;
        state.status = "stopped";
        state.lastMessage = "用户停止";
        state.stoppedAt = Date.now();
        state.updatedAt = state.stoppedAt;
        this.appendLog(characterName, "用户停止任务");
        this.saveNow();
        return { ok: true, state };
    }

    destroy(): void {
        this.destroyed = true;
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
        if (this.saveTimer) clearTimeout(this.saveTimer);
        this.saveTimer = null;

        const now = Date.now();
        for (const [name, state] of this.states) {
            if (!state.running) continue;
            this.bumpEpoch(name);
            state.running = false;
            state.status = "stopped";
            state.lastMessage = "broker 已停止";
            state.stoppedAt = now;
            state.updatedAt = now;
        }
        this.operationDeadlines.clear();
        this.pendingOutcomes.clear();
        this.saveNow();
    }

    private load(): void {
        try {
            if (!fs.existsSync(this.persistFile)) return;
            const doc = JSON.parse(fs.readFileSync(this.persistFile, "utf8")) as PersistedDocument;
            for (const raw of Array.isArray(doc.configs) ? doc.configs : []) {
                try {
                    const name = String(raw.characterName || "").trim();
                    if (!name) continue;
                    this.configs.set(name, normalizeConfig(name, raw));
                } catch (e: any) {
                    console.warn(`[enchantStoneWasher] skipped invalid config: ${e.message}`);
                }
            }
            for (const raw of Array.isArray(doc.states) ? doc.states : []) {
                const name = String(raw?.characterName || "").trim();
                if (!name || !this.configs.has(name)) continue;
                this.states.set(name, normalizePersistedState(name, raw));
            }
            console.log(`[enchantStoneWasher] loaded ${this.configs.size} configs`);
        } catch (e: any) {
            console.warn(`[enchantStoneWasher] load failed: ${e.message}`);
            this.configs.clear();
            this.states.clear();
        }
    }

    private saveNow(): void {
        if (this.saveTimer) clearTimeout(this.saveTimer);
        this.saveTimer = null;
        try {
            fs.mkdirSync(path.dirname(this.persistFile), { recursive: true });
            const doc: PersistedDocument = {
                version: 2,
                configs: Array.from(this.configs.values()),
                states: Array.from(this.states.values()),
            };
            fs.writeFileSync(this.persistFile, JSON.stringify(doc, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[enchantStoneWasher] save failed: ${e.message}`);
        }
    }

    private scheduleSave(): void {
        if (this.saveTimer || this.destroyed) return;
        this.saveTimer = setTimeout(() => this.saveNow(), SAVE_DEBOUNCE_MS);
    }

    private tick(): void {
        if (this.destroyed) return;
        const nameToInstance = new Map<string, { pid: number; connId: string }>();
        for (const inst of this.registry.list()) {
            if (inst.characterName) {
                nameToInstance.set(inst.characterName, { pid: inst.pid, connId: inst.connId });
            }
        }

        for (const [name, state] of this.states) {
            if (!state.running || this.inFlight.has(name)) continue;
            const cfg = this.configs.get(name);
            if (!cfg) continue;
            const instance = nameToInstance.get(name);
            if (!instance) {
                this.setWaitingMessage(state, "角色离线，等待上线");
                continue;
            }

            const nextOperationAt = Math.max(
                state.nextOperationAt,
                this.operationDeadlines.get(name) || 0
            );
            if (Date.now() < nextOperationAt) {
                state.nextOperationAt = nextOperationAt;
                this.setWaitingMessage(
                    state,
                    state.awaitingStateChange
                        ? "上一操作结果已确认，等待全局操作间隔"
                        : "等待全局操作间隔"
                );
                continue;
            }

            const epoch = this.epochs.get(name) || 0;
            this.inFlight.add(name);
            void this.runOne(cfg, state, instance.pid, instance.connId, epoch)
                .catch((e: any) => {
                    if (this.isActive(name, state, epoch)) {
                        this.finish(name, state, "error", "broker-error", e.message || String(e));
                    }
                })
                .finally(() => this.inFlight.delete(name));
        }
    }

    private async runOne(
        cfg: EnchantStoneWasherConfig,
        state: EnchantStoneWasherState,
        pid: number,
        connId: string,
        epoch: number
    ): Promise<void> {
        const query = await this.query(pid);
        if (!this.isActive(cfg.characterName, state, epoch)) return;

        if (!query.ok) {
            if (query.error.code === "invalid-query" || isTerminalResourceCode(query.error.code)) {
                this.finish(cfg.characterName, state, query.error.code, query.error.code, query.error.message);
            } else {
                this.setTransientError(cfg.characterName, state, query.error.code, query.error.message);
            }
            return;
        }

        if (!this.isSameInstance(cfg.characterName, pid, connId)) {
            this.setTransientError(
                cfg.characterName,
                state,
                "instance-changed-requery",
                "角色连接在查询期间发生变化，已取消本轮操作并重新查询"
            );
            return;
        }

        state.lastSnapshot = query.snapshot;
        state.updatedAt = Date.now();
        state.lastErrorCode = "";
        state.lastError = "";

        const pending = this.pendingOutcomes.get(cfg.characterName);
        if (pending) {
            const observed = pendingOutcomeObserved(pending, query.snapshot);
            if (!observed) {
                state.awaitingStateChange = true;
                if (Date.now() >= pending.expiresAt) {
                    this.pendingOutcomes.delete(cfg.characterName);
                    this.finish(
                        cfg.characterName,
                        state,
                        "state-not-applied",
                        "state-not-applied",
                        "全局间隔已满足，但游戏状态仍未体现上一操作，已停止以避免重复发包"
                    );
                } else {
                    this.setWaitingMessage(state, "全局间隔已满足，等待游戏状态体现上一操作");
                }
                return;
            }
            this.pendingOutcomes.delete(cfg.characterName);
            state.awaitingStateChange = false;
        }

        const decision = decideNext(cfg, query.snapshot);
        state.currentColor = decision.kind === "done" ? null : decision.color;
        state.currentSlot = decision.kind === "done" ? null : decision.slotIndex;

        if (decision.kind === "done") {
            this.finish(cfg.characterName, state, "done", "", "所有已启用颜色均已洗到第 12 格并达标");
            return;
        }
        if (decision.kind === "stop") {
            this.finish(cfg.characterName, state, decision.code, decision.code, decision.message);
            return;
        }

        await this.executeDecision(cfg, state, pid, connId, epoch, decision);
    }

    private async query(pid: number, includeInventory = false): Promise<QueryResult> {
        let result: { ok: boolean; detail?: string };
        try {
            result = await this.registry.sendCommand(pid, "queryEnchantStones", {}, 5000);
        } catch (e: any) {
            return {
                ok: false,
                error: { code: "query-transport", message: e.message || String(e) },
                transport: true,
            };
        }
        if (!result.ok) {
            return { ok: false, error: parseCommandError(result.detail, "query-failed"), transport: false };
        }
        try {
            const snapshot = parseQuerySnapshot(result.detail);
            if (includeInventory) await this.enrichInventory(pid, snapshot);
            return { ok: true, snapshot };
        } catch (e: any) {
            return {
                ok: false,
                error: { code: "invalid-query", message: e.message || String(e) },
                transport: false,
            };
        }
    }

    private async enrichInventory(pid: number, snapshot: EnchantStoneQuerySnapshot): Promise<void> {
        const source = snapshot.resources && typeof snapshot.resources === "object" &&
            !Array.isArray(snapshot.resources)
            ? snapshot.resources as Record<string, unknown>
            : {};
        if (source.shiningCrystal && source.rainbowPowder) return;

        try {
            const result = await this.registry.sendCommand(pid, "getBagItems", {}, 5000);
            if (!result.ok) return;
            const parsed = parseJson(result.detail);
            if (!Array.isArray(parsed)) return;

            const summarize = (itemId: number): Record<string, number> => {
                let owned = 0;
                let selectedCount = 0;
                let slotIndex = -1;
                let wire = 0;
                for (const raw of parsed) {
                    if (firstFiniteInt(raw?.itemId, 0) !== itemId) continue;
                    const count = Math.max(0, firstFiniteInt(raw?.count, 0));
                    owned += count;
                    if (count <= selectedCount) continue;
                    selectedCount = count;
                    slotIndex = nullableInt(raw?.slotIndex) ?? -1;
                    wire = nullableInt(raw?.bagId) ?? (slotIndex >= 0 ? slotIndex + 13 : 0);
                }
                return { itemId, owned, selectedCount, slotIndex, wire };
            };

            snapshot.resources = {
                ...source,
                shiningCrystal: source.shiningCrystal || summarize(SHINING_CRYSTAL_ITEM_ID),
                rainbowPowder: source.rainbowPowder || summarize(RAINBOW_POWDER_ITEM_ID),
            };
        } catch {
            // Older DLLs may not expose getBagItems; the stone snapshot remains usable.
        }
    }

    private async executeDecision(
        cfg: EnchantStoneWasherConfig,
        state: EnchantStoneWasherState,
        pid: number,
        connId: string,
        epoch: number,
        decision: Exclude<Decision, { kind: "done" } | { kind: "stop" }>
    ): Promise<void> {
        let action: string;
        let args: Record<string, unknown>;
        let description: string;

        if (decision.kind === "extend") {
            action = "enchantStoneExtend";
            args = { color: decision.color, waitMs: OUTCOME_CONFIRM_WAIT_MS };
            description = `${colorName(decision.color)}变更次数为 0，使用充值券`;
        } else if (decision.kind === "upgrade") {
            action = "enchantStoneUpgrade";
            args = { color: decision.color, waitMs: OUTCOME_CONFIRM_WAIT_MS };
            description = `${colorName(decision.color)}尝试开放第 ${decision.slotIndex + 1} 格`;
        } else {
            action = "enchantStoneOptionChange";
            args = {
                color: decision.color,
                useFixed: decision.useFixed,
                waitMs: OUTCOME_CONFIRM_WAIT_MS,
            };
            description = decision.useFixed
                ? `${colorName(decision.color)}第 ${decision.slotIndex + 1} 格属性已命中，固定属性洗值`
                : `${colorName(decision.color)}第 ${decision.slotIndex + 1} 格洗目标属性`;
        }

        try {
            if (!this.isSameInstance(cfg.characterName, pid, connId)) {
                this.setTransientError(
                    cfg.characterName,
                    state,
                    "instance-changed-requery",
                    "角色连接已变化，本轮未发送操作"
                );
                return;
            }

            const operationStartedAt = Date.now();
            const provisionalDeadline = operationStartedAt + cfg.waitMs;
            this.operationDeadlines.set(cfg.characterName, provisionalDeadline);
            state.lastOperationStartedAt = operationStartedAt;
            state.nextOperationAt = provisionalDeadline;
            state.awaitingStateChange = false;
            state.lastAction = action;
            state.lastMessage = description;
            state.actionCount += 1;
            if (decision.kind === "extend") state.extendCount += 1;
            if (decision.kind === "upgrade") state.upgradeCount += 1;
            if (decision.kind === "option-change") state.optionChangeCount += 1;
            state.updatedAt = operationStartedAt;
            this.appendLog(cfg.characterName, description);
            this.scheduleSave();

            const timeoutMs = OUTCOME_CONFIRM_WAIT_MS + 3500;
            const result = await this.registry.sendCommand(pid, action, args, timeoutMs);
            if (!this.isActive(cfg.characterName, state, epoch)) return;
            if (!result.ok) {
                const error = parseCommandError(result.detail, `${action}-failed`);
                if (isRetryablePreflightCode(error.code)) {
                    this.setTransientError(cfg.characterName, state, error.code, error.message);
                    return;
                }
                const status = isTerminalResourceCode(error.code) ? error.code : "error";
                this.finish(cfg.characterName, state, status, error.code, error.message);
                return;
            }

            const detail = tryParseJson(result.detail);
            if (detail?.confirmed === false) {
                this.finish(
                    cfg.characterName,
                    state,
                    "outcome-unknown",
                    "outcome-unknown",
                    "操作结果未确认，为避免重复扣除已暂停；请确认游戏状态后重新启动"
                );
                return;
            }
            if (detail?.outcomeValid !== true) {
                this.finish(
                    cfg.characterName,
                    state,
                    "unexpected-outcome",
                    "unexpected-outcome",
                    "服务端结果与本地状态不一致，已暂停以避免重复发包"
                );
                return;
            }
            if (decision.kind === "option-change" && detail?.applied !== true) {
                this.finish(
                    cfg.characterName,
                    state,
                    "unexpected-outcome",
                    "unexpected-outcome",
                    "属性变更回包未体现次数扣减，已暂停以避免重复发包"
                );
                return;
            }
            if (decision.kind === "extend" && detail?.extended !== true) {
                this.finish(
                    cfg.characterName,
                    state,
                    "unexpected-outcome",
                    "unexpected-outcome",
                    "充值回包未体现次数增加，已暂停以避免重复发包"
                );
                return;
            }

            const confirmedAt = Date.now();
            const nextOperationAt = confirmedAt + cfg.waitMs;
            const pending = buildPendingOutcome(
                decision,
                detail,
                nextOperationAt + OUTCOME_OBSERVE_TIMEOUT_MS
            );
            if (!pending) {
                this.finish(
                    cfg.characterName,
                    state,
                    "unexpected-outcome",
                    "unexpected-outcome",
                    "操作回包缺少可复核的前后状态，已暂停以避免重复发包"
                );
                return;
            }

            this.operationDeadlines.set(cfg.characterName, nextOperationAt);
            this.pendingOutcomes.set(cfg.characterName, pending);
            state.lastConfirmedAt = confirmedAt;
            state.nextOperationAt = nextOperationAt;
            state.awaitingStateChange = true;
            if (decision.kind === "upgrade" && detail?.confirmed === true && detail?.opened === false) {
                state.lastMessage = "强化失败结果已确认，等待全局间隔后复核状态";
                this.appendLog(cfg.characterName, state.lastMessage);
            } else {
                state.lastMessage = `${description}：状态变化已确认，等待全局间隔`;
            }
            state.updatedAt = Date.now();
            this.scheduleSave();
        } catch (e: any) {
            if (!this.isActive(cfg.characterName, state, epoch)) return;
            this.finish(
                cfg.characterName,
                state,
                "outcome-unknown",
                "outcome-unknown",
                `${action} 未收到完整 ACK，可能已经执行；为避免重复扣除已暂停：${e.message || String(e)}`
            );
        }
    }

    private finish(
        name: string,
        state: EnchantStoneWasherState,
        status: string,
        errorCode: string,
        message: string
    ): void {
        if (!state.running && state.status === status && state.lastMessage === message) return;
        this.bumpEpoch(name);
        state.running = false;
        state.status = status;
        state.lastMessage = message;
        state.lastErrorCode = errorCode;
        state.lastError = errorCode ? message : "";
        state.awaitingStateChange = false;
        state.stoppedAt = Date.now();
        state.updatedAt = state.stoppedAt;
        this.appendLog(name, `${status}: ${message}`);
        this.saveNow();
    }

    private setTransientError(
        name: string,
        state: EnchantStoneWasherState,
        code: string,
        message: string
    ): void {
        const changed = state.lastErrorCode !== code || state.lastError !== message;
        state.lastErrorCode = code;
        state.lastError = message;
        state.lastMessage = message;
        state.updatedAt = Date.now();
        if (changed) this.appendLog(name, `${code}: ${message}`);
        this.scheduleSave();
    }

    private setWaitingMessage(state: EnchantStoneWasherState, message: string): void {
        if (state.lastMessage === message) return;
        state.lastMessage = message;
        state.updatedAt = Date.now();
        this.scheduleSave();
    }

    private appendLog(name: string, message: string): void {
        const line = `[${new Date().toLocaleTimeString()}] ${message}`;
        let lines = this.logs.get(name);
        if (!lines) {
            lines = [];
            this.logs.set(name, lines);
        }
        lines.push(line);
        if (lines.length > LOG_LIMIT) lines.splice(0, lines.length - LOG_LIMIT);
    }

    private ensureState(name: string): EnchantStoneWasherState {
        let state = this.states.get(name);
        if (!state) {
            state = newState(name);
            this.states.set(name, state);
        }
        return state;
    }

    private bumpEpoch(name: string): number {
        const next = (this.epochs.get(name) || 0) + 1;
        this.epochs.set(name, next);
        return next;
    }

    private isActive(name: string, state: EnchantStoneWasherState, epoch: number): boolean {
        return !this.destroyed && this.states.get(name) === state && state.running &&
            (this.epochs.get(name) || 0) === epoch;
    }

    private isSameInstance(name: string, pid: number, connId: string): boolean {
        return this.registry.list().some((instance) =>
            instance.pid === pid &&
            instance.connId === connId &&
            instance.characterName === name
        );
    }
}

function newState(characterName: string): EnchantStoneWasherState {
    return {
        characterName,
        running: false,
        status: "idle",
        currentColor: null,
        currentSlot: null,
        lastAction: "",
        lastMessage: "",
        lastErrorCode: "",
        lastError: "",
        actionCount: 0,
        optionChangeCount: 0,
        upgradeCount: 0,
        extendCount: 0,
        startedAt: 0,
        stoppedAt: 0,
        lastOperationStartedAt: 0,
        lastConfirmedAt: 0,
        nextOperationAt: 0,
        awaitingStateChange: false,
        updatedAt: Date.now(),
    };
}

function normalizePersistedState(name: string, raw: Partial<EnchantStoneWasherState>): EnchantStoneWasherState {
    const state: EnchantStoneWasherState = {
        ...newState(name),
        status: typeof raw.status === "string" ? raw.status : "idle",
        currentColor: nullableInt(raw.currentColor),
        currentSlot: nullableInt(raw.currentSlot),
        lastAction: stringValue(raw.lastAction),
        lastMessage: stringValue(raw.lastMessage),
        lastErrorCode: stringValue(raw.lastErrorCode),
        lastError: stringValue(raw.lastError),
        actionCount: nonNegativeInt(raw.actionCount),
        optionChangeCount: nonNegativeInt(raw.optionChangeCount),
        upgradeCount: nonNegativeInt(raw.upgradeCount),
        extendCount: nonNegativeInt(raw.extendCount),
        startedAt: nonNegativeNumber(raw.startedAt),
        stoppedAt: nonNegativeNumber(raw.stoppedAt),
        lastOperationStartedAt: nonNegativeNumber(raw.lastOperationStartedAt),
        lastConfirmedAt: nonNegativeNumber(raw.lastConfirmedAt),
        nextOperationAt: nonNegativeNumber(raw.nextOperationAt),
        awaitingStateChange: false,
        updatedAt: nonNegativeNumber(raw.updatedAt) || Date.now(),
        lastSnapshot: raw.lastSnapshot,
    };
    if (raw.running || state.status === "running") {
        state.running = false;
        state.status = "stopped";
        state.lastMessage = "broker 重启，任务未自动恢复";
        state.stoppedAt = Date.now();
        state.updatedAt = state.stoppedAt;
    }
    return state;
}

function normalizeConfig(name: string, raw: Partial<EnchantStoneWasherConfig>): EnchantStoneWasherConfig {
    const sourceColors = Array.isArray(raw.colors) ? raw.colors : [];
    const colors: EnchantStoneColorConfig[] = [];

    for (let color = 0; color < COLOR_COUNT; ++color) {
        const indexed = sourceColors[color];
        const source = sourceColors.find((entry) => Number(entry?.color) === color) ||
            (indexed && indexed.color === undefined ? indexed : undefined);
        const sourceRecord = (source || {}) as any;
        const legacyTargets = Array.isArray(sourceRecord.targets)
            ? sourceRecord.targets
            : Array.isArray(sourceRecord.slots)
                ? sourceRecord.slots
                : [];
        const legacyNormal = legacyTargets.find((entry: any, slot: number) =>
            entry?.enabled === true && !isSpecialSlot(slot)
        );
        const legacySpecial = legacyTargets.find((entry: any, slot: number) =>
            entry?.enabled === true && isSpecialSlot(slot)
        );

        // Version 1 used per-slot switches. They cannot be mapped safely to the
        // new per-color session switch, so migrated colors stay disabled until
        // the user reviews and enables them. Target values are preserved where possible.
        const enabled = sourceRecord.enabled === true;
        const normalTarget = normalizeTarget(
            color,
            false,
            sourceRecord.normalTarget || legacyNormal
        );
        const specialTarget = normalizeTarget(
            color,
            true,
            sourceRecord.specialTarget || legacySpecial
        );
        colors.push({ color, enabled, normalTarget, specialTarget });
    }

    return {
        characterName: name,
        waitMs: clampInt(numberValue(raw.waitMs) || DEFAULT_WAIT_MS, MIN_WAIT_MS, MAX_WAIT_MS),
        colors,
    };
}

function attrAllowed(color: number, slot: number, attrId: number): boolean {
    if (attrId >= 1 && attrId <= 6) return true;
    const normalByColor = [7, 8, 9, 9];
    if (attrId === normalByColor[color]) return true;
    const specialByColor = [10, 11, 12, 13];
    return (slot + 1) % 3 === 0 && attrId === specialByColor[color];
}

function isSpecialSlot(slot: number): boolean {
    return (slot + 1) % 3 === 0;
}

function defaultTarget(color: number, special: boolean): EnchantStoneTarget {
    return {
        attrId: special ? [10, 11, 12, 13][color] : 1,
        minValue: 0,
    };
}

function normalizeTarget(color: number, special: boolean, raw: any): EnchantStoneTarget {
    const fallback = defaultTarget(color, special);
    if (!raw || typeof raw !== "object") return fallback;

    const attrId = integerValue(raw.attrId) || fallback.attrId;
    const minValue = integerValue(raw.minValue);
    const slot = special ? 2 : 0;
    if (!attrAllowed(color, slot, attrId)) {
        throw new Error(
            `${colorName(color)}${special ? "特殊格" : "普通格"}: attrId ${attrId} cannot appear here`
        );
    }
    if (minValue < 0) {
        throw new Error(`${colorName(color)}${special ? "特殊格" : "普通格"}: minValue must be non-negative`);
    }
    return { attrId, minValue };
}

function countEnabledColors(cfg: EnchantStoneWasherConfig): number {
    return cfg.colors.filter((color) => color.enabled).length;
}

function decideNext(cfg: EnchantStoneWasherConfig, snapshot: EnchantStoneQuerySnapshot): Decision {
    for (const colorCfg of cfg.colors) {
        if (!colorCfg.enabled) continue;

        const stone = snapshot.stones.find((entry) => entry.color === colorCfg.color);
        if (!stone) {
            return {
                kind: "stop",
                code: "invalid-query",
                message: `查询结果缺少${colorName(colorCfg.color)}石头`,
                color: colorCfg.color,
                slotIndex: 0,
            };
        }

        if (stone.topIndex < 0) {
            return { kind: "upgrade", color: colorCfg.color, slotIndex: 0 };
        }
        if (stone.topIndex >= SLOT_COUNT) {
            return {
                kind: "stop",
                code: "invalid-query",
                message: `${colorName(colorCfg.color)}当前顶格 ${stone.topIndex + 1} 超出范围`,
                color: colorCfg.color,
                slotIndex: SLOT_COUNT - 1,
            };
        }

        const slotIndex = stone.topIndex;
        const actual = stone.slots[slotIndex];
        if (!actual?.opened) {
            return {
                kind: "stop",
                code: "invalid-query",
                message: `${colorName(colorCfg.color)}第 ${slotIndex + 1} 格被标记为顶格但尚未开放`,
                color: colorCfg.color,
                slotIndex,
            };
        }
        const target = isSpecialSlot(slotIndex) ? colorCfg.specialTarget : colorCfg.normalTarget;
        if (actual.attrId !== target.attrId) {
            if (stone.remaining === 0) return { kind: "extend", color: colorCfg.color, slotIndex };
            return { kind: "option-change", color: colorCfg.color, slotIndex, useFixed: false };
        }
        if (actual.value < target.minValue) {
            if (stone.remaining === 0) return { kind: "extend", color: colorCfg.color, slotIndex };
            return { kind: "option-change", color: colorCfg.color, slotIndex, useFixed: true };
        }
        if (slotIndex < SLOT_COUNT - 1) {
            return { kind: "upgrade", color: colorCfg.color, slotIndex: slotIndex + 1 };
        }
    }
    return { kind: "done" };
}

function buildPendingOutcome(
    decision: Exclude<Decision, { kind: "done" } | { kind: "stop" }>,
    detail: any,
    expiresAt: number
): PendingOutcome | null {
    const before = actionStoneSummary(detail?.before);
    const after = actionStoneSummary(detail?.after);
    if (!before || !after) return null;

    let kind: PendingOutcome["kind"];
    let upgradeFailed = false;
    if (decision.kind === "option-change") {
        kind = "option-change";
        if (after.topIndex !== before.topIndex || after.remaining >= before.remaining) return null;
    } else if (decision.kind === "extend") {
        kind = "extend";
        if (after.topIndex !== before.topIndex || after.remaining <= before.remaining) return null;
    } else {
        kind = "upgrade";
        upgradeFailed = detail?.failed === true && detail?.opened !== true;
        if (!upgradeFailed && after.topIndex <= before.topIndex) return null;
    }

    return {
        kind,
        color: decision.color,
        beforeTopIndex: before.topIndex,
        beforeRemaining: before.remaining,
        afterTopIndex: after.topIndex,
        afterRemaining: after.remaining,
        afterMax: after.max,
        afterAttrId: after.attrId,
        afterValue: after.value,
        upgradeFailed,
        expiresAt,
    };
}

function pendingOutcomeObserved(
    pending: PendingOutcome,
    snapshot: EnchantStoneQuerySnapshot
): boolean {
    const stone = snapshot.stones.find((entry) => entry.color === pending.color);
    if (!stone) return false;

    if (pending.kind === "option-change") {
        if (stone.topIndex !== pending.afterTopIndex || stone.max !== pending.afterMax) return false;
        if (stone.remaining > pending.afterRemaining || stone.remaining >= pending.beforeRemaining) return false;
        if (stone.remaining < pending.afterRemaining) return true;
        const top = stone.slots[pending.afterTopIndex];
        return !!top?.opened && top.attrId === pending.afterAttrId && top.value === pending.afterValue;
    }
    if (pending.kind === "extend") {
        return stone.topIndex === pending.afterTopIndex &&
            stone.max === pending.afterMax &&
            stone.remaining >= pending.afterRemaining;
    }
    if (pending.upgradeFailed) {
        // Result code 17 is the authoritative outcome for a probabilistic failure;
        // a fresh query is still required, but the stone structure is expected not to change.
        return stone.topIndex >= pending.beforeTopIndex;
    }
    const opened = stone.slots[pending.afterTopIndex];
    return stone.topIndex >= pending.afterTopIndex && !!opened?.opened;
}

function actionStoneSummary(raw: any): {
    topIndex: number;
    remaining: number;
    max: number;
    attrId: number;
    value: number;
} | null {
    if (!raw || typeof raw !== "object") return null;
    const topIndexValue = firstFiniteNumber(raw.topIndex, raw.grade, Number.NaN);
    const remainingValue = firstFiniteNumber(raw.remaining, Number.NaN);
    const maxValue = firstFiniteNumber(raw.max, Number.NaN);
    if (!Number.isFinite(topIndexValue) ||
        !Number.isFinite(remainingValue) ||
        !Number.isFinite(maxValue)) {
        return null;
    }
    const topIndex = Math.trunc(topIndexValue);
    const slots = Array.isArray(raw.slots) ? raw.slots : [];
    const top = topIndex >= 0 ? (slots[topIndex] || {}) : {};
    return {
        topIndex,
        remaining: Math.trunc(remainingValue),
        max: Math.trunc(maxValue),
        attrId: firstFiniteInt(top.attrId, top.attributeId, top.id, 0),
        value: firstFiniteNumber(top.value, top.attrValue, 0),
    };
}

function parseQuerySnapshot(detail: unknown): EnchantStoneQuerySnapshot {
    const parsed = parseJson(detail);
    const root = Array.isArray(parsed) ? { stones: parsed } : parsed;
    if (!root || typeof root !== "object") throw new Error("queryEnchantStones returned a non-object");
    const rawStones = Array.isArray((root as any).stones)
        ? (root as any).stones
        : Array.isArray((root as any).colors)
            ? (root as any).colors
            : null;
    if (!rawStones) throw new Error("queryEnchantStones detail has no stones array");

    const stones: EnchantStoneSnapshot[] = [];
    for (let index = 0; index < rawStones.length; ++index) {
        const raw = rawStones[index] || {};
        const color = firstFiniteInt(raw.color, raw.colorIdx, raw.index, index);
        if (color < 0 || color >= COLOR_COUNT) continue;

        const rawSlots = Array.isArray(raw.slots)
            ? raw.slots
            : Array.isArray(raw.attrs)
                ? raw.attrs
                : [];
        const ids = Array.isArray(raw.attrIds) ? raw.attrIds : Array.isArray(raw.ids) ? raw.ids : [];
        const values = Array.isArray(raw.values) ? raw.values : [];
        const slots: EnchantStoneSlotSnapshot[] = [];
        for (let slot = 0; slot < SLOT_COUNT; ++slot) {
            const item = rawSlots[slot] || {};
            const attrId = firstFiniteInt(item.attrId, item.attributeId, item.id, ids[slot], 0);
            const value = firstFiniteNumber(item.value, item.attrValue, values[slot], 0);
            const opened = typeof item.opened === "boolean"
                ? item.opened
                : typeof item.enabled === "boolean"
                    ? item.enabled
                    : attrId !== 0;
            slots.push({ index: slot, opened, attrId, value });
        }

        const gradeValue = firstFiniteNumber(raw.grade, raw.tier, Number.NaN);
        const grade = Number.isFinite(gradeValue) ? Math.trunc(gradeValue) : Number.NaN;
        let topIndex: number;
        if (Number.isFinite(grade)) {
            topIndex = grade;
        } else if (Number.isFinite(Number(raw.topIndex))) {
            topIndex = Math.trunc(Number(raw.topIndex));
        } else if (Number.isFinite(Number(raw.currentSlot))) {
            topIndex = Math.trunc(Number(raw.currentSlot));
        } else if (Number.isFinite(Number(raw.openCount))) {
            topIndex = Math.trunc(Number(raw.openCount)) - 1;
        } else if (Number.isFinite(Number(raw.openedCount))) {
            topIndex = Math.trunc(Number(raw.openedCount)) - 1;
        } else {
            topIndex = -1;
            for (const slot of slots) if (slot.opened) topIndex = slot.index;
        }
        topIndex = clampInt(topIndex, -1, SLOT_COUNT - 1);
        if (Number.isFinite(Number(raw.openCount)) &&
            Math.trunc(Number(raw.openCount)) !== topIndex + 1) {
            throw new Error(`${colorName(color)} stone grade/openCount mismatch`);
        }

        const remaining = firstFiniteNumber(
            raw.remaining,
            raw.remainingChanges,
            raw.remainingCount,
            raw.remainCount,
            raw.changeRemaining,
            (root as any).remaining?.[color],
            Number.NaN
        );
        if (!Number.isFinite(remaining)) {
            throw new Error(`${colorName(color)} stone has no remaining count`);
        }
        const max = firstFiniteNumber(
            raw.max,
            raw.maxChanges,
            raw.maxCount,
            raw.changeMax,
            (root as any).max?.[color],
            0
        );
        stones.push({
            color,
            grade: Number.isFinite(grade) ? grade : topIndex,
            remaining: Math.max(0, Math.trunc(remaining)),
            max: Math.max(0, Math.trunc(max)),
            topIndex,
            slots,
            upgrade: raw.upgrade,
            change: raw.change,
        });
    }
    return {
        stones: stones.sort((a, b) => a.color - b.color),
        resources: (root as any).resources,
        queriedAt: Date.now(),
    };
}

function parseCommandError(detail: unknown, fallbackCode: string): ParsedCommandError {
    const parsed = tryParseJson(detail);
    const source = parsed?.error && typeof parsed.error === "object" ? parsed.error : parsed;
    const rawCode = source?.code ?? source?.errorCode ?? parsed?.code ?? fallbackCode;
    const rawMessage = source?.message ?? source?.detail ?? parsed?.message ?? parsed?.detail;
    const message = typeof rawMessage === "string" && rawMessage
        ? rawMessage
        : typeof detail === "string" && detail
            ? detail
            : fallbackCode;
    return { code: normalizeErrorCode(rawCode || fallbackCode), message };
}

function isTerminalResourceCode(code: string): boolean {
    return code === "no-fixed-coupon" ||
        code === "no-recharge-ticket" ||
        code === "insufficient-money" ||
        code === "max-recharge" ||
        code.startsWith("insufficient-");
}

function isRetryablePreflightCode(code: string): boolean {
    return code === "state-changed-requery" ||
        code === "no-change-count" ||
        code === "stone-unopened" ||
        code === "stone-max-grade";
}

function normalizeErrorCode(value: unknown): string {
    return String(value || "error").trim().toLowerCase().replace(/_/g, "-").replace(/\s+/g, "-");
}

function parseJson(value: unknown): any {
    if (typeof value !== "string") return value;
    const text = value.trim();
    if (!text) throw new Error("empty command detail");
    return JSON.parse(text);
}

function tryParseJson(value: unknown): any {
    try {
        return parseJson(value);
    } catch {
        return null;
    }
}

function colorName(color: number): string {
    return COLOR_NAMES[color] || `颜色 ${color}`;
}

function firstFiniteNumber(...values: unknown[]): number {
    for (const value of values) {
        if (value === null || value === undefined || value === "") continue;
        const n = Number(value);
        if (Number.isFinite(n)) return n;
    }
    return Number.NaN;
}

function firstFiniteInt(...values: unknown[]): number {
    const n = firstFiniteNumber(...values);
    return Number.isFinite(n) ? Math.trunc(n) : 0;
}

function numberValue(value: unknown): number {
    const n = Number(value);
    return Number.isFinite(n) ? n : 0;
}

function integerValue(value: unknown): number {
    return Math.trunc(numberValue(value));
}

function nonNegativeInt(value: unknown): number {
    return Math.max(0, integerValue(value));
}

function nonNegativeNumber(value: unknown): number {
    return Math.max(0, numberValue(value));
}

function nullableInt(value: unknown): number | null {
    if (value === null || value === undefined) return null;
    const n = Number(value);
    return Number.isFinite(n) ? Math.trunc(n) : null;
}

function stringValue(value: unknown): string {
    return typeof value === "string" ? value : "";
}

function clampInt(value: number, min: number, max: number): number {
    if (!Number.isFinite(value)) return min;
    return Math.max(min, Math.min(max, Math.trunc(value)));
}
