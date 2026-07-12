import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// autoCompose:自动合成技能宝石(三合一)。每个角色一条任务,轮询:
//   listComposeGems(读背包,按等级键分组) → 对所有「等级低于目标品级」的组,凑 3 个同组
//   宝石发一次 composeGems(411606) → 等一拍让回包落地 → 下一轮重新读背包。
//
// 三合一规则(抓包+IDA 实证):3 个同等级宝石(可同 itemId 同堆叠,也可同等级不同 itemId
// 混合 —— 引擎自身的 Compose_AutoFillByGrade 就跨 itemId 同级混)产出 1 个高一级宝石。
// DLL 侧 listComposeGems 已按「引擎等级键 gradeKey」分组(GetTableElem(55, itemTable+0x454)
// 的 elem+0x14),同键即可互合。
//
// 目标品级 = 用户选的等级(G/DG/XG/...)。「逐级合成直到目标」:把所有 rank < 目标 rank 的
// 组一路往上合,直到没有任何低于目标的组还能凑满 3 个为止。
//
// 等级 rank 来源:优先解析宝石名里的品级标记(N/G/DG/XG/SG),解析不出再退回按 gradeKey
// 升序临时定级。rank: 0=N 1=G 2=DG 3=XG 4=SG(与发条/洗发条同口径)。
//
// DLL 不在线 / query 失败按「不确定」跳过本轮,不误合。

// 品级链:N→G→DG→XG。本游戏技能宝石没有 SG,最高就是 XG(rank 3)。
const GRADE_NAMES = ["N", "G", "DG", "XG"];
const MAX_RANK = GRADE_NAMES.length - 1; // 3 = XG

export interface ComposeGroupView {
    gradeKey: number;
    sampleItemId: number;
    sampleName: string;
    totalCount: number;
    gradeRank: number;   // 0..4(解析自名字或 gradeKey 排序);-1=未知
    gradeLabel: string;  // 展示用("N"/"G"/...;未知给 "?")
    wireSlots: number[]; // 每件一个 slotIndex+13
}

export interface AutoComposeConfig {
    characterName: string;
    enabled: boolean;
    targetRank: number;  // 目标品级 rank(1=G 2=DG 3=XG)。把所有 rank<targetRank 的合上来。
    intervalMs: number;  // 两次合成之间的间隔(给回包落地留时间)
    maxBatches: number;  // 0=无上限;否则发够这么多次合成就停(防失控)

    // 运行时态(持久化,重启后能看到上次结果)
    batchCount: number;
    status: string;      // idle/running/done/no-gems/error/reached-max/offline
    lastError: string;
    lastRunAt: number;   // epoch ms
    lastGroups: ComposeGroupView[]; // 最近一次读到的分组快照(给 UI 显示)
}

