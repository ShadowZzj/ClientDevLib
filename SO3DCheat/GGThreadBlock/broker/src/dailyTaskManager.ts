import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

export const DAILY_TASK_GRADES = ["SSS", "SS", "S", "A", "B", "C"] as const;
export type DailyTaskGrade = (typeof DAILY_TASK_GRADES)[number];

export interface DailyTaskCharacterConfig {
    characterName: string;
    enabled: boolean;
    minGrade: DailyTaskGrade;
}

export interface DailyTaskEntry {
    index: number;
    taskId: number;
    state: number;
    progress: number;
    gradeRaw: number;
    gradeName: string;
    replaceable: boolean;
    hunt: boolean;
}

export interface DailyTaskRuntimeState {
    status: "idle" | "offline" | "running" | "done" | "unsafe" | "error" | "uncertain";
    lastAction: string;
    lastResult: string;
    lastRunAt: number;
    changeChance: number;
    completedCount: number;
    tasks: DailyTaskEntry[];
}

export interface DailyTaskCharacterView extends DailyTaskCharacterConfig, DailyTaskRuntimeState {}

interface PersistShape {
    characters: DailyTaskCharacterConfig[];
}

interface DailyTaskSnapshot {
    taskCount: number;
    changeChance: number;
    completedCount: number;
    safeLayout: boolean;
    tasks: DailyTaskEntry[];
}

interface StepResult {
    ok: boolean;
    acted: boolean;
}

type DailyTaskCommand = "finishDailyTask" | "rerollDailyTask" | "acceptDailyTask";

interface NextAction {
    command: DailyTaskCommand;
    task: DailyTaskEntry;
    label: string;
}

interface PendingProgressCheck {
    command: DailyTaskCommand;
    index: number;
    beforeChangeChance: number;
    beforeState: number;
}

const TICK_MS = 500;
const POLL_MS = 3000;
const RETRY_MS = 5000;
const QUERY_TIMEOUT_MS = 5000;
const ACTION_WAIT_MS = 5000;
const ACTION_TIMEOUT_MS = 8000;
const MAX_ACTIONS_PER_RUN = 24;
const AUTO_RUN_DEADLINE_MS = 60000;
const MANUAL_RUN_DEADLINE_MS = 30000;
const FIRST_HUNT_INDEX = 3;
const LAST_HUNT_INDEX = 7;
const EXPECTED_TASK_COUNT = 8;

export class DailyTaskManager {
    private readonly registry: InstanceRegistry;
    private readonly persistFile: string;
    private configs: DailyTaskCharacterConfig[] = [];
    private readonly runtime = new Map<string, DailyTaskRuntimeState>();
    private readonly nextRunAt = new Map<string, number>();
    private readonly configRevision = new Map<string, number>();
    private readonly inFlight = new Set<string>();
    private timer: NodeJS.Timeout | null = null;
    private stopping = false;

    constructor(registry: InstanceRegistry, dataDir: string) {
        this.registry = registry;
        this.persistFile = path.resolve(dataDir, "daily_task.json");
        this.load();
        this.timer = setInterval(() => this.tick(), TICK_MS);
    }

    private load(): void {
        try {
            if (!fs.existsSync(this.persistFile)) return;
            const parsed = JSON.parse(fs.readFileSync(this.persistFile, "utf8")) as PersistShape;
            const characters = Array.isArray(parsed.characters) ? parsed.characters : [];
            this.configs = characters
                .map((config) => normalizeConfig(config))
                .filter((config) => config.characterName.length > 0);
            console.log(`[dailyTask] loaded ${this.configs.length} character configs`);
        } catch (e: any) {
            console.warn(`[dailyTask] load failed: ${e.message}`);
            this.configs = [];
        }
    }

