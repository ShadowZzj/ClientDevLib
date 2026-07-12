import fs from "fs";
import path from "path";
import { v4 as uuid } from "uuid";
import { InstanceRegistry } from "./instances";
import { itemName } from "./itemNames";
import { getMapName } from "./mapNames";
import { PickupFilterManager } from "./pickupFilterManager";
import {
    buildCombatApproachCandidates,
    classifyLocalPlayerStatus,
    combatNavigationDistance,
    isRecoverableCombatPathError,
    isRecoverableCombatSelectionError,
    isCollectionContextStable,
    isTicketProtocolReady,
    isTransientLocalPositionError,
    isTransientPathStartError,
    observeStableMatch,
} from "./workflowSignals";

export type WorkflowNodeType =
    | "condition"
    | "waitUntil"
    | "pathTo"
    | "moveTo"
    | "warpTo"
    | "teleport"
    | "revive"
    | "approachEntity"
    | "interact"
    | "dialogSelect"
    | "dungeonEntry"
    | "clearMonsters"
    | "killBoss"
    | "collectFilteredDrops"
    | "wait"
    | "loop"
    | "command"
    | "end";

export interface WorkflowNode {
    id: string;
    type: WorkflowNodeType;
    name: string;
    params: Record<string, unknown>;
    next?: string;
    onTrue?: string;
    onFalse?: string;
    onFailure?: string;
    timeoutMs?: number;
    retries?: number;
    retryDelayMs?: number;
}

export interface WorkflowDefinition {
    id: string;
    name: string;
    description?: string;
    startNodeId: string;
    maxTransitions: number;
    maxRuntimeMs: number;
    nodes: WorkflowNode[];
    updatedAt: number;
}

export type WorkflowRunStatus =
    | "running"
    | "paused"
    | "stopping"
    | "stopped"
    | "completed"
    | "failed";

export interface WorkflowRunLog {
    seq: number;
    at: number;
    level: "info" | "warn" | "error";
    message: string;
    nodeId?: string;
}

export interface WorkflowRunSnapshot {
    id: string;
    workflowId: string;
    workflowName: string;
    pid: number;
    characterName?: string;
    status: WorkflowRunStatus;
    currentNodeId?: string;
    currentNodeName?: string;
    currentNodeType?: WorkflowNodeType;
    transitions: number;
    startedAt: number;
    updatedAt: number;
    endedAt?: number;
    lastError?: string;
    outputs: Record<string, unknown>;
    logs: WorkflowRunLog[];
}

export interface WorkflowStartError {
    pid: number;
    error: string;
}

export interface WorkflowStartResult {
    runs: WorkflowRunSnapshot[];
    errors: WorkflowStartError[];
}

interface PersistedWorkflows {
    version: number;
    workflows: WorkflowDefinition[];
}

interface CatalogParam {
    key: string;
    label: string;
    type: "number" | "string" | "boolean" | "json" | "select";
    required?: boolean;
    default?: unknown;
    options?: Array<{ label: string; value: unknown }>;
}

interface CatalogNodeType {
    type: WorkflowNodeType;
    name: string;
    description: string;
    params: CatalogParam[];
}

interface NodeBudget {
    startedActiveMs: number;
    timeoutMs: number;
}

interface WorkflowRunInternal extends WorkflowRunSnapshot {
    workflow: WorkflowDefinition;
    connId: string;
    pauseRequested: boolean;
    stopRequested: boolean;
    pauseStartedAt: number;
    totalPausedMs: number;
    wakeWaiters: Set<() => void>;
    loopCounts: Map<string, number>;
    logSeq: number;
    pathActive: boolean;
    pathStartInFlight: boolean;
    pathRestartRequired: boolean;
    pathStopPromise: Promise<boolean> | null;
    dungeonEntryActive: boolean;
    dungeonEntryStartInFlight: boolean;
    dungeonEntryRequestToken?: string;
    dungeonEntryGeneration?: number;
    dungeonEntryCancelPromise: Promise<boolean> | null;
    dungeonEntryAborted: boolean;
    dungeonEntryTicketConsumed: boolean;
    dungeonEntryTicketUseAttempted: boolean;
    executionFinished: boolean;
    leaseReleased: boolean;
    pendingTerminalStatus?: "stopped" | "completed" | "failed";
    pendingTerminalMessage?: string;
}

type NodeOutcome =
    | { kind: "next" }
    | { kind: "branch"; value: boolean }
    | { kind: "end"; success: boolean; message?: string };

interface NearbyEntity {
    id: number;
    kind?: number;
    monsterTblId?: number;
    isNpc?: boolean;
    attackable?: boolean;
    hp?: number;
    maxHp?: number;
    distance?: number;
    name?: string;
    x?: number;
    y?: number;
    z?: number;
}

interface NearbyDropItem {
    dropId: number;
    itemId: number;
    distance: number;
    x: number;
    y: number;
    z?: number;
    canPick: boolean;
    matchesPickupFilter: boolean;
    name?: string;
}

interface DungeonEntryEvent {
    seq: number;
    proto: number;
    result?: number;
    nInsd?: number;
    itemId?: number;
    count?: number;
    opt?: number;
    branch?: number;
}

interface DungeonEntryStatus {
    requestToken: string;
    transactionState: "Prepared" | "CommitIssued" | "Committed" | "CommitFailed" | "Cancelled" | "Expired";
    initialSelectClaimed: boolean;
    generation: number;
    state: "Idle" | "Armed" | "Accepted" | "Declined" | "Expired" | "Cancelled";
    baselineSeq: number;
    latestSeq: number;
    truncated: boolean;
    ticketCount: number;
    currentTicketCount: number;
    remainingMs: number;
    dialogSeenCount: number;
    lastDialogType: number;
    lastDialogMatch: string;
    lastDialogAuthorized: boolean;
    towerPromptAccepted: boolean;
    ticketAck: boolean;
    ticketAckSeq: number;
    events: DungeonEntryEvent[];
}

const WORKFLOW_FILE_NAME = "workflow_scripts.json";
const RUN_LOG_LIMIT = 300;
const MAX_RETAINED_RUNS = 200;
const DEFAULT_MAX_TRANSITIONS = 1000;
const DEFAULT_MAX_RUNTIME_MS = 30 * 60_000;
const MAX_TRANSITIONS_LIMIT = 100_000;
const MAX_RUNTIME_LIMIT_MS = 24 * 60 * 60_000;
const DEFAULT_POLL_MS = 500;
const PAUSED_HEALTH_CHECK_MS = 500;
const DEFAULT_EMPTY_DURATION_MS = 5000;
const PATH_START_READY_RETRY_MS = 5000;
const BOSS_ABSENCE_CONFIRM_MS = 3000;
const COMBAT_SELECTION_RETRY_MS = 10_000;
const COMBAT_PATH_RECOVERY_MS = 10_000;
const COMBAT_PATH_RECOVERY_MAX_ATTEMPTS = 12;
const COMBAT_TARGET_QUARANTINE_MS = 15_000;
const BOSS_RECOVERY_DELAY_MS = 2000;
const READ_ONLY_RETRY_ATTEMPTS = 3;
const COMBAT_TARGET_MIN_DLL_VERSION = "0.10";
const DUNGEON_ENTRY_MIN_DLL_VERSION = "0.11";
const DUNGEON_ENTRY_TICKET_ITEM_ID = 26419;
const DUNGEON_ENTRY_MAX_TICKETS = 1;
const DUNGEON_ENTRY_EXPECTED_WARP_TABLE_ID = 564;
const DUNGEON_ENTRY_START_SETTLE_MS = 17_000;
const DUNGEON_ENTRY_CONFIRM_TEXT = "Another 額外入場券";
const NATIVE_DROP_PICKUP_MIN_DLL_VERSION = "0.12";
const BLOCKED_GENERIC_COMMANDS = new Set([
    "pathTo",
    "stopPath",
    "beginDungeonEntry",
    "openNpcDialogFromWheel",
    "advanceDialogTowardOpt",
    "prepareDungeonEntry",
    "commitDungeonEntry",
    "queryDungeonEntry",
    "cancelDungeonEntry",
    "sendDialogSelectRaw",
    "selectDialogOption",
    "confirmDialog",
    "selectDropForPickup",
]);
const SUPPORTED_CONDITION_KINDS = new Set(["map", "mapName", "position", "dead", "entity", "player", "command"]);
const SUPPORTED_OPERATORS = new Set([
    "eq", "ne", "gt", "gte", "lt", "lte", "in", "contains", "exists", "notExists", "truthy", "falsy",
]);
const SUPPORTED_NODE_TYPES = new Set<WorkflowNodeType>([
    "condition",
    "waitUntil",
    "pathTo",
    "moveTo",
    "warpTo",
    "teleport",
    "revive",
    "approachEntity",
    "interact",
    "dialogSelect",
    "dungeonEntry",
    "clearMonsters",
    "killBoss",
    "collectFilteredDrops",
    "wait",
    "loop",
    "command",
    "end",
]);

const WORKFLOW_NODE_CATALOG: CatalogNodeType[] = [
    {
        type: "condition",
        name: "条件判断",
        description: "读取地图、位置、死亡、附近实体/玩家或通用命令结果并走真假分支。",
        params: [
            { key: "condition", label: "条件 JSON", type: "json", required: true },
            { key: "pollMs", label: "复核间隔(ms)", type: "number", default: DEFAULT_POLL_MS },
            { key: "stableDurationMs", label: "结果持续稳定(ms)", type: "number", default: 0 },
        ],
    },
    {
        type: "waitUntil",
        name: "等待条件",
        description: "持续重查条件直到成立或节点超时。",
        params: [
            { key: "condition", label: "条件 JSON", type: "json", required: true },
            { key: "pollMs", label: "轮询间隔(ms)", type: "number", default: DEFAULT_POLL_MS },
            { key: "stableDurationMs", label: "持续成立(ms)", type: "number", default: 0 },
        ],
    },
    {
        type: "pathTo",
        name: "远程寻路",
        description: "启动 DLL A* 寻路，并轮询 pathStatus 直到 arrived；停止运行时下发 stopPath。",
        params: [
            { key: "x", label: "目标 X", type: "number", required: true },
            { key: "y", label: "目标 Y", type: "number", required: true },
            { key: "action", label: "动作", type: "number", default: 1 },
            { key: "targetId", label: "目标生物 ID", type: "number", default: 0 },
            { key: "pollMs", label: "轮询间隔(ms)", type: "number", default: 300 },
        ],
    },
    {
        type: "moveTo",
        name: "短距移动",
        description: "单次 moveTo；默认轮询坐标确认到达。",
        params: [
            { key: "x", label: "目标 X", type: "number", required: true },
            { key: "y", label: "目标 Y", type: "number", required: true },
            { key: "action", label: "动作", type: "number", default: 1 },
            { key: "targetId", label: "目标生物 ID", type: "number", default: 0 },
            { key: "confirm", label: "确认到达", type: "boolean", default: true },
            { key: "arriveDistance", label: "到达半径", type: "number", default: 1 },
        ],
    },
    {
        type: "warpTo",
        name: "坐标瞬移",
        description: "下发 warpTo；默认轮询坐标确认。",
        params: [
            { key: "x", label: "目标 X", type: "number", required: true },
            { key: "y", label: "目标 Y", type: "number", required: true },
            { key: "confirm", label: "确认到达", type: "boolean", default: true },
            { key: "arriveDistance", label: "到达半径", type: "number", default: 2 },
        ],
    },
    {
        type: "teleport",
        name: "城市传送",
        description: "按 destId 或 cityName 传送，可等待预期 mapId。",
        params: [
            { key: "destId", label: "目的地 ID", type: "number" },
            { key: "cityName", label: "城市名", type: "string" },
            { key: "expectedMapId", label: "预期地图 ID", type: "number" },
            { key: "settleMs", label: "无地图确认时等待(ms)", type: "number", default: 1000 },
        ],
    },
    {
        type: "revive",
        name: "复活回城",
        description: "下发 reviveToTown 并确认角色脱离死亡状态，可校验地图。",
        params: [
            { key: "safetyCheck", label: "安全检查", type: "boolean", default: true },
            { key: "mode", label: "城镇模式", type: "number", default: 1 },
            { key: "expectedMapId", label: "预期地图 ID", type: "number" },
        ],
    },
    {
        type: "approachEntity",
        name: "靠近实体",
        description: "按实例 ID 或附近实体筛选目标，只寻路靠近；不调用 talkOrAttack，不打开对话。",
        params: [
            { key: "creatureId", label: "生物实例 ID", type: "number" },
            { key: "maxDistance", label: "搜索距离", type: "number", default: 100 },
            { key: "allVisible", label: "当前视野全部", type: "boolean", default: false },
            { key: "monsterTblId", label: "NPC/怪物表 ID", type: "number" },
            { key: "name", label: "名称精确匹配", type: "string" },
            { key: "nameContains", label: "名称包含", type: "string" },
            { key: "nameNotContains", label: "名称不包含", type: "string" },
            { key: "isNpc", label: "只匹配 NPC", type: "boolean" },
            { key: "attackable", label: "只匹配可攻击怪", type: "boolean" },
            { key: "approachDistance", label: "靠近距离", type: "number", default: 3 },
            { key: "pollMs", label: "距离轮询间隔(ms)", type: "number", default: 300 },
        ],
    },
    {
        type: "interact",
        name: "交互 NPC/怪物",
        description: "按 creatureId 或附近实体筛选最近目标，调用 talkOrAttack，可等待对话打开。",
        params: [
            { key: "creatureId", label: "生物实例 ID", type: "number" },
            { key: "maxDistance", label: "搜索距离", type: "number", default: 100 },
            { key: "allVisible", label: "当前视野全部", type: "boolean", default: false },
            { key: "monsterTblId", label: "NPC/怪物表 ID", type: "number" },
            { key: "name", label: "名称精确匹配", type: "string" },
            { key: "nameContains", label: "名称包含", type: "string" },
            { key: "nameNotContains", label: "名称不包含", type: "string" },
            { key: "isNpc", label: "只匹配 NPC", type: "boolean" },
            { key: "attackable", label: "只匹配可攻击怪", type: "boolean" },
            { key: "waitDialog", label: "等待对话打开", type: "boolean" },
        ],
    },
    {
        type: "dialogSelect",
        name: "选择对话",
        description: "可在运行时选取最近 NPC 的动态 id，并用固定 opt/sub 直接发包；也兼容固定 npcId 或当前对话索引。",
        params: [
            { key: "nearestNpc", label: "运行时取最近 NPC", type: "boolean", default: true },
            { key: "npcId", label: "NPC ID(直接发包)", type: "number" },
            { key: "maxDistance", label: "NPC 搜索半径", type: "number", default: 5 },
            { key: "allVisible", label: "搜索当前视野全部", type: "boolean", default: false },
            { key: "name", label: "NPC 名称精确匹配", type: "string" },
            { key: "nameContains", label: "NPC 名称包含", type: "string" },
            { key: "waitForNpcMs", label: "等待 NPC 出现(ms)", type: "number", default: 5000 },
            { key: "pollMs", label: "查询间隔(ms)", type: "number", default: 300 },
            { key: "option", label: "选项/opt", type: "number", required: true },
            { key: "sub", label: "sub", type: "number", default: 1 },
            { key: "raw", label: "直接发包", type: "boolean" },
            { key: "expectedMapId", label: "预期地图 ID", type: "number" },
        ],
    },
    {
        type: "dungeonEntry",
        name: "副本入場",
        description: "按遊戲原生 UI 依次執行互動輪盤「對話」、NPC 對話樹與最後一頁；實際發出固定 opt 後，免費次數用盡時限定確認最多一張額外入場券。",
        params: [
            { key: "maxDistance", label: "NPC 搜索半徑", type: "number", default: 5 },
            { key: "allVisible", label: "搜索當前視野全部", type: "boolean", default: false },
            { key: "name", label: "NPC 名稱精確匹配", type: "string" },
            { key: "nameContains", label: "NPC 名稱包含", type: "string" },
            { key: "waitForNpcMs", label: "等待 NPC 出現(ms)", type: "number", default: 5000 },
            { key: "nativeDialogTimeoutMs", label: "等待原生對話與 opt(ms)", type: "number", default: 12000 },
            { key: "nativeDialogMaxSteps", label: "原生對話最大推進步數", type: "number", default: 10 },
            { key: "pollMs", label: "查詢間隔(ms)", type: "number", default: 300 },
            { key: "option", label: "入口 opt", type: "number", required: true },
            { key: "sub", label: "sub", type: "number", default: 1 },
            { key: "autoUseExtraTicket", label: "免費次數用盡時自動使用額外入場券", type: "boolean", default: false },
            { key: "ticketItemId", label: "額外入場券 itemId", type: "number", default: DUNGEON_ENTRY_TICKET_ITEM_ID },
            { key: "confirmTextContains", label: "確認框文字包含", type: "string" },
            { key: "expectedMapNameContains", label: "成功地圖名稱包含", type: "string", required: true },
            { key: "mapStableDurationMs", label: "地圖穩定時間(ms)", type: "number", default: 1500 },
        ],
    },
    {
        type: "clearMonsters",
        name: "清理周围怪物",
        description: "过滤 attackable 且 HP>0 的怪物，持续攻击最近目标，连续空场后完成。",
        params: [
            { key: "maxDistance", label: "清怪半径", type: "number", default: 100 },
            { key: "allVisible", label: "当前视野全部", type: "boolean", default: false },
            { key: "monsterTblId", label: "怪物表 ID", type: "number" },
            { key: "name", label: "名称精确匹配", type: "string" },
            { key: "nameContains", label: "名称包含", type: "string" },
            { key: "nameNotContains", label: "名称不包含", type: "string" },
            { key: "approachDistance", label: "选中前靠近距离", type: "number", default: 3 },
            { key: "pollMs", label: "重查间隔(ms)", type: "number", default: 500 },
            { key: "reissueMs", label: "同目标重发间隔(ms)", type: "number", default: 3000 },
            { key: "emptyConfirmations", label: "连续空场次数", type: "number", default: 2 },
            { key: "emptyDurationMs", label: "连续空场最短时间(ms)", type: "number", default: DEFAULT_EMPTY_DURATION_MS },
        ],
    },
    {
        type: "killBoss",
        name: "锁定并击杀 BOSS",
        description: "名称与最大 HP 达标后锁定 creatureId；后续用当前 HP/稳定消失判断死亡。",
        params: [
            { key: "maxDistance", label: "初次搜索半径", type: "number", default: 100 },
            { key: "allVisible", label: "当前视野全部", type: "boolean", default: true },
            { key: "monsterTblId", label: "怪物表 ID", type: "number" },
            { key: "name", label: "名称精确匹配", type: "string" },
            { key: "nameContains", label: "名称包含", type: "string", default: "[BOSS]" },
            { key: "nameNotContains", label: "名称不包含", type: "string" },
            { key: "maxHpGt", label: "最大 HP 严格大于", type: "number", default: 800_000_000 },
            { key: "waitForAppearanceMs", label: "等待出现(ms)", type: "number", default: 180_000 },
            { key: "skipIfAbsent", label: "未出现时跳过", type: "boolean", default: false },
            { key: "approachDistance", label: "选中前靠近距离", type: "number", default: 3 },
            { key: "pollMs", label: "重查间隔(ms)", type: "number", default: 500 },
            { key: "reissueMs", label: "同目标重发间隔(ms)", type: "number", default: 2000 },
            { key: "missingConfirmations", label: "连续消失次数", type: "number", default: 3 },
            { key: "missingDurationMs", label: "连续消失最短时间(ms)", type: "number", default: 3000 },
        ],
    },
    {
        type: "collectFilteredDrops",
        name: "按过滤拾取",
        description: "按全局拾取执行方式处理过滤掉落；单项不可达、不可捡或确认超时时安全跳过。",
        params: [
            { key: "maxDistance", label: "搜索半径(0=当前视野)", type: "number", default: 0 },
            { key: "includeUnpickable", label: "等待暂不可捡物品", type: "boolean", default: true },
            { key: "approachDistance", label: "靠近距离", type: "number", default: 2.5 },
            { key: "pollMs", label: "重查间隔(ms)", type: "number", default: 500 },
            { key: "emptyDurationMs", label: "连续空场最短时间(ms)", type: "number", default: 3000 },
            { key: "nativePickupTimeoutMs", label: "原生拾取确认时间(ms)", type: "number", default: 5000 },
            { key: "maxDurationMs", label: "最长拾取时间(ms)", type: "number", default: 100_000 },
        ],
    },
    {
        type: "wait",
        name: "等待",
        description: "可暂停、可停止的定时等待。",
        params: [{ key: "ms", label: "等待时间(ms)", type: "number", required: true }],
    },
    {
        type: "loop",
        name: "计数循环",
        description: "未达到 count 时走 onTrue，达到后清零并走 onFalse。",
        params: [{ key: "count", label: "循环次数", type: "number", required: true }],
    },
    {
        type: "command",
        name: "通用命令",
        description: "高级节点：透传现有 DLL command，并可保存 detail。pathTo/stopPath 禁用；非幂等命令不要配置 retries。",
        params: [
            { key: "action", label: "命令 action", type: "string", required: true },
            { key: "args", label: "参数 JSON", type: "json", default: {} },
            { key: "saveAs", label: "输出变量名", type: "string" },
            { key: "parseJson", label: "解析 detail JSON", type: "boolean", default: true },
        ],
    },
    {
        type: "end",
        name: "结束",
        description: "显式成功或失败结束当前 PID 的运行。",
        params: [
            { key: "success", label: "成功结束", type: "boolean", default: true },
            { key: "message", label: "结束消息", type: "string" },
        ],
    },
];

class StopRequestedError extends Error {
    constructor() {
        super("workflow stopped");
    }
}

class NodeTimeoutError extends Error {
    constructor(message = "node timed out") {
        super(message);
    }
}

class RecoverableCombatTargetError extends Error {
    readonly targetId: number;
    readonly reason: string;

    constructor(target: NearbyEntity, reason: string) {
        super(`combat target ${target.name || target.id} is temporarily unavailable: ${reason}`);
        this.name = "RecoverableCombatTargetError";
        this.targetId = target.id;
        this.reason = reason;
    }
}

export class WorkflowEngine {
    private readonly registry: InstanceRegistry;
    private readonly pickupFilterManager: PickupFilterManager;
    private readonly persistFile: string;
    private readonly workflows = new Map<string, WorkflowDefinition>();
    private readonly runs = new Map<string, WorkflowRunInternal>();
    private readonly activeByPid = new Map<number, string>();
    private readonly runTasks = new Set<Promise<void>>();
    private destroyed = false;
    private destroyPromise: Promise<void> | null = null;

    constructor(
        registry: InstanceRegistry,
        dataDir: string,
        pickupFilterManager: PickupFilterManager
    ) {
        this.registry = registry;
        this.pickupFilterManager = pickupFilterManager;
        this.persistFile = path.resolve(dataDir, WORKFLOW_FILE_NAME);
        this.load();
    }

    getCatalog(): { version: number; types: CatalogNodeType[] } {
        return { version: 1, types: WORKFLOW_NODE_CATALOG.map((entry) => ({ ...entry, params: entry.params.map((p) => ({ ...p })) })) };
    }

    listWorkflows(): WorkflowDefinition[] {
        return Array.from(this.workflows.values())
            .sort((a, b) => a.name.localeCompare(b.name, "zh-CN"))
            .map(cloneWorkflow);
    }

    getWorkflow(id: string): WorkflowDefinition | undefined {
        const workflow = this.workflows.get(id);
        return workflow ? cloneWorkflow(workflow) : undefined;
    }

    setWorkflow(id: string, raw: Partial<WorkflowDefinition>): WorkflowDefinition {
        if (this.destroyed) throw new Error("workflow engine is shutting down");
        const workflow = normalizeWorkflow(id, raw);
        validateWorkflow(workflow);
        const previous = this.workflows.get(workflow.id);
        this.workflows.set(workflow.id, workflow);
        try {
            this.save();
        } catch (e) {
            if (previous) this.workflows.set(workflow.id, previous);
            else this.workflows.delete(workflow.id);
            throw e;
        }
        return cloneWorkflow(workflow);
    }

    deleteWorkflow(id: string): boolean {
        const previous = this.workflows.get(id);
        if (!previous) return false;
        this.workflows.delete(id);
        try {
            this.save();
        } catch (e) {
            this.workflows.set(id, previous);
            throw e;
        }
        return true;
    }