interface PersistShape {
    characters: AutoComposeConfig[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "auto_compose.json"
);

const DEFAULT_INTERVAL_MS = 250;
const MIN_INTERVAL_MS = 80;
const TICK_MS = 150;

// DLL 原始分组(listComposeGems 返回)。
interface RawGroup {
    gradeKey: number;
    sampleItemId: number;
    sampleName: string;
    totalCount: number;
    wireSlots: number[];
}

// 从宝石名里解析品级标记。实测格式是「点号 + 品级」做后缀,后面可能跟空格/状态括号:
//   "[攻擊] 冷卻時間減少.XG (未開放)" / "[恢復] 技能AP減少.DG" / "...療癒恢復增加.N"
// 也兼容老的括号格式 "...(DG)" / "[XG]..."。DG/XG 这类双字母必须优先于 G 匹配。
// 大小写无关。匹配不到返回 -1。
function parseGradeRankFromName(name: string): number {
    if (!name) return -1;
    const up = name.toUpperCase();
    // 1) 点号后缀:".XG" / ".DG" / ".G" / ".N"(后接边界:空格/全角空格/(/(/[/【/末尾)
    let m = up.match(/\.(XG|DG|SG|G|N)(?=$|[\s　(（\[【])/);
    // 2) 退回括号包裹格式
    if (!m) m = up.match(/[\(\（\[【]\s*(XG|DG|SG|G|N)\s*[\)\）\]】]/);
    const tag = m ? m[1] : null;
    if (tag) {
        // SG 不在本游戏品级链里;若真出现,当作最高级 XG 处理(不再往上合)。
        if (tag === "SG") return MAX_RANK;
        const idx = GRADE_NAMES.indexOf(tag);
        if (idx >= 0) return idx;
    }
    return -1;
}

// 给一批原始分组定 rank:能从名字解析就用名字;整批都解析不出,就按 gradeKey 升序当作
// N、G、DG… 临时排(同一背包内 gradeKey 一定随等级单调)。
function assignRanks(raw: RawGroup[]): ComposeGroupView[] {
    const byName = raw.map((g) => ({ g, rank: parseGradeRankFromName(g.sampleName) }));
    const anyParsed = byName.some((x) => x.rank >= 0);
    let views: ComposeGroupView[];
    if (anyParsed) {
        views = byName.map(({ g, rank }) => toView(g, rank));
    } else {
        // 全部未知:按 gradeKey 升序临时定级 0,1,2,…
        const sorted = [...raw].sort((a, b) => a.gradeKey - b.gradeKey);
        const keyToRank = new Map<number, number>();
        sorted.forEach((g, i) => keyToRank.set(g.gradeKey, Math.min(i, MAX_RANK)));
        views = raw.map((g) => toView(g, keyToRank.get(g.gradeKey) ?? -1));
    }
    return views;
}

function toView(g: RawGroup, rank: number): ComposeGroupView {
    return {
        gradeKey: g.gradeKey,
        sampleItemId: g.sampleItemId,
        sampleName: g.sampleName,
        totalCount: g.totalCount,
        gradeRank: rank,
        gradeLabel: rank >= 0 && rank < GRADE_NAMES.length ? GRADE_NAMES[rank] : "?",
        wireSlots: Array.isArray(g.wireSlots) ? g.wireSlots : [],
    };
}

export class AutoComposeManager {
    private configs: AutoComposeConfig[] = [];
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    private nextRunAt = new Map<string, number>();
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
                console.log(`[autoCompose] loaded ${this.configs.length} configs`);
            }
        } catch (e: any) {
            console.warn(`[autoCompose] load failed: ${e.message}`);
            this.configs = [];
        }
    }

