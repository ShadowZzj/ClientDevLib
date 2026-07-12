import fs from "fs/promises";
import path from "path";
import { InstanceRegistry } from "./instances";

// reviveTeleport:复活传送远控,批量版的 GGThreadBlock imgui TeleportTownModule。
// 两类目的地,都对选中的一批账号并行下发:
//   1) 固定城镇(狮子城 / 乐园镇)—— 一条 reviveToTown(412017),mode 固定写死
//      (乐园镇=1,狮子城=2,与 imgui 同),无需配置。
//   2) 废墟(废墟回声谷 / 废墟乐园镇 …)—— 聚合序列:reviveToTown 回某城 -> 等场景
//      加载 -> moveTo 到城内 NPC 落点(轮询坐标到达)-> sendDialogSelectRaw 发废墟对应
//      的 411026 对话项。整条序列由 broker 编排(用既有 DLL 命令),废墟定义存
//      revive_teleport.json,web 可增删改,改配置不必重编 DLL。
//
// 设计对照 imgui:UI 线程不能阻塞,故 imgui 丢 worker 线程跑;broker 这边本就是异步
// 编排,每账号一条独立 async 序列(Promise.all 并行),逻辑与 TeleportTownModule 一致。

export const TOWN_PARADISE_MODE = 1; // 乐园镇
export const TOWN_LION_MODE = 2; // 狮子城

export interface RuinsDef {
    id: string; // 稳定 id(前端编辑用),空则按 name 生成
    name: string; // 显示名,如 "废墟回声谷"
    viaCityMode: number; // 先回哪个城:1=乐园镇 2=狮子城(默认 2,与 imgui 同)
    walkX: number; // 城内 NPC 落点
    walkY: number;
    npcId: number; // 对话目标 NPC id
    dialogOption: number; // 411026 的 opt(废墟回声谷=10282,废墟乐园镇=10281)
    dialogSub: number; // 411026 的 sub,默认 1
    settleMs: number; // 回城后等场景加载,默认 3000
    walkTimeoutMs: number; // 寻路到达上限,超时也发对话(服务端校验距离),默认 15000
    reissueMs: number; // 寻路途中重发 moveTo 的周期,默认 2000
    arriveDist: number; // 距落点多近算到达,默认 1
}

export interface ReviveTeleportConfig {
    ruins: RuinsDef[];
}

export interface TeleportResult {
    pid: number;
    characterName: string;
    ok: boolean;
    phase?: string; // 失败时停在哪一步(warp/walk/dialog)
    error?: string;
}

export interface DispatchRequest {
    kind: "town" | "ruins";
    townMode?: number; // kind=town:1 乐园镇 / 2 狮子城
    ruinsId?: string; // kind=ruins:废墟定义 id
    pids?: number[];
    characterNames?: string[];
}

const CONFIG_FILE_NAME = "revive_teleport.json";
// DLL 单条命令都是本地直发,很快;给足超时即可。
const CMD_TIMEOUT_MS = 6000;

export class ReviveTeleportManager {
    private readonly configFile: string;
    private config: ReviveTeleportConfig = { ruins: [] };

    constructor(private readonly registry: InstanceRegistry, dataDir: string) {
        this.configFile = path.resolve(dataDir, CONFIG_FILE_NAME);
        void this.loadConfig();
    }

    getConfig(): ReviveTeleportConfig {
        return { ruins: this.config.ruins.map((r) => ({ ...r })) };
    }

    async setConfig(raw: Partial<ReviveTeleportConfig>): Promise<ReviveTeleportConfig> {
        const ruins = Array.isArray(raw?.ruins) ? raw.ruins.map(normalizeRuins) : [];
        // id 去重 / 补全
        const seen = new Set<string>();
        for (const r of ruins) {
            if (!r.id || seen.has(r.id)) r.id = genId(r.name, seen);
            seen.add(r.id);
        }
        this.config = { ruins };
        await this.saveConfig();
        return this.getConfig();
    }