    private save(): void {
        const tempFile = `${this.persistFile}.tmp`;
        try {
            fs.mkdirSync(path.dirname(this.persistFile), { recursive: true });
            fs.writeFileSync(tempFile, JSON.stringify({ characters: this.configs } satisfies PersistShape, null, 2), "utf8");
            fs.renameSync(tempFile, this.persistFile);
        } catch (e: any) {
            try {
                if (fs.existsSync(tempFile)) fs.unlinkSync(tempFile);
            } catch {
                // 保留原始保存错误。
            }
            console.warn(`[dailyTask] save failed: ${e.message}`);
        }
    }

    private tick(): void {
        if (this.stopping) return;
        const now = Date.now();
        const online = new Map<string, number>();
        for (const instance of this.registry.list()) {
            if (instance.characterName) online.set(instance.characterName, instance.pid);
        }

        for (const config of this.configs) {
            if (!config.enabled) continue;
            const pid = online.get(config.characterName);
            if (pid === undefined) {
                this.patchRuntime(config.characterName, { status: "offline", lastResult: "角色离线" });
                continue;
            }
            if (this.inFlight.has(config.characterName)) continue;
            if (now < (this.nextRunAt.get(config.characterName) ?? 0)) continue;

            this.inFlight.add(config.characterName);
            this.runOne(config, pid, true).finally(() => this.inFlight.delete(config.characterName));
        }
    }