    private save(): void {
        try {
            const shape: PersistShape = { characters: this.configs };
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(shape, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[autoCompose] save failed: ${e.message}`);
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
            if (pid === undefined) {
                if (cfg.status === "running") cfg.status = "offline";
                continue;
            }
            if (this.inFlight.has(cfg.characterName)) continue;

            const due = this.nextRunAt.get(cfg.characterName) ?? 0;
            if (now < due) continue;
            const interval = Math.max(MIN_INTERVAL_MS, cfg.intervalMs);
            this.nextRunAt.set(cfg.characterName, now + interval);

            this.inFlight.add(cfg.characterName);
            this.runOne(cfg, pid).finally(() => {
                this.inFlight.delete(cfg.characterName);
            });
        }
    }

    // 查当前背包分组。DLL 不在线/失败返回 null(本轮跳过)。
    private async queryGroups(pid: number): Promise<ComposeGroupView[] | null> {
        try {
            const r = await this.registry.sendCommand(pid, "listComposeGems", {}, 5000);
            if (!r.ok) return null;
            const raw = JSON.parse(r.detail || "[]") as RawGroup[];
            if (!Array.isArray(raw)) return null;
            return assignRanks(raw);
        } catch {
            return null;
        }
    }

    private async runOne(cfg: AutoComposeConfig, pid: number): Promise<void> {
        const groups = await this.queryGroups(pid);
        if (groups === null) {
            // 不确定:不动 enabled,等下一轮。
            cfg.status = "running";
            cfg.lastError = "查询背包失败,稍后重试";
            this.save();
            return;
        }
        cfg.lastGroups = groups;

        // 目标:把所有 rank < targetRank 且 rank>=0 的组合上来。挑「能凑满 3 个」且 rank 最低的
        // 一组先合(逐级:先把 N 合成 G,再 G 合 DG…)。
        const candidates = groups
            .filter((g) => g.gradeRank >= 0 && g.gradeRank < cfg.targetRank && g.totalCount >= 3)
            .sort((a, b) => a.gradeRank - b.gradeRank);

        if (candidates.length === 0) {
            // 没有任何低于目标的组还能凑 3 个 → 完成(或本来就没料)。
            const anyLowLeftover = groups.some(
                (g) => g.gradeRank >= 0 && g.gradeRank < cfg.targetRank && g.totalCount > 0
            );
            cfg.enabled = false;
            cfg.status = anyLowLeftover ? "done" : "no-gems";
            cfg.lastError = "";
            cfg.lastRunAt = Date.now();
            this.save();
            console.log(`[autoCompose] ${cfg.characterName} 合成完成,status=${cfg.status} batches=${cfg.batchCount}`);
            return;
        }

        const group = candidates[0];
        // 取该组前 3 件的 wire 槽 → 转回 slotIndex(wire-13)发 composeGems(DLL 再 +13)。
        // 这里发 slotIndex,因为 composeGems 的 args.slots 语义是背包 arrayIndex。
        const slots = group.wireSlots.slice(0, 3).map((w) => w - 13);
        if (slots.length !== 3 || slots.some((s) => s < 0)) {
            cfg.status = "error";
            cfg.lastError = "分组槽位异常(wireSlots 不足 3 或越界)";
            this.save();
            return;
        }

        try {
            const r = await this.registry.sendCommand(pid, "composeGems", { slots }, 5000);
            if (!r.ok) {
                cfg.status = "error";
                cfg.lastError = r.detail || "composeGems 被拒绝";
                this.save();
                console.warn(`[autoCompose] ${cfg.characterName} composeGems rejected: ${cfg.lastError}`);
                return;
            }
        } catch (e: any) {
            cfg.status = "error";
            cfg.lastError = e.message;
            this.save();
            return;
        }

        cfg.batchCount += 1;
        cfg.status = "running";
        cfg.lastError = "";
        cfg.lastRunAt = Date.now();

        if (cfg.maxBatches > 0 && cfg.batchCount >= cfg.maxBatches) {
            cfg.enabled = false;
            cfg.status = "reached-max";
            console.log(`[autoCompose] ${cfg.characterName} 达到上限 ${cfg.maxBatches} 次,停止。`);
        }
        this.save();
    }

    // ---------------- HTTP 用的 CRUD ----------------

    listConfigs(): AutoComposeConfig[] {
        return this.configs;
    }

    getConfig(characterName: string): AutoComposeConfig | undefined {
        return this.configs.find((c) => c.characterName === characterName);
    }

    setConfig(characterName: string, body: Partial<AutoComposeConfig>): AutoComposeConfig {
        const existing = this.configs.find((c) => c.characterName === characterName);
        const merged = normalizeConfig({ ...(existing ?? {}), ...body, characterName });
        // 重新启用时清掉上一次的终态,从头跑。
        if (merged.enabled && (!existing || !existing.enabled)) {
            merged.batchCount = 0;
            merged.status = "running";
            merged.lastError = "";
            this.nextRunAt.delete(characterName);
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
        this.nextRunAt.delete(characterName);
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

function clampInt(v: number, lo: number, hi: number): number {
    if (!Number.isFinite(v)) return lo;
    return Math.max(lo, Math.min(hi, Math.trunc(v)));
}

function normalizeConfig(c: Partial<AutoComposeConfig>): AutoComposeConfig {
    return {
        characterName: String(c.characterName ?? ""),
        enabled: c.enabled ?? false,
        targetRank: clampInt(Number(c.targetRank ?? 1), 1, MAX_RANK),
        intervalMs: Math.max(MIN_INTERVAL_MS, Number(c.intervalMs ?? DEFAULT_INTERVAL_MS) || DEFAULT_INTERVAL_MS),
        maxBatches: Math.max(0, Number(c.maxBatches ?? 0) || 0),
        batchCount: Math.max(0, Number(c.batchCount ?? 0) || 0),
        status: typeof c.status === "string" ? c.status : "idle",
        lastError: typeof c.lastError === "string" ? c.lastError : "",
        lastRunAt: Number(c.lastRunAt ?? 0) || 0,
        lastGroups: Array.isArray(c.lastGroups) ? c.lastGroups : [],
    };
}
