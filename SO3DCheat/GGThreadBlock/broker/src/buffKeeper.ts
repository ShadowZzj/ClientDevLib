import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// buffKeeper:轮询每个在线角色的 buff 快照,某条规则要求的 buff 缺失就发 castSkill 补上。
// 规则按角色分组,每条 { buffId 或 buffName, skillId, targetId, ... }。匹配优先 buffId,
// buffId<=0 时退回 buffName 精确匹配(UTF-8)。
//
// 两类规则:
//   - rules:守护「自己」的 buff(queryBuffs 查自身,castSkill targetId=0)。
//   - partyRules:守护「组队成员」的 buff(queryParty 拿每人 buff + nearby,缺 buff 的
//     近身成员就 castSkill targetId=该成员 userId)。只对 nearby=true 的成员放 —— 远处
//     队友引擎压根加不到。每条 partyRule 整体一个节流:一个 eligible 轮次只补一个成员
//     (skill 本身有 CD,一轮多放也会被引擎挡),下一轮再补下一个。
//
// 跟 cashScheduler 一样:DLL 不在线 / queryBuffs|queryParty 失败按"不确定"跳过本轮,不误放。

export interface BuffRule {
    id: string;             // 规则唯一 id(前端生成,稳定 key)
    enabled: boolean;
    buffId: number;         // >0 优先按 id 匹配;<=0 用 buffName
    buffName: string;       // buffId<=0 时按名字精确匹配
    skillId: number;        // 缺 buff 时释放的技能
    targetId: number;       // 0 = 自身 / 无目标
    minRecastMs: number;    // 同规则两次施放最小间隔,防冷却期间刷包
    lastCast: number;       // epoch ms,0 = 从未(运行时态,持久化也存)
    requireMonsterHp: boolean; // true=受角色级「怪物血量闸门」控制:周围没 HP>阈值 的怪就本轮跳过
}

// 组队成员 buff 规则。target 在运行时按缺 buff 的成员 userId 决定,所以没有 targetId 字段。
export interface PartyBuffRule {
    id: string;
    enabled: boolean;
    buffId: number;         // >0 优先按 id 匹配;<=0 用 buffName
    buffName: string;
    skillId: number;        // 缺 buff 时对该成员释放的技能
    includeSelf: boolean;   // true 时自己缺 buff 也补(targetId=0);false 只管队友
    minRecastMs: number;    // 整条规则一个节流(不分成员)
    lastCast: number;       // epoch ms,0 = 从未
    // 作用人物(门禁):非空时,只有列表里至少一个角色名在视野(nearby 队友)里,本规则
    // 才整体生效 —— 生效后照常给所有缺 buff 的近身成员补(不限于列表里的人)。列表里
    // 一个都不在视野则整条规则本轮跳过。空列表=无门禁,始终生效(旧行为)。按角色名匹配。
    triggerNames: string[];
    requireMonsterHp: boolean; // true=受角色级「怪物血量闸门」控制;与 triggerNames 是 AND 关系
}

export interface BuffKeeperCharacterConfig {
    characterName: string;
    enabled: boolean;       // 角色级总开关
    pollMs: number;         // 该角色轮询间隔
    rules: BuffRule[];
    partyRules: PartyBuffRule[];
    // 怪物血量闸门(角色级共用):周围存在 HP>monsterHpThreshold 的「怪物」(attackable=true,
    // NPC/宠物/树不算)时闸门开,勾了 requireMonsterHp 的规则才放行补 buff;否则本轮跳过。没勾的规则
    // 不受影响。threshold<=0 = 只要有活怪(HP>0)就算闸门开。
    monsterHpThreshold: number;
    monsterScanDistance: number; // getNearbyNpcs 扫描距离(格);0 = AOI 内全部
}