    private async runOne(
        config: DailyTaskCharacterConfig,
        pid: number,
        requireEnabled = false
    ): Promise<StepResult> {
        const revision = this.configRevision.get(config.characterName) ?? 0;
        const startedAt = Date.now();
        const deadlineMs = requireEnabled ? AUTO_RUN_DEADLINE_MS : MANUAL_RUN_DEADLINE_MS;
        let acted = false;
        let actionCount = 0;
        let pendingProgress: PendingProgressCheck | null = null;

        while (true) {
            if (this.stopping) return { ok: true, acted };
            const currentConfig = this.configs.find((entry) => entry.characterName === config.characterName);
            if (!currentConfig || (this.configRevision.get(config.characterName) ?? 0) !== revision)
                return { ok: true, acted };
            if (requireEnabled && !currentConfig.enabled) {
                this.patchRuntime(config.characterName, {
                    status: "idle",
                    lastAction: "",
                    lastResult: "自动处理已关闭",
                });
                return { ok: true, acted };
            }
            if (!this.registry.list().some(
                (instance) => instance.characterName === config.characterName && instance.pid === pid
            )) {
                this.patchRuntime(config.characterName, {
                    status: "offline",
                    lastResult: "角色已离线或游戏进程已更换",
                });
                this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                return { ok: false, acted };
            }
            config = currentConfig;

            let snapshot: DailyTaskSnapshot;
            try {
                const reply = await this.registry.sendCommand(pid, "queryDailyTasks", {}, QUERY_TIMEOUT_MS);
                if (!reply.ok) {
                    if (!this.isRunCurrent(config.characterName, revision, pid, requireEnabled))
                        return { ok: true, acted };
                    this.fail(config.characterName, "error", `查询失败: ${reply.detail || "DLL 拒绝查询"}`);
                    this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                    return { ok: false, acted };
                }
                snapshot = parseSnapshot(reply.detail);
            } catch (e: any) {
                if (!this.isRunCurrent(config.characterName, revision, pid, requireEnabled))
                    return { ok: true, acted };
                this.fail(config.characterName, "error", `查询失败: ${e.message}`);
                this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                return { ok: false, acted };
            }

            if (!this.isRunCurrent(config.characterName, revision, pid, requireEnabled))
                return { ok: true, acted };

            const safetyError = validateSafeLayout(snapshot);
            if (safetyError) {
                this.patchRuntime(config.characterName, {
                    status: "unsafe",
                    lastAction: "",
                    lastResult: `安全检查未通过: ${safetyError}`,
                    lastRunAt: Date.now(),
                    changeChance: snapshot.changeChance,
                    completedCount: snapshot.completedCount,
                    tasks: snapshot.tasks,
                });
                this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                return { ok: false, acted };
            }

            if (pendingProgress && !actionMadeProgress(pendingProgress, snapshot)) {
                this.fail(
                    config.characterName,
                    "uncertain",
                    `第 ${pendingProgress.index + 1} 个任务的动作已确认，但重新查询未看到预期状态变化`
                );
                this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                return { ok: false, acted };
            }
            pendingProgress = null;

            this.patchRuntime(config.characterName, {
                status: "running",
                lastResult: actionCount > 0 ? `已连续执行 ${actionCount} 个动作，正在重新查询` : "已读取每日任务",
                lastRunAt: Date.now(),
                changeChance: snapshot.changeChance,
                completedCount: snapshot.completedCount,
                tasks: snapshot.tasks,
            });

            const huntTasks = snapshot.tasks.filter((task) => task.index >= FIRST_HUNT_INDEX && task.index <= LAST_HUNT_INDEX);
            const nextAction = decideNextAction(snapshot, config.minGrade);
            if (nextAction) {
                if (actionCount >= MAX_ACTIONS_PER_RUN || Date.now() - startedAt >= deadlineMs) {
                    const reason = actionCount >= MAX_ACTIONS_PER_RUN
                        ? `${MAX_ACTIONS_PER_RUN} 个动作安全上限`
                        : `${Math.round(deadlineMs / 1000)} 秒时间上限`;
                    this.patchRuntime(config.characterName, {
                        status: "running",
                        lastAction: "",
                        lastResult: `本轮连续执行 ${actionCount} 个动作并达到${reason}，稍后从最新状态继续`,
                    });
                    this.nextRunAt.set(config.characterName, Date.now() + POLL_MS);
                    return { ok: true, acted };
                }
                if (!this.isRunCurrent(config.characterName, revision, pid, requireEnabled))
                    return { ok: true, acted };

                const result = await this.executeAction(
                    config,
                    pid,
                    nextAction.command,
                    nextAction.task,
                    nextAction.label,
                    revision
                );
                acted = acted || result.acted;
                if (!result.ok) return { ok: false, acted };
                pendingProgress = {
                    command: nextAction.command,
                    index: nextAction.task.index,
                    beforeChangeChance: snapshot.changeChance,
                    beforeState: nextAction.task.state,
                };
                actionCount += 1;
                continue;
            }

            const allDone = huntTasks.every((task) => task.state === 3);
            const actionSummary = actionCount > 0 ? `；本轮连续执行 ${actionCount} 个动作` : "";
            this.patchRuntime(config.characterName, {
                status: allDone ? "done" : "running",
                lastAction: "",
                lastResult: allDone
                    ? `五个猎杀任务均已完成${actionSummary}`
                    : `已领取任务，等待任务完成${actionSummary}`,
            });
            this.nextRunAt.set(config.characterName, Date.now() + POLL_MS);
            return { ok: true, acted };
        }
    }

    private isConfigRevisionCurrent(characterName: string, revision: number): boolean {
        return !this.stopping
            && (this.configRevision.get(characterName) ?? 0) === revision
            && this.configs.some((entry) => entry.characterName === characterName);
    }

    private isRunCurrent(
        characterName: string,
        revision: number,
        pid: number,
        requireEnabled: boolean
    ): boolean {
        if (!this.isConfigRevisionCurrent(characterName, revision)) return false;
        const config = this.configs.find((entry) => entry.characterName === characterName);
        if (!config || (requireEnabled && !config.enabled)) return false;
        return this.registry.list().some(
            (instance) => instance.characterName === characterName && instance.pid === pid
        );
    }