    startWorkflow(id: string, rawPids: unknown): WorkflowStartResult {
        if (this.destroyed) throw new Error("workflow engine is shutting down");
        const workflow = this.workflows.get(id);
        if (!workflow) throw new Error(`workflow not found: ${id}`);
        const pids = normalizePids(rawPids);
        if (pids.length === 0) throw new Error("pids must contain at least one positive integer");

        const result: WorkflowStartResult = { runs: [], errors: [] };
        const online = this.registry.list();
        for (const pid of pids) {
            const instance = online.find((entry) => entry.pid === pid);
            if (!instance) {
                result.errors.push({ pid, error: "instance offline" });
                continue;
            }
            if (workflow.nodes.some((node) => node.type === "clearMonsters" || node.type === "killBoss") &&
                !versionAtLeast(instance.dllVersion, COMBAT_TARGET_MIN_DLL_VERSION)) {
                result.errors.push({
                    pid,
                    error: `combat target selection requires GGThreadBlock.dll >= ${COMBAT_TARGET_MIN_DLL_VERSION}; current ${instance.dllVersion || "unknown"}. Restart the game client after updating the DLL.`,
                });
                continue;
            }
            if (!getMapName(instance.status?.mapId).includes("賢者之塔") &&
                workflow.nodes.some((node) => node.type === "dungeonEntry") &&
                !versionAtLeast(instance.dllVersion, DUNGEON_ENTRY_MIN_DLL_VERSION)) {
                result.errors.push({
                    pid,
                    error: `dungeonEntry requires GGThreadBlock.dll >= ${DUNGEON_ENTRY_MIN_DLL_VERSION}; current ${instance.dllVersion || "unknown"}. Restart the game client after updating the DLL.`,
                });
                continue;
            }
            if (!getMapName(instance.status?.mapId).includes("賢者之塔") &&
                workflow.nodes.some((node) => node.type === "dungeonEntry") &&
                instance.nativeNpcUiBridgeReady !== true) {
                result.errors.push({
                    pid,
                    error: "dungeonEntry requires the native NPC UI bridge; restart the game client with the updated DLL and check the bridge startup log.",
                });
                continue;
            }
            const activeRunId = this.activeByPid.get(pid);
            if (activeRunId) {
                result.errors.push({ pid, error: `pid already has active workflow run ${activeRunId}` });
                continue;
            }

            const runId = uuid();
            if (!this.registry.acquireCommandLease(pid, runId)) {
                result.errors.push({ pid, error: "pid command channel is busy or leased" });
                continue;
            }

            const now = Date.now();
            const run: WorkflowRunInternal = {
                id: runId,
                workflowId: workflow.id,
                workflowName: workflow.name,
                pid,
                characterName: instance.characterName,
                status: "running",
                currentNodeId: workflow.startNodeId,
                transitions: 0,
                startedAt: now,
                updatedAt: now,
                outputs: {},
                logs: [],
                workflow: cloneWorkflow(workflow),
                connId: instance.connId,
                pauseRequested: false,
                stopRequested: false,
                pauseStartedAt: 0,
                totalPausedMs: 0,
                wakeWaiters: new Set(),
                loopCounts: new Map(),
                logSeq: 0,
                pathActive: false,
                pathStartInFlight: false,
                pathRestartRequired: false,
                pathStopPromise: null,
                dungeonEntryActive: false,
                dungeonEntryStartInFlight: false,
                dungeonEntryRequestToken: undefined,
                dungeonEntryGeneration: undefined,
                dungeonEntryCancelPromise: null,
                dungeonEntryAborted: false,
                dungeonEntryTicketConsumed: false,
                dungeonEntryTicketUseAttempted: false,
                executionFinished: false,
                leaseReleased: false,
                pendingTerminalStatus: undefined,
                pendingTerminalMessage: undefined,
            };
            this.runs.set(run.id, run);
            this.activeByPid.set(pid, run.id);
            this.appendLog(run, "info", `开始执行工作流“${workflow.name}”`);
            result.runs.push(this.snapshot(run));
            const task = this.executeRun(run);
            this.runTasks.add(task);
            void task.then(
                () => this.runTasks.delete(task),
                () => this.runTasks.delete(task)
            );
        }
        this.pruneRuns();
        return result;
    }

    listRuns(): WorkflowRunSnapshot[] {
        return Array.from(this.runs.values())
            .sort((a, b) => b.startedAt - a.startedAt)
            .map((run) => this.snapshot(run));
    }

    getRun(id: string): WorkflowRunSnapshot | undefined {
        const run = this.runs.get(id);
        return run ? this.snapshot(run) : undefined;
    }

    async pauseRun(id: string): Promise<WorkflowRunSnapshot | undefined> {
        const run = this.runs.get(id);
        if (!run || run.status !== "running" || run.stopRequested || run.pauseRequested) {
            return run ? this.snapshot(run) : undefined;
        }
        run.pauseRequested = true;
        run.pauseStartedAt = Date.now();
        run.updatedAt = run.pauseStartedAt;
        this.appendLog(run, "info", "正在安全暂停运行");
        if (run.pathActive || run.pathStartInFlight) {
            run.pathRestartRequired = true;
            const waitStarted = Date.now();
            while (run.pathStartInFlight && Date.now() - waitStarted < 5500) {
                await new Promise<void>((resolve) => setTimeout(resolve, 25));
            }
        }
        if (run.pathActive) await this.stopPath(run, 2);
        if (run.dungeonEntryActive || run.dungeonEntryStartInFlight ||
            run.dungeonEntryRequestToken) {
            run.dungeonEntryAborted = true;
            const cleanupConfirmed = await this.cancelDungeonEntryAfterStart(run, 10);
            run.stopRequested = true;
            run.pauseRequested = false;
            run.status = "stopping";
            run.updatedAt = Date.now();
            this.appendLog(
                run,
                cleanupConfirmed ? "warn" : "error",
                cleanupConfirmed
                    ? "副本入场事务不可安全续跑；已取消事务并停止工作流"
                    : "pause could not confirm dungeon entry cleanup; fail-closed to stopping"
            );
            this.wake(run);
            return this.snapshot(run);
        }
        if (run.stopRequested) return this.snapshot(run);
        run.status = "paused";
        run.updatedAt = Date.now();
        this.appendLog(run, "info", "运行已暂停");
        return this.snapshot(run);
    }

    resumeRun(id: string): WorkflowRunSnapshot | undefined {
        const run = this.runs.get(id);
        if (!run || run.status !== "paused" || !run.pauseRequested || run.stopRequested) {
            return run ? this.snapshot(run) : undefined;
        }
        const now = Date.now();
        run.totalPausedMs += Math.max(0, now - run.pauseStartedAt);
        run.pauseStartedAt = 0;
        run.pauseRequested = false;
        run.status = "running";
        run.updatedAt = now;
        this.appendLog(run, "info", "运行已恢复");
        this.wake(run);
        return this.snapshot(run);
    }

    async stopRun(id: string): Promise<WorkflowRunSnapshot | undefined> {
        const run = this.runs.get(id);
        if (!run) return undefined;
        if (!isActiveStatus(run.status)) return this.snapshot(run);
        run.stopRequested = true;
        run.pauseRequested = false;
        run.pathRestartRequired = false;
        run.status = "stopping";
        run.updatedAt = Date.now();
        this.appendLog(run, "info", "请求停止运行");
        this.wake(run);
        if (run.pathActive) await this.stopPath(run, 3);
        let dungeonCleanupConfirmed = true;
        if (run.dungeonEntryActive || run.dungeonEntryStartInFlight ||
            run.dungeonEntryRequestToken) {
            run.dungeonEntryAborted = true;
            dungeonCleanupConfirmed = await this.cancelDungeonEntryAfterStart(run, 3);
            if (dungeonCleanupConfirmed) {
                run.dungeonEntryActive = false;
                run.dungeonEntryRequestToken = undefined;
                run.dungeonEntryGeneration = undefined;
            }
        }
        if (run.executionFinished && dungeonCleanupConfirmed) {
            const terminalStatus = run.pendingTerminalStatus ?? "stopped";
            const terminalMessage = run.pendingTerminalMessage ??
                "工作流已停止；副本入场事务已清理";
            this.finishRun(run, terminalStatus, terminalMessage);
            this.releaseRunLeaseIfSafe(run);
        }
        return this.snapshot(run);
    }

    destroy(): Promise<void> {
        if (this.destroyPromise) return this.destroyPromise;
        this.destroyPromise = this.destroyInternal();
        return this.destroyPromise;
    }

    private async destroyInternal(): Promise<void> {
        this.destroyed = true;
        const stopAttempts: Promise<boolean>[] = [];
        for (const run of this.runs.values()) {
            if (!isActiveStatus(run.status)) continue;
            run.stopRequested = true;
            run.pauseRequested = false;
            run.pathRestartRequired = false;
            run.status = "stopping";
            this.wake(run);
            if (run.pathActive) stopAttempts.push(this.stopPath(run, 3));
            if (run.dungeonEntryActive || run.dungeonEntryStartInFlight ||
                run.dungeonEntryRequestToken) {
                run.dungeonEntryAborted = true;
                stopAttempts.push(this.cancelDungeonEntryAfterStart(run, 3));
            }
        }
        await Promise.allSettled(stopAttempts);
        if (this.runTasks.size > 0) {
            await Promise.race([
                Promise.allSettled(Array.from(this.runTasks)),
                new Promise<void>((resolve) =>
                    setTimeout(resolve, DUNGEON_ENTRY_START_SETTLE_MS + 1000)),
            ]);
        }
        for (const run of this.runs.values()) {
            this.registry.releaseCommandLease(run.pid, run.id);
        }
    }

    private load(): void {
        try {
            if (!fs.existsSync(this.persistFile)) return;
            const doc = JSON.parse(fs.readFileSync(this.persistFile, "utf8")) as PersistedWorkflows;
            for (const raw of Array.isArray(doc.workflows) ? doc.workflows : []) {
                try {
                    const id = String(raw?.id || "").trim();
                    if (!id) continue;
                    const workflow = normalizeWorkflow(id, raw);
                    validateWorkflow(workflow);
                    this.workflows.set(id, workflow);
                } catch (e: any) {
                    console.warn(`[workflow] skipped invalid definition: ${e.message || String(e)}`);
                }
            }
            console.log(`[workflow] loaded ${this.workflows.size} definitions`);
        } catch (e: any) {
            console.warn(`[workflow] load failed: ${e.message || String(e)}`);
            this.workflows.clear();
        }
    }

    private save(): void {
        const tempFile = `${this.persistFile}.tmp`;
        try {
            fs.mkdirSync(path.dirname(this.persistFile), { recursive: true });
            const doc: PersistedWorkflows = {
                version: 1,
                workflows: Array.from(this.workflows.values()),
            };
            fs.writeFileSync(tempFile, JSON.stringify(doc, null, 2), "utf8");
            fs.renameSync(tempFile, this.persistFile);
        } catch (e: any) {
            try {
                if (fs.existsSync(tempFile)) fs.unlinkSync(tempFile);
            } catch {
                // Preserve the original write error.
            }
            throw new Error(`save workflow scripts failed: ${e.message || String(e)}`);
        }
    }

    private async executeRun(run: WorkflowRunInternal): Promise<void> {
        let nodeId = run.workflow.startNodeId;
        try {
            for (;;) {
                await this.waitUntilRunnable(run);
                this.ensureRuntimeBudget(run);
                const node = run.workflow.nodes.find((entry) => entry.id === nodeId);
                if (!node) throw new Error(`node not found: ${nodeId}`);

                run.currentNodeId = node.id;
                run.currentNodeName = node.name;
                run.currentNodeType = node.type;
                run.updatedAt = Date.now();
                this.appendLog(run, "info", `进入节点：${node.name || node.id} (${node.type})`, node.id);

                let outcome: NodeOutcome | undefined;
                let lastError: Error | undefined;
                const retries = clampInt(node.retries, 0, 20, 0);
                for (let attempt = 0; attempt <= retries; attempt += 1) {
                    await this.waitUntilRunnable(run);
                    const budget: NodeBudget = {
                        startedActiveMs: this.activeElapsedMs(run),
                        timeoutMs: this.nodeTimeoutMs(node),
                    };
                    try {
                        outcome = await this.executeNode(run, node, budget);
                        lastError = undefined;
                        break;
                    } catch (e: any) {
                        if (e instanceof StopRequestedError) throw e;
                        lastError = e instanceof Error ? e : new Error(String(e));
                        const prefix = attempt < retries
                            ? `节点失败，准备第 ${attempt + 2}/${retries + 1} 次尝试`
                            : "节点失败";
                        this.appendLog(run, attempt < retries ? "warn" : "error", `${prefix}：${lastError.message}`, node.id);
                        if (attempt < retries) {
                            await this.waitDelay(run, clampInt(node.retryDelayMs, 0, 60_000, 500));
                        }
                    }
                }

                if (!outcome) {
                    const message = lastError?.message || "node failed";
                    run.outputs.lastError = { nodeId: node.id, message, at: Date.now() };
                    if (run.dungeonEntryActive || run.dungeonEntryStartInFlight ||
                        run.dungeonEntryRequestToken) {
                        run.dungeonEntryAborted = true;
                        const cleanupConfirmed = await this.cancelDungeonEntryAfterStart(run, 10);
                        if (!cleanupConfirmed) {
                            throw new Error(
                                `dungeon entry cleanup could not be confirmed; refusing failure transition: ${message}`
                            );
                        }
                        run.dungeonEntryActive = false;
                        run.dungeonEntryRequestToken = undefined;
                        run.dungeonEntryGeneration = undefined;
                    }
                    if (node.onFailure) {
                        this.appendLog(run, "warn", `失败转移到 ${node.onFailure}`, node.id);
                        nodeId = this.transition(run, node.onFailure);
                        continue;
                    }
                    throw lastError || new Error(message);
                }

                if (outcome.kind === "end") {
                    if (outcome.success) {
                        this.finishRun(run, "completed", outcome.message || "工作流完成");
                    } else {
                        throw new Error(outcome.message || "工作流以失败结束");
                    }
                    return;
                }

                const nextId = outcome.kind === "branch"
                    ? (outcome.value ? node.onTrue : node.onFalse) || node.next
                    : node.next;
                if (!nextId) {
                    this.finishRun(run, "completed", "到达流程末端");
                    return;
                }
                nodeId = this.transition(run, nextId);
            }
        } catch (e: any) {
            if (e instanceof StopRequestedError || run.stopRequested || this.destroyed) {
                this.finishRun(run, "stopped", "工作流已停止");
            } else {
                this.finishRun(run, "failed", e?.message || String(e));
            }
        } finally {
            if (run.pathActive) await this.stopPath(run, 3);
            let dungeonCleanupConfirmed = true;
            if (run.dungeonEntryActive || run.dungeonEntryStartInFlight ||
                run.dungeonEntryRequestToken) {
                run.dungeonEntryAborted = true;
                dungeonCleanupConfirmed = await this.cancelDungeonEntryAfterStart(run, 10);
                if (dungeonCleanupConfirmed) {
                    run.dungeonEntryActive = false;
                    run.dungeonEntryRequestToken = undefined;
                    run.dungeonEntryGeneration = undefined;
                }
            }
            run.executionFinished = true;
            if (!dungeonCleanupConfirmed) {
                run.status = "stopping";
                run.endedAt = undefined;
                run.updatedAt = Date.now();
                const message =
                    "dungeon entry cleanup remains unconfirmed; command lease is retained fail-closed";
                this.appendLog(run, "error", message, run.currentNodeId);
                this.wake(run);
            } else {
                this.releaseRunLeaseIfSafe(run);
            }
        }
    }

    private transition(run: WorkflowRunInternal, nextId: string): string {
        if (!run.workflow.nodes.some((entry) => entry.id === nextId)) {
            throw new Error(`transition target not found: ${nextId}`);
        }
        run.transitions += 1;
        run.updatedAt = Date.now();
        if (run.transitions > run.workflow.maxTransitions) {
            throw new Error(`transition limit exceeded (${run.workflow.maxTransitions})`);
        }
        return nextId;
    }

    private async executeNode(
        run: WorkflowRunInternal,
        node: WorkflowNode,
        budget: NodeBudget
    ): Promise<NodeOutcome> {
        switch (node.type) {
            case "condition": {
                const condition = conditionFromParams(node.params);
                const pollMs = clampInt(node.params.pollMs, 100, 10_000, DEFAULT_POLL_MS);
                const stableDurationMs = clampInt(node.params.stableDurationMs, 0, 10 * 60_000, 0);
                let value = await this.evaluateCondition(run, condition, budget);
                if (stableDurationMs > 0) {
                    let observedSinceActiveMs = this.activeElapsedMs(run);
                    for (;;) {
                        if (this.activeElapsedMs(run) - observedSinceActiveMs >= stableDurationMs) break;
                        await this.waitDelay(run, pollMs, budget);
                        const nextValue = await this.evaluateCondition(run, condition, budget);
                        if (nextValue !== value) {
                            value = nextValue;
                            observedSinceActiveMs = this.activeElapsedMs(run);
                        }
                    }
                }
                this.appendLog(run, "info", `条件结果：${value ? "真" : "假"}`, node.id);
                return { kind: "branch", value };
            }
            case "waitUntil": {
                const condition = conditionFromParams(node.params);
                const pollMs = clampInt(node.params.pollMs, 100, 10_000, DEFAULT_POLL_MS);
                const stableDurationMs = clampInt(node.params.stableDurationMs, 0, 10 * 60_000, 0);
                let matchedSinceActiveMs: number | null = null;
                for (;;) {
                    await this.checkPoint(run, budget);
                    if (await this.evaluateCondition(run, condition, budget)) {
                        if (matchedSinceActiveMs === null) matchedSinceActiveMs = this.activeElapsedMs(run);
                        if (this.activeElapsedMs(run) - matchedSinceActiveMs >= stableDurationMs) {
                            return { kind: "next" };
                        }
                    } else {
                        matchedSinceActiveMs = null;
                    }
                    await this.waitDelay(run, pollMs, budget);
                }
            }
            case "pathTo":
                await this.executePathTo(run, node.params, budget);
                return { kind: "next" };
            case "moveTo":
                await this.executeMoveTo(run, node.params, budget);
                return { kind: "next" };
            case "warpTo":
                await this.executeWarpTo(run, node.params, budget);
                return { kind: "next" };
            case "teleport":
                await this.executeTeleport(run, node.params, budget);
                return { kind: "next" };
            case "revive":
                await this.executeRevive(run, node.params, budget);
                return { kind: "next" };
            case "approachEntity":
                await this.executeApproachEntity(run, node.params, budget);
                return { kind: "next" };
            case "interact":
                await this.executeInteract(run, node.params, budget);
                return { kind: "next" };
            case "dialogSelect":
                await this.executeDialogSelect(run, node.params, budget);
                return { kind: "next" };
            case "dungeonEntry":
                await this.executeDungeonEntry(run, node.params, budget);
                return { kind: "next" };
            case "clearMonsters":
                await this.executeClearMonsters(run, node.params, budget);
                return { kind: "next" };
            case "killBoss":
                await this.executeKillBoss(run, node.params, budget);
                return { kind: "next" };
            case "collectFilteredDrops":
                await this.executeCollectFilteredDrops(run, node.params, budget);
                return { kind: "next" };
            case "wait":
                await this.waitDelay(run, nonNegativeNumber(node.params.ms, 0), budget);
                return { kind: "next" };
            case "loop": {
                const count = clampInt(node.params.count, 0, MAX_TRANSITIONS_LIMIT, 1);
                const current = run.loopCounts.get(node.id) || 0;
                if (current < count) {
                    run.loopCounts.set(node.id, current + 1);
                    run.outputs[`loop.${node.id}`] = current + 1;
                    return { kind: "branch", value: true };
                }
                run.loopCounts.delete(node.id);
                run.outputs[`loop.${node.id}`] = count;
                return { kind: "branch", value: false };
            }
            case "command":
                await this.executeGenericCommand(run, node.params, budget);
                return { kind: "next" };
            case "end":
                return {
                    kind: "end",
                    success: node.params.success !== false,
                    message: stringOrUndefined(node.params.message),
                };
        }
    }

    private async executePathTo(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget,
        completeWhen?: () => Promise<boolean>
    ): Promise<void> {
        const x = requiredNumber(params.x, "pathTo.x");
        const y = requiredNumber(params.y, "pathTo.y");
        const action = clampInt(params.action, 1, 3, 1);
        if (action !== 1 && action !== 3) throw new Error("pathTo.action must be 1 or 3");
        const targetId = nonNegativeInt(params.targetId, 0);
        const pollMs = clampInt(params.pollMs, 100, 5000, 300);
        const targetTolerance = positiveNumber(params.targetTolerance, 0.75);
        const pathArgs = { x, y, action, targetId };
        const started = await this.startPathCommandWithRetry(run, pathArgs, budget, completeWhen);
        if (!started) return;
        let completed = false;
        try {
            for (;;) {
                await this.checkPoint(run, budget);
                if (run.pathRestartRequired) {
                    const restarted = await this.startPathCommandWithRetry(
                        run,
                        pathArgs,
                        budget,
                        completeWhen
                    );
                    if (!restarted) {
                        completed = true;
                        run.pathRestartRequired = false;
                        return;
                    }
                    if (!run.pauseRequested) run.pathRestartRequired = false;
                    continue;
                }
                if (completeWhen && await completeWhen()) {
                    if (!await this.stopPath(run, 2)) {
                        throw new Error("failed to stop path after reaching approach distance");
                    }
                    completed = true;
                    run.pathRestartRequired = false;
                    return;
                }
                const statusReply = await this.sendReadOnly(run, "pathStatus", {}, budget, 4000);
                if (run.pathRestartRequired) continue;
                ensureCommandOk(statusReply, "pathStatus");
                const status = parseDetail(statusReply.detail);
                const state = String(status?.state || "");
                const targetX = finiteNumber(status?.targetX);
                const targetY = finiteNumber(status?.targetY);
                if (targetX === undefined || targetY === undefined) {
                    throw new Error("pathStatus missing targetX/targetY");
                }
                if (Math.hypot(targetX - x, targetY - y) > targetTolerance) {
                    throw new Error(
                        `path target was overwritten: expected (${x}, ${y}), actual (${targetX}, ${targetY})`
                    );
                }
                run.outputs.pathStatus = status;
                if (state === "arrived") {
                    completed = true;
                    run.pathActive = false;
                    run.pathRestartRequired = false;
                    return;
                }
                if (state === "failed") throw new Error(`path failed: ${status?.error || "unknown error"}`);
                if (state === "idle") throw new Error("path stopped before arrival");
                await this.waitDelay(run, pollMs, budget);
            }
        } finally {
            if (!completed && run.pathActive) await this.stopPath(run, 2);
            if (!completed) run.pathRestartRequired = false;
        }
    }

    private async startPathCommandWithRetry(
        run: WorkflowRunInternal,
        args: { x: number; y: number; action: number; targetId: number },
        budget: NodeBudget,
        completeWhen?: () => Promise<boolean>
    ): Promise<boolean> {
        const startedActiveMs = this.activeElapsedMs(run);
        let attempts = 0;
        for (;;) {
            try {
                await this.startPathCommand(run, args, budget);
                return true;
            } catch (e) {
                if (!isTransientPathStartError(e)) throw e;
                attempts += 1;
                if (completeWhen && await completeWhen()) {
                    this.appendLog(
                        run,
                        "info",
                        "寻路起点暂未就绪期间目标条件已满足，跳过本次寻路",
                        run.currentNodeId
                    );
                    return false;
                }

                const elapsedMs = this.activeElapsedMs(run) - startedActiveMs;
                if (elapsedMs >= PATH_START_READY_RETRY_MS) {
                    throw new Error(
                        `pathTo start remained not ready for ${Math.trunc(elapsedMs)}ms ` +
                        "(local player or collision map unavailable)"
                    );
                }
                if (attempts === 1) {
                    this.appendLog(
                        run,
                        "warn",
                        `寻路起点暂未就绪，最多等待 ${PATH_START_READY_RETRY_MS}ms 后重试`,
                        run.currentNodeId
                    );
                }
                await this.waitDelay(run, DEFAULT_POLL_MS, budget);
            }
        }
    }

