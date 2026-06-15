import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// clockworkWasher:自动洗发条。每个角色一条任务,轮询「读当前发条状态 → 判停止条件 →
// 不满足就发一次洗(washClockwork)」。读状态用 queryClockwork(读 item+0xB0),洗用
// washClockwork(发 411590)。两者都走 DLL 远控命令。
//
// 停止条件 = 等级达标 AND 属性达标(任一项关掉则恒为真):
//   - 等级:targetGrade<=0 不看;否则要求 当前 grade >= targetGrade。grade 0..4 = N/G/DG/XG/SG。
//   - 属性:requiredAttrs 为空不看;否则要求「命中条数 >= minMatchCount」,
//     单条命中 = 装备三条属性里有 id==需求id 且 value>=minValue。
//
// 跟 buffKeeper 一样:DLL 不在线 / query 失败按「不确定」跳过本轮,不误洗。
// 读状态在洗之前 —— query 反映的是上一次洗的结果(511132 已把新属性写回 item+0xB0),
// 所以轮询间隔(pollMs)要 > 一次洗的服务端往返(默认 800ms 足够)。

export interface RequiredAttr {
    id: number;       // 属性 id(1..23)
    minValue: number; // 单属性: 数值>=minValue。复合(15..20): 增加量 M>=minValue。
    maxN?: number;    // 仅复合: 每N等級的 N<=maxN(N 越小越好);0/缺省=不限。
}

export interface SpringAttrView {
    id: number;
    value: number;
    name?: string;
    percent?: boolean;
    composite?: boolean; // 15..20: "每N等級增加X +M"
    n?: number;          // 复合: 每N等級 (越小越好)
    m?: number;          // 复合: 增加量 (越大越好)
}

export interface ClockworkWasherConfig {
    characterName: string;
    enabled: boolean;
    slotIndex: number;          // 普通背包装备 arrayIndex(0..191),即 getBagItems 的 slotIndex
    itemName: string;           // 显示用
    springType: number;         // 0=實習生 / 1=高手 / 2=武爾坎努斯
    targetGrade: number;        // 0=不看等级;否则洗到 grade>=targetGrade
    requiredAttrs: RequiredAttr[];
    minMatchCount: number;      // 至少命中几条 requiredAttrs
    pollMs: number;             // 两次洗之间的间隔
    maxWashes: number;          // 0=无上限;否则洗够这么多次就停(防失控)

    // 运行时态(也持久化,方便重启后看到上次结果)
    washCount: number;
    status: string;             // idle/running/done/no-spring/error/reached-max
    lastGrade: number;
    lastAttrs: SpringAttrView[];
    lastError: string;
    lastWashAt: number;         // epoch ms
}