    private async executeAction(
        config: DailyTaskCharacterConfig,
        pid: number,
        action: DailyTaskCommand,
        task: DailyTaskEntry,
        label: string,
        revision: number
    ): Promise<StepResult> {
        const actionText = `${label}第 ${task.index + 1} 个任务`;
        this.patchRuntime(config.characterName, { status: "running", lastAction: actionText, lastRunAt: Date.now() });

        try {
            const reply = await this.registry.sendCommand(
                pid,
                action,
                { index: task.index, waitMs: ACTION_WAIT_MS },
                ACTION_TIMEOUT_MS
            );
            if (!reply.ok) {
                if (this.isConfigRevisionCurrent(config.characterName, revision)) {
                    this.fail(config.characterName, "error", `${actionText}失败: ${reply.detail || "DLL 拒绝动作"}`, actionText);
                    this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                }
                return { ok: false, acted: true };
            }

            const outcome = parseActionOutcome(reply.detail);
            if (!outcome.confirmed || !outcome.outcomeValid) {
                if (this.isConfigRevisionCurrent(config.characterName, revision)) {
                    this.fail(config.characterName, "uncertain", `${actionText}的结果未确认，下一轮将先重新查询`, actionText);
                    this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
                }
                return { ok: false, acted: true };
            }

            if (this.isConfigRevisionCurrent(config.characterName, revision)) {
                this.patchRuntime(config.characterName, {
                    status: "running",
                    lastAction: actionText,
                    lastResult: `${actionText}成功，立即重新查询并继续`,
                    lastRunAt: Date.now(),
                });
            }
            return { ok: true, acted: true };
        } catch (e: any) {
            if (this.isConfigRevisionCurrent(config.characterName, revision)) {
                this.fail(config.characterName, "uncertain", `${actionText}异常: ${e.message}；下一轮将先重新查询`, actionText);
                this.nextRunAt.set(config.characterName, Date.now() + RETRY_MS);
            }
            return { ok: false, acted: true };
        }
    }

    private fail(
        characterName: string,
        status: DailyTaskRuntimeState["status"],
        message: string,
        lastAction = ""
    ): void {
        this.patchRuntime(characterName, { status, lastAction, lastResult: message, lastRunAt: Date.now() });
        console.warn(`[dailyTask] ${characterName}: ${message}`);
    }

    private patchRuntime(characterName: string, patch: Partial<DailyTaskRuntimeState>): void {
        const current = this.runtime.get(characterName) ?? emptyRuntime();
        this.runtime.set(characterName, { ...current, ...patch });
    }

    listConfigs(): DailyTaskCharacterView[] {
        return this.configs.map((config) => this.toView(config));
    }

    getConfig(characterName: string): DailyTaskCharacterView | undefined {
        const config = this.configs.find((entry) => entry.characterName === characterName);
        return config ? this.toView(config) : undefined;
    }

    setConfig(characterName: string, body: Partial<DailyTaskCharacterConfig>): DailyTaskCharacterView {
        const existing = this.configs.find((entry) => entry.characterName === characterName);
        const config = normalizeConfig({ ...(existing ?? {}), ...body, characterName });
        this.configRevision.set(characterName, (this.configRevision.get(characterName) ?? 0) + 1);
        if (existing) this.configs[this.configs.indexOf(existing)] = config;
        else this.configs.push(config);

        if (config.enabled && (!existing?.enabled || existing.minGrade !== config.minGrade)) {
            this.nextRunAt.delete(characterName);
            this.patchRuntime(characterName, { status: "idle", lastResult: "等待执行" });
        } else if (!config.enabled) {
            this.nextRunAt.delete(characterName);
            this.patchRuntime(characterName, { status: "idle", lastAction: "", lastResult: "自动处理已关闭" });
        }
        this.save();
        return this.toView(config);
    }

    deleteConfig(characterName: string): boolean {
        const before = this.configs.length;
        this.configRevision.set(characterName, (this.configRevision.get(characterName) ?? 0) + 1);
        this.configs = this.configs.filter((entry) => entry.characterName !== characterName);
        this.runtime.delete(characterName);
        this.nextRunAt.delete(characterName);
        const deleted = before !== this.configs.length;
        if (deleted) this.save();
        return deleted;
    }