    // 批量下发。对每个选中的在线账号并行跑一条序列,汇总结果。
    async dispatch(req: DispatchRequest): Promise<TeleportResult[]> {
        const targets = this.resolveTargets(req.pids, req.characterNames);
        if (targets.length === 0) throw new Error("no online targets matched");

        if (req.kind === "town") {
            const mode = toInt(req.townMode, 0);
            if (mode !== TOWN_PARADISE_MODE && mode !== TOWN_LION_MODE)
                throw new Error("townMode must be 1 (乐园镇) or 2 (狮子城)");
            return Promise.all(targets.map((t) => this.runTown(t, mode)));
        }

        // kind === "ruins"
        const def = this.config.ruins.find((r) => r.id === String(req.ruinsId || ""));
        if (!def) throw new Error(`unknown ruinsId: ${req.ruinsId}`);
        return Promise.all(targets.map((t) => this.runRuins(t, def)));
    }

    destroy(): void {
        // 无定时器 / 无监听。
    }

    // ---- 固定城镇:一条 reviveToTown ----
    private async runTown(t: Target, mode: number): Promise<TeleportResult> {
        try {
            const r = await this.registry.sendCommand(
                t.pid,
                "reviveToTown",
                { safetyCheck: true, mode },
                CMD_TIMEOUT_MS
            );
            return { pid: t.pid, characterName: t.characterName, ok: !!r.ok, phase: "warp", error: r.ok ? undefined : r.detail };
        } catch (e: any) {
            return { pid: t.pid, characterName: t.characterName, ok: false, phase: "warp", error: e?.message || String(e) };
        }
    }

    // ---- 废墟:回城 -> 等待 -> 寻路 -> 发对话 ----
    private async runRuins(t: Target, def: RuinsDef): Promise<TeleportResult> {
        // 1) 回城(reviveToTown viaCityMode)
        try {
            const r = await this.registry.sendCommand(
                t.pid,
                "reviveToTown",
                { safetyCheck: true, mode: def.viaCityMode },
                CMD_TIMEOUT_MS
            );
            if (!r.ok)
                return { pid: t.pid, characterName: t.characterName, ok: false, phase: "warp", error: r.detail || "reviveToTown failed" };
        } catch (e: any) {
            return { pid: t.pid, characterName: t.characterName, ok: false, phase: "warp", error: e?.message || String(e) };
        }

        // 2) 等场景加载
        await sleep(def.settleMs);

        // 3) 寻路到 NPC 落点(超时也继续 —— 服务端会校验距离)
        try {
            await this.walkTo(t.pid, def);
        } catch (e: any) {
            // 寻路途中的偶发命令异常不致命,继续发对话。
            console.warn(`[reviveTeleport] walk warn pid=${t.pid}: ${e?.message || e}`);
        }

        // 4) 发废墟对应的对话项(411026)
        try {
            const r = await this.registry.sendCommand(
                t.pid,
                "sendDialogSelectRaw",
                { npcId: def.npcId, option: def.dialogOption, sub: def.dialogSub },
                CMD_TIMEOUT_MS
            );
            return { pid: t.pid, characterName: t.characterName, ok: !!r.ok, phase: "dialog", error: r.ok ? undefined : r.detail };
        } catch (e: any) {
            return { pid: t.pid, characterName: t.characterName, ok: false, phase: "dialog", error: e?.message || String(e) };
        }
    }

    // moveTo 驱动引擎寻路;每 reissueMs 重发一次防中途被打断(同目标重发无害)。轮询
    // 本地坐标直到进入到达半径或超时。对照 imgui TeleportTownModule::WalkToNpc。
    private async walkTo(pid: number, def: RuinsDef): Promise<void> {
        const start = Date.now();
        const arriveDistSq = def.arriveDist * def.arriveDist;
        let lastMove = 0;
        while (Date.now() - start < def.walkTimeoutMs) {
            const now = Date.now();
            if (lastMove === 0 || now - lastMove >= def.reissueMs) {
                try {
                    await this.registry.sendCommand(pid, "moveTo", { x: def.walkX, y: def.walkY, action: 1 }, CMD_TIMEOUT_MS);
                } catch {
                    /* 单次 moveTo 失败不致命,下一轮重发 */
                }
                lastMove = now;
            }

            try {
                const r = await this.registry.sendCommand(pid, "getLocalPosition", {}, CMD_TIMEOUT_MS);
                if (r.ok) {
                    const pos = parseJson<{ x: number; y: number }>(r.detail, { x: NaN, y: NaN });
                    if (Number.isFinite(pos.x) && Number.isFinite(pos.y)) {
                        const dx = pos.x - def.walkX;
                        const dy = pos.y - def.walkY;
                        if (dx * dx + dy * dy <= arriveDistSq) return; // 已到达
                    }
                }
            } catch {
                /* 读坐标失败就下一轮再试 */
            }
            await sleep(200);
        }
        // 超时:不抛错,调用方会继续发对话(服务端校验距离)。
    }