interface PersistShape {
    characters: ClockworkWasherConfig[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "clockwork_washer.json"
);

// 收包确认版:每次洗在 DLL 里等 511132 回包才返回,本身就按服务端往返(RTT~200ms)自限速,
// 所以 pollMs 只是「两次洗之间额外的节流间隔」,可设 0 = 收到回包立刻洗下一次。
const DEFAULT_POLL_MS = 0;
const MIN_POLL_MS = 0;
const TICK_MS = 120;

// 发条系统硬限制(来源 2usealol 发条系统说明,与 web 同一份):
//   发条最高等级: 0實習生=DG(3) 1高手=SG(5) 2武爾坎努斯=SG(5)。等级棘轮,只升不降。
//   属性最低出现等级: N(1) 起的基础属性;G(2) 起 HP%/AP%/每级属性/经验/副本伤害;
//   DG(3) 起 增加伤害/减少伤害。要求的属性/目标等级超过发条上限就永远洗不出来。
const SPRING_MAX_GRADE = [3, 5, 5];
const GRADE_NAMES = ['—', 'N', 'G', 'DG', 'XG', 'SG'];
const ATTR_MIN_GRADE: Record<number, number> = {
    1: 1, 2: 1, 3: 1, 4: 1, 5: 1, 6: 1, 7: 1, 8: 1, 9: 1, 10: 1, 21: 1,
    11: 2, 12: 2, 15: 2, 16: 2, 17: 2, 18: 2, 19: 2, 20: 2, 22: 2, 23: 2,
    13: 3, 14: 3,
};
// 各属性各等级(index 0..4=N/G/DG/XG/SG)能洗出的最大数值;0=该等级未解锁。
const ATTR_VALUE_MAX: Record<number, number[]> = {
    1: [25, 45, 75, 135, 210], 2: [25, 45, 75, 135, 210], 5: [25, 45, 75, 135, 210],
    3: [5, 10, 15, 20, 25], 4: [5, 10, 15, 20, 25], 6: [5, 10, 15, 20, 25],
    7: [5, 10, 15, 20, 25], 8: [5, 10, 15, 20, 25],
    9: [50, 200, 500, 1000, 1500], 10: [50, 200, 500, 1000, 1500],
    11: [0, 1, 2, 3, 4], 12: [0, 1, 2, 3, 4], 13: [0, 0, 1, 3, 4], 14: [0, 0, 1, 3, 4],
    21: [3, 5, 10, 15, 20], 22: [0, 3, 5, 10, 15], 23: [0, 1, 1, 3, 4],
};
// 复合属性(15-20): 每级点数 M 上限 + 最小等级间隔 N(越小越好)。
const COMP_M_MAX: Record<number, number[]> = {
    15: [0, 1, 1, 2, 2], 16: [0, 1, 1, 2, 2], 17: [0, 1, 1, 2, 2], 18: [0, 1, 1, 2, 2],
    19: [0, 1, 1, 1, 1], 20: [0, 1, 1, 1, 1],
};
const COMP_N_MIN: Record<number, number[]> = {
    15: [0, 6, 4, 4, 3], 16: [0, 6, 4, 4, 3], 17: [0, 6, 4, 4, 3], 18: [0, 6, 4, 4, 3],
    19: [0, 40, 30, 15, 13], 20: [0, 40, 30, 15, 13],
};

interface QueryResult {
    valid: boolean;
    grade: number;
    attrs: SpringAttrView[];
}

// washClockwork 命令的返回(DLL 等 511132 回包后给出真实结果)。
interface WashResult {
    confirmed: boolean;       // 是否在 waitMs 内等到了 511132 回包
    resultCode?: number;      // 0=成功;非0=服务端拒绝
    grade?: number;
    attrs?: SpringAttrView[];
}

export class ClockworkWasher {
    private configs: ClockworkWasherConfig[] = [];
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    private nextPollAt = new Map<string, number>();
    private inFlight = new Set<string>();

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.start();
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                const parsed = JSON.parse(raw) as PersistShape;
                const list = Array.isArray(parsed.characters) ? parsed.characters : [];
                this.configs = list.map((c) => normalizeConfig(c));
                console.log(`[clockworkWasher] loaded ${this.configs.length} configs`);
            }
        } catch (e: any) {
            console.warn(`[clockworkWasher] load failed: ${e.message}`);
            this.configs = [];
        }
    }

    private save(): void {
        try {
            const shape: PersistShape = { characters: this.configs };
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(shape, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[clockworkWasher] save failed: ${e.message}`);
        }
    }

    private start(): void {
        this.timer = setInterval(() => this.tick(), TICK_MS);
    }

    private async tick(): Promise<void> {
        const now = Date.now();
        const nameToPid = new Map<string, number>();
        for (const inst of this.registry.list()) {
            if (inst.characterName) nameToPid.set(inst.characterName, inst.pid);
        }

        for (const cfg of this.configs) {
            if (!cfg.enabled) continue;
            const pid = nameToPid.get(cfg.characterName);
            if (pid === undefined) continue; // 角色不在线
            if (this.inFlight.has(cfg.characterName)) continue;

            const due = this.nextPollAt.get(cfg.characterName) ?? 0;
            if (now < due) continue;
            const pollMs = Math.max(MIN_POLL_MS, cfg.pollMs);
            this.nextPollAt.set(cfg.characterName, now + pollMs);

            this.inFlight.add(cfg.characterName);
            this.runOne(cfg, pid).finally(() => {
                this.inFlight.delete(cfg.characterName);
            });
        }
    }

    private async runOne(cfg: ClockworkWasherConfig, pid: number): Promise<void> {
        // 硬限制守护:目标等级/需要属性超过该发条能洗出的上限 → 永远不可能,直接停,别白洗。
        const bad = infeasibleReason(cfg);
        if (bad) {
            cfg.enabled = false;
            cfg.status = "error";
            cfg.lastError = bad;
            this.save();
            console.warn(`[clockworkWasher] ${cfg.characterName} 配置不可行: ${bad}`);
            return;
        }

        // 洗一次:DLL 发 411590 后等 511132 回包,返回真实结果(grade+3属性)。不再读内存,
        // 也不依赖游戏里开着洗发条界面。
        let res: WashResult;
        try {
            const r = await this.registry.sendCommand(
                pid,
                "washClockwork",
                { slotIndex: cfg.slotIndex, springType: cfg.springType, waitMs: 1500 },
                5000
            );
            if (!r.ok) {
                const detail = r.detail || "";
                if (/not found/i.test(detail)) {
                    cfg.enabled = false;
                    cfg.status = "no-spring";
                    cfg.lastError = "背包/cash背包里没有这种发条了";
                } else {
                    cfg.status = "error";
                    cfg.lastError = detail;
                }
                this.save();
                console.warn(`[clockworkWasher] ${cfg.characterName} washClockwork rejected: ${detail}`);
                return;
            }
            res = JSON.parse(r.detail || "{}") as WashResult;
        } catch (e: any) {
            cfg.status = "error";
            cfg.lastError = e.message;
            this.save();
            console.warn(`[clockworkWasher] ${cfg.characterName} washClockwork error: ${e.message}`);
            return;
        }

        if (!res.confirmed) {
            // 发了但没等到 511132 回包(服务端慢/掉包)。算洗了一次,下一轮继续,不据此判停。
            cfg.washCount += 1;
            cfg.status = "running";
            cfg.lastError = "上次未收到回包,继续";
            cfg.lastWashAt = Date.now();
            this.save();
            return;
        }
        if (typeof res.resultCode === "number" && res.resultCode !== 0) {
            cfg.enabled = false;
            cfg.status = "error";
            cfg.lastError = `服务端拒绝洗发条 (code ${res.resultCode})`;
            this.save();
            return;
        }

        cfg.washCount += 1;
        cfg.lastGrade = res.grade ?? 0;
        cfg.lastAttrs = Array.isArray(res.attrs) ? res.attrs : [];
        cfg.lastError = "";
        cfg.lastWashAt = Date.now();

        const state: QueryResult = { valid: true, grade: cfg.lastGrade, attrs: cfg.lastAttrs };
        if (conditionMet(cfg, state)) {
            cfg.enabled = false;
            cfg.status = "done";
            this.save();
            console.log(`[clockworkWasher] ${cfg.characterName} 条件达成,停止。grade=${cfg.lastGrade} washes=${cfg.washCount}`);
            return;
        }
        if (cfg.maxWashes > 0 && cfg.washCount >= cfg.maxWashes) {
            cfg.enabled = false;
            cfg.status = "reached-max";
            this.save();
            console.log(`[clockworkWasher] ${cfg.characterName} 达到上限 ${cfg.maxWashes} 次,停止。`);
            return;
        }
        cfg.status = "running";
        this.save();
    }

    // ---------------- HTTP 用的 CRUD ----------------

    listConfigs(): ClockworkWasherConfig[] {
        return this.configs;
    }

    getConfig(characterName: string): ClockworkWasherConfig | undefined {
        return this.configs.find((c) => c.characterName === characterName);
    }

    setConfig(characterName: string, body: Partial<ClockworkWasherConfig>): ClockworkWasherConfig {
        const existing = this.configs.find((c) => c.characterName === characterName);
        const merged = normalizeConfig({ ...(existing ?? {}), ...body, characterName });
        // 重新启用时清掉上一次的终态,从头跑。
        if (merged.enabled && (!existing || !existing.enabled)) {
            merged.washCount = 0;
            merged.status = "running";
            merged.lastError = "";
            this.nextPollAt.delete(characterName);
        }
        if (existing) {
            this.configs[this.configs.indexOf(existing)] = merged;
        } else {
            this.configs.push(merged);
        }
        this.save();
        return merged;
    }

    deleteConfig(characterName: string): boolean {
        const before = this.configs.length;
        this.configs = this.configs.filter((c) => c.characterName !== characterName);
        this.nextPollAt.delete(characterName);
        if (this.configs.length !== before) {
            this.save();
            return true;
        }
        return false;
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
    }
}

// 在「用户目标等级」下取某数值表的上限。targetGrade>0 时按目标等级判(用户冲的就是这个等级,
// 比如 XG 的 减伤 上限是 3 而不是 SG 的 4);该等级该属性还没解锁(查到 0)才回退到发条最高等级
// (等级是棘轮,会一路冲到最高,所以未解锁属性的真实上限在最高等级)。
function ceilingAt(
    table: Record<number, number[]>,
    id: number,
    targetGrade: number,
    springMax: number
): { val: number; grade: number } {
    const eff = targetGrade > 0 ? Math.min(targetGrade, springMax) : springMax;
    const arr = table[id];
    if (!arr) return { val: 0, grade: eff };
    let g = eff;
    let val = arr[g - 1] ?? 0;
    if (val === 0) {
        g = springMax;
        val = arr[springMax - 1] ?? 0;
    }
    return { val, grade: g };
}

// 配置是否「永远洗不出来」。返回原因字符串,可行返回 null。
function infeasibleReason(cfg: ClockworkWasherConfig): string | null {
    const max = SPRING_MAX_GRADE[cfg.springType] ?? 5;
    if (cfg.targetGrade > max) {
        const sn = ['實習生', '高手', '武爾坎努斯'][cfg.springType] ?? '该';
        return `${sn}的發條最高只能洗到 ${GRADE_NAMES[max]},目标等级 ${GRADE_NAMES[cfg.targetGrade] ?? cfg.targetGrade} 永远洗不出来`;
    }
    for (const r of cfg.requiredAttrs ?? []) {
        const mg = ATTR_MIN_GRADE[r.id];
        if (mg && mg > max) {
            return `需要属性 #${r.id} 要 ${GRADE_NAMES[mg]}+ 等级才会出现,但该发条最高 ${GRADE_NAMES[max]}`;
        }
        if (r.id in COMP_M_MAX) {
            const m = ceilingAt(COMP_M_MAX, r.id, cfg.targetGrade, max);
            if (m.val > 0 && r.minValue > m.val) {
                return `属性 #${r.id} 每级点数最高 +${m.val}(${GRADE_NAMES[m.grade]}),要求 +${r.minValue} 洗不出来`;
            }
            const n = ceilingAt(COMP_N_MIN, r.id, cfg.targetGrade, max);
            if (r.maxN && r.maxN > 0 && n.val > 0 && r.maxN < n.val) {
                return `属性 #${r.id} 最小等级间隔为 每${n.val}级(${GRADE_NAMES[n.grade]}),要求 ≤每${r.maxN}级 洗不出来`;
            }
        } else if (r.id in ATTR_VALUE_MAX) {
            const v = ceilingAt(ATTR_VALUE_MAX, r.id, cfg.targetGrade, max);
            if (v.val > 0 && r.minValue > v.val) {
                return `属性 #${r.id} 数值最高 ${v.val}(${GRADE_NAMES[v.grade]}),要求 ≥${r.minValue} 洗不出来`;
            }
        }
    }
    return null;
}