    async runNow(characterName: string): Promise<{ ok: boolean; detail: string; config?: DailyTaskCharacterView }> {
        const instance = this.registry.list().find((entry) => entry.characterName === characterName);
        if (!instance) return { ok: false, detail: "角色不在线" };
        if (this.inFlight.has(characterName)) return { ok: false, detail: "正在处理，请稍后再试" };

        let config = this.configs.find((entry) => entry.characterName === characterName);
        if (!config) {
            config = normalizeConfig({ characterName, enabled: false, minGrade: "SS" });
            this.configs.push(config);
            this.save();
        }

        this.inFlight.add(characterName);
        try {
            const result = await this.runOne(config, instance.pid, false);
            const view = this.toView(config);
            return { ok: result.ok, detail: view.lastResult, config: view };
        } finally {
            this.inFlight.delete(characterName);
        }
    }

    destroy(): void {
        this.stopping = true;
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }

    private toView(config: DailyTaskCharacterConfig): DailyTaskCharacterView {
        return { ...config, ...(this.runtime.get(config.characterName) ?? emptyRuntime()) };
    }
}

export function decideNextAction(
    snapshot: DailyTaskSnapshot,
    minGrade: DailyTaskGrade,
    random: () => number = Math.random
): NextAction | null {
    const huntTasks = snapshot.tasks.filter(
        (task) => task.index >= FIRST_HUNT_INDEX && task.index <= LAST_HUNT_INDEX
    );

    const finish = huntTasks.find((task) => task.state === 2);
    if (finish) return { command: "finishDailyTask", task: finish, label: "完成" };

    const threshold = gradeToRaw(minGrade);
    const belowTarget = huntTasks.filter((task) => task.state === 0 && task.gradeRaw > threshold);
    if (snapshot.changeChance > 0 && belowTarget.length > 0) {
        const worstGrade = Math.max(...belowTarget.map((task) => task.gradeRaw));
        const worst = belowTarget.filter((task) => task.gradeRaw === worstGrade);
        const selectedIndex = Math.min(worst.length - 1, Math.floor(random() * worst.length));
        return { command: "rerollDailyTask", task: worst[selectedIndex], label: "更换" };
    }

    const accept = huntTasks.find((task) => task.state === 0);
    if (accept) return { command: "acceptDailyTask", task: accept, label: "领取" };
    return null;
}

function actionMadeProgress(check: PendingProgressCheck, snapshot: DailyTaskSnapshot): boolean {
    const task = snapshot.tasks.find((entry) => entry.index === check.index);
    if (!task) return false;
    if (check.command === "rerollDailyTask") {
        return check.beforeChangeChance > 0
            && snapshot.changeChance === check.beforeChangeChance - 1
            && task.state === 0;
    }
    if (check.command === "acceptDailyTask") {
        return check.beforeState === 0 && task.state >= 1;
    }
    return check.beforeState === 2 && task.state === 3;
}

function emptyRuntime(): DailyTaskRuntimeState {
    return {
        status: "idle",
        lastAction: "",
        lastResult: "等待执行",
        lastRunAt: 0,
        changeChance: 0,
        completedCount: 0,
        tasks: [],
    };
}

function normalizeConfig(config: Partial<DailyTaskCharacterConfig>): DailyTaskCharacterConfig {
    return {
        characterName: typeof config.characterName === "string" ? config.characterName.trim() : "",
        enabled: config.enabled ?? false,
        minGrade: normalizeGrade(config.minGrade),
    };
}

function normalizeGrade(value: unknown): DailyTaskGrade {
    if (typeof value === "number" && Number.isInteger(value) && value >= 0 && value < DAILY_TASK_GRADES.length) {
        return DAILY_TASK_GRADES[value];
    }
    if (typeof value === "string") {
        const grade = value.toUpperCase() as DailyTaskGrade;
        if (DAILY_TASK_GRADES.includes(grade)) return grade;
    }
    return "SS";
}