    private async startPathCommand(
        run: WorkflowRunInternal,
        args: { x: number; y: number; action: number; targetId: number },
        budget: NodeBudget
    ): Promise<void> {
        await this.checkPoint(run, budget);
        const remaining = this.remainingBudgetMs(run, budget);
        if (remaining <= 0) throw new NodeTimeoutError();
        run.pathStartInFlight = true;
        try {
            const reply = await this.registry.sendCommand(
                run.pid,
                "pathTo",
                args,
                Math.max(100, Math.min(5000, remaining)),
                run.id
            );
            this.ensureSameConnection(run);
            ensureCommandOk(reply, "pathTo");
            run.pathActive = true;
            if (run.stopRequested || this.destroyed) {
                await this.stopPath(run, 3);
                throw new StopRequestedError();
            }
            // pause 可能发生在 pathTo 已发出、ACK 尚未返回的窗口；这里必须补停，
            // 保留 restart 标记，恢复后由同一节点重新发相同目标并重新确认。
            if (run.pauseRequested) {
                run.pathRestartRequired = true;
                await this.stopPath(run, 2);
                run.pathStartInFlight = false;
                await this.waitUntilRunnable(run);
            }
        } finally {
            run.pathStartInFlight = false;
        }
    }

    private async executeMoveTo(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const x = requiredNumber(params.x, "moveTo.x");
        const y = requiredNumber(params.y, "moveTo.y");
        const action = clampInt(params.action, 1, 3, 1);
        if (action !== 1 && action !== 3) throw new Error("moveTo.action must be 1 or 3");
        const targetId = nonNegativeInt(params.targetId, 0);
        const reply = await this.send(run, "moveTo", { x, y, action, targetId }, budget, 5000);
        ensureCommandOk(reply, "moveTo");
        if (params.confirm === false) return;
        await this.waitForPosition(
            run,
            x,
            y,
            positiveNumber(params.arriveDistance, 1),
            clampInt(params.pollMs, 100, 5000, 300),
            budget
        );
    }

    private async executeWarpTo(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const x = requiredNumber(params.x, "warpTo.x");
        const y = requiredNumber(params.y, "warpTo.y");
        const reply = await this.send(run, "warpTo", { x, y }, budget, 5000);
        ensureCommandOk(reply, "warpTo");
        if (params.confirm === false) return;
        await this.waitForPosition(
            run,
            x,
            y,
            positiveNumber(params.arriveDistance, 2),
            clampInt(params.pollMs, 100, 5000, 300),
            budget
        );
    }

    private async executeTeleport(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const destId = nonNegativeInt(params.destId, 0);
        const cityName = String(params.cityName || "").trim();
        if (destId <= 0 && !cityName) throw new Error("teleport requires destId or cityName");
        const args = destId > 0 ? { destId } : { cityName };
        const reply = await this.send(run, "teleport", args, budget, 5000);
        ensureCommandOk(reply, "teleport");
        const expectedMapId = finiteNumber(params.expectedMapId);
        if (expectedMapId !== undefined) {
            await this.waitForStatus(run, budget, (status) => Number(status?.mapId) === expectedMapId,
                clampInt(params.pollMs, 100, 5000, DEFAULT_POLL_MS));
        } else {
            await this.waitDelay(run, nonNegativeNumber(params.settleMs, 1000), budget);
        }
    }

    private async executeRevive(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const args = {
            safetyCheck: params.safetyCheck !== false,
            mode: clampInt(params.mode, 1, 1000, 1),
        };
        const reply = await this.send(run, "reviveToTown", args, budget, 5000);
        ensureCommandOk(reply, "reviveToTown");
        const expectedMapId = finiteNumber(params.expectedMapId);
        await this.waitForStatus(
            run,
            budget,
            (status) => classifyLocalPlayerStatus(status) === "alive" &&
                (expectedMapId === undefined || Number(status?.mapId) === expectedMapId),
            clampInt(params.pollMs, 100, 5000, DEFAULT_POLL_MS)
        );
    }