// 单条已洗属性 a 是否满足某条需求 req。
function attrSatisfies(a: SpringAttrView, req: RequiredAttr): boolean {
    if (!a || a.id !== req.id) return false;
    if (a.composite) {
        // 复合: 增加量 M>=minValue 且 每N等級 N<=maxN(N 越小越好)。
        const m = a.m ?? 0;
        const n = a.n ?? 0;
        if (m < req.minValue) return false;
        if (req.maxN && req.maxN > 0 && n > req.maxN) return false;
        return true;
    }
    return a.value >= req.minValue;
}

function conditionMet(cfg: ClockworkWasherConfig, state: QueryResult): boolean {
    const gradeMet = cfg.targetGrade <= 0 || state.grade >= cfg.targetGrade;
    let attrsMet = true;
    const reqs = Array.isArray(cfg.requiredAttrs) ? cfg.requiredAttrs : [];
    if (reqs.length > 0) {
        const attrs = (state.attrs || []).filter((a) => a && a.id);
        if (cfg.minMatchCount > 0) {
            // 发条属性可重复:数「3 条里有几条命中了需求(任一)」,重复的(如两条魔法力)
            // 也分别计数。命中 minMatchCount 条即满足。
            let hits = 0;
            for (const a of attrs) {
                if (reqs.some((req) => attrSatisfies(a, req))) hits += 1;
            }
            attrsMet = hits >= cfg.minMatchCount;
        } else {
            // 0 = 每条需求都至少出现一次。
            attrsMet = reqs.every((req) => attrs.some((a) => attrSatisfies(a, req)));
        }
    }
    return gradeMet && attrsMet;
}

