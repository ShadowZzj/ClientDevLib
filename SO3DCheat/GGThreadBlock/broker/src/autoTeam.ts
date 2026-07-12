import fs from "fs/promises";
import path from "path";
import { InstanceRegistry } from "./instances";

// autoTeam:自动组队远控。两类一次性动作,broker 不持有状态机:
//   1) fetchPartyList(pid) —— 用一个在线角色本地开窗(0x4400/0x4402)抓全量队伍列表,
//      web「刷新队伍列表」展示创建人/分配/人数。
//   2) join({names|pids, leaderName|partyId}) —— 对每个选中角色各下发一次加入(DLL 内部
//      自己开窗抓表按队长名匹配再发加入包),各角色独立进程,并行下发。
// 另持久化一份轻量配置(上次选中的角色 + 创建人名),纯为多开页面刷新后回填选择。

export interface PartyBoardEntry {
    partyId: number;
    distribution: number; // 分配方式(原始枚举值)
    curMembers: number;
    maxMembers: number;
    mapId: number;
    partyName: string;
    leaderName: string; // 创建人 / 队长
}

export interface PartyBoardSnapshot {
    ok: boolean;
    curPage: number;
    maxPage: number;
    parties: PartyBoardEntry[];
}

export interface AutoTeamConfig {
    leaderName: string;
    selectedNames: string[];
    intervalSec: number; // 多账号依次加入的间隔(秒),默认 3
}

export interface JoinResult {
    pid: number;
    characterName: string;
    ok: boolean;
    partyId: number;
    error?: string;
}

export interface JoinRequest {
    pids?: number[];
    characterNames?: string[];
    leaderName?: string;
    partyId?: number;
    intervalMs?: number; // 多账号依次加入的间隔,默认 3s,避免队长瞬间被多个加入请求压垮
}

const CONFIG_FILE_NAME = "auto_team.json";
// 抓表 / 加入都要 DLL 端本地开窗 + 轮询等回流(~秒级),给足超时。
const PARTY_LIST_TIMEOUT_MS = 20000;
const JOIN_TIMEOUT_MS = 25000;
const DEFAULT_JOIN_INTERVAL_MS = 3000;

export class AutoTeamManager {
    private readonly configFile: string;
    private config: AutoTeamConfig = defaultConfig();

    constructor(private readonly registry: InstanceRegistry, dataDir: string) {
        this.configFile = path.resolve(dataDir, CONFIG_FILE_NAME);
        void this.loadConfig();
    }

    getConfig(): AutoTeamConfig {
        return {
            leaderName: this.config.leaderName,
            selectedNames: [...this.config.selectedNames],
            intervalSec: this.config.intervalSec,
        };
    }

    async setConfig(raw: Partial<AutoTeamConfig>): Promise<AutoTeamConfig> {
        this.config = normalizeConfig({ ...this.config, ...raw });
        await this.saveConfig();
        return this.getConfig();
    }

    // 用某个在线角色拉一次全量队伍列表。
    async fetchPartyList(pid: number): Promise<PartyBoardSnapshot> {
        if (!pid) throw new Error("missing pid");
        const r = await this.registry.sendCommand(pid, "getPartyList", {}, PARTY_LIST_TIMEOUT_MS);
        if (!r.ok) throw new Error(`getPartyList failed: ${r.detail || "unknown"}`);
        const parsed = parseJson<any>(r.detail, {});
        return {
            ok: !!parsed.ok,
            curPage: toInt(parsed.curPage, 0),
            maxPage: toInt(parsed.maxPage, 0),
            parties: Array.isArray(parsed.parties) ? parsed.parties.map(normalizeEntry) : [],
        };
    }