    private async executeApproachEntity(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const approachDistance = positiveNumber(params.approachDistance, 3);
        const pollMs = clampInt(params.pollMs, 100, 5000, 300);
        const directId = nonNegativeInt(params.creatureId, 0);

        const findTarget = async (lockedId = 0): Promise<NearbyEntity> => {
            const entities = await this.queryNearbyEntities(run, params, budget);
            const wantedId = lockedId || directId;
            const target = wantedId > 0
                ? entities.find((entry) => entry.id === wantedId)
                : entities.sort(byDistance)[0];
            if (!target) {
                throw new Error(wantedId > 0
                    ? `approach target ${wantedId} is no longer nearby`
                    : "matching nearby entity not found");
            }
            return target;
        };
        const targetDistance = (target: NearbyEntity): number => {
            const distance = finiteNumber(target.distance);
            if (distance === undefined) throw new Error("approach target missing distance");
            return distance;
        };
        const recordTarget = (target: NearbyEntity): void => {
            run.outputs.lastEntity = target;
        };
        const logArrived = (target: NearbyEntity): void => {
            this.appendLog(
                run,
                "info",
                `已靠近 ${target.name || target.id}，距离 ${roundNumber(targetDistance(target))}`,
                run.currentNodeId
            );
        };

        let target = await findTarget();
        const lockedId = target.id;
        for (;;) {
            recordTarget(target);
            if (targetDistance(target) <= approachDistance) {
                logArrived(target);
                return;
            }
            const targetX = finiteNumber(target.x);
            const targetY = finiteNumber(target.y);
            if (targetX === undefined || targetY === undefined) {
                throw new Error("approach target missing coordinates");
            }

            await this.executePathTo(run, {
                x: targetX,
                y: targetY,
                action: 1,
                targetId: 0,
                pollMs,
            }, budget, async () => {
                target = await findTarget(lockedId);
                recordTarget(target);
                return targetDistance(target) <= approachDistance;
            });

            target = await findTarget(lockedId);
            recordTarget(target);
            if (targetDistance(target) <= approachDistance) {
                logArrived(target);
                return;
            }
            const currentX = finiteNumber(target.x);
            const currentY = finiteNumber(target.y);
            if (currentX === undefined || currentY === undefined) {
                throw new Error("approach target missing coordinates");
            }
            if (Math.hypot(currentX - targetX, currentY - targetY) <= 0.5) {
                throw new Error(
                    `path reached entity coordinates but distance is ${roundNumber(targetDistance(target))}`
                );
            }
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async executeInteract(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        let target: NearbyEntity | undefined;
        const directId = nonNegativeInt(params.creatureId, 0);
        if (directId > 0) {
            target = { id: directId };
        } else {
            const entities = await this.queryNearbyEntities(run, params, budget);
            target = entities.sort(byDistance)[0];
            if (!target) throw new Error("matching nearby entity not found");
        }
        const reply = await this.send(run, "talkOrAttack", { creatureId: target.id }, budget, 5000);
        ensureCommandOk(reply, "talkOrAttack");
        run.outputs.lastEntity = target;
        const waitDialog = typeof params.waitDialog === "boolean" ? params.waitDialog : target.isNpc === true;
        if (!waitDialog) return;
        const pollMs = clampInt(params.pollMs, 100, 5000, 300);
        for (;;) {
            await this.checkPoint(run, budget);
            const dialogReply = await this.send(run, "getDialog", {}, budget, 3000);
            ensureCommandOk(dialogReply, "getDialog");
            const dialog = parseDetail(dialogReply.detail);
            if (dialog?.open === true) {
                run.outputs.dialog = dialog;
                return;
            }
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async executeDialogSelect(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const option = requiredInteger(params.option, "dialogSelect.option");
        let npcId = nonNegativeInt(params.npcId, 0);
        const nearestNpc = params.nearestNpc === true;
        const raw = params.raw === true || npcId > 0 || nearestNpc;
        if (raw && option === 9575) {
            throw new Error("raw opt=9575 is reserved for the targeted dungeonEntry node");
        }
        let selectedNpc: NearbyEntity | undefined;
        if (nearestNpc) {
            const waitForNpcMs = nonNegativeNumber(params.waitForNpcMs, 5000);
            const pollMs = clampInt(params.pollMs, 100, 5000, 300);
            const startedActiveMs = this.activeElapsedMs(run);
            for (;;) {
                const candidates = (await this.queryNearbyEntities(
                    run,
                    { ...params, isNpc: true },
                    budget
                ))
                    .filter((entry) => entry.distance !== undefined && Number.isFinite(entry.distance))
                    .sort((left, right) =>
                        Number(left.distance) - Number(right.distance) || left.id - right.id
                    );
                selectedNpc = candidates[0];
                if (selectedNpc) break;
                if (this.activeElapsedMs(run) - startedActiveMs >= waitForNpcMs) {
                    const scope = params.allVisible === true
                        ? "current view"
                        : `distance ${nonNegativeNumber(params.maxDistance, 5)}`;
                    throw new Error(`matching nearby NPC not found within ${scope}`);
                }
                await this.waitDelay(run, pollMs, budget);
            }
            npcId = selectedNpc.id;
            const resolved = {
                npcId,
                monsterTblId: selectedNpc.monsterTblId,
                name: selectedNpc.name,
                distance: selectedNpc.distance,
                option,
                sub: nonNegativeInt(params.sub, 1),
            };
            run.outputs.lastEntity = selectedNpc;
            run.outputs.lastDialogSelect = resolved;
            if (run.currentNodeId) run.outputs[`dialogSelect.${run.currentNodeId}`] = resolved;
            this.appendLog(
                run,
                "info",
                `动态选取最近 NPC ${selectedNpc.name || "(unnamed)"}，npcId=${npcId}，` +
                    `monsterTblId=${selectedNpc.monsterTblId ?? 0}，距离=${Number(selectedNpc.distance).toFixed(1)}，opt=${option}`
            );
        }
        const action = raw ? "sendDialogSelectRaw" : "selectDialogOption";
        const args = raw
            ? { npcId, option, sub: nonNegativeInt(params.sub, 1) }
            : { option };
        if (raw && npcId <= 0) throw new Error("raw dialogSelect requires npcId or nearestNpc=true");
        const reply = await this.send(run, action, args, budget, 5000);
        ensureCommandOk(reply, action);

        const expectedMapId = finiteNumber(params.expectedMapId);
        if (expectedMapId !== undefined) {
            await this.waitForStatus(run, budget, (status) => Number(status?.mapId) === expectedMapId,
                clampInt(params.pollMs, 100, 5000, DEFAULT_POLL_MS));
        } else if (finiteNumber(params.settleMs) !== undefined) {
            await this.waitDelay(run, nonNegativeNumber(params.settleMs, 0), budget);
        }
    }

    private async executeDungeonEntry(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        this.ensureSameConnection(run);
        const instance = this.registry.list().find((entry) => entry.pid === run.pid);
        if (!instance || !versionAtLeast(instance.dllVersion, DUNGEON_ENTRY_MIN_DLL_VERSION)) {
            throw new Error(
                `dungeonEntry requires GGThreadBlock.dll >= ${DUNGEON_ENTRY_MIN_DLL_VERSION}; current ${instance?.dllVersion || "unknown"}. Restart the game client after updating the DLL.`
            );
        }
        if (instance.nativeNpcUiBridgeReady !== true) {
            throw new Error(
                "dungeonEntry native NPC UI bridge is unavailable; restart the game client and check the NativeUiBridge startup log"
            );
        }
        if (run.dungeonEntryActive || run.dungeonEntryStartInFlight ||
            run.dungeonEntryRequestToken) {
            throw new Error("a previous dungeon entry token is still awaiting cleanup");
        }
        const option = requiredInteger(params.option, "dungeonEntry.option");
        const sub = nonNegativeInt(params.sub, 1);
        const autoUse = params.autoUseExtraTicket === true;
        if (autoUse &&
            (run.dungeonEntryTicketUseAttempted || run.dungeonEntryTicketConsumed)) {
            throw new Error("this workflow run has already attempted its one allowed extra entry ticket");
        }
        const ticketItemId = nonNegativeInt(params.ticketItemId, DUNGEON_ENTRY_TICKET_ITEM_ID);
        const expectedMapName = String(params.expectedMapNameContains || "").trim();
        const pollMs = clampInt(params.pollMs, 100, 5000, 300);
        const waitForNpcMs = nonNegativeNumber(params.waitForNpcMs, 5000);
        const nativeDialogTimeoutMs = clampInt(params.nativeDialogTimeoutMs, 5000, 30_000, 12_000);
        const nativeDialogMaxSteps = clampInt(params.nativeDialogMaxSteps, 1, 32, 10);
        const mapStableDurationMs = clampInt(params.mapStableDurationMs, 0, 60_000, 1500);
        const scopeTtlMs = clampInt(params.scopeTtlMs, 5000, 60_000, 30_000);
        if (ticketItemId !== DUNGEON_ENTRY_TICKET_ITEM_ID) {
            throw new Error(`dungeonEntry.ticketItemId must be ${DUNGEON_ENTRY_TICKET_ITEM_ID}`);
        }
        if (!expectedMapName) throw new Error("dungeonEntry.expectedMapNameContains is required");

        const npcStartedActiveMs = this.activeElapsedMs(run);
        let selectedNpc: NearbyEntity | undefined;
        for (;;) {
            const candidates = (await this.queryNearbyEntities(
                run,
                { ...params, isNpc: true },
                budget
            ))
                .filter((entry) => entry.distance !== undefined && Number.isFinite(entry.distance))
                .sort((left, right) =>
                    Number(left.distance) - Number(right.distance) || left.id - right.id
                );
            selectedNpc = candidates[0];
            if (selectedNpc) break;
            if (this.activeElapsedMs(run) - npcStartedActiveMs >= waitForNpcMs) {
                const scope = params.allVisible === true
                    ? "current view"
                    : `distance ${nonNegativeNumber(params.maxDistance, 5)}`;
                throw new Error(`matching nearby NPC not found within ${scope}`);
            }
            await this.waitDelay(run, pollMs, budget);
        }

        if (Number(selectedNpc.distance) > 3) {
            await this.executeApproachEntity(run, {
                ...params,
                creatureId: selectedNpc.id,
                isNpc: true,
                approachDistance: 3,
                pollMs,
            }, budget);
            const refreshed = (await this.queryNearbyEntities(
                run,
                { ...params, isNpc: true },
                budget
            )).find((entry) => entry.id === selectedNpc!.id);
            if (!refreshed || finiteNumber(refreshed.distance) === undefined ||
                Number(refreshed.distance) > 3) {
                throw new Error(`dungeon entry NPC ${selectedNpc.id} is not within interaction distance 3`);
            }
            selectedNpc = refreshed;
        }

        run.outputs.lastEntity = selectedNpc;
        run.dungeonEntryAborted = false;
        let ticketConsumedConfirmed = false;
        let ticketCountBefore: number | undefined;
        let queryAfterSeq = 0;
        let completed = false;
        let primaryError: Error | undefined;
        try {
            const talkReply = await this.sendNativeUi(
                run,
                "talkOrAttack",
                { creatureId: selectedNpc.id },
                budget
            );
            ensureCommandOk(talkReply, "talkOrAttack");

            const dialogStartedActiveMs = this.activeElapsedMs(run);
            let dialogOptionIndex: number | undefined;
            let nativeDialogSteps = 0;
            let wheelDialogOpened = false;
            for (;;) {
                await this.checkPoint(run, budget);
                const dialogReply = await this.send(run, "getDialog", {}, budget, 3000);
                ensureCommandOk(dialogReply, "getDialog");
                const dialog = parseDetail(dialogReply.detail);
                const dialogNpcId = isRecord(dialog) ? finiteNumber(dialog.npcInteractId) : undefined;
                if (dialog?.open === true && dialogNpcId === selectedNpc.id) {
                    const advanceReply = await this.sendNativeUi(
                        run,
                        "advanceDialogTowardOpt",
                        {
                            npcId: selectedNpc.id,
                            targetOpt: option,
                            allowAdvance: nativeDialogSteps < nativeDialogMaxSteps,
                        },
                        budget
                    );
                    const advanced = parseDetail(advanceReply.detail);
                    if (!advanceReply.ok) {
                        if (isRecord(advanced) && advanced.retryable === true) {
                            if (this.activeElapsedMs(run) - dialogStartedActiveMs >= nativeDialogTimeoutMs) {
                                throw new Error(
                                    `native NPC dialog remained unstable before opt=${option}: ${String(advanced.error || "retryable read failure")}`
                                );
                            }
                            await this.waitDelay(run, pollMs, budget);
                            continue;
                        }
                        ensureCommandOk(advanceReply, "advanceDialogTowardOpt");
                    }
                    if (!isRecord(advanced) || advanced.ok !== true) {
                        throw new Error("advanceDialogTowardOpt returned invalid detail");
                    }
                    if (advanced.found === true) {
                        dialogOptionIndex = requiredInteger(
                            advanced.optionIndex,
                            "advanceDialogTowardOpt.optionIndex"
                        );
                        const warpTableId = requiredInteger(
                            advanced.warpTableId,
                            "advanceDialogTowardOpt.warpTableId"
                        );
                        if (warpTableId !== DUNGEON_ENTRY_EXPECTED_WARP_TABLE_ID) {
                            throw new Error(
                                `native target opt=${option} resolved warp table ${warpTableId}, expected ${DUNGEON_ENTRY_EXPECTED_WARP_TABLE_ID}`
                            );
                        }
                        run.outputs.dialog = dialog;
                        this.appendLog(
                            run,
                            "info",
                            `已由 DLL 唯一定位 opt=${option} index=${dialogOptionIndex} warpTable=${warpTableId}`,
                            run.currentNodeId
                        );
                        break;
                    }
                    if (advanced.advanced !== true) {
                        throw new Error("advanceDialogTowardOpt neither found nor advanced the dialog");
                    }
                    nativeDialogSteps += 1;
                    this.appendLog(
                        run,
                        "info",
                        `原生對話安全推進 ${nativeDialogSteps}/${nativeDialogMaxSteps}（未發送非目標 opt）`,
                        run.currentNodeId
                    );
                } else if (!wheelDialogOpened) {
                    const wheelReply = await this.sendNativeUi(
                        run,
                        "openNpcDialogFromWheel",
                        { npcId: selectedNpc.id },
                        budget
                    );
                    const wheel = parseDetail(wheelReply.detail);
                    const wheelClaimed = isRecord(wheel) &&
                        (wheel.callbackInvoked === true || wheel.alreadyOpen === true);
                    if (!wheelReply.ok) {
                        if (wheelClaimed) {
                            wheelDialogOpened = true;
                            this.appendLog(
                                run,
                                "info",
                                `DLL 已經只執行一次輪盤「對話」；等待普通 NPC 對話建立，NPC=${selectedNpc.id}`,
                                run.currentNodeId
                            );
                            if (this.activeElapsedMs(run) - dialogStartedActiveMs >= nativeDialogTimeoutMs) {
                                throw new Error("NPC wheel talk was invoked once, but the ordinary dialog did not open before timeout");
                            }
                            await this.waitDelay(run, pollMs, budget);
                            continue;
                        }
                        if (isRecord(wheel) && wheel.retryable === true) {
                            if (this.activeElapsedMs(run) - dialogStartedActiveMs >= nativeDialogTimeoutMs) {
                                throw new Error(
                                    `NPC interaction wheel did not become ready: ${String(wheel.error || "retryable wheel state")}`
                                );
                            }
                            await this.waitDelay(run, pollMs, budget);
                            continue;
                        }
                        ensureCommandOk(wheelReply, "openNpcDialogFromWheel");
                    }
                    if (!isRecord(wheel) || wheel.ok !== true) {
                        throw new Error("openNpcDialogFromWheel returned invalid detail");
                    }
                    wheelDialogOpened = true;
                    this.appendLog(
                        run,
                        "info",
                        `已在 GameLoop 執行互動輪盤「對話」，NPC=${selectedNpc.id}，UI=${String(wheel.foregroundUiId ?? 10)}`,
                        run.currentNodeId
                    );
                }
                if (this.activeElapsedMs(run) - dialogStartedActiveMs >= nativeDialogTimeoutMs) {
                    throw new Error(
                        `native NPC dialog did not expose opt=${option} for npc=${selectedNpc.id}`
                    );
                }
                await this.waitDelay(run, pollMs, budget);
            }

            const resolvedDialogOptionIndex = requiredInteger(
                dialogOptionIndex,
                "dungeonEntry native dialog option index"
            );
            await this.checkPoint(run, budget);
            this.ensureSameConnection(run);
            if (this.remainingBudgetMs(run, budget) < 5000) throw new NodeTimeoutError();
            const requestToken = uuid();
            run.dungeonEntryRequestToken = requestToken;
            run.dungeonEntryActive = true;
            run.dungeonEntryStartInFlight = true;
            await this.prepareDungeonEntryCritical(run, budget, {
                requestToken,
                npcId: selectedNpc.id,
                opt: option,
                sub,
                dialogOptionIndex: resolvedDialogOptionIndex,
                expectedWarpTableId: DUNGEON_ENTRY_EXPECTED_WARP_TABLE_ID,
                autoUseExtraTicket: autoUse,
                ttlMs: scopeTtlMs,
            });

            if (run.dungeonEntryAborted || run.pauseRequested) {
                run.dungeonEntryAborted = true;
                await this.cancelDungeonEntry(run, 2);
                throw new Error("dungeon entry cancelled before commit; target opt was not sent");
            }

            const committed = await this.commitDungeonEntryCritical(run, budget, requestToken);
            const generation = requiredInteger(committed.generation, "commitDungeonEntry.generation");
            const baselineSeq = nonNegativeInt(committed.baselineSeq, 0);
            const ticketCount = nonNegativeInt(committed.ticketCount, 0);
            ticketCountBefore = ticketCount;
            queryAfterSeq = baselineSeq;
            run.dungeonEntryGeneration = generation;
            run.outputs.lastDungeonEntry = {
                requestToken,
                generation,
                npcId: selectedNpc.id,
                npcName: selectedNpc.name,
                option,
                sub,
                dialogOptionIndex: resolvedDialogOptionIndex,
                expectedWarpTableId: DUNGEON_ENTRY_EXPECTED_WARP_TABLE_ID,
                autoUseExtraTicket: autoUse,
                ticketItemId,
                ticketCountBefore: ticketCount,
                maxTicketUses: DUNGEON_ENTRY_MAX_TICKETS,
            };
            this.appendLog(
                run,
                "info",
                `已完成 ${selectedNpc.name || "(unnamed)"} 的原生對話選項與最後一頁，GameLoop 已實際發出 opt=${option}；generation=${generation}`,
                run.currentNodeId
            );

            if (run.dungeonEntryAborted || run.pauseRequested) {
                run.dungeonEntryAborted = true;
                await this.cancelDungeonEntry(run, 2);
                throw new Error("副本入場在暫停時已取消；恢復後不會重新發送入口 opt");
            }

            let afterSeq = baselineSeq;
            let firstRegistrationSeq = 0;
            let ticketBranch = false;
            let freeRegistrationAccepted = false;
            let acceptedRegistrationResult: number | undefined;
            let scopedAccepted = false;
            let ticketUpdateSeq = 0;
            let continuationSeq = 0;
            let postTicketRegistrationSeq = 0;
            let matchedMapSinceActiveMs: number | null = null;
            let lastScopedState = "Armed";
            let lastDialogSeenCount = 0;

            for (;;) {
                await this.checkPoint(run, budget);
                if (run.dungeonEntryAborted) {
                    throw new Error("副本入場已取消；不會重新發送入口 opt");
                }

                const reply = await this.send(
                    run,
                    "queryDungeonEntry",
                    { requestToken, afterSeq },
                    budget,
                    4000
                );
                if (run.dungeonEntryAborted) {
                    throw new Error("副本入場已取消；不會重新發送入口 opt");
                }
                ensureCommandOk(reply, "queryDungeonEntry");
                const status = normalizeDungeonEntryStatus(parseDetail(reply.detail));
                if (status.requestToken !== requestToken) {
                    throw new Error(`queryDungeonEntry token mismatch: expected ${requestToken}, actual ${status.requestToken}`);
                }
                if (status.generation !== generation) {
                    throw new Error(`queryDungeonEntry generation mismatch: expected ${generation}, actual ${status.generation}`);
                }
                if (status.truncated) {
                    throw new Error("dungeonEntry ordered event history was truncated");
                }
                if (["CommitFailed", "Cancelled", "Expired"].includes(status.transactionState)) {
                    throw new Error(`dungeon entry transaction became ${status.transactionState}`);
                }
                run.outputs.lastDungeonEntry = {
                    ...(isRecord(run.outputs.lastDungeonEntry) ? run.outputs.lastDungeonEntry : {}),
                    diagnostics: summarizeDungeonEntryStatus(status),
                };
                if (status.state !== lastScopedState) {
                    this.appendLog(
                        run,
                        "info",
                        `限定入場確認狀態：${lastScopedState} -> ${status.state}；dialogSeen=${status.dialogSeenCount}，lastType=${status.lastDialogType}，match=${status.lastDialogMatch}，authorized=${status.lastDialogAuthorized}`,
                        run.currentNodeId
                    );
                    lastScopedState = status.state;
                }
                if (status.dialogSeenCount > lastDialogSeenCount) {
                    this.appendLog(
                        run,
                        "info",
                        `限定入場彈窗：seen=${status.dialogSeenCount}，type=${status.lastDialogType}，match=${status.lastDialogMatch}，authorized=${status.lastDialogAuthorized}，towerPromptAccepted=${status.towerPromptAccepted}`,
                        run.currentNodeId
                    );
                    lastDialogSeenCount = status.dialogSeenCount;
                }

                const orderedEvents = status.events
                    .filter((event) => event.seq > afterSeq)
                    .sort((left, right) => left.seq - right.seq);
                for (const event of orderedEvents) {
                    afterSeq = Math.max(afterSeq, event.seq);
                    if (event.proto === 511602) {
                        const result = finiteNumber(event.result);
                        const nInsd = finiteNumber(event.nInsd);
                        if (result === undefined || nInsd === undefined) {
                            throw new Error("511602 event is missing result/nInsd");
                        }
                        if (firstRegistrationSeq === 0) {
                            firstRegistrationSeq = event.seq;
                            if (result === 5 && nInsd === 1) {
                                ticketBranch = true;
                                this.appendLog(
                                    run,
                                    "info",
                                    `收到 511602 seq=${event.seq} result=5,nInsd=1；等待一次性票券確認`,
                                    run.currentNodeId
                                );
                                if (!autoUse) {
                                    throw new Error("免費入場次數已用盡，且未啟用自動使用額外入場券");
                                }
                                if (ticketCount <= 0) {
                                    throw new Error("免费次数用尽且没有额外入场券");
                                }
                            } else if ((result === 0 || result === 1 || result === 9) && nInsd === 1) {
                                freeRegistrationAccepted = true;
                                acceptedRegistrationResult = result;
                            } else {
                                throw new Error(`副本入場被服務端拒絕：511602 result=${result}, nInsd=${nInsd}`);
                            }
                        } else if (ticketBranch) {
                            if ((result === 0 || result === 1 || result === 9) && nInsd === 1) {
                                postTicketRegistrationSeq = event.seq;
                                acceptedRegistrationResult = result;
                            } else {
                                throw new Error(`使用票券後入場被服務端拒絕：511602 result=${result}, nInsd=${nInsd}`);
                            }
                        }
                    } else if (event.proto === 521603 && event.opt === option) {
                        if (event.branch === 26) {
                            throw new Error(`521603 opt=${option} returned forbidden branch=26`);
                        }
                        continuationSeq = event.seq;
                    }
                }
                afterSeq = Math.max(afterSeq, status.latestSeq);
                queryAfterSeq = afterSeq;

                if (ticketBranch && status.state === "Declined") {
                    throw new Error(
                        `額外入場券確認框未被限定確認器接受（dialogSeen=${status.dialogSeenCount}, lastType=${status.lastDialogType}, match=${status.lastDialogMatch}, authorized=${status.lastDialogAuthorized}, towerPromptAccepted=${status.towerPromptAccepted}）`
                    );
                }
                if (ticketBranch && status.state === "Expired") {
                    throw new Error(
                        `等待限定額外入場券確認已超時（dialogSeen=${status.dialogSeenCount}, lastType=${status.lastDialogType}, match=${status.lastDialogMatch}, authorized=${status.lastDialogAuthorized}, towerPromptAccepted=${status.towerPromptAccepted}）`
                    );
                }
                if (ticketBranch && status.state === "Cancelled") {
                    throw new Error("副本入場限定確認已取消");
                }
                if (status.state === "Accepted") {
                    scopedAccepted = true;
                    run.dungeonEntryTicketUseAttempted = true;
                }

                if (ticketBranch && status.ticketAck) {
                    ticketUpdateSeq = status.ticketAckSeq;
                    if (ticketCount <= 0) throw new Error("ticket ACK arrived without a ticket baseline");
                    if (ticketUpdateSeq <= firstRegistrationSeq) {
                        throw new Error("額外入場券 511325 ACK 次序不成立");
                    }
                    ticketConsumedConfirmed = true;
                    run.dungeonEntryTicketConsumed = true;
                }

                const ticketProtocolReady = isTicketProtocolReady({
                    ticketBranch,
                    scopedAccepted,
                    ticketConsumedConfirmed,
                    firstRegistrationSeq,
                    continuationSeq,
                    ticketUpdateSeq,
                    postTicketRegistrationSeq,
                });
                const protocolReady = freeRegistrationAccepted || ticketProtocolReady;

                const gameStatus = await this.queryStatus(run, budget);
                if (run.dungeonEntryAborted) {
                    throw new Error("副本入場已取消；不會重新發送入口 opt");
                }
                const mapName = String(gameStatus?.mapName || "");
                const mapObservation = observeStableMatch(
                    matchedMapSinceActiveMs,
                    mapName.includes(expectedMapName),
                    this.activeElapsedMs(run),
                    mapStableDurationMs
                );
                matchedMapSinceActiveMs = mapObservation.sinceMs;
                if (mapObservation.complete) {
                    completed = true;
                    if (ticketBranch && !ticketConsumedConfirmed) {
                        run.dungeonEntryTicketUseAttempted = true;
                    }
                    const ticketCountAfter = ticketConsumedConfirmed
                        ? status.currentTicketCount
                        : undefined;
                    run.outputs.lastDungeonEntry = {
                        ...(isRecord(run.outputs.lastDungeonEntry) ? run.outputs.lastDungeonEntry : {}),
                        path: ticketBranch ? "ticket" : (freeRegistrationAccepted ? "free" : "stable-map"),
                        completionSignal: protocolReady ? "protocol_and_map" : "stable_map",
                        protocolConfirmed: protocolReady,
                        ticketConsumedConfirmed,
                        ...(ticketCountAfter === undefined ? {} : { ticketCountAfter }),
                        mapId: gameStatus.mapId,
                        mapName,
                        registrationResult: acceptedRegistrationResult,
                        eventsThroughSeq: afterSeq,
                    };
                    let message: string;
                    if (ticketConsumedConfirmed) {
                        message = `已確認額外入場券 ${ticketCount} -> ${ticketCountAfter}，` +
                            `511602 result=${acceptedRegistrationResult}，並穩定進入 ${mapName}`;
                    } else if (freeRegistrationAccepted) {
                        message = `免費入場成功（511602 result=${acceptedRegistrationResult}），已穩定進入 ${mapName}`;
                    } else {
                        message = `已穩定進入 ${mapName}；不再等待尚未齊全的入口協議事件`;
                    }
                    this.appendLog(run, "info", message, run.currentNodeId);
                    return;
                }
                await this.waitDelay(run, pollMs, budget);
            }
        } catch (e: any) {
            if (!ticketConsumedConfirmed && run.dungeonEntryRequestToken) {
                let finalStatus = await this.queryDungeonEntryBestEffort(
                    run,
                    run.dungeonEntryRequestToken,
                    queryAfterSeq
                );
                if (finalStatus?.initialSelectClaimed === true &&
                    finalStatus.state === "Accepted" && finalStatus.ticketAck !== true) {
                    const graceDeadline = Date.now() + 10_000;
                    while (Date.now() < graceDeadline) {
                        await new Promise<void>((resolve) => setTimeout(resolve, 250));
                        const refreshed = await this.queryDungeonEntryBestEffort(
                            run,
                            run.dungeonEntryRequestToken,
                            finalStatus.latestSeq
                        );
                        if (refreshed) finalStatus = refreshed;
                        if (finalStatus.ticketAck === true) break;
                    }
                }
                if (finalStatus) {
                    if (finalStatus.state === "Accepted") {
                        run.dungeonEntryTicketUseAttempted = true;
                    }
                    run.outputs.lastDungeonEntry = {
                        ...(isRecord(run.outputs.lastDungeonEntry) ? run.outputs.lastDungeonEntry : {}),
                        diagnostics: summarizeDungeonEntryStatus(finalStatus),
                    };
                }
                if (finalStatus?.initialSelectClaimed === true && finalStatus.ticketAck === true) {
                    if (ticketCountBefore === undefined) ticketCountBefore = finalStatus.ticketCount;
                    ticketConsumedConfirmed = true;
                    run.dungeonEntryTicketConsumed = true;
                    run.outputs.lastDungeonEntry = {
                        ...(isRecord(run.outputs.lastDungeonEntry) ? run.outputs.lastDungeonEntry : {}),
                        ticketConsumedConfirmed: true,
                        ticketCountAfter: Math.max(0, ticketCountBefore - 1),
                    };
                }
            }
            const message = e instanceof Error ? e.message : String(e);
            if (ticketConsumedConfirmed && !completed) {
                primaryError = new Error(`票券已消耗但入场失败：${message}`);
            } else {
                primaryError = e instanceof Error ? e : new Error(message);
            }
            throw primaryError;
        } finally {
            let cleanupConfirmed = !run.dungeonEntryActive && !run.dungeonEntryStartInFlight;
            if (run.dungeonEntryActive || run.dungeonEntryStartInFlight) {
                cleanupConfirmed = await this.cancelDungeonEntryAfterStart(run, 2);
            }
            if (cleanupConfirmed) {
                run.dungeonEntryActive = false;
                run.dungeonEntryRequestToken = undefined;
                run.dungeonEntryGeneration = undefined;
            } else {
                const cleanupMessage =
                    "targeted dungeon entry cleanup could not be confirmed; refusing workflow transition";
                this.appendLog(
                    run,
                    "warn",
                    "targeted dungeon entry cleanup is still pending; preserving requestToken for outer retry",
                    run.currentNodeId
                );
                if (primaryError) {
                    primaryError.message = `${primaryError.message}; ${cleanupMessage}`;
                } else {
                    throw new Error(cleanupMessage);
                }
            }
        }
    }

    private async resolveCombatTargetDistance(
        run: WorkflowRunInternal,
        target: NearbyEntity,
        approachDistance: number,
        budget: NodeBudget,
        validateState: () => Promise<void>
    ): Promise<number | undefined> {
        const reportedDistance = finiteNumber(target.distance);
        if (reportedDistance !== undefined && reportedDistance <= approachDistance) {
            return reportedDistance;
        }

        const startedActiveMs = this.activeElapsedMs(run);
        let attempts = 0;
        for (;;) {
            await validateState();
            try {
                const position = await this.queryPosition(run, budget);
                return combatNavigationDistance(
                    position.x,
                    position.y,
                    target.x,
                    target.y,
                    reportedDistance
                );
            } catch (e) {
                if (!isTransientLocalPositionError(e)) throw e;
                attempts += 1;
                const elapsedMs = this.activeElapsedMs(run) - startedActiveMs;
                if (elapsedMs >= PATH_START_READY_RETRY_MS) {
                    throw new RecoverableCombatTargetError(
                        target,
                        `local player position remained unavailable for ${Math.trunc(elapsedMs)}ms`
                    );
                }
                if (attempts === 1) {
                    this.appendLog(
                        run,
                        "warn",
                        "战斗位置暂时不可读，复核角色与地图后短暂重试",
                        run.currentNodeId
                    );
                }
                await this.waitDelay(run, Math.min(300, DEFAULT_POLL_MS), budget);
            }
        }
    }

    private async approachCombatTarget(
        run: WorkflowRunInternal,
        target: NearbyEntity,
        approachDistance: number,
        pollMs: number,
        budget: NodeBudget,
        validateState: () => Promise<void>
    ): Promise<NearbyEntity | undefined> {
        let current: NearbyEntity | undefined = target;
        for (;;) {
            await this.checkPoint(run, budget);
            if (!current) return undefined;
            if (current.hp !== undefined && current.hp <= 0) return current;
            const distance = await this.resolveCombatTargetDistance(
                run,
                current,
                approachDistance,
                budget,
                validateState
            );
            if (distance === undefined) throw new Error(`combat target ${target.id} is missing distance`);
            if (distance <= approachDistance) return current;

            await this.waitDelay(run, Math.min(250, Math.max(100, pollMs)), budget);
            await validateState();
            const confirmedEntities = await this.queryNearbyEntities(
                run,
                { allVisible: true, maxDistance: 0 },
                budget
            );
            current = confirmedEntities.find((entry) => entry.id === target.id);
            if (!current) return undefined;
            if (current.hp !== undefined && current.hp <= 0) return current;
            const confirmedDistance = await this.resolveCombatTargetDistance(
                run,
                current,
                approachDistance,
                budget,
                validateState
            );
            if (confirmedDistance === undefined) {
                throw new Error(`combat target ${target.id} is missing distance`);
            }
            if (confirmedDistance <= approachDistance) {
                this.appendLog(
                    run,
                    "info",
                    "忽略一次战斗目标距离跳变，复核后目标仍在攻击范围内",
                    run.currentNodeId
                );
                return current;
            }
            let endpointRetryStartedActiveMs = this.activeElapsedMs(run);
            let endpointAttempts = 0;
            let candidateCursor = 0;
            let lastEndpointError = "";
            for (;;) {
                await validateState();
                const latestEntities = await this.queryNearbyEntities(
                    run,
                    { allVisible: true, maxDistance: 0 },
                    budget
                );
                current = latestEntities.find((entry) => entry.id === target.id);
                if (!current) return undefined;
                if (current.hp !== undefined && current.hp <= 0) return current;
                const latestDistance = await this.resolveCombatTargetDistance(
                    run,
                    current,
                    approachDistance,
                    budget,
                    validateState
                );
                if (latestDistance === undefined) {
                    throw new Error(`combat target ${target.id} is missing distance`);
                }
                if (latestDistance <= approachDistance) return current;

                const targetX = finiteNumber(current.x);
                const targetY = finiteNumber(current.y);
                if (targetX === undefined || targetY === undefined) {
                    throw new Error(`combat target ${target.id} is missing coordinates`);
                }
                const playerPosition = await this.queryPosition(run, budget);
                const candidates = buildCombatApproachCandidates(
                    playerPosition.x,
                    playerPosition.y,
                    targetX,
                    targetY,
                    approachDistance
                );
                const destinations = candidates.length > 0
                    ? candidates
                    : [{ x: targetX, y: targetY }];
                const destination = destinations[candidateCursor % destinations.length];

                try {
                    await this.executePathTo(run, {
                        x: destination.x,
                        y: destination.y,
                        action: 1,
                        targetId: 0,
                        pollMs,
                    }, budget, async () => {
                        const entities = await this.queryNearbyEntities(
                            run,
                            { allVisible: true, maxDistance: 0 },
                            budget
                        );
                        current = entities.find((entry) => entry.id === target.id);
                        if (!current || (current.hp !== undefined && current.hp <= 0)) return true;
                        const refreshedDistance = await this.resolveCombatTargetDistance(
                            run,
                            current,
                            approachDistance,
                            budget,
                            validateState
                        );
                        if (refreshedDistance === undefined) {
                            throw new Error(`combat target ${target.id} is missing distance`);
                        }
                        return refreshedDistance <= approachDistance;
                    });
                    await validateState();
                    const postPathEntities = await this.queryNearbyEntities(
                        run,
                        { allVisible: true, maxDistance: 0 },
                        budget
                    );
                    current = postPathEntities.find((entry) => entry.id === target.id);
                    if (!current) return undefined;
                    if (current.hp !== undefined && current.hp <= 0) return current;
                    const postPathDistance = await this.resolveCombatTargetDistance(
                        run,
                        current,
                        approachDistance,
                        budget,
                        validateState
                    );
                    if (postPathDistance === undefined) {
                        throw new Error(`combat target ${target.id} is missing distance`);
                    }
                    if (postPathDistance <= approachDistance) return current;

                    candidateCursor += 1;
                    const madeProgress = postPathDistance <= latestDistance - 1;
                    if (madeProgress) {
                        endpointAttempts = 0;
                        endpointRetryStartedActiveMs = this.activeElapsedMs(run);
                        this.appendLog(
                            run,
                            "info",
                            `战斗靠近已有进展但尚未进入攻击范围，刷新目标并改试下一邻格`,
                            run.currentNodeId
                        );
                        continue;
                    }

                    endpointAttempts += 1;
                    lastEndpointError =
                        `candidate (${roundNumber(destination.x)},${roundNumber(destination.y)}) ` +
                        `completed but target distance remained ${roundNumber(postPathDistance)}`;
                    const elapsedMs = this.activeElapsedMs(run) - endpointRetryStartedActiveMs;
                    if (endpointAttempts >= COMBAT_PATH_RECOVERY_MAX_ATTEMPTS ||
                        elapsedMs >= COMBAT_PATH_RECOVERY_MS) {
                        throw new RecoverableCombatTargetError(
                            current,
                            `could not find a reachable adjacent tile after ` +
                            `${endpointAttempts} attempts/${Math.trunc(elapsedMs)}ms: ${lastEndpointError}`
                        );
                    }
                    const backoffMs = Math.min(1000, 250 * (2 ** Math.min(2, endpointAttempts - 1)));
                    this.appendLog(
                        run,
                        endpointAttempts === 1 ? "warn" : "info",
                        `已走完战斗候选格但仍未进入攻击范围，改试下一邻格 ` +
                        `${endpointAttempts}/${COMBAT_PATH_RECOVERY_MAX_ATTEMPTS}，退避 ${backoffMs}ms`,
                        run.currentNodeId
                    );
                    await this.waitDelay(run, backoffMs, budget);
                } catch (e) {
                    if (!isRecoverableCombatPathError(e)) throw e;
                    candidateCursor += 1;
                    endpointAttempts += 1;
                    lastEndpointError = e instanceof Error ? e.message : String(e);
                    await validateState();
                    const retryEntities = await this.queryNearbyEntities(
                        run,
                        { allVisible: true, maxDistance: 0 },
                        budget
                    );
                    current = retryEntities.find((entry) => entry.id === target.id);
                    if (!current) return undefined;
                    if (current.hp !== undefined && current.hp <= 0) return current;
                    const retryDistance = await this.resolveCombatTargetDistance(
                        run,
                        current,
                        approachDistance,
                        budget,
                        validateState
                    );
                    if (retryDistance === undefined) {
                        throw new Error(`combat target ${target.id} is missing distance`);
                    }
                    if (retryDistance <= approachDistance) return current;
                    const elapsedMs = this.activeElapsedMs(run) - endpointRetryStartedActiveMs;
                    if (endpointAttempts >= COMBAT_PATH_RECOVERY_MAX_ATTEMPTS ||
                        elapsedMs >= COMBAT_PATH_RECOVERY_MS) {
                        throw new RecoverableCombatTargetError(
                            current,
                            `could not find a reachable adjacent tile after ` +
                            `${endpointAttempts} attempts/${Math.trunc(elapsedMs)}ms: ${lastEndpointError}`
                        );
                    }
                    const backoffMs = Math.min(1000, 250 * (2 ** Math.min(2, endpointAttempts - 1)));
                    this.appendLog(
                        run,
                        endpointAttempts === 1 ? "warn" : "info",
                        `战斗靠近候选格 (${roundNumber(destination.x)},${roundNumber(destination.y)}) 不可走，` +
                        `改试下一攻击范围邻格 ` +
                        `${endpointAttempts}/${COMBAT_PATH_RECOVERY_MAX_ATTEMPTS}，` +
                        `退避 ${backoffMs}ms`,
                        run.currentNodeId
                    );
                    await this.waitDelay(run, backoffMs, budget);
                }
            }
        }
    }

    private async selectMonsterForAttack(
        run: WorkflowRunInternal,
        target: NearbyEntity,
        pollMs: number,
        budget: NodeBudget,
        validateState: () => Promise<void>
    ): Promise<Record<string, unknown> | undefined> {
        const startedActiveMs = this.activeElapsedMs(run);
        let attempts = 0;
        let lastError = "";
        for (;;) {
            try {
                const reply = await this.send(
                    run,
                    "selectMonsterForAttack",
                    { creatureId: target.id },
                    budget,
                    5000
                );
                ensureCommandOk(reply, "selectMonsterForAttack");
                const selection = parseDetail(reply.detail);
                if (!isRecord(selection) || selection.targetSelected !== true ||
                    finiteNumber(selection.creatureId) !== target.id) {
                    throw new Error(`selectMonsterForAttack did not confirm creature ${target.id}`);
                }
                return selection;
            } catch (e) {
                if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
                this.ensureSameConnection(run);
                if (!isRecoverableCombatSelectionError(e)) throw e;
                lastError = e instanceof Error ? e.message : String(e);
                await validateState();
                const entities = await this.queryNearbyEntities(
                    run,
                    { allVisible: true, maxDistance: 0 },
                    budget
                );
                const current = entities.find((entry) => entry.id === target.id);
                if (!current || (current.hp !== undefined && current.hp <= 0)) return undefined;

                attempts += 1;
                const elapsedMs = this.activeElapsedMs(run) - startedActiveMs;
                if (elapsedMs >= COMBAT_SELECTION_RETRY_MS) {
                    throw new RecoverableCombatTargetError(
                        current,
                        `selectMonsterForAttack remained unavailable for ${Math.trunc(elapsedMs)}ms: ${lastError}`
                    );
                }
                if (attempts === 1) {
                    this.appendLog(
                        run,
                        "warn",
                        `原生选中/攻击暂未接受，目标仍有效，最多重试 ${COMBAT_SELECTION_RETRY_MS}ms：${lastError}`,
                        run.currentNodeId
                    );
                }
                await this.waitDelay(run, Math.min(500, Math.max(100, pollMs)), budget);
            }
        }
    }

    private async executeKillBoss(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const allVisible = params.allVisible !== false;
        const maxDistance = allVisible ? 0 : (finiteNumber(params.maxDistance) ?? 100);
        if (!allVisible && maxDistance <= 0) {
            throw new Error("killBoss.maxDistance must be > 0 unless allVisible is true");
        }
        const maxHpGt = nonNegativeNumber(params.maxHpGt, 800_000_000);
        const waitForAppearanceMs = clampInt(params.waitForAppearanceMs, 0, 10 * 60_000, 180_000);
        const skipIfAbsent = params.skipIfAbsent === true;
        const effectiveWaitForAppearanceMs = skipIfAbsent
            ? Math.max(waitForAppearanceMs, BOSS_ABSENCE_CONFIRM_MS)
            : waitForAppearanceMs;
        const approachDistance = positiveNumber(params.approachDistance, 3);
        const pollMs = clampInt(params.pollMs, 100, 10_000, 500);
        const reissueMs = clampInt(params.reissueMs, 200, 30_000, 2000);
        const missingNeeded = clampInt(params.missingConfirmations, 1, 20, 3);
        const missingDurationMs = clampInt(params.missingDurationMs, 0, 10 * 60_000, 3000);
        let initialStatus = await this.queryStatus(run, budget);
        initialStatus = await this.requireAliveStatus(run, initialStatus, budget, "starting killBoss");
        const stableMapId = finiteNumber(initialStatus?.mapId);
        if (stableMapId === undefined) throw new Error("killBoss cannot read current mapId");
        const schemaEntities = await this.queryNearbyEntities(run, { allVisible: true, maxDistance: 0 }, budget);
        if (schemaEntities.some((entry) => entry.maxHp === undefined)) {
            throw new Error("getNearbyNpcs missing maxHp; update GGThreadBlock.dll and restart the game client");
        }

        const ensureCombatState = async (): Promise<void> => {
            let status = await this.queryStatus(run, budget);
            status = await this.requireAliveStatus(run, status, budget, "fighting boss");
            await this.requireExpectedMapStatus(
                run,
                status,
                budget,
                stableMapId,
                "fighting boss"
            );
        };
        const waitStartedActiveMs = this.activeElapsedMs(run);
        let target: NearbyEntity | undefined;
        for (;;) {
            await this.checkPoint(run, budget);
            await ensureCombatState();
            const namedCandidates = await this.queryNearbyEntities(run, {
                ...params,
                allVisible,
                maxDistance,
                isNpc: false,
                attackable: true,
                hpGt: 0,
                maxHpGt: undefined,
            }, budget);
            const invalidMaxHp = namedCandidates.find((entry) =>
                entry.maxHp === undefined || !Number.isSafeInteger(entry.maxHp) || entry.maxHp <= 0);
            if (invalidMaxHp) {
                throw new Error(
                    `BOSS maxHp unavailable or invalid for creature ${invalidMaxHp.id}; update GGThreadBlock.dll and restart the game client`
                );
            }
            target = namedCandidates
                .filter((entry) => Number(entry.maxHp) > maxHpGt)
                .sort((a, b) => Number(b.maxHp) - Number(a.maxHp) || byDistance(a, b))[0];
            if (target) break;

            const waitedMs = this.activeElapsedMs(run) - waitStartedActiveMs;
            if (waitedMs >= effectiveWaitForAppearanceMs) {
                const result = {
                    skipped: skipIfAbsent,
                    killed: false,
                    maxHpGt,
                    waitedMs,
                    completedAt: Date.now(),
                };
                run.outputs.killBoss = result;
                run.outputs[`killBoss.${run.currentNodeId || "unknown"}`] = result;
                if (skipIfAbsent) {
                    this.appendLog(
                        run,
                        "info",
                        `未找到最大 HP > ${Math.trunc(maxHpGt)} 的 BOSS，按配置跳过`,
                        run.currentNodeId
                    );
                    return;
                }
                throw new Error(
                    `boss with max HP > ${Math.trunc(maxHpGt)} did not appear within ${effectiveWaitForAppearanceMs}ms`
                );
            }
            await this.waitDelay(run, pollMs, budget);
        }

        const lockedId = target.id;
        const result: Record<string, unknown> = {
            creatureId: lockedId,
            monsterTblId: target.monsterTblId,
            name: target.name,
            initialHp: target.hp,
            maxHp: target.maxHp,
            maxHpGt,
            skipped: false,
            killed: false,
            lockedAt: Date.now(),
        };
        run.outputs.lastBoss = target;
        run.outputs.killBoss = result;
        run.outputs[`killBoss.${run.currentNodeId || "unknown"}`] = result;
        this.appendLog(
            run,
            "info",
            `锁定 BOSS ${target.name || lockedId}，ID ${lockedId}，当前 HP ${Math.trunc(target.hp || 0)}，最大 HP ${Math.trunc(target.maxHp || 0)}，阈值 ${Math.trunc(maxHpGt)}`,
            run.currentNodeId
        );

        let lastIssuedAt = 0;
        let missingCount = 0;
        let missingSinceActiveMs: number | null = null;
        let recoveryCount = 0;
        let attackQueuedObserved = false;
        let lastReliableDistance: number | undefined;
        let unverifiedMissingLogged = false;
        let explicitHpDeathObserved = false;
        const recoverLockedBoss = async (error: RecoverableCombatTargetError): Promise<void> => {
            if (error.targetId !== lockedId) {
                throw new Error(
                    `combat recovery target mismatch: expected ${lockedId}, actual ${error.targetId}`
                );
            }
            if (!await this.stopPath(run, 3)) {
                throw new Error(`cannot recover locked BOSS ${lockedId}: stopPath was not confirmed`);
            }
            await ensureCombatState();
            recoveryCount += 1;
            missingCount = 0;
            missingSinceActiveMs = null;
            lastIssuedAt = 0;
            result.recoveryCount = recoveryCount;
            result.lastRecoveryError = error.reason;
            result.lastRecoveryAt = Date.now();
            this.appendLog(
                run,
                "warn",
                `BOSS ${target.name || lockedId} 战斗动作暂时不可用，保持原 ID 锁定，` +
                    `${BOSS_RECOVERY_DELAY_MS}ms 后重新观察：${error.reason}`,
                run.currentNodeId
            );
            await this.waitDelay(run, BOSS_RECOVERY_DELAY_MS, budget);
        };
        for (;;) {
            await this.checkPoint(run, budget);
            await ensureCombatState();
            const entities = await this.queryNearbyEntities(run, { allVisible: true, maxDistance: 0 }, budget);
            let current = entities.find((entry) => entry.id === lockedId);
            let currentDistance: number | undefined;
            try {
                currentDistance = current
                    ? await this.resolveCombatTargetDistance(
                        run,
                        current,
                        approachDistance,
                        budget,
                        ensureCombatState
                    )
                    : undefined;
                if (current && currentDistance !== undefined) {
                    lastReliableDistance = currentDistance;
                    result.lastDistance = roundNumber(currentDistance);
                }
                if (current && (current.hp === undefined || current.hp > 0) &&
                    currentDistance !== undefined && currentDistance > approachDistance) {
                    this.appendLog(
                        run,
                        "info",
                        `靠近锁定 BOSS ${current.name || lockedId}，当前${formatCombatDistance(current, currentDistance)}`,
                        run.currentNodeId
                    );
                    current = await this.approachCombatTarget(
                        run,
                        current,
                        approachDistance,
                        pollMs,
                        budget,
                        ensureCombatState
                    );
                    await ensureCombatState();
                    currentDistance = current && (current.hp === undefined || current.hp > 0)
                        ? await this.resolveCombatTargetDistance(
                            run,
                            current,
                            approachDistance,
                            budget,
                            ensureCombatState
                        )
                        : undefined;
                }
            } catch (e) {
                if (!(e instanceof RecoverableCombatTargetError)) throw e;
                await recoverLockedBoss(e);
                continue;
            }
            if (current && currentDistance !== undefined) {
                lastReliableDistance = currentDistance;
                result.lastDistance = roundNumber(currentDistance);
            }
            if (current?.hp !== undefined && current.hp <= 0) {
                explicitHpDeathObserved = true;
                result.explicitHpDeathObserved = true;
                result.lastHp = current.hp;
            }
            if (!current || (current.hp !== undefined && current.hp <= 0)) {
                if (missingSinceActiveMs === null) missingSinceActiveMs = this.activeElapsedMs(run);
                missingCount += 1;
                const missingForMs = this.activeElapsedMs(run) - missingSinceActiveMs;
                if (missingCount >= missingNeeded && missingForMs >= missingDurationMs) {
                    await ensureCombatState();
                    const explicitHpDeath = explicitHpDeathObserved ||
                        (current?.hp !== undefined && current.hp <= 0);
                    const missingAfterConfirmedEngagement = attackQueuedObserved &&
                        lastReliableDistance !== undefined &&
                        lastReliableDistance <= approachDistance;
                    if (!explicitHpDeath && !missingAfterConfirmedEngagement) {
                        result.unverifiedMissing = true;
                        result.unverifiedMissingForMs = missingForMs;
                        result.lastReliableDistance = lastReliableDistance;
                        if (!unverifiedMissingLogged) {
                            unverifiedMissingLogged = true;
                            this.appendLog(
                                run,
                                "warn",
                                `锁定 BOSS ${target.name || lockedId} 暂时离开视野，但缺少近身攻击后的死亡证据，` +
                                    "保持原 ID 等待其重新出现",
                                run.currentNodeId
                            );
                        }
                        await this.waitDelay(run, pollMs, budget);
                        continue;
                    }
                    result.killed = true;
                    if (current?.hp !== undefined) result.lastHp = current.hp;
                    result.missingConfirmations = missingCount;
                    result.missingForMs = missingForMs;
                    result.deathSignal = explicitHpDeath
                        ? "hp_non_positive"
                        : "missing_after_confirmed_engagement";
                    result.completedAt = Date.now();
                    this.appendLog(
                        run,
                        "info",
                        `锁定 BOSS ${target.name || lockedId} 当前 HP <= 0 或已稳定消失，确认死亡`,
                        run.currentNodeId
                    );
                    return;
                }
                await this.waitDelay(run, pollMs, budget);
                continue;
            }

            missingCount = 0;
            missingSinceActiveMs = null;
            unverifiedMissingLogged = false;
            delete result.unverifiedMissing;
            delete result.unverifiedMissingForMs;
            run.outputs.lastBoss = current;
            result.lastHp = current.hp;
            const now = Date.now();
            if (lastIssuedAt === 0 || now - lastIssuedAt >= reissueMs) {
                let selection: Record<string, unknown> | undefined;
                try {
                    selection = await this.selectMonsterForAttack(
                        run,
                        current,
                        pollMs,
                        budget,
                        ensureCombatState
                    );
                } catch (e) {
                    if (!(e instanceof RecoverableCombatTargetError)) throw e;
                    await recoverLockedBoss(e);
                    continue;
                }
                if (!selection) {
                    await this.waitDelay(run, pollMs, budget);
                    continue;
                }
                lastIssuedAt = Date.now();
                result.lastSelection = selection;
                if (selection.attackQueued === true) {
                    attackQueuedObserved = true;
                    result.attackQueuedObserved = true;
                }
                const actionState = selection.attackQueued === true
                    ? "已排入"
                    : "等待引擎空闲重试";
                this.appendLog(
                    run,
                    "info",
                    `已靠近并选中 BOSS ${current.name || lockedId}，` +
                        `当前 HP ${Math.trunc(current.hp || 0)}，` +
                        `${formatCombatDistance(current, currentDistance)}，攻击动作=${actionState}`,
                    run.currentNodeId
                );
            }
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async executeClearMonsters(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const allVisible = params.allVisible === true;
        const maxDistance = allVisible ? 0 : (finiteNumber(params.maxDistance) ?? 100);
        if (!allVisible && maxDistance <= 0) {
            throw new Error("clearMonsters.maxDistance must be > 0 unless allVisible is true");
        }
        const pollMs = clampInt(params.pollMs, 100, 10_000, 500);
        const reissueMs = clampInt(params.reissueMs, 200, 30_000, 3000);
        const approachDistance = positiveNumber(params.approachDistance, 3);
        const emptyNeeded = clampInt(params.emptyConfirmations, 1, 20, 2);
        const emptyDurationMs = clampInt(
            params.emptyDurationMs,
            0,
            10 * 60_000,
            DEFAULT_EMPTY_DURATION_MS
        );
        let initialStatus = await this.queryStatus(run, budget);
        initialStatus = await this.requireAliveStatus(run, initialStatus, budget, "starting clearMonsters");
        const stableMapId = finiteNumber(initialStatus?.mapId);
        if (stableMapId === undefined) throw new Error("clearMonsters cannot read current mapId");

        const ensureCombatState = async (): Promise<void> => {
            let status = await this.queryStatus(run, budget);
            status = await this.requireAliveStatus(run, status, budget, "clearing monsters");
            await this.requireExpectedMapStatus(
                run,
                status,
                budget,
                stableMapId,
                "clearing monsters"
            );
        };

        let emptyCount = 0;
        let emptySinceActiveMs: number | null = null;
        let lastTargetId = 0;
        let lastAttackAt = 0;
        let quarantineOnlyLogged = false;
        let reactivatedCount = 0;
        const quarantineFailures = new Map<number, number>();
        const quarantinedTargets = new Map<number, {
            creatureId: number;
            name: string;
            reason: string;
            failures: number;
            quarantinedAt: number;
            retryAfterActiveMs: number;
            reactivateWhenNear: boolean;
        }>();
        const isolatedTargetOutput = (): Array<Record<string, unknown>> =>
            Array.from(quarantinedTargets.values()).map((entry) => ({
                creatureId: entry.creatureId,
                name: entry.name,
                reason: entry.reason,
                failures: entry.failures,
                quarantinedAt: entry.quarantinedAt,
            }));
        const refreshQuarantine = (visible: NearbyEntity[]): NearbyEntity[] => {
            const visibleById = new Map<number, NearbyEntity>(
                visible.map((entry) => [entry.id, entry] as const)
            );
            const activeNow = this.activeElapsedMs(run);
            for (const [creatureId, quarantine] of quarantinedTargets) {
                const current = visibleById.get(creatureId);
                if (!current) {
                    quarantinedTargets.delete(creatureId);
                    quarantineFailures.delete(creatureId);
                    continue;
                }
                const reportedDistance = finiteNumber(current.distance);
                const reason = quarantine.reactivateWhenNear &&
                    reportedDistance !== undefined && reportedDistance <= approachDistance
                    ? "目标已进入攻击距离"
                    : activeNow >= quarantine.retryAfterActiveMs
                        ? "隔离冷却已结束"
                        : "";
                if (!reason) continue;
                quarantinedTargets.delete(creatureId);
                reactivatedCount += 1;
                this.appendLog(
                    run,
                    "info",
                    `重新尝试先前不可达目标 ${current.name || creatureId}：${reason}`,
                    run.currentNodeId
                );
            }
            return visible.filter((entry) => !quarantinedTargets.has(entry.id));
        };
        const quarantineTarget = async (
            target: NearbyEntity,
            error: RecoverableCombatTargetError,
            reactivateWhenNear: boolean
        ): Promise<void> => {
            if (error.targetId !== target.id) {
                throw new Error(
                    `combat recovery target mismatch: expected ${target.id}, actual ${error.targetId}`
                );
            }
            if (!await this.stopPath(run, 3)) {
                throw new Error(`cannot isolate combat target ${target.id}: stopPath was not confirmed`);
            }
            await ensureCombatState();
            const failures = (quarantineFailures.get(target.id) || 0) + 1;
            quarantineFailures.set(target.id, failures);
            quarantinedTargets.set(target.id, {
                creatureId: target.id,
                name: target.name || String(target.monsterTblId || target.id),
                reason: error.reason,
                failures,
                quarantinedAt: Date.now(),
                retryAfterActiveMs: this.activeElapsedMs(run) +
                    Math.max(
                        COMBAT_TARGET_QUARANTINE_MS,
                        emptyDurationMs,
                        (emptyNeeded - 1) * pollMs
                    ) + pollMs,
                reactivateWhenNear,
            });
            lastTargetId = 0;
            lastAttackAt = 0;
            run.outputs.clearMonstersRecovery = {
                isolatedTargets: isolatedTargetOutput(),
                reactivatedCount,
                updatedAt: Date.now(),
            };
            this.appendLog(
                run,
                "warn",
                `目标 ${target.name || target.monsterTblId || target.id} 持续不可处理，` +
                    `已临时隔离并继续其他怪物：${error.reason}`,
                run.currentNodeId
            );
        };
        for (;;) {
            await this.checkPoint(run, budget);
            await ensureCombatState();
            const visibleEntities = (await this.queryNearbyEntities(run, {
                ...params,
                maxDistance,
                allVisible,
                attackable: true,
                hpGt: 0,
            }, budget)).sort(byDistance);
            const entities = refreshQuarantine(visibleEntities);
            if (entities.length === 0) {
                if (visibleEntities.length > 0 && quarantinedTargets.size > 0 && !quarantineOnlyLogged) {
                    quarantineOnlyLogged = true;
                    this.appendLog(
                        run,
                        "warn",
                        `当前仅剩 ${quarantinedTargets.size} 个持续不可达目标，` +
                            "按空场稳定窗口观察后跳过，不中断后续流程",
                        run.currentNodeId
                    );
                }
                if (emptySinceActiveMs === null) emptySinceActiveMs = this.activeElapsedMs(run);
                emptyCount += 1;
                const emptyForMs = this.activeElapsedMs(run) - emptySinceActiveMs;
                if (emptyCount >= emptyNeeded && emptyForMs >= emptyDurationMs) {
                    const finalVisible = (await this.queryNearbyEntities(run, {
                        ...params,
                        maxDistance,
                        allVisible,
                        attackable: true,
                        hpGt: 0,
                    }, budget)).sort(byDistance);
                    const finalActionable = refreshQuarantine(finalVisible);
                    if (finalActionable.length > 0) {
                        emptyCount = 0;
                        emptySinceActiveMs = null;
                        quarantineOnlyLogged = false;
                        continue;
                    }
                    let finalStatus = await this.queryStatus(run, budget);
                    finalStatus = await this.requireAliveStatus(
                        run,
                        finalStatus,
                        budget,
                        "confirming an empty monster field"
                    );
                    await this.requireExpectedMapStatus(
                        run,
                        finalStatus,
                        budget,
                        stableMapId,
                        "confirming an empty monster field"
                    );
                    run.outputs.clearMonsters = {
                        mapId: stableMapId,
                        emptyConfirmations: emptyCount,
                        emptyForMs,
                        rawVisibleCount: finalVisible.length,
                        completionReason: finalVisible.length === 0
                            ? "empty"
                            : "no_actionable_targets",
                        isolatedTargets: isolatedTargetOutput(),
                        reactivatedCount,
                        completedAt: Date.now(),
                    };
                    if (quarantinedTargets.size > 0) {
                        this.appendLog(
                            run,
                            "warn",
                            `已跳过 ${quarantinedTargets.size} 个持续不可达目标，战斗节点继续后续流程`,
                            run.currentNodeId
                        );
                    }
                    return;
                }
                await this.waitDelay(run, pollMs, budget);
                continue;
            }

            emptyCount = 0;
            emptySinceActiveMs = null;
            quarantineOnlyLogged = false;
            let target = entities[0];
            let targetDistance: number | undefined;
            try {
                targetDistance = await this.resolveCombatTargetDistance(
                    run,
                    target,
                    approachDistance,
                    budget,
                    ensureCombatState
                );
                if (targetDistance !== undefined && targetDistance > approachDistance) {
                    this.appendLog(
                        run,
                        "info",
                        `靠近 ${target.name || target.monsterTblId || target.id}，当前${formatCombatDistance(target, targetDistance)}`,
                        run.currentNodeId
                    );
                    const approached = await this.approachCombatTarget(
                        run,
                        target,
                        approachDistance,
                        pollMs,
                        budget,
                        ensureCombatState
                    );
                    if (!approached) {
                        await this.waitDelay(run, pollMs, budget);
                        continue;
                    }
                    if (approached.hp !== undefined && approached.hp <= 0) {
                        await this.waitDelay(run, pollMs, budget);
                        continue;
                    }
                    target = approached;
                    targetDistance = await this.resolveCombatTargetDistance(
                        run,
                        target,
                        approachDistance,
                        budget,
                        ensureCombatState
                    );
                }
            } catch (e) {
                if (!(e instanceof RecoverableCombatTargetError)) throw e;
                await quarantineTarget(target, e, true);
                await this.waitDelay(run, pollMs, budget);
                continue;
            }
            const now = Date.now();
            if (target.id !== lastTargetId || now - lastAttackAt >= reissueMs) {
                let selection: Record<string, unknown> | undefined;
                try {
                    selection = await this.selectMonsterForAttack(
                        run,
                        target,
                        pollMs,
                        budget,
                        ensureCombatState
                    );
                } catch (e) {
                    if (!(e instanceof RecoverableCombatTargetError)) throw e;
                    await quarantineTarget(target, e, false);
                    await this.waitDelay(run, pollMs, budget);
                    continue;
                }
                if (!selection) {
                    await this.waitDelay(run, pollMs, budget);
                    continue;
                }
                lastTargetId = target.id;
                lastAttackAt = Date.now();
                run.outputs.lastMonster = target;
                const actionState = selection.attackQueued === true
                    ? "已排入"
                    : "等待引擎空闲重试";
                this.appendLog(
                    run,
                    "info",
                    `已靠近并选中 ${target.name || target.monsterTblId || target.id}，` +
                        `${formatCombatDistance(target, targetDistance)}，攻击动作=${actionState}`,
                    run.currentNodeId
                );
            }
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async executeCollectFilteredDrops(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const maxDistance = nonNegativeNumber(params.maxDistance, 0);
        const includeUnpickable = params.includeUnpickable !== false;
        const approachDistance = positiveNumber(params.approachDistance, 2.5);
        const pollMs = clampInt(params.pollMs, 100, 10_000, 500);
        const emptyDurationMs = clampInt(params.emptyDurationMs, 0, 60_000, 3000);
        const nativePickupTimeoutMs = clampInt(
            params.nativePickupTimeoutMs,
            1000,
            30_000,
            5000
        );
        const maxDurationMs = clampInt(params.maxDurationMs, 1000, 10 * 60_000, 100_000);
        const pickupExecutionMode = this.pickupFilterManager.getConfig().pickupExecutionMode;
        const pickupLimitIsNodeLimit = maxDurationMs <= budget.timeoutMs;
        const pickupBudget: NodeBudget = {
            startedActiveMs: budget.startedActiveMs,
            timeoutMs: Math.min(budget.timeoutMs, maxDurationMs),
        };

        let initialStatus = await this.queryStatus(run, pickupBudget);
        initialStatus = await this.requireAliveStatus(
            run,
            initialStatus,
            pickupBudget,
            "starting drop collection"
        );
        const stableMapId = finiteNumber(initialStatus?.mapId);
        if (stableMapId === undefined) throw new Error("collectFilteredDrops cannot read current mapId");

        const seenDropIds = new Set<number>();
        const resolvedDropIds = new Set<number>();
        const skippedDrops = new Map<number, string>();
        let nativeDispatchCount = 0;
        let emptySinceActiveMs: number | null = null;
        let lastLoggedDropId = 0;
        let skippedCollectionReason: string | undefined;
        const writeResult = (timedOut: boolean): void => {
            run.outputs.collectFilteredDrops = {
                mapId: stableMapId,
                pickupExecutionMode,
                timedOut,
                approachedDropCount: seenDropIds.size,
                resolvedDropCount: resolvedDropIds.size,
                nativeDispatchCount,
                skippedDropCount: skippedDrops.size,
                skippedDrops: Array.from(skippedDrops, ([dropId, reason]) => ({ dropId, reason })),
                ...(skippedCollectionReason ? { skippedCollectionReason } : {}),
                elapsedMs: this.activeElapsedMs(run) - pickupBudget.startedActiveMs,
                completedAt: Date.now(),
            };
        };

        try {
            const applyReply = await this.send(
                run,
                "setPickupFilter",
                this.pickupFilterManager.getWirePayload(run.pid),
                pickupBudget,
                5000
            );
            ensureCommandOk(applyReply, "setPickupFilter");
            const applied = parseDetail(applyReply.detail);
            if (pickupExecutionMode === "nativeGame") {
                const instance = this.registry.list().find((entry) => entry.pid === run.pid);
                const supported = Boolean(
                    instance &&
                    versionAtLeast(instance.dllVersion, NATIVE_DROP_PICKUP_MIN_DLL_VERSION) &&
                    instance.nativeDropPickupReady === true &&
                    isRecord(applied) &&
                    applied.pickupExecutionMode === "nativeGame"
                );
                if (!supported) {
                    skippedCollectionReason =
                        `游戏原生拾取需要 GGThreadBlock.dll >= ${NATIVE_DROP_PICKUP_MIN_DLL_VERSION} ` +
                        "且 nativeDropPickupReady=true";
                    writeResult(false);
                    this.appendLog(
                        run,
                        "warn",
                        `${skippedCollectionReason}；本实例已禁用旧式直接拾取，本段跳过`,
                        run.currentNodeId
                    );
                    return;
                }
            }
        } catch (e) {
            if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
            const message = e instanceof Error ? e.message : String(e);
            await this.confirmCollectionCanContinue(run, stableMapId, message);
            skippedCollectionReason = `同步全局拾取方式失败: ${message}`;
            writeResult(false);
            this.appendLog(
                run,
                "warn",
                `${skippedCollectionReason}；跳过本段拾取并继续`,
                run.currentNodeId
            );
            return;
        }

        this.appendLog(
            run,
            "info",
            pickupExecutionMode === "nativeGame"
                ? "拾取执行方式：游戏原生拾取（寻路靠近后触发 action=4）"
                : "拾取执行方式：DLL 自动拾取",
            run.currentNodeId
        );

        try {
            for (;;) {
                await this.checkPoint(run, pickupBudget);
                let status = await this.queryStatus(run, pickupBudget);
                status = await this.requireAliveStatus(run, status, pickupBudget, "collecting drops");
                if (finiteNumber(status?.mapId) !== stableMapId) {
                    throw new Error(
                        `map changed while collecting drops: expected ${stableMapId}, actual ${status?.mapId}`
                    );
                }

                let drops: NearbyDropItem[];
                try {
                    drops = await this.queryNearbyDropItems(run, {
                        maxDistance,
                        includeUnpickable,
                        onlyFilterMatches: true,
                        limit: 512,
                    }, pickupBudget);
                } catch (e) {
                    if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
                    const message = e instanceof Error ? e.message : String(e);
                    await this.confirmCollectionCanContinue(run, stableMapId, message);
                    skippedCollectionReason = message;
                    writeResult(false);
                    this.appendLog(
                        run,
                        "warn",
                        `读取掉落或拾取过滤失败，角色仍在当前地图，跳过本段拾取并继续：${message}`,
                        run.currentNodeId
                    );
                    return;
                }

                const candidates = drops.filter((drop) => !skippedDrops.has(drop.dropId));
                if (candidates.length === 0) {
                    if (emptySinceActiveMs === null) emptySinceActiveMs = this.activeElapsedMs(run);
                    if (this.activeElapsedMs(run) - emptySinceActiveMs >= emptyDurationMs) {
                        writeResult(false);
                        if (skippedDrops.size > 0) {
                            this.appendLog(
                                run,
                                "warn",
                                `已跳过 ${skippedDrops.size} 个无法处理的掉落，继续后续流程`,
                                run.currentNodeId
                            );
                        }
                        return;
                    }
                    await this.waitDelay(run, pollMs, pickupBudget);
                    continue;
                }

                emptySinceActiveMs = null;
                const target = candidates.sort((a, b) => {
                    if (a.canPick !== b.canPick) return a.canPick ? -1 : 1;
                    return a.distance - b.distance;
                })[0];
                seenDropIds.add(target.dropId);
                run.outputs.lastDrop = target;
                if (target.dropId !== lastLoggedDropId) {
                    this.appendLog(
                        run,
                        "info",
                        `靠近掉落 ${target.name || target.itemId}，距离 ${roundNumber(target.distance)}`,
                        run.currentNodeId
                    );
                    lastLoggedDropId = target.dropId;
                }

                if (target.distance > approachDistance) {
                    try {
                        await this.executePathTo(run, {
                            x: target.x,
                            y: target.y,
                            action: 1,
                            targetId: 0,
                            pollMs: 300,
                        }, pickupBudget);
                    } catch (e) {
                        if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
                        const message = e instanceof Error ? e.message : String(e);
                        await this.confirmCollectionCanContinue(run, stableMapId, message);
                        skippedDrops.set(target.dropId, message);
                        this.appendLog(
                            run,
                            "warn",
                            `无法靠近掉落 ${target.name || target.itemId}，跳过该掉落并继续：${message}`,
                            run.currentNodeId
                        );
                    }
                }

                if (pickupExecutionMode === "nativeGame" && !skippedDrops.has(target.dropId)) {
                    const result = await this.collectDropWithNativeGame(
                        run,
                        target,
                        stableMapId,
                        approachDistance,
                        nativePickupTimeoutMs,
                        pollMs,
                        pickupBudget
                    );
                    if (result.attempted) nativeDispatchCount += 1;
                    if (result.resolved) {
                        resolvedDropIds.add(target.dropId);
                        this.appendLog(
                            run,
                            "info",
                            `游戏原生拾取已处理 ${target.name || target.itemId}，dropId=${target.dropId} 已消失`,
                            run.currentNodeId
                        );
                    } else {
                        const reason = result.reason || "原生拾取未确认完成";
                        skippedDrops.set(target.dropId, reason);
                        this.appendLog(
                            run,
                            "warn",
                            `游戏原生拾取跳过 ${target.name || target.itemId}：${reason}`,
                            run.currentNodeId
                        );
                    }
                }
                await this.waitDelay(run, pollMs, pickupBudget);
            }
        } catch (e) {
            if (e instanceof NodeTimeoutError && pickupLimitIsNodeLimit &&
                (this.activeElapsedMs(run) - pickupBudget.startedActiveMs >= maxDurationMs ||
                    this.remainingBudgetMs(run, pickupBudget) < 5000) &&
                this.activeElapsedMs(run) < run.workflow.maxRuntimeMs) {
                writeResult(true);
                this.appendLog(run, "warn", `拾取达到 ${maxDurationMs}ms 上限，按配置跳过`, run.currentNodeId);
                return;
            }
            throw e;
        }
    }

    private async collectDropWithNativeGame(
        run: WorkflowRunInternal,
        target: NearbyDropItem,
        stableMapId: number,
        approachDistance: number,
        nativePickupTimeoutMs: number,
        pollMs: number,
        budget: NodeBudget
    ): Promise<{ resolved: boolean; attempted: boolean; reason?: string }> {
        const readExact = async (): Promise<{
            drop?: NearbyDropItem;
            error?: string;
        }> => {
            await this.assertCollectionContext(run, stableMapId, budget);
            try {
                return { drop: await this.queryDropItemState(run, target.dropId, budget) };
            } catch (e) {
                if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
                const message = e instanceof Error ? e.message : String(e);
                await this.confirmCollectionCanContinue(run, stableMapId, message);
                return { error: `精确掉落状态不可用: ${message}` };
            }
        };
        const validateTarget = (
            current: NearbyDropItem
        ): string | undefined => {
            if (current.itemId !== target.itemId) {
                return `dropId=${target.dropId} 的 itemId 已从 ${target.itemId} 变为 ${current.itemId}`;
            }
            if (!current.matchesPickupFilter) return "物品已不再匹配当前拾取过滤";
            return undefined;
        };

        const pickableWaitStarted = this.activeElapsedMs(run);
        let current: NearbyDropItem | undefined;
        for (;;) {
            const exact = await readExact();
            if (exact.error) {
                return {
                    resolved: false,
                    attempted: false,
                    reason: exact.error,
                };
            }
            current = exact.drop;
            if (!current) return { resolved: true, attempted: false };
            const invalid = validateTarget(current);
            if (invalid) return { resolved: false, attempted: false, reason: invalid };
            if (current.canPick) break;
            if (this.activeElapsedMs(run) - pickableWaitStarted >= nativePickupTimeoutMs) {
                return {
                    resolved: false,
                    attempted: false,
                    reason: `等待 ${nativePickupTimeoutMs}ms 后仍不可拾取`,
                };
            }
            await this.waitDelay(run, pollMs, budget);
        }

        let dispatchError = "";
        try {
            const reply = await this.sendNativeUi(
                run,
                "selectDropForPickup",
                {
                    dropId: target.dropId,
                    expectedItemId: target.itemId,
                    expectedMapId: stableMapId,
                    maxDistance: Math.max(approachDistance + 2, 5),
                },
                budget
            );
            ensureCommandOk(reply, "selectDropForPickup");
            const detail = parseDetail(reply.detail);
            if (!isRecord(detail)) {
                dispatchError = "selectDropForPickup 返回了无效结果";
            } else if (detail.alreadyGone === true) {
                return { resolved: true, attempted: true };
            } else if (detail.ok !== true) {
                dispatchError = String(detail.error || "游戏拒绝原生拾取动作");
            } else {
                this.appendLog(
                    run,
                    "info",
                    `已在 GameLoop 触发原生掉落点击，dropId=${target.dropId}，` +
                        `action=4，动作排入=${detail.actionQueued === true ? "是" : "否"}；等待精确消失`,
                    run.currentNodeId
                );
            }
        } catch (e) {
            if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
            dispatchError = e instanceof Error ? e.message : String(e);
            await this.confirmCollectionCanContinue(run, stableMapId, dispatchError);
        }

        const confirmStarted = this.activeElapsedMs(run);
        for (;;) {
            const exact = await readExact();
            if (exact.error) {
                return {
                    resolved: false,
                    attempted: true,
                    reason: exact.error,
                };
            }
            current = exact.drop;
            if (!current) return { resolved: true, attempted: true };
            const invalid = validateTarget(current);
            if (invalid) return { resolved: false, attempted: true, reason: invalid };
            if (this.activeElapsedMs(run) - confirmStarted >= nativePickupTimeoutMs) {
                const prefix = dispatchError ? `原生动作未确认（${dispatchError}）；` : "";
                return {
                    resolved: false,
                    attempted: true,
                    reason: `${prefix}等待 ${nativePickupTimeoutMs}ms 后 dropId=${target.dropId} 仍存在`,
                };
            }
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async assertCollectionContext(
        run: WorkflowRunInternal,
        stableMapId: number,
        budget: NodeBudget
    ): Promise<void> {
        let status = await this.queryStatus(run, budget);
        status = await this.requireAliveStatus(run, status, budget, "collecting drops");
        if (finiteNumber(status?.mapId) !== stableMapId) {
            throw new Error(
                `map changed while collecting drops: expected ${stableMapId}, actual ${status?.mapId}`
            );
        }
    }

    private async confirmCollectionCanContinue(
        run: WorkflowRunInternal,
        stableMapId: number,
        pickupError: string
    ): Promise<void> {
        if (run.pathActive && !await this.stopPath(run, 3)) {
            throw new Error(`${pickupError}; cannot safely skip because stopPath was not confirmed`);
        }
        await this.waitUntilRunnable(run);
        this.ensureRuntimeBudget(run);
        this.ensureSameConnection(run);
        const confirmBudget = {
            startedActiveMs: this.activeElapsedMs(run),
            timeoutMs: 5000,
        };
        let status = await this.queryStatus(run, confirmBudget);
        status = await this.requireAliveStatus(
            run,
            status,
            confirmBudget,
            "checking whether collection can continue"
        );
        const actualMapId = finiteNumber(status?.mapId);
        if (!isCollectionContextStable(stableMapId, actualMapId, false)) {
            throw new Error(
                `${pickupError}; map changed while collecting drops: expected ${stableMapId}, actual ${status?.mapId}`
            );
        }
    }

    private async executeGenericCommand(
        run: WorkflowRunInternal,
        params: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<void> {
        const action = String(params.action || "").trim();
        if (!action) throw new Error("command.action is required");
        if (BLOCKED_GENERIC_COMMANDS.has(action)) {
            throw new Error(`${action} must use its dedicated workflow node`);
        }
        // Generic command may be non-idempotent. The catalog warns users not to combine
        // such commands with node retries unless repeating the side effect is known-safe.
        const args = isRecord(params.args) ? params.args : {};
        const reply = await this.send(run, action, args, budget, 5000);
        ensureCommandOk(reply, action);
        const saveAs = String(params.saveAs || "").trim();
        if (saveAs) {
            run.outputs[saveAs] = params.parseJson === false ? reply.detail : parseDetail(reply.detail);
        }
    }

    private async evaluateCondition(
        run: WorkflowRunInternal,
        condition: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<boolean> {
        const kind = String(condition.kind || condition.type || "").trim();
        switch (kind) {
            case "map": {
                const status = await this.queryStatus(run, budget);
                const expected = condition.mapId ?? condition.value;
                return compareValue(status?.mapId, condition.operator || "eq", expected);
            }
            case "mapName": {
                const status = await this.queryStatus(run, budget);
                return compareValue(
                    status?.mapName,
                    condition.operator || "contains",
                    condition.value
                );
            }
            case "position": {
                const position = await this.queryPosition(run, budget);
                const x = requiredNumber(condition.x, "condition.position.x");
                const y = requiredNumber(condition.y, "condition.position.y");
                const distance = Math.hypot(Number(position.x) - x, Number(position.y) - y);
                const threshold = positiveNumber(condition.distance ?? condition.value, 1);
                return compareValue(distance, condition.operator || "lte", threshold);
            }
            case "dead": {
                const status = await this.queryStatus(run, budget);
                const expected = typeof condition.value === "boolean" ? condition.value : true;
                const statusKind = classifyLocalPlayerStatus(status);
                if (statusKind !== "alive" && statusKind !== "dead") {
                    throw new Error(`cannot evaluate dead condition from ${statusKind} player status`);
                }
                return compareValue(statusKind === "dead", condition.operator || "eq", expected);
            }
            case "entity": {
                const entities = await this.queryNearbyEntities(run, condition, budget);
                return compareCollectionCount(entities.length, condition);
            }
            case "player": {
                const players = await this.queryNearbyPlayers(run, condition, budget);
                return compareCollectionCount(players.length, condition);
            }
            case "command": {
                const action = String(condition.action || "").trim();
                if (!action) throw new Error("command condition requires action");
                if (BLOCKED_GENERIC_COMMANDS.has(action)) {
                    throw new Error(`${action} must use its dedicated workflow node`);
                }
                const args = isRecord(condition.args) ? condition.args : {};
                const reply = await this.send(run, action, args, budget, 5000);
                ensureCommandOk(reply, action);
                const parsed = parseDetail(reply.detail);
                const value = stringOrUndefined(condition.path)
                    ? getJsonPath(parsed, String(condition.path))
                    : parsed;
                const saveAs = String(condition.saveAs || "").trim();
                if (saveAs) run.outputs[saveAs] = parsed;
                return compareValue(value, condition.operator || "truthy", condition.value);
            }
            default:
                throw new Error(`unsupported condition kind: ${kind || "(empty)"}`);
        }
    }

    private async queryNearbyEntities(
        run: WorkflowRunInternal,
        filter: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<NearbyEntity[]> {
        const maxDistance = filter.allVisible === true ? 0 : nonNegativeNumber(filter.maxDistance, 100);
        const reply = await this.sendReadOnly(run, "getNearbyNpcs", { maxDistance }, budget, 4000);
        ensureCommandOk(reply, "getNearbyNpcs");
        const parsed = parseDetail(reply.detail);
        if (!Array.isArray(parsed)) throw new Error("getNearbyNpcs returned non-array detail");
        const monsterTblIds = normalizeNumberSet(filter.monsterTblId ?? filter.monsterTblIds);
        const exactName = String(filter.name || "").trim();
        const containsName = String(filter.nameContains || "").trim();
        const notContainsName = String(filter.nameNotContains || "").trim();
        const hasIsNpc = typeof filter.isNpc === "boolean";
        const hasAttackable = typeof filter.attackable === "boolean";
        const hpGt = finiteNumber(filter.hpGt);
        const maxHpGt = finiteNumber(filter.maxHpGt);

        return parsed
            .filter(isRecord)
            .map((entry): NearbyEntity => ({
                id: Number(entry.id),
                kind: finiteNumber(entry.kind),
                monsterTblId: finiteNumber(entry.monsterTblId),
                isNpc: typeof entry.isNpc === "boolean" ? entry.isNpc : undefined,
                attackable: typeof entry.attackable === "boolean" ? entry.attackable : undefined,
                hp: finiteNumber(entry.hp),
                maxHp: finiteNumber(entry.maxHp),
                distance: finiteNumber(entry.distance),
                name: typeof entry.name === "string" ? entry.name : undefined,
                x: finiteNumber(entry.x),
                y: finiteNumber(entry.y),
                z: finiteNumber(entry.z),
            }))
            .filter((entry) => Number.isFinite(entry.id) && entry.id > 0)
            .filter((entry) => monsterTblIds.size === 0 ||
                (entry.monsterTblId !== undefined && monsterTblIds.has(entry.monsterTblId)))
            .filter((entry) => !exactName || entry.name === exactName)
            .filter((entry) => !containsName || String(entry.name || "").includes(containsName))
            .filter((entry) => !notContainsName || !String(entry.name || "").includes(notContainsName))
            .filter((entry) => !hasIsNpc || entry.isNpc === filter.isNpc)
            .filter((entry) => !hasAttackable || entry.attackable === filter.attackable)
            .filter((entry) => hpGt === undefined || (entry.hp !== undefined && entry.hp > hpGt))
            .filter((entry) => maxHpGt === undefined || (entry.maxHp !== undefined && entry.maxHp > maxHpGt));
    }

    private async queryNearbyDropItems(
        run: WorkflowRunInternal,
        filter: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<NearbyDropItem[]> {
        const args = {
            maxDistance: nonNegativeNumber(filter.maxDistance, 0),
            includeUnpickable: filter.includeUnpickable === true,
            onlyFilterMatches: filter.onlyFilterMatches !== false,
            limit: clampInt(filter.limit, 1, 2048, 512),
        };
        const reply = await this.sendReadOnly(run, "getNearbyDropItems", args, budget, 4000);
        ensureCommandOk(reply, "getNearbyDropItems");
        const parsed = parseDetail(reply.detail);
        if (!Array.isArray(parsed)) throw new Error("getNearbyDropItems returned non-array detail");
        return parsed
            .filter(isRecord)
            .map((entry): NearbyDropItem => {
                const itemId = Number(entry.itemId);
                return {
                    dropId: Number(entry.dropId),
                    itemId,
                    distance: Number(entry.distance),
                    x: Number(entry.x),
                    y: Number(entry.y),
                    z: finiteNumber(entry.z),
                    canPick: entry.canPick === true,
                    matchesPickupFilter: entry.matchesPickupFilter === true,
                    name: itemName(itemId) || undefined,
                };
            })
            .filter((entry) => Number.isFinite(entry.dropId) && entry.dropId > 0)
            .filter((entry) => Number.isFinite(entry.itemId) && entry.itemId > 0)
            .filter((entry) => Number.isFinite(entry.distance) && entry.distance >= 0)
            .filter((entry) => Number.isFinite(entry.x) && Number.isFinite(entry.y))
            .filter((entry) => !args.onlyFilterMatches || entry.matchesPickupFilter);
    }

    private async queryDropItemState(
        run: WorkflowRunInternal,
        dropId: number,
        budget: NodeBudget
    ): Promise<NearbyDropItem | undefined> {
        const reply = await this.sendReadOnly(run, "getDropItemState", { dropId }, budget, 4000);
        ensureCommandOk(reply, "getDropItemState");
        const parsed = parseDetail(reply.detail);
        if (!isRecord(parsed) || Number(parsed.dropId) !== dropId ||
            typeof parsed.present !== "boolean") {
            throw new Error("getDropItemState returned invalid detail");
        }
        if (!parsed.present) return undefined;

        const itemId = Number(parsed.itemId);
        const state: NearbyDropItem = {
            dropId,
            itemId,
            distance: Number(parsed.distance),
            x: Number(parsed.x),
            y: Number(parsed.y),
            z: finiteNumber(parsed.z),
            canPick: parsed.canPick === true,
            matchesPickupFilter: parsed.matchesPickupFilter === true,
            name: itemName(itemId) || undefined,
        };
        if (!Number.isFinite(state.itemId) || state.itemId <= 0 ||
            !Number.isFinite(state.distance) || state.distance < 0 ||
            !Number.isFinite(state.x) || !Number.isFinite(state.y)) {
            throw new Error("getDropItemState returned invalid present-drop fields");
        }
        return state;
    }

    private async queryNearbyPlayers(
        run: WorkflowRunInternal,
        filter: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<Record<string, unknown>[]> {
        const maxDistance = nonNegativeNumber(filter.maxDistance, 600);
        const reply = await this.sendReadOnly(run, "getNearbyPlayers", { maxDistance }, budget, 4000);
        ensureCommandOk(reply, "getNearbyPlayers");
        const parsed = parseDetail(reply.detail);
        if (!Array.isArray(parsed)) throw new Error("getNearbyPlayers returned non-array detail");
        const exactName = String(filter.name || "").trim();
        const containsName = String(filter.nameContains || "").trim();
        return parsed
            .filter(isRecord)
            .filter((entry) => !exactName || entry.name === exactName)
            .filter((entry) => !containsName || String(entry.name || "").includes(containsName));
    }

    private async queryStatus(run: WorkflowRunInternal, budget: NodeBudget): Promise<any> {
        const reply = await this.sendReadOnly(run, "getStatus", {}, budget, 4000);
        ensureCommandOk(reply, "getStatus");
        const status = parseDetail(reply.detail);
        if (!isRecord(status)) throw new Error("getStatus returned invalid detail");
        status.mapName = getMapName(status.mapId);
        run.outputs.status = status;
        return status;
    }

    private async requireAliveStatus(
        run: WorkflowRunInternal,
        firstStatus: any,
        budget: NodeBudget,
        context: string
    ): Promise<any> {
        const firstKind = classifyLocalPlayerStatus(firstStatus);
        if (firstKind === "alive") return firstStatus;
        if (firstKind === "disconnecting") {
            const idleMs = finiteNumber(firstStatus?.worldRecvIdleMs);
            throw new Error(
                `world connection is silent and client is closing while ${context}` +
                (idleMs === undefined ? "" : ` (world recv idle ${Math.trunc(idleMs)}ms)`)
            );
        }

        await this.waitDelay(run, 300, budget);
        let secondStatus: any;
        try {
            secondStatus = await this.queryStatus(run, budget);
        } catch (e) {
            if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
            const message = e instanceof Error ? e.message : String(e);
            throw new Error(`local player status became unavailable while ${context}: ${message}`);
        }

        const secondKind = classifyLocalPlayerStatus(secondStatus);
        if (secondKind === "alive") {
            this.appendLog(
                run,
                "warn",
                `忽略一次瞬时玩家状态异常（${firstKind}），复核后角色仍存活`,
                run.currentNodeId
            );
            return secondStatus;
        }
        if (secondKind === "disconnecting") {
            const idleMs = finiteNumber(secondStatus?.worldRecvIdleMs);
            throw new Error(
                `world connection is silent and client is closing while ${context}` +
                (idleMs === undefined ? "" : ` (world recv idle ${Math.trunc(idleMs)}ms)`)
            );
        }
        if (firstKind === "dead" && secondKind === "dead") {
            throw new Error(`character died while ${context}`);
        }
        throw new Error(
            `local player status is unavailable while ${context} ` +
            `(first=${firstKind}, second=${secondKind})`
        );
    }

    private async requireExpectedMapStatus(
        run: WorkflowRunInternal,
        firstStatus: any,
        budget: NodeBudget,
        expectedMapId: number,
        context: string
    ): Promise<any> {
        const firstMapId = finiteNumber(firstStatus?.mapId);
        if (firstMapId === expectedMapId) return firstStatus;

        await this.waitDelay(run, 300, budget);
        let secondStatus = await this.queryStatus(run, budget);
        secondStatus = await this.requireAliveStatus(run, secondStatus, budget, context);
        const secondMapId = finiteNumber(secondStatus?.mapId);
        if (secondMapId === expectedMapId) {
            this.appendLog(
                run,
                "warn",
                `忽略一次瞬时地图状态 ${firstMapId ?? "?"}，复核后仍在 ${expectedMapId}`,
                run.currentNodeId
            );
            return secondStatus;
        }
        throw new Error(
            `map changed while ${context}: expected ${expectedMapId}, ` +
            `observed ${firstMapId ?? "?"} then ${secondMapId ?? "?"}`
        );
    }

    private async queryPosition(run: WorkflowRunInternal, budget: NodeBudget): Promise<any> {
        const reply = await this.sendReadOnly(run, "getLocalPosition", {}, budget, 4000);
        ensureCommandOk(reply, "getLocalPosition");
        const position = parseDetail(reply.detail);
        if (!isRecord(position) || finiteNumber(position.x) === undefined || finiteNumber(position.y) === undefined) {
            throw new Error("getLocalPosition returned invalid detail");
        }
        run.outputs.position = position;
        return position;
    }

    private async waitForPosition(
        run: WorkflowRunInternal,
        x: number,
        y: number,
        arriveDistance: number,
        pollMs: number,
        budget: NodeBudget
    ): Promise<void> {
        for (;;) {
            await this.checkPoint(run, budget);
            const position = await this.queryPosition(run, budget);
            if (Math.hypot(Number(position.x) - x, Number(position.y) - y) <= arriveDistance) return;
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async waitForStatus(
        run: WorkflowRunInternal,
        budget: NodeBudget,
        predicate: (status: any) => boolean,
        pollMs: number
    ): Promise<void> {
        for (;;) {
            await this.checkPoint(run, budget);
            const status = await this.queryStatus(run, budget);
            if (predicate(status)) return;
            await this.waitDelay(run, pollMs, budget);
        }
    }

    private async send(
        run: WorkflowRunInternal,
        action: string,
        args: Record<string, unknown>,
        budget: NodeBudget,
        preferredTimeoutMs: number
    ): Promise<{ ok: boolean; detail?: string }> {
        await this.checkPoint(run, budget);
        this.ensureSameConnection(run);
        const remaining = this.remainingBudgetMs(run, budget);
        if (remaining <= 0) throw new NodeTimeoutError();
        const timeoutMs = Math.max(100, Math.min(preferredTimeoutMs, remaining));
        const result = await this.registry.sendCommand(run.pid, action, args, timeoutMs, run.id);
        this.ensureSameConnection(run);
        await this.checkPoint(run, budget);
        return result;
    }

    private async sendReadOnly(
        run: WorkflowRunInternal,
        action: string,
        args: Record<string, unknown>,
        budget: NodeBudget,
        preferredTimeoutMs: number
    ): Promise<{ ok: boolean; detail?: string }> {
        let lastError: unknown;
        for (let attempt = 1; attempt <= READ_ONLY_RETRY_ATTEMPTS; attempt += 1) {
            try {
                const result = await this.send(run, action, args, budget, preferredTimeoutMs);
                ensureCommandOk(result, action);
                return result;
            } catch (e) {
                if (e instanceof StopRequestedError || e instanceof NodeTimeoutError) throw e;
                this.ensureSameConnection(run);
                lastError = e;
                if (attempt >= READ_ONLY_RETRY_ATTEMPTS) throw e;
                if (attempt === 1) {
                    const message = e instanceof Error ? e.message : String(e);
                    this.appendLog(
                        run,
                        "warn",
                        `只读状态 ${action} 暂时失败，准备复核：${message}`,
                        run.currentNodeId
                    );
                }
                await this.waitDelay(run, 250, budget);
            }
        }
        throw lastError;
    }

    private async sendNativeUi(
        run: WorkflowRunInternal,
        action: string,
        args: Record<string, unknown>,
        budget: NodeBudget
    ): Promise<{ ok: boolean; detail?: string }> {
        // DLL's native bridge has a 2.5s claim deadline plus a 1s executing
        // grace. Never let the outer IPC deadline fire first, and never issue a
        // UI mutation when the node has too little budget left to receive its
        // claimed/ambiguous result.
        const ipcTimeoutMs = 5000;
        await this.checkPoint(run, budget);
        this.ensureSameConnection(run);
        if (this.remainingBudgetMs(run, budget) < ipcTimeoutMs) {
            throw new NodeTimeoutError();
        }
        const result = await this.registry.sendCommand(
            run.pid,
            action,
            args,
            ipcTimeoutMs,
            run.id
        );
        this.ensureSameConnection(run);
        await this.checkPoint(run, budget);
        return result;
    }

    private async prepareDungeonEntryCritical(
        run: WorkflowRunInternal,
        budget: NodeBudget,
        args: Record<string, unknown>
    ): Promise<Record<string, any>> {
        this.ensureSameConnection(run);
        const remaining = this.remainingBudgetMs(run, budget);
        if (remaining < 5000) {
            run.dungeonEntryStartInFlight = false;
            throw new NodeTimeoutError();
        }
        const timeoutMs = 5000;
        try {
            const result = await this.registry.sendCommand(
                run.pid,
                "prepareDungeonEntry",
                args,
                timeoutMs,
                run.id
            );
            ensureCommandOk(result, "prepareDungeonEntry");
            const parsed = parseDetail(result.detail);
            if (!isRecord(parsed)) throw new Error("prepareDungeonEntry returned invalid detail");
            const requestToken = String(args.requestToken || "");
            if (String(parsed.requestToken || "") !== requestToken ||
                String(parsed.transactionState || "") !== "Prepared") {
                throw new Error("prepareDungeonEntry returned a mismatched transaction");
            }
            this.ensureSameConnection(run);
            return parsed;
        } catch (error) {
            const requestToken = String(args.requestToken || "");
            const status = requestToken
                ? await this.queryDungeonEntryBestEffort(run, requestToken, 0)
                : undefined;
            if (status?.requestToken === requestToken && status.transactionState === "Prepared") {
                return status as unknown as Record<string, any>;
            }
            throw error;
        } finally {
            run.dungeonEntryStartInFlight = false;
        }
    }

    private async commitDungeonEntryCritical(
        run: WorkflowRunInternal,
        budget: NodeBudget,
        requestToken: string
    ): Promise<Record<string, any>> {
        await this.checkPoint(run, budget);
        this.ensureSameConnection(run);
        const remaining = this.remainingBudgetMs(run, budget);
        if (remaining < 5000) throw new NodeTimeoutError();
        const timeoutMs = Math.min(10_000, remaining);
        run.dungeonEntryStartInFlight = true;
        try {
            const result = await this.registry.sendCommand(
                run.pid,
                "commitDungeonEntry",
                { requestToken },
                timeoutMs,
                run.id
            );
            ensureCommandOk(result, "commitDungeonEntry");
            const parsed = parseDetail(result.detail);
            if (!isRecord(parsed)) throw new Error("commitDungeonEntry returned invalid detail");
            if (String(parsed.requestToken || "") !== requestToken ||
                String(parsed.transactionState || "") !== "Committed") {
                throw new Error("commitDungeonEntry returned a mismatched transaction");
            }
            run.dungeonEntryGeneration = requiredInteger(
                parsed.generation,
                "commitDungeonEntry.generation"
            );
            this.ensureSameConnection(run);
            return parsed;
        } catch (error: any) {
            for (let attempt = 0; attempt < 3; attempt += 1) {
                const status = await this.queryDungeonEntryBestEffort(run, requestToken, 0);
                if (status?.requestToken === requestToken) {
                    if (status.transactionState === "Committed") {
                        run.dungeonEntryGeneration = status.generation;
                        return status as unknown as Record<string, any>;
                    }
                    if (["CommitFailed", "Cancelled", "Expired"].includes(status.transactionState)) {
                        throw new Error(
                            `commitDungeonEntry became ${status.transactionState}; target opt will not be retried`
                        );
                    }
                }
                if (attempt < 2) await new Promise<void>((resolve) => setTimeout(resolve, 100));
            }
            throw error;
        } finally {
            run.dungeonEntryStartInFlight = false;
        }
    }

    private cancelDungeonEntry(run: WorkflowRunInternal, attempts = 2): Promise<boolean> {
        if (run.dungeonEntryCancelPromise) return run.dungeonEntryCancelPromise;
        const task = this.cancelDungeonEntryAttempts(run, Math.max(1, attempts));
        run.dungeonEntryCancelPromise = task;
        void task.then(() => {
            if (run.dungeonEntryCancelPromise === task) run.dungeonEntryCancelPromise = null;
        });
        return task;
    }

    private async queryDungeonEntryBestEffort(
        run: WorkflowRunInternal,
        requestToken: string,
        afterSeq: number
    ): Promise<DungeonEntryStatus | undefined> {
        try {
            const result = await this.registry.sendCommand(
                run.pid,
                "queryDungeonEntry",
                { requestToken, afterSeq },
                2000,
                run.id
            );
            ensureCommandOk(result, "queryDungeonEntry");
            const status = normalizeDungeonEntryStatus(parseDetail(result.detail));
            return status.requestToken === requestToken ? status : undefined;
        } catch {
            return undefined;
        }
    }

    private async cancelDungeonEntryAfterStart(
        run: WorkflowRunInternal,
        attempts = 2
    ): Promise<boolean> {
        const waitStarted = Date.now();
        while (run.dungeonEntryStartInFlight &&
            Date.now() - waitStarted < DUNGEON_ENTRY_START_SETTLE_MS) {
            await new Promise<void>((resolve) => setTimeout(resolve, 25));
        }
        if (!run.dungeonEntryRequestToken) {
            return !run.dungeonEntryStartInFlight;
        }
        return this.cancelDungeonEntry(run, attempts);
    }

    private async cancelDungeonEntryAttempts(
        run: WorkflowRunInternal,
        attempts: number
    ): Promise<boolean> {
        const requestToken = run.dungeonEntryRequestToken;
        if (!requestToken) {
            run.dungeonEntryActive = false;
            return true;
        }
        const generation = run.dungeonEntryGeneration ?? 0;
        let lastError = "";
        const maxAttempts = Math.max(1, attempts);
        for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
            try {
                const currentInstance = this.registry.list().find((entry) => entry.pid === run.pid);
                const connectionChanged = Boolean(
                    currentInstance && currentInstance.connId !== run.connId
                );
                const result = await this.registry.sendCommand(
                    run.pid,
                    "cancelDungeonEntry",
                    { requestToken },
                    3000,
                    run.id
                );
                if (!result.ok && connectionChanged) {
                    const rejected = parseDetail(result.detail);
                    const error = isRecord(rejected) ? String(rejected.error || "") : "";
                    const currentToken = isRecord(rejected)
                        ? String(rejected.currentRequestToken || "")
                        : "";
                    if (error.includes("request token/generation mismatch") &&
                        currentToken !== requestToken) {
                        run.dungeonEntryActive = false;
                        this.appendLog(
                            run,
                            "warn",
                            "DLL connection changed and the replacement connection does not own the old entry token; treating the old scoped confirmer as gone",
                            run.currentNodeId
                        );
                        return true;
                    }
                }
                ensureCommandOk(result, "cancelDungeonEntry");
                const detail = parseDetail(result.detail);
                const cancelled = isRecord(detail) && detail.cancelled === true;
                const state = isRecord(detail) ? String(detail.state || "unknown") : "unknown";
                run.dungeonEntryActive = false;
                this.appendLog(
                    run,
                    "info",
                    cancelled
                        ? `已取消限定副本入場確認 generation=${generation}`
                        : `限定副本入場確認 generation=${generation} 已處於 ${state}，無需重複取消`,
                    run.currentNodeId
                );
                return true;
            } catch (e: any) {
                lastError = e?.message || String(e);
                if (attempt < maxAttempts) {
                    await new Promise<void>((resolve) => setTimeout(resolve, 100));
                }
            }
        }
        this.appendLog(
            run,
            "warn",
            `cancelDungeonEntry ${maxAttempts} 次嘗試均失敗：${lastError}`,
            run.currentNodeId
        );
        return false;
    }

    private stopPath(run: WorkflowRunInternal, attempts = 2): Promise<boolean> {
        if (run.pathStopPromise) return run.pathStopPromise;
        const task = this.stopPathAttempts(run, Math.max(2, attempts));
        run.pathStopPromise = task;
        void task.then(() => {
            if (run.pathStopPromise === task) run.pathStopPromise = null;
        });
        return task;
    }

    private async stopPathAttempts(run: WorkflowRunInternal, attempts: number): Promise<boolean> {
        let lastError = "";
        const maxAttempts = Math.max(1, attempts);
        for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
            try {
                this.ensureSameConnection(run);
                const result = await this.registry.sendCommand(run.pid, "stopPath", {}, 3000, run.id);
                ensureCommandOk(result, "stopPath");
                run.pathActive = false;
                this.appendLog(run, "info", "已确认 stopPath", run.currentNodeId);
                return true;
            } catch (e: any) {
                lastError = e.message || String(e);
                if (attempt < maxAttempts) await new Promise<void>((resolve) => setTimeout(resolve, 100));
            }
        }
        // 失败时保留 pathActive=true，pause 健康检查、stop、节点 finally 与 engine
        // destroy 都能继续重试，不能用一次失败永久抑制后续 stopPath。
        this.appendLog(run, "warn", `stopPath ${maxAttempts} 次尝试均失败：${lastError}`, run.currentNodeId);
        return false;
    }

    private ensureSameConnection(run: WorkflowRunInternal): void {
        const current = this.registry.list().find((entry) => entry.pid === run.pid);
        if (!current) throw new Error("instance offline");
        if (current.connId !== run.connId) throw new Error("instance connection changed");
    }

    private async checkPoint(run: WorkflowRunInternal, budget?: NodeBudget): Promise<void> {
        await this.waitUntilRunnable(run);
        this.ensureRuntimeBudget(run);
        if (budget && this.remainingNodeBudgetMs(run, budget) <= 0) throw new NodeTimeoutError();
        this.ensureSameConnection(run);
    }

    private async waitUntilRunnable(run: WorkflowRunInternal): Promise<void> {
        if (run.stopRequested || this.destroyed) throw new StopRequestedError();
        while (run.pauseRequested) {
            if (run.stopRequested || this.destroyed) throw new StopRequestedError();
            // 暂停不是脱离运行时：周期核对连接，避免角色已离线/同 PID 重连后
            // 永久占用 activeByPid 与 command lease。
            this.ensureSameConnection(run);
            if (run.pathActive) {
                run.pathRestartRequired = true;
                await this.stopPath(run, 1);
            }
            await this.waitForWakeOrTimeout(run, PAUSED_HEALTH_CHECK_MS);
        }
        if (run.status === "paused") run.status = "running";
    }

    private waitForWakeOrTimeout(run: WorkflowRunInternal, timeoutMs: number): Promise<void> {
        return new Promise<void>((resolve) => {
            let settled = false;
            const wake = () => {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                run.wakeWaiters.delete(wake);
                resolve();
            };
            const timer = setTimeout(wake, timeoutMs);
            run.wakeWaiters.add(wake);
        });
    }

    private async waitDelay(run: WorkflowRunInternal, ms: number, budget?: NodeBudget): Promise<void> {
        let remaining = Math.max(0, ms);
        while (remaining > 0) {
            await this.waitUntilRunnable(run);
            this.ensureRuntimeBudget(run);
            if (budget && this.remainingNodeBudgetMs(run, budget) <= 0) throw new NodeTimeoutError();
            const chunk = Math.min(100, remaining,
                budget ? Math.max(1, this.remainingNodeBudgetMs(run, budget)) : remaining);
            const before = this.activeElapsedMs(run);
            await new Promise<void>((resolve) => setTimeout(resolve, chunk));
            remaining -= Math.max(0, this.activeElapsedMs(run) - before);
        }
        await this.waitUntilRunnable(run);
    }

    private wake(run: WorkflowRunInternal): void {
        for (const resolve of run.wakeWaiters) resolve();
        run.wakeWaiters.clear();
    }

    private activeElapsedMs(run: WorkflowRunInternal): number {
        const currentPause = run.pauseRequested && run.pauseStartedAt > 0
            ? Date.now() - run.pauseStartedAt
            : 0;
        return Math.max(0, Date.now() - run.startedAt - run.totalPausedMs - currentPause);
    }

    private remainingNodeBudgetMs(run: WorkflowRunInternal, budget: NodeBudget): number {
        return budget.timeoutMs - (this.activeElapsedMs(run) - budget.startedActiveMs);
    }

    private remainingBudgetMs(run: WorkflowRunInternal, budget: NodeBudget): number {
        const nodeRemaining = this.remainingNodeBudgetMs(run, budget);
        const runRemaining = run.workflow.maxRuntimeMs - this.activeElapsedMs(run);
        return Math.floor(Math.min(nodeRemaining, runRemaining));
    }

    private ensureRuntimeBudget(run: WorkflowRunInternal): void {
        if (run.stopRequested || this.destroyed) throw new StopRequestedError();
        if (this.activeElapsedMs(run) > run.workflow.maxRuntimeMs) {
            throw new Error(`runtime limit exceeded (${run.workflow.maxRuntimeMs}ms)`);
        }
    }

    private nodeTimeoutMs(node: WorkflowNode): number {
        if (finiteNumber(node.timeoutMs) !== undefined) {
            return clampInt(node.timeoutMs, 100, MAX_RUNTIME_LIMIT_MS, 10_000);
        }
        switch (node.type) {
            case "pathTo":
            case "approachEntity": return 120_000;
            case "clearMonsters": return 10 * 60_000;
            case "killBoss": return 15 * 60_000;
            case "collectFilteredDrops":
                return clampInt(node.params.maxDurationMs, 1000, 10 * 60_000, 100_000) + 10_000;
            case "waitUntil": return 60_000;
            case "dungeonEntry": return 60_000;
            case "revive":
            case "teleport":
            case "interact": return 30_000;
            case "wait": return Math.max(1000, nonNegativeNumber(node.params.ms, 0) + 1000);
            default: return 10_000;
        }
    }

    private finishRun(run: WorkflowRunInternal, status: "stopped" | "completed" | "failed", message: string): void {
        if (!isActiveStatus(run.status) && run.endedAt) return;
        const now = Date.now();
        run.status = status;
        run.updatedAt = now;
        run.endedAt = now;
        run.pendingTerminalStatus = status;
        run.pendingTerminalMessage = message;
        if (status === "failed") run.lastError = message;
        this.appendLog(run, status === "failed" ? "error" : "info", message, run.currentNodeId);
        this.wake(run);
    }

    private releaseRunLeaseIfSafe(run: WorkflowRunInternal): boolean {
        if (!run.executionFinished || run.dungeonEntryActive ||
            run.dungeonEntryStartInFlight || run.dungeonEntryRequestToken) {
            return false;
        }
        if (this.activeByPid.get(run.pid) === run.id) this.activeByPid.delete(run.pid);
        if (!run.leaseReleased) {
            this.registry.releaseCommandLease(run.pid, run.id);
            run.leaseReleased = true;
        }
        return true;
    }

    private appendLog(
        run: WorkflowRunInternal,
        level: WorkflowRunLog["level"],
        message: string,
        nodeId?: string
    ): void {
        run.logSeq += 1;
        run.logs.push({ seq: run.logSeq, at: Date.now(), level, message, nodeId });
        if (run.logs.length > RUN_LOG_LIMIT) run.logs.splice(0, run.logs.length - RUN_LOG_LIMIT);
        run.updatedAt = Date.now();
    }

    private snapshot(run: WorkflowRunInternal): WorkflowRunSnapshot {
        return {
            id: run.id,
            workflowId: run.workflowId,
            workflowName: run.workflowName,
            pid: run.pid,
            characterName: run.characterName,
            status: run.status,
            currentNodeId: run.currentNodeId,
            currentNodeName: run.currentNodeName,
            currentNodeType: run.currentNodeType,
            transitions: run.transitions,
            startedAt: run.startedAt,
            updatedAt: run.updatedAt,
            endedAt: run.endedAt,
            lastError: run.lastError,
            outputs: deepClone(run.outputs),
            logs: run.logs.map((entry) => ({ ...entry })),
        };
    }

    private pruneRuns(): void {
        if (this.runs.size <= MAX_RETAINED_RUNS) return;
        const completed = Array.from(this.runs.values())
            .filter((run) => !isActiveStatus(run.status))
            .sort((a, b) => a.updatedAt - b.updatedAt);
        while (this.runs.size > MAX_RETAINED_RUNS && completed.length > 0) {
            this.runs.delete(completed.shift()!.id);
        }
    }
}

function normalizeWorkflow(id: string, raw: Partial<WorkflowDefinition>): WorkflowDefinition {
    const normalizedId = String(id || "").trim();
    if (!normalizedId) throw new Error("workflow id is required");
    const nodes = Array.isArray(raw.nodes) ? raw.nodes.map(normalizeNode) : [];
    return {
        id: normalizedId,
        name: String(raw.name || normalizedId).trim() || normalizedId,
        description: String(raw.description || "").trim() || undefined,
        startNodeId: String(raw.startNodeId || nodes[0]?.id || "").trim(),
        maxTransitions: clampInt(raw.maxTransitions, 1, MAX_TRANSITIONS_LIMIT, DEFAULT_MAX_TRANSITIONS),
        maxRuntimeMs: clampInt(raw.maxRuntimeMs, 1000, MAX_RUNTIME_LIMIT_MS, DEFAULT_MAX_RUNTIME_MS),
        nodes,
        updatedAt: Date.now(),
    };
}

function normalizeNode(raw: any, index: number): WorkflowNode {
    const id = String(raw?.id || `node-${index + 1}`).trim();
    const type = String(raw?.type || "") as WorkflowNodeType;
    const params = isRecord(raw?.params) ? deepClone(raw.params) : {};
    if (type === "killBoss" && Object.prototype.hasOwnProperty.call(params, "initialHpGt")) {
        if (Object.prototype.hasOwnProperty.call(params, "maxHpGt") &&
            Number(params.maxHpGt) !== Number(params.initialHpGt)) {
            throw new Error(`${id} has conflicting maxHpGt and legacy initialHpGt`);
        }
        if (!Object.prototype.hasOwnProperty.call(params, "maxHpGt")) {
            params.maxHpGt = params.initialHpGt;
        }
        delete params.initialHpGt;
    }
    return {
        id,
        type,
        name: String(raw?.name || id).trim() || id,
        params,
        next: optionalId(raw?.next),
        onTrue: optionalId(raw?.onTrue),
        onFalse: optionalId(raw?.onFalse),
        onFailure: optionalId(raw?.onFailure),
        timeoutMs: finiteNumber(raw?.timeoutMs),
        retries: finiteNumber(raw?.retries),
        retryDelayMs: finiteNumber(raw?.retryDelayMs),
    };
}

function validateWorkflow(workflow: WorkflowDefinition): void {
    if (workflow.nodes.length === 0) throw new Error("workflow must contain at least one node");
    const byId = new Map<string, WorkflowNode>();
    for (const node of workflow.nodes) {
        if (!node.id) throw new Error("node id is required");
        if (byId.has(node.id)) throw new Error(`duplicate node id: ${node.id}`);
        if (!SUPPORTED_NODE_TYPES.has(node.type)) throw new Error(`unsupported node type: ${node.type}`);
        byId.set(node.id, node);
        validateNodeParams(node);
    }
    if (!workflow.startNodeId || !byId.has(workflow.startNodeId)) {
        throw new Error(`start node not found: ${workflow.startNodeId || "(empty)"}`);
    }
    for (const node of workflow.nodes) {
        for (const [edge, target] of [
            ["next", node.next],
            ["onTrue", node.onTrue],
            ["onFalse", node.onFalse],
            ["onFailure", node.onFailure],
        ] as const) {
            if (target && !byId.has(target)) throw new Error(`${node.id}.${edge} target not found: ${target}`);
        }
        if ((node.type === "condition" || node.type === "loop") && !node.onTrue && !node.onFalse && !node.next) {
            throw new Error(`${node.id} requires onTrue/onFalse or next`);
        }
    }
}

function validateNodeParams(node: WorkflowNode): void {
    const p = node.params;
    if (node.timeoutMs !== undefined && (!Number.isFinite(node.timeoutMs) || node.timeoutMs <= 0)) {
        throw new Error(`${node.id}.timeoutMs must be > 0`);
    }
    if (node.retries !== undefined && (!Number.isSafeInteger(node.retries) || node.retries < 0 || node.retries > 20)) {
        throw new Error(`${node.id}.retries must be an integer between 0 and 20`);
    }
    if (node.retryDelayMs !== undefined && (!Number.isFinite(node.retryDelayMs) || node.retryDelayMs < 0)) {
        throw new Error(`${node.id}.retryDelayMs must be >= 0`);
    }
    switch (node.type) {
        case "condition":
        case "waitUntil": {
            validateCondition(conditionFromParams(p), node.id);
            if ((node.type === "condition" || node.type === "waitUntil") &&
                finiteNumber(p.pollMs) !== undefined && Number(p.pollMs) < 100) {
                throw new Error(`${node.id}.pollMs must be >= 100`);
            }
            if ((node.type === "condition" || node.type === "waitUntil") &&
                finiteNumber(p.stableDurationMs) !== undefined &&
                Number(p.stableDurationMs) < 0) {
                throw new Error(`${node.id}.stableDurationMs must be >= 0`);
            }
            break;
        }
        case "pathTo":
        case "moveTo":
        case "warpTo":
            requiredNumber(p.x, `${node.id}.x`);
            requiredNumber(p.y, `${node.id}.y`);
            if ((node.type === "pathTo" || node.type === "moveTo") && p.action !== undefined) {
                const action = requiredInteger(p.action, `${node.id}.action`);
                if (action !== 1 && action !== 3) throw new Error(`${node.id}.action must be 1 or 3`);
            }
            if (node.type === "pathTo" && finiteNumber(p.targetTolerance) !== undefined && Number(p.targetTolerance) <= 0) {
                throw new Error(`${node.id}.targetTolerance must be > 0`);
            }
            break;
        case "teleport":
            if (nonNegativeInt(p.destId, 0) <= 0 && !String(p.cityName || "").trim()) {
                throw new Error(`${node.id} requires destId or cityName`);
            }
            break;
        case "dialogSelect": {
            const dialogOption = requiredInteger(p.option, `${node.id}.option`);
            const npcId = nonNegativeInt(p.npcId, 0);
            const nearestNpc = p.nearestNpc === true;
            if ((p.raw === true || npcId > 0 || nearestNpc) && dialogOption === 9575) {
                throw new Error(`${node.id}.option 9575 is reserved for dungeonEntry`);
            }
            if (p.nearestNpc !== undefined && typeof p.nearestNpc !== "boolean") {
                throw new Error(`${node.id}.nearestNpc must be boolean`);
            }
            if (nearestNpc) {
                if (p.raw !== true) throw new Error(`${node.id}.nearestNpc requires raw=true`);
                if (npcId > 0) throw new Error(`${node.id} cannot set both nearestNpc=true and npcId`);
                if (p.isNpc === false) throw new Error(`${node.id}.nearestNpc cannot use isNpc=false`);
                const allVisible = p.allVisible === true;
                const maxDistance = finiteNumber(p.maxDistance) ?? 5;
                if (!allVisible && maxDistance <= 0) {
                    throw new Error(`${node.id}.maxDistance must be > 0 unless allVisible is true`);
                }
                if (finiteNumber(p.waitForNpcMs) !== undefined && Number(p.waitForNpcMs) < 0) {
                    throw new Error(`${node.id}.waitForNpcMs must be >= 0`);
                }
                if (finiteNumber(p.pollMs) !== undefined && Number(p.pollMs) < 100) {
                    throw new Error(`${node.id}.pollMs must be >= 100`);
                }
                validateEntityFilter({ ...p, isNpc: true }, node.id);
            } else if ((p.raw === true || npcId > 0) && npcId <= 0) {
                throw new Error(`${node.id} raw dialogSelect requires npcId or nearestNpc=true`);
            }
            break;
        }
        case "dungeonEntry": {
            const entryOpt = requiredInteger(p.option, `${node.id}.option`);
            if (entryOpt !== 9575) {
                throw new Error(`${node.id}.option must be 9575 for the targeted Another entry flow`);
            }
            if (nonNegativeInt(p.sub, 1) !== 1) {
                throw new Error(`${node.id}.sub must be 1`);
            }
            if (node.retries !== undefined && node.retries !== 0) {
                throw new Error(`${node.id}.retries must be 0 because dungeonEntry is non-idempotent`);
            }
            if (p.autoUseExtraTicket !== undefined && typeof p.autoUseExtraTicket !== "boolean") {
                throw new Error(`${node.id}.autoUseExtraTicket must be boolean`);
            }
            if (nonNegativeInt(p.ticketItemId, DUNGEON_ENTRY_TICKET_ITEM_ID) !== DUNGEON_ENTRY_TICKET_ITEM_ID) {
                throw new Error(`${node.id}.ticketItemId must be ${DUNGEON_ENTRY_TICKET_ITEM_ID}`);
            }
            if (p.maxTicketUses !== undefined && Number(p.maxTicketUses) !== DUNGEON_ENTRY_MAX_TICKETS) {
                throw new Error(`${node.id}.maxTicketUses must be ${DUNGEON_ENTRY_MAX_TICKETS}`);
            }
            const confirmText = String(p.confirmTextContains || "").trim();
            if (confirmText !== DUNGEON_ENTRY_CONFIRM_TEXT) {
                throw new Error(`${node.id}.confirmTextContains must be '${DUNGEON_ENTRY_CONFIRM_TEXT}'`);
            }
            if (!String(p.expectedMapNameContains || "").trim()) {
                throw new Error(`${node.id}.expectedMapNameContains is required`);
            }
            const allVisible = p.allVisible === true;
            const maxDistance = finiteNumber(p.maxDistance) ?? 5;
            if (!allVisible && maxDistance <= 0) {
                throw new Error(`${node.id}.maxDistance must be > 0 unless allVisible is true`);
            }
            if (finiteNumber(p.waitForNpcMs) !== undefined && Number(p.waitForNpcMs) < 0) {
                throw new Error(`${node.id}.waitForNpcMs must be >= 0`);
            }
            if (finiteNumber(p.pollMs) !== undefined && Number(p.pollMs) < 100) {
                throw new Error(`${node.id}.pollMs must be >= 100`);
            }
            if (finiteNumber(p.nativeDialogMaxSteps) !== undefined &&
                (Number(p.nativeDialogMaxSteps) < 1 || Number(p.nativeDialogMaxSteps) > 32)) {
                throw new Error(`${node.id}.nativeDialogMaxSteps must be in 1..32`);
            }
            if (finiteNumber(p.nativeDialogTimeoutMs) !== undefined &&
                (Number(p.nativeDialogTimeoutMs) < 5000 || Number(p.nativeDialogTimeoutMs) > 30_000)) {
                throw new Error(`${node.id}.nativeDialogTimeoutMs must be in 5000..30000`);
            }
            if (finiteNumber(p.mapStableDurationMs) !== undefined && Number(p.mapStableDurationMs) < 0) {
                throw new Error(`${node.id}.mapStableDurationMs must be >= 0`);
            }
            validateEntityFilter({ ...p, isNpc: true }, node.id);
            break;
        }
        case "interact":
            validateEntityFilter(p, node.id);
            break;
        case "approachEntity":
            validateEntityFilter(p, node.id);
            if (p.approachDistance !== undefined &&
                (finiteNumber(p.approachDistance) === undefined || Number(p.approachDistance) <= 0)) {
                throw new Error(`${node.id}.approachDistance must be > 0`);
            }
            if (p.pollMs !== undefined &&
                (finiteNumber(p.pollMs) === undefined || Number(p.pollMs) < 100)) {
                throw new Error(`${node.id}.pollMs must be >= 100`);
            }
            break;
        case "wait":
            if (requiredNumber(p.ms, `${node.id}.ms`) < 0) throw new Error(`${node.id}.ms must be >= 0`);
            break;
        case "loop":
            if (requiredInteger(p.count, `${node.id}.count`) < 0) throw new Error(`${node.id}.count must be >= 0`);
            break;
        case "clearMonsters": {
            const allVisible = p.allVisible === true;
            const maxDistance = finiteNumber(p.maxDistance) ?? 100;
            if (!allVisible && maxDistance <= 0) {
                throw new Error(`${node.id}.maxDistance must be > 0 unless allVisible is true`);
            }
            if (finiteNumber(p.emptyDurationMs) !== undefined && Number(p.emptyDurationMs) < 0) {
                throw new Error(`${node.id}.emptyDurationMs must be >= 0`);
            }
            if (p.approachDistance !== undefined && positiveNumber(p.approachDistance, 0) <= 0) {
                throw new Error(`${node.id}.approachDistance must be > 0`);
            }
            validateEntityFilter(p, node.id);
            break;
        }
        case "killBoss": {
            const allVisible = p.allVisible !== false;
            const maxDistance = finiteNumber(p.maxDistance) ?? 100;
            if (!allVisible && maxDistance <= 0) {
                throw new Error(`${node.id}.maxDistance must be > 0 unless allVisible is true`);
            }
            const maxHpGt = p.maxHpGt;
            if (maxHpGt !== undefined &&
                (!Number.isSafeInteger(maxHpGt) || Number(maxHpGt) < 0)) {
                throw new Error(`${node.id}.maxHpGt must be a non-negative safe integer`);
            }
            if (finiteNumber(p.waitForAppearanceMs) !== undefined && Number(p.waitForAppearanceMs) < 0) {
                throw new Error(`${node.id}.waitForAppearanceMs must be >= 0`);
            }
            if (p.skipIfAbsent !== undefined && typeof p.skipIfAbsent !== "boolean") {
                throw new Error(`${node.id}.skipIfAbsent must be boolean`);
            }
            if (finiteNumber(p.pollMs) !== undefined && Number(p.pollMs) < 100) {
                throw new Error(`${node.id}.pollMs must be >= 100`);
            }
            if (finiteNumber(p.reissueMs) !== undefined && Number(p.reissueMs) < 200) {
                throw new Error(`${node.id}.reissueMs must be >= 200`);
            }
            if (p.missingConfirmations !== undefined &&
                (!Number.isSafeInteger(p.missingConfirmations) || Number(p.missingConfirmations) < 1 ||
                    Number(p.missingConfirmations) > 20)) {
                throw new Error(`${node.id}.missingConfirmations must be an integer between 1 and 20`);
            }
            if (finiteNumber(p.missingDurationMs) !== undefined && Number(p.missingDurationMs) < 0) {
                throw new Error(`${node.id}.missingDurationMs must be >= 0`);
            }
            if (p.approachDistance !== undefined && positiveNumber(p.approachDistance, 0) <= 0) {
                throw new Error(`${node.id}.approachDistance must be > 0`);
            }
            if (node.retries && node.retries > 0) {
                throw new Error(`${node.id}.retries must be 0 because killBoss locks one creatureId internally`);
            }
            validateEntityFilter(p, node.id);
            break;
        }
        case "collectFilteredDrops": {
            if (finiteNumber(p.maxDistance) !== undefined && Number(p.maxDistance) < 0) {
                throw new Error(`${node.id}.maxDistance must be >= 0`);
            }
            if (positiveNumber(p.approachDistance, 0) <= 0) {
                throw new Error(`${node.id}.approachDistance must be > 0`);
            }
            if (positiveNumber(p.maxDurationMs, 0) <= 0) {
                throw new Error(`${node.id}.maxDurationMs must be > 0`);
            }
            if (finiteNumber(p.emptyDurationMs) !== undefined && Number(p.emptyDurationMs) < 0) {
                throw new Error(`${node.id}.emptyDurationMs must be >= 0`);
            }
            if (finiteNumber(p.pollMs) !== undefined && Number(p.pollMs) < 100) {
                throw new Error(`${node.id}.pollMs must be >= 100`);
            }
            if (finiteNumber(p.nativePickupTimeoutMs) !== undefined &&
                Number(p.nativePickupTimeoutMs) < 1000) {
                throw new Error(`${node.id}.nativePickupTimeoutMs must be >= 1000`);
            }
            if (p.includeUnpickable !== undefined && typeof p.includeUnpickable !== "boolean") {
                throw new Error(`${node.id}.includeUnpickable must be boolean`);
            }
            if (node.retries && node.retries > 0) {
                throw new Error(
                    `${node.id}.retries must be 0 because native pickup issues each drop action at most once`
                );
            }
            break;
        }
        case "command": {
            const action = String(p.action || "").trim();
            if (!action) throw new Error(`${node.id} requires command action`);
            if (BLOCKED_GENERIC_COMMANDS.has(action)) {
                throw new Error(`${node.id}: ${action} must use its dedicated workflow node`);
            }
            if (p.args !== undefined && !isRecord(p.args)) throw new Error(`${node.id}.args must be an object`);
            break;
        }
    }
}

function validateCondition(condition: Record<string, unknown>, nodeId: string): void {
    const kind = String(condition.kind || condition.type || "").trim();
    if (!SUPPORTED_CONDITION_KINDS.has(kind)) {
        throw new Error(`${nodeId} has unsupported condition.kind: ${kind || "(empty)"}`);
    }
    const defaultOperator = kind === "entity" || kind === "player"
        ? "exists"
        : kind === "position"
            ? "lte"
            : kind === "mapName"
                ? "contains"
            : kind === "command"
                ? "truthy"
                : "eq";
    const operator = String(condition.operator || defaultOperator);
    if (!SUPPORTED_OPERATORS.has(operator)) throw new Error(`${nodeId} has unsupported operator: ${operator}`);

    if (operator === "in" && !Array.isArray(condition.value)) {
        throw new Error(`${nodeId} operator 'in' requires an array value`);
    }
    const comparisonValue = kind === "map" ? condition.mapId ?? condition.value : condition.value;
    if (["gt", "gte", "lt", "lte"].includes(operator) && finiteNumber(comparisonValue) === undefined && kind !== "position") {
        throw new Error(`${nodeId} operator '${operator}' requires a numeric value`);
    }

    switch (kind) {
        case "map": {
            const expected = condition.mapId ?? condition.value;
            if (operator === "in") {
                if (!(condition.value as unknown[]).every((value) => finiteNumber(value) !== undefined)) {
                    throw new Error(`${nodeId} map 'in' values must be numbers`);
                }
            } else if (finiteNumber(expected) === undefined) {
                throw new Error(`${nodeId} map condition requires mapId/value`);
            }
            break;
        }
        case "mapName": {
            const expected = String(condition.value || "").trim();
            if (!expected) throw new Error(`${nodeId} mapName condition requires a non-empty value`);
            if (!["eq", "ne", "contains"].includes(operator)) {
                throw new Error(`${nodeId} mapName condition has invalid operator ${operator}`);
            }
            break;
        }
        case "position":
            requiredNumber(condition.x, `${nodeId}.condition.x`);
            requiredNumber(condition.y, `${nodeId}.condition.y`);
            if (positiveNumber(condition.distance ?? condition.value, 0) <= 0) {
                throw new Error(`${nodeId} position condition requires distance/value > 0`);
            }
            if (!["eq", "ne", "gt", "gte", "lt", "lte"].includes(operator)) {
                throw new Error(`${nodeId} position condition requires a numeric operator`);
            }
            break;
        case "dead":
            if (condition.value !== undefined && typeof condition.value !== "boolean") {
                throw new Error(`${nodeId} dead condition value must be boolean`);
            }
            if (!["eq", "ne", "truthy", "falsy"].includes(operator)) {
                throw new Error(`${nodeId} dead condition has invalid operator ${operator}`);
            }
            break;
        case "entity":
            validateEntityFilter(condition, nodeId);
            validateCollectionOperator(condition, nodeId, operator);
            break;
        case "player":
            validateNameFilter(condition, nodeId);
            if (finiteNumber(condition.maxDistance) !== undefined && Number(condition.maxDistance) <= 0) {
                throw new Error(`${nodeId}.condition.maxDistance must be > 0`);
            }
            validateCollectionOperator(condition, nodeId, operator);
            break;
        case "command": {
            const action = String(condition.action || "").trim();
            if (!action) throw new Error(`${nodeId} command condition requires action`);
            if (BLOCKED_GENERIC_COMMANDS.has(action)) {
                throw new Error(`${nodeId}: ${action} must use its dedicated workflow node`);
            }
            if (condition.args !== undefined && !isRecord(condition.args)) {
                throw new Error(`${nodeId}.condition.args must be an object`);
            }
            if (condition.path !== undefined && typeof condition.path !== "string") {
                throw new Error(`${nodeId}.condition.path must be a string`);
            }
            break;
        }
    }
}

function validateCollectionOperator(
    condition: Record<string, unknown>,
    nodeId: string,
    operator: string
): void {
    if (!["exists", "notExists", "eq", "ne", "gt", "gte", "lt", "lte"].includes(operator)) {
        throw new Error(`${nodeId} collection condition has invalid operator ${operator}`);
    }
    if (!["exists", "notExists"].includes(operator) && finiteNumber(condition.value) === undefined) {
        throw new Error(`${nodeId} collection operator '${operator}' requires numeric value`);
    }
}

function validateEntityFilter(filter: Record<string, unknown>, nodeId: string): void {
    if (finiteNumber(filter.maxDistance) !== undefined && Number(filter.maxDistance) < 0) {
        throw new Error(`${nodeId}.maxDistance must be >= 0`);
    }
    const ids = filter.monsterTblId ?? filter.monsterTblIds;
    if (ids !== undefined) {
        const values = Array.isArray(ids) ? ids : [ids];
        if (values.some((value) => finiteNumber(value) === undefined)) {
            throw new Error(`${nodeId}.monsterTblId(s) must be numeric`);
        }
    }
    if (filter.isNpc !== undefined && typeof filter.isNpc !== "boolean") {
        throw new Error(`${nodeId}.isNpc must be boolean`);
    }
    if (filter.attackable !== undefined && typeof filter.attackable !== "boolean") {
        throw new Error(`${nodeId}.attackable must be boolean`);
    }
    if (filter.allVisible !== undefined && typeof filter.allVisible !== "boolean") {
        throw new Error(`${nodeId}.allVisible must be boolean`);
    }
    if (filter.hpGt !== undefined && (finiteNumber(filter.hpGt) === undefined || Number(filter.hpGt) < 0)) {
        throw new Error(`${nodeId}.hpGt must be >= 0`);
    }
    if (filter.maxHpGt !== undefined &&
        (!Number.isSafeInteger(filter.maxHpGt) || Number(filter.maxHpGt) < 0)) {
        throw new Error(`${nodeId}.maxHpGt must be a non-negative safe integer`);
    }
    validateNameFilter(filter, nodeId);
}

function validateNameFilter(filter: Record<string, unknown>, nodeId: string): void {
    if (filter.name !== undefined && typeof filter.name !== "string") {
        throw new Error(`${nodeId}.name must be a string`);
    }
    if (filter.nameContains !== undefined && typeof filter.nameContains !== "string") {
        throw new Error(`${nodeId}.nameContains must be a string`);
    }
    if (filter.nameNotContains !== undefined && typeof filter.nameNotContains !== "string") {
        throw new Error(`${nodeId}.nameNotContains must be a string`);
    }
}

function cloneWorkflow(workflow: WorkflowDefinition): WorkflowDefinition {
    return deepClone(workflow);
}

function versionAtLeast(actual: unknown, required: string): boolean {
    const parse = (value: unknown): number[] | null => {
        const text = String(value || "").trim();
        if (!/^\d+(?:\.\d+)*$/.test(text)) return null;
        return text.split(".").map((part) => Number(part));
    };
    const current = parse(actual);
    const minimum = parse(required);
    if (!current || !minimum) return false;
    const length = Math.max(current.length, minimum.length);
    for (let index = 0; index < length; index += 1) {
        const left = current[index] || 0;
        const right = minimum[index] || 0;
        if (left !== right) return left > right;
    }
    return true;
}

function deepClone<T>(value: T): T {
    return JSON.parse(JSON.stringify(value)) as T;
}

function normalizePids(raw: unknown): number[] {
    if (!Array.isArray(raw)) return [];
    const seen = new Set<number>();
    const out: number[] = [];
    for (const value of raw) {
        const pid = Number(value);
        if (!Number.isSafeInteger(pid) || pid <= 0 || seen.has(pid)) continue;
        seen.add(pid);
        out.push(pid);
    }
    return out;
}

function conditionFromParams(params: Record<string, unknown>): Record<string, unknown> {
    return isRecord(params.condition) ? params.condition : params;
}

function compareCollectionCount(count: number, condition: Record<string, unknown>): boolean {
    const operator = String(condition.operator || "exists");
    if (operator === "exists") return count > 0;
    if (operator === "notExists") return count === 0;
    return compareValue(count, operator, condition.value ?? 1);
}

function compareValue(actual: unknown, rawOperator: unknown, expected: unknown): boolean {
    const operator = String(rawOperator || "eq");
    switch (operator) {
        case "eq": return actual === expected || (isNumeric(actual) && isNumeric(expected) && Number(actual) === Number(expected));
        case "ne": return !compareValue(actual, "eq", expected);
        case "gt": return Number(actual) > Number(expected);
        case "gte": return Number(actual) >= Number(expected);
        case "lt": return Number(actual) < Number(expected);
        case "lte": return Number(actual) <= Number(expected);
        case "in": return Array.isArray(expected) && expected.some((value) => compareValue(actual, "eq", value));
        case "contains":
            return typeof actual === "string"
                ? actual.includes(String(expected ?? ""))
                : Array.isArray(actual) && actual.some((value) => compareValue(value, "eq", expected));
        case "exists": return actual !== undefined && actual !== null;
        case "notExists": return actual === undefined || actual === null;
        case "truthy": return Boolean(actual);
        case "falsy": return !actual;
        default: throw new Error(`unsupported operator: ${operator}`);
    }
}

function getJsonPath(root: unknown, pathText: string): unknown {
    const normalized = pathText.trim()
        .replace(/^\$\.?/, "")
        .replace(/\[(\d+)\]/g, ".$1");
    if (!normalized) return root;
    let current: any = root;
    for (const segment of normalized.split(".").filter(Boolean)) {
        if (current === null || current === undefined) return undefined;
        current = current[segment];
    }
    return current;
}

function parseDetail(detail: unknown): any {
    if (typeof detail !== "string") return detail;
    const text = detail.trim();
    if (!text) return "";
    try {
        return JSON.parse(text);
    } catch {
        return text;
    }
}

function normalizeDungeonEntryStatus(raw: unknown): DungeonEntryStatus {
    if (!isRecord(raw)) throw new Error("queryDungeonEntry returned invalid detail");
    const requestToken = String(raw.requestToken || "");
    if (!requestToken) throw new Error("queryDungeonEntry returned an empty requestToken");
    const transactionState = String(raw.transactionState || "");
    if (!["Prepared", "CommitIssued", "Committed", "CommitFailed", "Cancelled", "Expired"].includes(transactionState)) {
        throw new Error(`queryDungeonEntry returned invalid transactionState: ${transactionState || "(empty)"}`);
    }
    const generation = requiredInteger(raw.generation, "queryDungeonEntry.generation");
    const state = String(raw.confirmState || raw.state || "");
    if (!["Idle", "Armed", "Accepted", "Declined", "Expired", "Cancelled"].includes(state)) {
        throw new Error(`queryDungeonEntry returned invalid state: ${state || "(empty)"}`);
    }
    if (!Array.isArray(raw.events)) throw new Error("queryDungeonEntry.events must be an array");
    const events = raw.events.map((entry: unknown, index: number): DungeonEntryEvent => {
        if (!isRecord(entry)) throw new Error(`queryDungeonEntry.events[${index}] must be an object`);
        return {
            seq: requiredInteger(entry.seq, `queryDungeonEntry.events[${index}].seq`),
            proto: requiredInteger(entry.proto, `queryDungeonEntry.events[${index}].proto`),
            result: finiteNumber(entry.result),
            nInsd: finiteNumber(entry.nInsd),
            itemId: finiteNumber(entry.itemId),
            count: finiteNumber(entry.count),
            opt: finiteNumber(entry.opt),
            branch: finiteNumber(entry.branch),
        };
    });
    return {
        requestToken,
        transactionState: transactionState as DungeonEntryStatus["transactionState"],
        initialSelectClaimed: raw.initialSelectClaimed === true,
        generation,
        state: state as DungeonEntryStatus["state"],
        baselineSeq: nonNegativeInt(raw.baselineSeq, 0),
        latestSeq: nonNegativeInt(raw.latestSeq, 0),
        truncated: raw.truncated === true,
        ticketCount: nonNegativeInt(raw.ticketCount, 0),
        currentTicketCount: nonNegativeInt(raw.currentTicketCount, 0),
        remainingMs: nonNegativeInt(raw.remainingMs, 0),
        dialogSeenCount: nonNegativeInt(raw.dialogSeenCount, 0),
        lastDialogType: finiteNumber(raw.lastDialogType) ?? -1,
        lastDialogMatch: String(raw.lastDialogMatch || "None"),
        lastDialogAuthorized: raw.lastDialogAuthorized === true,
        towerPromptAccepted: raw.towerPromptAccepted === true,
        ticketAck: raw.ticketAck === true,
        ticketAckSeq: nonNegativeInt(raw.ticketAckSeq, 0),
        events,
    };
}

function summarizeDungeonEntryStatus(status: DungeonEntryStatus): Record<string, unknown> {
    return {
        requestToken: status.requestToken,
        transactionState: status.transactionState,
        initialSelectClaimed: status.initialSelectClaimed,
        state: status.state,
        remainingMs: status.remainingMs,
        dialogSeenCount: status.dialogSeenCount,
        lastDialogType: status.lastDialogType,
        lastDialogMatch: status.lastDialogMatch,
        lastDialogAuthorized: status.lastDialogAuthorized,
        towerPromptAccepted: status.towerPromptAccepted,
        ticketCount: status.ticketCount,
        currentTicketCount: status.currentTicketCount,
        ticketAck: status.ticketAck,
        ticketAckSeq: status.ticketAckSeq,
        latestSeq: status.latestSeq,
    };
}

function ensureCommandOk(result: { ok: boolean; detail?: string }, action: string): void {
    if (!result.ok) throw new Error(`${action} failed: ${result.detail || "DLL rejected command"}`);
}

function normalizeNumberSet(raw: unknown): Set<number> {
    const values = Array.isArray(raw) ? raw : raw === undefined || raw === null || raw === "" ? [] : [raw];
    return new Set(values.map(Number).filter((value) => Number.isFinite(value)));
}

function byDistance(a: NearbyEntity, b: NearbyEntity): number {
    return (a.distance ?? Number.POSITIVE_INFINITY) - (b.distance ?? Number.POSITIVE_INFINITY);
}

function formatCombatDistance(target: NearbyEntity, resolvedDistance: unknown): string {
    const distance = finiteNumber(resolvedDistance);
    const reported = finiteNumber(target.distance);
    if (distance !== undefined && reported !== undefined && Math.abs(distance - reported) >= 5) {
        return `平面距离 ${roundNumber(distance)}（原始三维 ${roundNumber(reported)}）`;
    }
    return `距离 ${roundNumber(distance ?? reported)}`;
}

function isActiveStatus(status: WorkflowRunStatus): boolean {
    return status === "running" || status === "paused" || status === "stopping";
}

function optionalId(value: unknown): string | undefined {
    const id = String(value || "").trim();
    return id || undefined;
}

function stringOrUndefined(value: unknown): string | undefined {
    return typeof value === "string" && value.trim() ? value.trim() : undefined;
}

function finiteNumber(value: unknown): number | undefined {
    if (value === null || value === undefined || value === "") return undefined;
    const number = Number(value);
    return Number.isFinite(number) ? number : undefined;
}

function requiredNumber(value: unknown, field: string): number {
    const number = finiteNumber(value);
    if (number === undefined) throw new Error(`${field} must be a finite number`);
    return number;
}

function requiredInteger(value: unknown, field: string): number {
    const number = requiredNumber(value, field);
    if (!Number.isSafeInteger(number)) throw new Error(`${field} must be an integer`);
    return number;
}

function nonNegativeNumber(value: unknown, fallback: number): number {
    const number = finiteNumber(value);
    return number === undefined ? fallback : Math.max(0, number);
}

function positiveNumber(value: unknown, fallback: number): number {
    const number = finiteNumber(value);
    return number === undefined || number <= 0 ? fallback : number;
}

function nonNegativeInt(value: unknown, fallback: number): number {
    const number = finiteNumber(value);
    return number === undefined ? fallback : Math.max(0, Math.trunc(number));
}

function clampInt(value: unknown, min: number, max: number, fallback: number): number {
    const number = finiteNumber(value);
    if (number === undefined) return fallback;
    return Math.min(max, Math.max(min, Math.trunc(number)));
}

function isRecord(value: unknown): value is Record<string, any> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isNumeric(value: unknown): boolean {
    return value !== null && value !== "" && Number.isFinite(Number(value));
}

function roundNumber(value: unknown): string {
    const number = finiteNumber(value);
    return number === undefined ? "?" : String(Math.round(number * 10) / 10);
}