function normalizeConfig(c: Partial<ClockworkWasherConfig>): ClockworkWasherConfig {
    const reqs = Array.isArray(c.requiredAttrs)
        ? c.requiredAttrs
              .map((r) => ({
                  id: Number(r.id) || 0,
                  minValue: Number(r.minValue) || 0,
                  maxN: Math.max(0, Number(r.maxN ?? 0) || 0),
              }))
              .filter((r) => r.id > 0)
        : [];
    return {
        characterName: String(c.characterName ?? ""),
        enabled: c.enabled ?? false,
        slotIndex: Number(c.slotIndex ?? 0) || 0,
        itemName: typeof c.itemName === "string" ? c.itemName : "",
        springType: clampInt(Number(c.springType ?? 0), 0, 2),
        targetGrade: clampInt(Number(c.targetGrade ?? 0), 0, 5),
        requiredAttrs: reqs,
        minMatchCount: Math.max(0, Number(c.minMatchCount ?? 0) || 0),
        pollMs: Math.max(MIN_POLL_MS, Number(c.pollMs ?? DEFAULT_POLL_MS) || DEFAULT_POLL_MS),
        maxWashes: Math.max(0, Number(c.maxWashes ?? 0) || 0),
        washCount: Math.max(0, Number(c.washCount ?? 0) || 0),
        status: typeof c.status === "string" ? c.status : "idle",
        lastGrade: Number(c.lastGrade ?? 0) || 0,
        lastAttrs: Array.isArray(c.lastAttrs) ? c.lastAttrs : [],
        lastError: typeof c.lastError === "string" ? c.lastError : "",
        lastWashAt: Number(c.lastWashAt ?? 0) || 0,
    };
}

function clampInt(v: number, lo: number, hi: number): number {
    if (!Number.isFinite(v)) return lo;
    return Math.max(lo, Math.min(hi, Math.trunc(v)));
}