    // 对一批角色(pids 或角色名)依次下发加入。优先 leaderName;也可直接给 partyId 跳过名字解析。
    // 串行而非并行:多个角色同时涌入会让队长侧处理不过来,故每个之间隔 intervalMs(默认 3s)。
    async join(req: JoinRequest): Promise<JoinResult[]> {
        const leaderName = String(req.leaderName || "").trim();
        const partyId = toInt(req.partyId, 0);
        if (!leaderName && partyId <= 0) throw new Error("missing leaderName or partyId");

        let targets = this.resolveTargets(req.pids, req.characterNames);
        if (targets.length === 0) throw new Error("no online targets matched");

        // 创建人本人若也在加入名单里(在线角色),跳过他 —— 队长不需要加入自己的队伍。
        if (leaderName) {
            const before = targets.length;
            targets = targets.filter((t) => t.characterName.trim() !== leaderName);
            if (targets.length < before) {
                console.log(`[autoTeam] 跳过创建人本人 ${leaderName},不加入自己的队伍`);
            }
            if (targets.length === 0) throw new Error("加入名单里只有创建人自己,无需加入");
        }

        const intervalMs = Math.max(0, toInt(req.intervalMs, DEFAULT_JOIN_INTERVAL_MS));
        const args = partyId > 0 ? { partyId } : { leaderName };
        const results: JoinResult[] = [];
        for (let i = 0; i < targets.length; i++) {
            const t = targets[i];
            if (i > 0 && intervalMs > 0) await sleep(intervalMs);
            try {
                const r = await this.registry.sendCommand(t.pid, "joinPartyByLeader", args, JOIN_TIMEOUT_MS);
                const parsed = parseJson<any>(r.detail, {});
                results.push({
                    pid: t.pid,
                    characterName: t.characterName,
                    ok: r.ok && !!parsed.ok,
                    partyId: toInt(parsed.partyId, 0),
                    error: parsed.error || (r.ok ? undefined : r.detail),
                });
            } catch (e: any) {
                results.push({
                    pid: t.pid,
                    characterName: t.characterName,
                    ok: false,
                    partyId: 0,
                    error: e?.message || String(e),
                });
            }
        }
        return results;
    }

    destroy(): void {
        // 无定时器 / 无监听,留空以对齐其余 manager 的生命周期接口。
    }

    private resolveTargets(pids?: number[], names?: string[]): Array<{ pid: number; characterName: string }> {
        const online = this.registry.list();
        const out: Array<{ pid: number; characterName: string }> = [];
        const seen = new Set<number>();
        const add = (inst?: { pid: number; characterName?: string }) => {
            if (inst && !seen.has(inst.pid)) {
                seen.add(inst.pid);
                out.push({ pid: inst.pid, characterName: inst.characterName || "" });
            }
        };
        if (Array.isArray(pids)) {
            for (const raw of pids) {
                const pid = Number(raw);
                add(online.find((i) => i.pid === pid));
            }
        }
        if (Array.isArray(names)) {
            for (const raw of names) {
                const name = String(raw || "").trim();
                if (!name) continue;
                add(online.find((i) => (i.characterName || "").trim() === name));
            }
        }
        return out;
    }

    private async loadConfig(): Promise<void> {
        try {
            const raw = await fs.readFile(this.configFile, "utf8");
            this.config = normalizeConfig(JSON.parse(raw));
        } catch (e: any) {
            if (e?.code !== "ENOENT") {
                console.warn(`[autoTeam] config read failed: ${e.message || String(e)}`);
            }
            await this.saveConfig();
        }
    }

    private async saveConfig(): Promise<void> {
        const tmp = `${this.configFile}.tmp`;
        await fs.mkdir(path.dirname(this.configFile), { recursive: true });
        await fs.writeFile(tmp, JSON.stringify(this.config, null, 2), "utf8");
        await fs.rename(tmp, this.configFile);
    }
}

function defaultConfig(): AutoTeamConfig {
    return { leaderName: "", selectedNames: [], intervalSec: DEFAULT_JOIN_INTERVAL_MS / 1000 };
}

function normalizeConfig(raw: any): AutoTeamConfig {
    const selectedNames = Array.isArray(raw?.selectedNames)
        ? raw.selectedNames.map((n: unknown) => String(n ?? "").trim()).filter(Boolean)
        : [];
    const intervalSec = Number.isFinite(Number(raw?.intervalSec))
        ? Math.max(0, Math.floor(Number(raw.intervalSec)))
        : DEFAULT_JOIN_INTERVAL_MS / 1000;
    return {
        leaderName: typeof raw?.leaderName === "string" ? raw.leaderName.trim() : "",
        selectedNames: Array.from(new Set(selectedNames)),
        intervalSec,
    };
}

function normalizeEntry(raw: any): PartyBoardEntry {
    return {
        partyId: toInt(raw?.partyId, 0),
        distribution: toInt(raw?.distribution, 0),
        curMembers: toInt(raw?.curMembers, 0),
        maxMembers: toInt(raw?.maxMembers, 0),
        mapId: toInt(raw?.mapId, 0),
        partyName: typeof raw?.partyName === "string" ? raw.partyName : "",
        leaderName: typeof raw?.leaderName === "string" ? raw.leaderName : "",
    };
}

function toInt(value: unknown, fallback: number): number {
    const n = Number(value);
    return Number.isFinite(n) ? Math.floor(n) : fallback;
}

function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function parseJson<T>(raw: unknown, fallback: T): T {
    try {
        if (typeof raw !== "string") return fallback;
        return JSON.parse(raw) as T;
    } catch {
        return fallback;
    }
}
