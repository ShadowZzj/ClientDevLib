import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// buffKeeper:轮询每个在线角色的 buff 快照,某条规则要求的 buff 缺失就发 castSkill 补上。
// 规则按角色分组,每条 { buffId 或 buffName, skillId, targetId, ... }。匹配优先 buffId,
// buffId<=0 时退回 buffName 精确匹配(UTF-8)。
//
// 跟 cashScheduler 一样:DLL 不在线 / queryBuffs 失败按"不确定"跳过本轮,不误放。

export interface BuffRule {
    id: string;             // 规则唯一 id(前端生成,稳定 key)
    enabled: boolean;
    buffId: number;         // >0 优先按 id 匹配;<=0 用 buffName
    buffName: string;       // buffId<=0 时按名字精确匹配
    skillId: number;        // 缺 buff 时释放的技能
    targetId: number;       // 0 = 自身 / 无目标
    minRecastMs: number;    // 同规则两次施放最小间隔,防冷却期间刷包
    lastCast: number;       // epoch ms,0 = 从未(运行时态,持久化也存)
}

export interface BuffKeeperCharacterConfig {
    characterName: string;
    enabled: boolean;       // 角色级总开关
    pollMs: number;         // 该角色轮询间隔
    rules: BuffRule[];
}

interface PersistShape {
    characters: BuffKeeperCharacterConfig[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "buff_keeper.json"
);

const DEFAULT_POLL_MS = 4000;
const MIN_POLL_MS = 1500;
const DEFAULT_MIN_RECAST_MS = 3000;

interface BuffSnapshotItem {
    buffId: number;
    name: string;
    kind: string;
    remainingMs: number;
    skillId: number;
}

export class BuffKeeper {
    private configs: BuffKeeperCharacterConfig[] = [];
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    // 每角色下一次允许轮询的时间戳,实现 per-character pollMs(全局 tick 跑得更密)。
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
                this.configs = Array.isArray(parsed.characters) ? parsed.characters : [];
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
        // 全局 tick 1s 一跳,真正的轮询频率由每角色 pollMs + nextPollAt 控制。
        this.timer = setInterval(() => this.tick(), 1000);
    }

    private async tick(): Promise<void> {
        const now = Date.now();
        const nameToPid = new Map<string, number>();
        for (const inst of this.registry.list()) {
            if (inst.characterName) nameToPid.set(inst.characterName, inst.pid);
        }

        for (const cfg of this.configs) {
            if (!cfg.enabled) continue;
            if (!cfg.rules.some((r) => r.enabled)) continue;

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
        let buffs: BuffSnapshotItem[];
        try {
            const r = await this.registry.sendCommand(pid, "queryBuffs", { kind: -1 }, 3000);
            if (!r.ok) {
                console.warn(`[buffKeeper] queryBuffs failed for ${cfg.characterName}: ${r.detail} — skip`);
                return;
            }
            buffs = JSON.parse(r.detail || "[]") as BuffSnapshotItem[];
        } catch (e: any) {
            console.warn(`[buffKeeper] queryBuffs error ${cfg.characterName}: ${e.message} — skip`);
            return;
        }

        const hasById = new Set<number>();
        const hasByName = new Set<string>();
        for (const b of buffs) {
            if (typeof b.buffId === "number") hasById.add(b.buffId);
            if (typeof b.name === "string" && b.name) hasByName.add(b.name);
        }

        const now = Date.now();
        let dirty = false;
        for (const rule of cfg.rules) {
            if (!rule.enabled) continue;
            if (!rule.skillId || rule.skillId <= 0) continue;

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
                dirty = true;
                const tag = rule.buffId > 0 ? `buffId=${rule.buffId}` : `name="${rule.buffName}"`;
                if (r.ok) {
                    console.log(`[buffKeeper] ${cfg.characterName} ${tag} missing → cast skill=${rule.skillId} ok`);
                } else {
                    console.log(`[buffKeeper] ${cfg.characterName} ${tag} cast skill=${rule.skillId} rejected: ${r.detail}`);
                }
            } catch (e: any) {
                console.warn(`[buffKeeper] castSkill error ${cfg.characterName} skill=${rule.skillId}: ${e.message}`);
            }
        }
        if (dirty) this.save();
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
        const cfg: BuffKeeperCharacterConfig = {
            characterName,
            enabled: body.enabled ?? existing?.enabled ?? false,
            pollMs: Math.max(MIN_POLL_MS, Number(body.pollMs ?? existing?.pollMs ?? DEFAULT_POLL_MS)),
            rules,
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
    };
}