interface PersistShape {
    characters: BuffKeeperCharacterConfig[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "buff_keeper.json"
);

const DEFAULT_POLL_MS = 4000;
// 组队 buff 守护是「一轮一个 cast,引擎施完立刻补下一个」的节奏,所以轮询要密。
// 引擎一次只施一个法(前摇/全局CD),broker 发再快也只受理一个,密轮询只是让
// 「引擎空闲 → 下一个 cast」的间隙更短。
const MIN_POLL_MS = 250;
const DEFAULT_MIN_RECAST_MS = 3000;

interface BuffSnapshotItem {
    buffId: number;
    name: string;
    kind: string;
    remainingMs: number;
    skillId: number;
}

interface PartyMemberSnapshot {
    index: number;
    name: string;
    userId: number;
    isSelf: boolean;
    online: boolean;
    hp: number;             // 当前血量;0=死亡,-1=状态读取失败(未知)
    nearby: boolean;
    distance: number;
    buffs: BuffSnapshotItem[];
}

interface PartySnapshot {
    inParty: boolean;
    role: number;
    selfIndex: number;
    members: PartyMemberSnapshot[];
}

// getNearbyNpcs 返回的单条(只取闸门要用的字段)。buffGateMonster 是 DLL
// 专门为 BuffKeeper 计算的保守普通敌怪语义,与通用 attackable 分离。
interface NearbyNpcItem {
    hp: number;
    buffGateMonster?: boolean;
}

export function hasHighHpBuffGateMonster(items: NearbyNpcItem[], threshold: number): boolean {
    return items.some((n) =>
        n.buffGateMonster === true && typeof n.hp === "number" && n.hp > threshold);
}

export class BuffKeeper {
    private configs: BuffKeeperCharacterConfig[] = [];
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    // 每角色下一次允许轮询的时间戳,实现 per-character pollMs(全局 tick 跑得更密)。
    private nextPollAt = new Map<string, number>();
    private inFlight = new Set<string>();
    // 组队规则节流:key = `${ruleId}:${memberUserId}`,运行时态(队友会变,不持久化)。
    private partyCastAt = new Map<string, number>();
    // 旧 DLL 不返回 buffGateMonster;每 PID 只警告一次并保持闸门关闭。
    private missingBuffGateFieldWarned = new Set<number>();

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
                // 旧配置没有 partyRules / rules / 怪物闸门字段,补默认值,后续访问不用每处判空。
                this.configs = list.map((c) => ({
                    ...c,
                    rules: Array.isArray(c.rules) ? c.rules : [],
                    partyRules: Array.isArray(c.partyRules) ? c.partyRules : [],
                    monsterHpThreshold: Number(c.monsterHpThreshold ?? 0) || 0,
                    monsterScanDistance: Number(c.monsterScanDistance ?? 0) || 0,
                }));
                console.log(`[buffKeeper] loaded ${this.configs.length} character configs`);
            }
        } catch (e: any) {
            console.warn(`[buffKeeper] load failed: ${e.message}`);
            this.configs = [];
        }
    }

    private save(): void {
        try {
            const shape: PersistShape = { characters: this.configs };
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(shape, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[buffKeeper] save failed: ${e.message}`);
        }
    }

    private start(): void {
        // tick 跑得密(200ms),真正的轮询频率由每角色 pollMs + nextPollAt 控制。
        this.timer = setInterval(() => this.tick(), 200);
    }

    private async tick(): Promise<void> {
        const now = Date.now();
        const nameToPid = new Map<string, number>();
        for (const inst of this.registry.list()) {
            if (inst.characterName) nameToPid.set(inst.characterName, inst.pid);
        }

        for (const cfg of this.configs) {
            if (!cfg.enabled) continue;
            const hasSelfRule  = cfg.rules.some((r) => r.enabled);
            const hasPartyRule = (cfg.partyRules ?? []).some((r) => r.enabled);
            if (!hasSelfRule && !hasPartyRule) continue;

            const pid = nameToPid.get(cfg.characterName);
            if (pid === undefined) continue; // 角色不在线

            // 同角色上一轮还没回来就别再发,避免命令堆积。
            if (this.inFlight.has(cfg.characterName)) continue;

            const due = this.nextPollAt.get(cfg.characterName) ?? 0;
            if (now < due) continue;
            const pollMs = Math.max(MIN_POLL_MS, cfg.pollMs || DEFAULT_POLL_MS);
            this.nextPollAt.set(cfg.characterName, now + pollMs);

            this.inFlight.add(cfg.characterName);
            this.runCharacter(cfg, pid).finally(() => {
                this.inFlight.delete(cfg.characterName);
            });
        }
    }

    private async runCharacter(cfg: BuffKeeperCharacterConfig, pid: number): Promise<void> {
        // 怪物血量闸门:任一启用规则勾了 requireMonsterHp 就先查一次周围怪物,算出闸门开没开,
        // 传给两个 runner 复用(一轮只查一次)。查询失败 → 闸门按「关」处理,gated 规则本轮跳过;
        // 没勾的规则不受影响。没有任何 gated 规则时压根不查,省一次往返。
        let monsterGateOpen = false;
        const needMonsterGate =
            cfg.rules.some((r) => r.enabled && r.requireMonsterHp) ||
            (cfg.partyRules ?? []).some((r) => r.enabled && r.requireMonsterHp);
        if (needMonsterGate) {
            monsterGateOpen = await this.checkMonsterGate(cfg, pid);
        }

        // 引擎一次只施一个法,自身 + 组队共用这一个施法名额:本轮发了一个就停,
        // 下一轮(MIN_POLL_MS 后)引擎空了再发下一个。自身优先于组队。
        let dirty = false;
        let cast = false;
        if (cfg.rules.some((r) => r.enabled)) {
            const r = await this.runSelfRules(cfg, pid, monsterGateOpen);
            dirty = r.dirty;
            cast = r.cast;
        }
        if (!cast && (cfg.partyRules ?? []).some((r) => r.enabled)) {
            await this.runPartyRules(cfg, pid, monsterGateOpen);
        }
        if (dirty) this.save();
    }

    // 周围是否存在 HP>阈值 的普通敌怪。走 getNearbyNpcs 的 buffGateMonster 字段,
    // 不复用 attackable,避免机关/任务目标改变 BuffKeeper 闸门。
    // 只读命令,失败/异常返回 false(闸门关)—— 跟 queryBuffs/queryParty 一样,拿不准就不放行,不误放。
    private async checkMonsterGate(cfg: BuffKeeperCharacterConfig, pid: number): Promise<boolean> {
        const threshold = cfg.monsterHpThreshold > 0 ? cfg.monsterHpThreshold : 0;
        const maxDistance = cfg.monsterScanDistance > 0 ? cfg.monsterScanDistance : 0;
        try {
            const r = await this.registry.sendCommand(pid, "getNearbyNpcs", { maxDistance }, 3000);
            if (!r.ok) {
                console.warn(`[buffKeeper] getNearbyNpcs failed for ${cfg.characterName}: ${r.detail} — gate closed`);
                return false;
            }
            const parsed = JSON.parse(r.detail || "[]");
            if (!Array.isArray(parsed)) throw new Error("getNearbyNpcs returned non-array detail");
            const arr = parsed as NearbyNpcItem[];
            if (arr.some((n) => typeof n.buffGateMonster !== "boolean")) {
                if (!this.missingBuffGateFieldWarned.has(pid)) {
                    this.missingBuffGateFieldWarned.add(pid);
                    console.warn(
                        `[buffKeeper] getNearbyNpcs missing buffGateMonster for ${cfg.characterName}; ` +
                        "update GGThreadBlock.dll to >=0.14 and restart the game client — gate closed");
                }
                return false;
            }
            this.missingBuffGateFieldWarned.delete(pid);
            return hasHighHpBuffGateMonster(arr, threshold);
        } catch (e: any) {
            console.warn(`[buffKeeper] getNearbyNpcs error ${cfg.characterName}: ${e.message} — gate closed`);
            return false;
        }
    }

    // 守护自身 buff:queryBuffs 查自己,缺的就 castSkill targetId=0(或规则指定 targetId)。
    // 一轮最多发一个(引擎一次一个法),返回 { dirty: 是否动了持久化字段, cast: 是否用掉施法名额 }。
    private async runSelfRules(cfg: BuffKeeperCharacterConfig, pid: number, monsterGateOpen: boolean): Promise<{ dirty: boolean; cast: boolean }> {
        let buffs: BuffSnapshotItem[];
        try {
            const r = await this.registry.sendCommand(pid, "queryBuffs", { kind: -1 }, 3000);
            if (!r.ok) {
                console.warn(`[buffKeeper] queryBuffs failed for ${cfg.characterName}: ${r.detail} — skip`);
                return { dirty: false, cast: false };
            }
            buffs = JSON.parse(r.detail || "[]") as BuffSnapshotItem[];
        } catch (e: any) {
            console.warn(`[buffKeeper] queryBuffs error ${cfg.characterName}: ${e.message} — skip`);
            return { dirty: false, cast: false };
        }

        const hasById = new Set<number>();
        const hasByName = new Set<string>();
        for (const b of buffs) {
            if (typeof b.buffId === "number") hasById.add(b.buffId);
            if (typeof b.name === "string" && b.name) hasByName.add(b.name);
        }

        const now = Date.now();
        for (const rule of cfg.rules) {
            if (!rule.enabled) continue;
            if (!rule.skillId || rule.skillId <= 0) continue;
            if (rule.requireMonsterHp && !monsterGateOpen) continue; // 怪物血量闸门未开

            const present =
                rule.buffId > 0 ? hasById.has(rule.buffId) : hasByName.has(rule.buffName);
            if (present) continue;

            const minRecast = rule.minRecastMs > 0 ? rule.minRecastMs : DEFAULT_MIN_RECAST_MS;
            if (now - rule.lastCast < minRecast) continue;

            try {
                const r = await this.registry.sendCommand(
                    pid,
                    "castSkill",
                    { skillId: rule.skillId, targetId: rule.targetId || 0, checkCanCast: true },
                    5000
                );
                // 不管引擎接不接(冷却 / 没学会都会 ok=false),都推进 lastCast,
                // 这样冷却中不会每轮狂发;minRecast 应配 >= 技能冷却。
                rule.lastCast = now;
                const tag = rule.buffId > 0 ? `buffId=${rule.buffId}` : `name="${rule.buffName}"`;
                if (r.ok) {
                    console.log(`[buffKeeper] ${cfg.characterName} ${tag} missing → cast skill=${rule.skillId} ok`);
                } else {
                    console.log(`[buffKeeper] ${cfg.characterName} ${tag} cast skill=${rule.skillId} rejected: ${r.detail}`);
                }
            } catch (e: any) {
                console.warn(`[buffKeeper] castSkill error ${cfg.characterName} skill=${rule.skillId}: ${e.message}`);
            }
            // 一轮一个:自身补了一个就走,把施法名额让出去,下一轮再继续。
            return { dirty: true, cast: true };
        }
        return { dirty: false, cast: false };
    }

    // 守护组队成员 buff:queryParty 拿每人 buff + nearby,对缺 buff 的近身成员 castSkill
    // targetId=该成员 userId。节流按 (规则×成员) 独立(partyCastAt)。
    // 关键:引擎一次只施一个法,所以本轮**最多发一个 cast 就返回**,靠密轮询(MIN_POLL_MS)
    // 在引擎施完后立刻补下一个 —— 一轮狂发 9 个只会被引擎吞掉 8 个、白刷日志。
    // 返回 true 表示本轮已经用掉施法名额(调用方据此不再发自身/其它 cast)。
    private async runPartyRules(cfg: BuffKeeperCharacterConfig, pid: number, monsterGateOpen: boolean): Promise<boolean> {
        let snap: PartySnapshot;
        try {
            const r = await this.registry.sendCommand(pid, "queryParty", { buffs: true }, 3000);
            if (!r.ok) {
                console.warn(`[buffKeeper] queryParty failed for ${cfg.characterName}: ${r.detail} — skip`);
                return false;
            }
            snap = JSON.parse(r.detail || "{}") as PartySnapshot;
        } catch (e: any) {
            console.warn(`[buffKeeper] queryParty error ${cfg.characterName}: ${e.message} — skip`);
            return false;
        }
        if (!snap.inParty || !Array.isArray(snap.members)) return false;

        const now = Date.now();
        for (const rule of cfg.partyRules) {
            if (!rule.enabled) continue;
            if (!rule.skillId || rule.skillId <= 0) continue;
            if (rule.requireMonsterHp && !monsterGateOpen) continue; // 怪物血量闸门未开
            const minRecast = rule.minRecastMs > 0 ? rule.minRecastMs : DEFAULT_MIN_RECAST_MS;

            // 门禁:配了「作用人物」就得列表里至少一个角色名在视野(nearby)里才放;
            // 一个都不在视野则整条规则本轮跳过。空列表=无门禁。
            const triggers = rule.triggerNames ?? [];
            if (triggers.length > 0) {
                const gateOpen = snap.members.some((m) => m.nearby && triggers.includes(m.name));
                if (!gateOpen) continue;
            }

            for (const m of snap.members) {
                if (!m.nearby) continue;                          // 不在视野,引擎加不到
                if (m.hp === 0) continue;                         // 死人补不了 buff;hp=-1 是读取失败,按未知放行
                if (m.isSelf && !rule.includeSelf) continue;
                if (!m.userId || m.userId === 0xFFFFFFFF) continue;

                const present = rule.buffId > 0
                    ? m.buffs.some((b) => b.buffId === rule.buffId)
                    : m.buffs.some((b) => b.name === rule.buffName);
                if (present) continue;

                // 节流 key 按 (规则, 成员 userId) —— 运行时态,不持久化(队友会变)。
                const key = `${rule.id}:${m.userId}`;
                if (now - (this.partyCastAt.get(key) ?? 0) < minRecast) continue;
                this.partyCastAt.set(key, now);

                const tag = rule.buffId > 0 ? `buffId=${rule.buffId}` : `name="${rule.buffName}"`;
                try {
                    const r = await this.registry.sendCommand(
                        pid,
                        "castSkill",
                        { skillId: rule.skillId, targetId: m.isSelf ? 0 : m.userId, checkCanCast: true },
                        5000
                    );
                    if (r.ok) {
                        console.log(`[buffKeeper] ${cfg.characterName} party ${tag} missing on ${m.name} → cast skill=${rule.skillId} ok`);
                    } else {
                        console.log(`[buffKeeper] ${cfg.characterName} party ${tag} on ${m.name} cast skill=${rule.skillId} rejected: ${r.detail}`);
                    }
                } catch (e: any) {
                    console.warn(`[buffKeeper] party castSkill error ${cfg.characterName} skill=${rule.skillId}: ${e.message}`);
                }
                // 一轮一个:发出去就走,下一轮(MIN_POLL_MS 后)引擎空了再补下一个。
                return true;
            }
        }
        return false;
    }

    // ---------------- HTTP 用的 CRUD ----------------

    listConfigs(): BuffKeeperCharacterConfig[] {
        return this.configs;
    }

    getConfig(characterName: string): BuffKeeperCharacterConfig | undefined {
        return this.configs.find((c) => c.characterName === characterName);
    }

    setConfig(characterName: string, body: Partial<BuffKeeperCharacterConfig>): BuffKeeperCharacterConfig {
        const existing = this.configs.find((c) => c.characterName === characterName);
        const rules = Array.isArray(body.rules) ? body.rules.map((r) => normalizeRule(r)) : (existing?.rules ?? []);
        const partyRules = Array.isArray(body.partyRules)
            ? body.partyRules.map((r) => normalizePartyRule(r))
            : (existing?.partyRules ?? []);
        const cfg: BuffKeeperCharacterConfig = {
            characterName,
            enabled: body.enabled ?? existing?.enabled ?? false,
            pollMs: Math.max(MIN_POLL_MS, Number(body.pollMs ?? existing?.pollMs ?? DEFAULT_POLL_MS)),
            rules,
            partyRules,
            monsterHpThreshold: Math.max(0, Number(body.monsterHpThreshold ?? existing?.monsterHpThreshold ?? 0)) || 0,
            monsterScanDistance: Math.max(0, Number(body.monsterScanDistance ?? existing?.monsterScanDistance ?? 0)) || 0,
        };
        if (existing) {
            this.configs[this.configs.indexOf(existing)] = cfg;
        } else {
            this.configs.push(cfg);
        }
        this.save();
        return cfg;
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

function normalizeRule(r: Partial<BuffRule>): BuffRule {
    return {
        id: typeof r.id === "string" && r.id ? r.id : Math.random().toString(36).slice(2),
        enabled: r.enabled ?? true,
        buffId: Number(r.buffId ?? 0) || 0,
        buffName: typeof r.buffName === "string" ? r.buffName : "",
        skillId: Number(r.skillId ?? 0) || 0,
        targetId: Number(r.targetId ?? 0) || 0,
        minRecastMs: Number(r.minRecastMs ?? DEFAULT_MIN_RECAST_MS) || DEFAULT_MIN_RECAST_MS,
        lastCast: Number(r.lastCast ?? 0) || 0,
        requireMonsterHp: r.requireMonsterHp ?? false,
    };
}

function normalizePartyRule(r: Partial<PartyBuffRule>): PartyBuffRule {
    return {
        id: typeof r.id === "string" && r.id ? r.id : Math.random().toString(36).slice(2),
        enabled: r.enabled ?? true,
        buffId: Number(r.buffId ?? 0) || 0,
        buffName: typeof r.buffName === "string" ? r.buffName : "",
        skillId: Number(r.skillId ?? 0) || 0,
        includeSelf: r.includeSelf ?? false,
        minRecastMs: Number(r.minRecastMs ?? DEFAULT_MIN_RECAST_MS) || DEFAULT_MIN_RECAST_MS,
        lastCast: Number(r.lastCast ?? 0) || 0,
        triggerNames: Array.isArray(r.triggerNames)
            ? r.triggerNames.filter((n) => typeof n === "string" && n)
            : [],
        requireMonsterHp: r.requireMonsterHp ?? false,
    };
}