function gradeToRaw(grade: DailyTaskGrade): number {
    return DAILY_TASK_GRADES.indexOf(grade);
}

function parseSnapshot(detail?: string): DailyTaskSnapshot {
    let value: unknown;
    try {
        value = JSON.parse(detail || "");
    } catch {
        throw new Error("DLL 返回的任务快照不是有效 JSON");
    }
    if (!isRecord(value) || !Array.isArray(value.tasks)) throw new Error("DLL 返回的任务快照结构无效");

    const tasks = value.tasks.map((item, position) => parseTask(item, position));
    return {
        taskCount: requiredInteger(value.taskCount, "taskCount"),
        changeChance: requiredInteger(value.changeChance, "changeChance"),
        completedCount: requiredInteger(value.completedCount, "completedCount"),
        safeLayout: value.safeLayout === true,
        tasks,
    };
}

function parseTask(value: unknown, position: number): DailyTaskEntry {
    if (!isRecord(value)) throw new Error(`tasks[${position}] 结构无效`);
    return {
        index: requiredInteger(value.index, `tasks[${position}].index`),
        taskId: requiredInteger(value.taskId, `tasks[${position}].taskId`),
        state: requiredInteger(value.state, `tasks[${position}].state`),
        progress: requiredInteger(value.progress, `tasks[${position}].progress`),
        gradeRaw: requiredInteger(value.gradeRaw, `tasks[${position}].gradeRaw`),
        gradeName: typeof value.gradeName === "string" ? value.gradeName : "",
        replaceable: value.replaceable === true,
        hunt: value.hunt === true,
    };
}

function validateSafeLayout(snapshot: DailyTaskSnapshot): string | null {
    if (!snapshot.safeLayout) return "DLL 未确认当前任务布局安全";
    if (snapshot.taskCount !== EXPECTED_TASK_COUNT || snapshot.tasks.length !== EXPECTED_TASK_COUNT) {
        return `任务数量应为 ${EXPECTED_TASK_COUNT}，实际为 ${snapshot.taskCount}/${snapshot.tasks.length}`;
    }
    if (snapshot.changeChance < 0) return "更换次数无效";

    const byIndex = new Map(snapshot.tasks.map((task) => [task.index, task]));
    if (byIndex.size !== EXPECTED_TASK_COUNT) return "任务序号重复";
    for (let index = 0; index < EXPECTED_TASK_COUNT; index += 1) {
        const task = byIndex.get(index);
        if (!task) return `缺少第 ${index + 1} 个任务`;
        if (task.taskId <= 0 || task.state < 0 || task.state > 3 || task.progress < 0) {
            return `第 ${index + 1} 个任务字段无效`;
        }
        if (task.gradeRaw < 0 || task.gradeRaw >= DAILY_TASK_GRADES.length) {
            return `第 ${index + 1} 个任务等级无效`;
        }
        if (index < FIRST_HUNT_INDEX && task.hunt) return `固定任务 ${index + 1} 被标记为猎杀任务`;
        if (index >= FIRST_HUNT_INDEX && (!task.hunt || !task.replaceable)) {
            return `猎杀任务 ${index + 1} 未同时标记 hunt/replaceable`;
        }
    }
    return null;
}

function parseActionOutcome(detail?: string): { confirmed: boolean; outcomeValid: boolean } {
    let value: unknown;
    try {
        value = JSON.parse(detail || "");
    } catch {
        return { confirmed: false, outcomeValid: false };
    }
    if (!isRecord(value)) return { confirmed: false, outcomeValid: false };
    return { confirmed: value.confirmed === true, outcomeValid: value.outcomeValid === true };
}

function requiredInteger(value: unknown, field: string): number {
    if (typeof value !== "number" || !Number.isSafeInteger(value)) throw new Error(`${field} 不是有效整数`);
    return value;
}

function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}