    private resolveTargets(pids?: number[], names?: string[]): Target[] {
        const online = this.registry.list();
        const out: Target[] = [];
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
            const parsed = JSON.parse(raw);
            const ruins = Array.isArray(parsed?.ruins) ? parsed.ruins.map(normalizeRuins) : [];
            this.config = { ruins: ruins.length ? ruins : defaultRuins() };
        } catch (e: any) {
            if (e?.code !== "ENOENT") {
                console.warn(`[reviveTeleport] config read failed: ${e.message || String(e)}`);
            }
            // 首次:写入两个默认废墟(回声谷 / 乐园镇),与 imgui 一致。
            this.config = { ruins: defaultRuins() };
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

interface Target {
    pid: number;
    characterName: string;
}

// 默认废墟定义(取自 GGThreadBlock imgui TeleportTownModule.h:NPC 19129,落点
// (226,155),先回狮子城 mode=2,等 3s;回声谷 opt=10282,乐园镇 opt=10281)。
function defaultRuins(): RuinsDef[] {
    const base = {
        viaCityMode: TOWN_LION_MODE,
        walkX: 226,
        walkY: 155,
        npcId: 19129,
        dialogSub: 1,
        settleMs: 3000,
        walkTimeoutMs: 15000,
        reissueMs: 2000,
        arriveDist: 1,
    };
    return [
        { id: "ruins-echo-valley", name: "废墟回声谷", dialogOption: 10282, ...base },
        { id: "ruins-paradise-town", name: "废墟乐园镇", dialogOption: 10281, ...base },
    ];
}

function normalizeRuins(raw: any): RuinsDef {
    return {
        id: typeof raw?.id === "string" ? raw.id.trim() : "",
        name: typeof raw?.name === "string" ? raw.name.trim() : "",
        viaCityMode: toInt(raw?.viaCityMode, TOWN_LION_MODE) === TOWN_PARADISE_MODE ? TOWN_PARADISE_MODE : TOWN_LION_MODE,
        walkX: toFloat(raw?.walkX, 0),
        walkY: toFloat(raw?.walkY, 0),
        npcId: toInt(raw?.npcId, 0),
        dialogOption: toInt(raw?.dialogOption, 0),
        dialogSub: toInt(raw?.dialogSub, 1),
        settleMs: clampInt(raw?.settleMs, 3000, 0, 60000),
        walkTimeoutMs: clampInt(raw?.walkTimeoutMs, 15000, 1000, 120000),
        reissueMs: clampInt(raw?.reissueMs, 2000, 200, 30000),
        arriveDist: Math.max(0.1, toFloat(raw?.arriveDist, 1)),
    };
}

function genId(name: string, taken: Set<string>): string {
    const slug = (name || "ruins").replace(/\s+/g, "-").toLowerCase().slice(0, 24) || "ruins";
    let id = slug;
    let n = 1;
    while (taken.has(id)) id = `${slug}-${n++}`;
    return id;
}

function toInt(value: unknown, fallback: number): number {
    const n = Number(value);
    return Number.isFinite(n) ? Math.floor(n) : fallback;
}

function toFloat(value: unknown, fallback: number): number {
    const n = Number(value);
    return Number.isFinite(n) ? n : fallback;
}

function clampInt(value: unknown, fallback: number, min: number, max: number): number {
    const n = toInt(value, fallback);
    return Math.min(max, Math.max(min, n));
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
