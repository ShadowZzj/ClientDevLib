import fs from "fs";
import path from "path";
import { InstanceRegistry, DialogSelectEvent, TeleportEvent } from "./instances";

// syncManager:多分组同步。每个分组指定一个主角色 + 多个副角色,副角色镜像主
// 角色的动作。一个账号(角色名)在所有分组里只能出现一次(主或副),归一化时
// 按分组顺序抢占,后面的分组不能再要已被占用的名字。
//
// 同步三类动作(每组各自开关):
//   1. 移动 —— 跟随主角色「当前位置」。复用 status 遥测(主角色每 1.5s 上报
//      posX/posY),follow tick 把主角色当前坐标 moveTo 给每个副角色,引擎寻路。
//      主角色没动就不重发(按组去重),免得刷命令。
//   2. NPC 对话 —— 主角色每发一次 CG_NPC_DIALOG_SELECT(411026),DLL 经 NetLog hook
//      上报 dialogSelect 帧;立即并发重放 sendDialogSelectRaw 给同组在线副角色。
//   3. 城市传送 —— 主角色每发一次城市传送(411076),DLL 上报 teleport 帧(带
//      destId);立即并发把 teleport{destId} 重放给同组在线副角色。
//
// 跟 buffKeeper 一样:DLL 不在线就跳过,不阻塞。主≠副,主角色不会镜像给自己。

export interface SyncGroup {
    id: string;
    name: string;
    enabled: boolean;           // 本组开关(关掉这组就停,不影响其它组)
    masterName: string;
    slaveNames: string[];
    mirrorPosition: boolean;    // 镜像移动(自动寻路到主角色当前坐标)
    mirrorDialog: boolean;      // 镜像 NPC 对话选择
    mirrorTeleport: boolean;    // 镜像城市传送
    // 移动方式:
    //   overlap = 重叠站位,副角色全部 moveTo 主角色坐标,最终重合(默认,旧行为)。
    //   line    = 排队站位,副角色沿一个方向依次排开,间距 lineSpacing。
    formation: "overlap" | "line";
    lineDirection: "x+" | "x-" | "y+" | "y-"; // 排队方向
    lineSpacing: number;        // 排队相邻格间距(世界坐标),默认 1
    // 排队站位时是否避免与其它玩家重合。勾上则遍历主角色附近玩家(自己算占用),
    // 沿方向逐格跳过被占的格子,保证每个副角色站位不和任何玩家重合。
    lineAvoidPlayers: boolean;
}

export interface SyncConfig {
    followIntervalMs: number;   // 位置跟随轮询间隔(全局共享)
    posEpsilon: number;         // 主角色移动超过这个距离才重新下发 moveTo(全局)
    // 错峰下发:每个副角色发 moveTo 前等一个 [0, followJitterMs) 的随机延迟,每拍
    // 独立 roll。目标坐标不变(最终位置仍精确对齐),只打散起步时刻,避免六个副角色
    // 同一毫秒齐步走被人看出是脚本。0 = 关闭(旧的齐发行为)。
    followJitterMs: number;
    // 路径随机:开了之后,副角色不再直线寻路到最终点,而是先走若干个随机途经点
    // 再到最终点。每个副角色从自己当前坐标出发各算各的折线,所以即使最终点相同/
    // 相邻,中途路线也各不相同,不会六个号一条线齐步走。最终落点精确不变。
    //   pathWaypoints:每条路径插几个随机途经点(1~3)。
    //   pathOffsetMax:途经点在垂直于「起点→终点」方向上的最大随机偏移(世界坐标)。
    //   pathSegDelayMs:发完一段 moveTo 后等多久再发下一段(给引擎时间往途经点走)。
    // 注意:这套主要在主角色停下、副角色做最后对齐时发挥作用。主角色持续跑图时
    // 每拍都会重排并取消上一条未走完的折线,此时引擎跟随本身路线就够分散。
    pathRandomize: boolean;
    pathWaypoints: number;
    pathOffsetMax: number;
    pathSegDelayMs: number;
    groups: SyncGroup[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "sync_config.json"
);

const DEFAULT_FOLLOW_MS = 1500;
const MIN_FOLLOW_MS = 800;
const DEFAULT_POS_EPSILON = 1.0;
const DEFAULT_FOLLOW_JITTER_MS = 600;
const DEFAULT_PATH_WAYPOINTS = 2;
const DEFAULT_PATH_OFFSET_MAX = 2.0;
const DEFAULT_PATH_SEG_DELAY_MS = 700;

export class SyncManager {
    private config: SyncConfig = defaultConfig();
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    // 每组上一次扇出的主角色坐标,用于去重:主角色没动就不重发 moveTo。key=groupId。
    private lastPushedPos = new Map<string, { x: number; y: number }>();
    // 每个副角色当前的「移动序列代号」。路径随机时一次 moveTo 会拆成多段异步下发,
    // 每开一条新序列就 bump 这个代号;旧序列每段前比对,代号变了就自行中止,避免
    // 主角色又动了之后两条折线序列对同一个号抢发 moveTo。key=pid。
    private moveGen = new Map<number, number>();
    private followInFlight = false;
    private onDialogSelect: (ev: DialogSelectEvent) => void;
    private onTeleport: (ev: TeleportEvent) => void;

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.onDialogSelect = (ev) => this.handleDialogSelect(ev);
        this.onTeleport = (ev) => this.handleTeleport(ev);
        this.registry.on("dialogSelect", this.onDialogSelect);
        this.registry.on("teleport", this.onTeleport);
        this.start();
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                const parsed = JSON.parse(raw) as any;
                this.config = normalizeConfig(parsed);
                console.log(
                    `[sync] loaded config groups=${this.config.groups.length}`
                );
            }
        } catch (e: any) {
            console.warn(`[sync] load failed: ${e.message}`);
            this.config = defaultConfig();
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[sync] save failed: ${e.message}`);
        }
    }

    private start(): void {
        const interval = Math.max(MIN_FOLLOW_MS, this.config.followIntervalMs || DEFAULT_FOLLOW_MS);
        if (this.timer) clearInterval(this.timer);
        this.timer = setInterval(() => this.followTick(), interval);
    }

    // 某个分组的在线副角色 pid。在线且 characterName 唯一映射到 pid,排除主角色自身。
    private onlineSlavePids(group: SyncGroup): number[] {
        return this.onlineSlaves(group).map((s) => s.pid);
    }

    // 按 group.slaveNames 顺序返回在线副角色 {name, pid}。排队站位靠这个顺序决定
    // 谁站第 1 格、谁站第 2 格,所以必须稳定(不能用 registry.list() 的乱序)。
    private onlineSlaves(group: SyncGroup): { name: string; pid: number }[] {
        const byName = new Map<string, number>();
        for (const inst of this.registry.list()) {
            if (inst.characterName) byName.set(inst.characterName, inst.pid);
        }
        const out: { name: string; pid: number }[] = [];
        for (const name of group.slaveNames) {
            if (!name || name === group.masterName) continue;
            const pid = byName.get(name);
            if (pid !== undefined) out.push({ name, pid });
        }
        return out;
    }

    private masterInstance(group: SyncGroup) {
        if (!group.masterName) return undefined;
        return this.registry.list().find((i) => i.characterName === group.masterName);
    }

    // 根据主角色名找它所在的分组(每个名字只属于一个组,这里只匹配 master)。
    private groupByMaster(masterName?: string): SyncGroup | undefined {
        if (!masterName) return undefined;
        return this.config.groups.find((g) => g.masterName === masterName);
    }

    private async followTick(): Promise<void> {
        if (this.followInFlight) return;

        this.followInFlight = true;
        try {
            for (const group of this.config.groups) {
                if (!group.enabled || !group.mirrorPosition) continue;
                await this.followGroup(group);
            }
        } finally {
            this.followInFlight = false;
        }
    }

    private async followGroup(group: SyncGroup): Promise<void> {
        const master = this.masterInstance(group);
        if (!master) return;
        const x = master.status.posX;
        const y = master.status.posY;
        if (typeof x !== "number" || typeof y !== "number") return;

        // 主角色没动(在 epsilon 内)就不重发,避免站桩时狂刷 moveTo。
        // 注意:排队+避让模式即使主角色没动,附近玩家也可能变,但为了不刷命令仍
        // 沿用位置去重 —— 避让只在主角色位移触发的重排时计算一次,够用了。
        const eps = this.config.posEpsilon > 0 ? this.config.posEpsilon : DEFAULT_POS_EPSILON;
        const last = this.lastPushedPos.get(group.id);
        if (last) {
            const dx = x - last.x;
            const dy = y - last.y;
            if (dx * dx + dy * dy < eps * eps) return;
        }

        const slaves = this.onlineSlaves(group);
        if (slaves.length === 0) return;

        this.lastPushedPos.set(group.id, { x, y });

        // 重叠站位:全部 moveTo 主角色坐标(旧行为)。
        if (group.formation !== "line") {
            for (const s of slaves) this.dispatchMove(s.pid, x, y);
            return;
        }

        // 排队站位:沿 lineDirection 依次排开,间距 lineSpacing。
        const targets = await this.computeLineTargets(group, master.pid, x, y, slaves.length);
        slaves.forEach((s, i) => this.dispatchMove(s.pid, targets[i].x, targets[i].y));
    }

    // 给一个副角色安排「走到 (x,y)」。fire-and-forget:开一条新移动序列(bump 该
    // pid 的代号),立即返回,不阻塞 followTick。序列内部按需做错峰延迟和分段折线,
    // 每段前比对代号 —— 主角色又动了触发新序列时,旧序列会自行中止。
    private dispatchMove(pid: number, x: number, y: number): void {
        const gen = (this.moveGen.get(pid) ?? 0) + 1;
        this.moveGen.set(pid, gen);
        void this.runMoveSequence(pid, x, y, gen);
    }

    private async runMoveSequence(pid: number, x: number, y: number, gen: number): Promise<void> {
        // 错峰起步:各等一个随机延迟,打散六个号的起步时刻。
        const jitter = this.config.followJitterMs;
        if (jitter > 0) {
            await sleep(Math.floor(Math.random() * jitter));
            if (this.moveGen.get(pid) !== gen) return; // 已被新序列取代
        }

        // 不开路径随机:直接走最终点(引擎自己寻路)。
        if (!this.config.pathRandomize) {
            await this.sendMove(pid, x, y);
            return;
        }

        // 从副角色「自己当前坐标」出发,构造一条带随机途经点的折线。每个号起点不同、
        // 偏移随机,所以中途路线各不相同;最后一个点精确等于最终点,落位仍对齐。
        const start = this.slavePos(pid);
        if (!start) {
            await this.sendMove(pid, x, y); // 不知道起点就退化成直接走
            return;
        }
        // 几何途经点(不含起点/终点)。broker 读不到地图,这些点可能落在墙/水/山里。
        const rawWaypoints = buildRandomWaypoints(
            start.x, start.y, x, y,
            this.config.pathWaypoints, this.config.pathOffsetMax
        );
        // 让该副角色的 DLL 用引擎碰撞表把途经点校验/吸附成真正可走的点。失败(地图
        // 没就绪等)就退化成只走终点 —— 宁可直线,也不发可能落进障碍里的瞎猜点。
        const waypoints = await this.snapWaypoints(pid, start.x, start.y, rawWaypoints);
        if (this.moveGen.get(pid) !== gen) return;

        // 校验后的可走途经点 + 精确终点。终点是主角色站过的真实坐标,交给引擎寻路。
        const path = [...waypoints, { x, y }];
        for (let i = 0; i < path.length; i++) {
            if (this.moveGen.get(pid) !== gen) return; // 主角色又动了,这条折线作废
            await this.sendMove(pid, path[i].x, path[i].y);
            // moveTo 会覆盖上一个目标,所以发完一段要等引擎把角色往途经点带一段,
            // 再发下一段。最后一段(最终点)不用等。
            if (i < path.length - 1) await sleep(this.config.pathSegDelayMs);
        }
    }

    // 把几何途经点发给副角色的 DLL,用引擎碰撞表(RaycastFurthestWalkable)校验,
    // 返回吸附到可走 tile 的途经点。命令失败/超时/无可走点都返回空数组,调用方
    // 此时只走终点(直线寻路),不会去走没校验过的瞎猜点。
    private async snapWaypoints(
        pid: number,
        startX: number,
        startY: number,
        points: { x: number; y: number }[]
    ): Promise<{ x: number; y: number }[]> {
        if (points.length === 0) return [];
        try {
            const r = await this.registry.sendCommand(
                pid, "snapPath", { startX, startY, points }, 4000
            );
            if (!r.ok || !r.detail) return [];
            const parsed = JSON.parse(r.detail);
            const arr = Array.isArray(parsed?.points) ? parsed.points : [];
            const out: { x: number; y: number }[] = [];
            for (const p of arr) {
                if (typeof p?.x === "number" && typeof p?.y === "number") {
                    out.push({ x: p.x, y: p.y });
                }
            }
            return out;
        } catch (e: any) {
            console.warn(`[sync] snapPath pid=${pid} failed: ${e.message}`);
            return [];
        }
    }

    private async sendMove(pid: number, x: number, y: number): Promise<void> {
        await this.registry
            .sendCommand(pid, "moveTo", { x, y, action: 1 }, 4000)
            .catch((e: any) => {
                console.warn(`[sync] moveTo pid=${pid} failed: ${e.message}`);
            });
    }

    // 副角色自己也是在线 instance,会上报 posX/posY。拿它当折线起点。
    private slavePos(pid: number): { x: number; y: number } | undefined {
        const inst = this.registry.list().find((i) => i.pid === pid);
        const x = inst?.status.posX;
        const y = inst?.status.posY;
        if (typeof x === "number" && typeof y === "number") return { x, y };
        return undefined;
    }

    // 排队站位:从主角色坐标出发,沿方向每隔 lineSpacing 放一个槽位。返回 count 个
    // 目标坐标(顺序对应 onlineSlaves 顺序)。
    //   不避让(lineAvoidPlayers=false):第 k 个副角色 = 主坐标 + (k+1)*spacing*dir。
    //   避让(lineAvoidPlayers=true):把主角色附近玩家坐标标成「已占用」(含主角色
    //     自己),从主坐标起沿方向逐格扫,跳过被占的格,把空格依次分给副角色,
    //     保证每个副角色站位不和任何已知玩家重合。
    private async computeLineTargets(
        group: SyncGroup,
        masterPid: number,
        x: number,
        y: number,
        count: number
    ): Promise<{ x: number; y: number }[]> {
        const spacing = group.lineSpacing > 0 ? group.lineSpacing : 1;
        const [dx, dy] = directionVec(group.lineDirection);

        if (!group.lineAvoidPlayers) {
            const out: { x: number; y: number }[] = [];
            for (let k = 0; k < count; k++) {
                const step = (k + 1) * spacing;
                out.push({ x: x + dx * step, y: y + dy * step });
            }
            return out;
        }

        // 拿主角色附近玩家坐标当占用集。失败就退化成不避让(至少别卡住)。
        const occupied = await this.fetchOccupiedCells(masterPid, spacing);
        // 主角色自己所在格也算占用。
        occupied.add(cellKey(x, y, spacing));

        const out: { x: number; y: number }[] = [];
        let step = 1; // 从主坐标的下一格开始往外站
        for (let k = 0; k < count; k++) {
            // 沿方向找下一个没被占用的格。设上限防死循环(占用集异常时不至于无限)。
            let guard = 0;
            for (;;) {
                const tx = x + dx * spacing * step;
                const ty = y + dy * spacing * step;
                const key = cellKey(tx, ty, spacing);
                step++;
                if (!occupied.has(key) || guard++ > 1000) {
                    occupied.add(key); // 占住,后面的副角色不再站这
                    out.push({ x: tx, y: ty });
                    break;
                }
            }
        }
        return out;
    }

    // 给主角色发 getNearbyPlayers,把附近玩家坐标量化成格子集合。命令失败/超时
    // 返回空集(调用方会退化成不避让)。
    private async fetchOccupiedCells(masterPid: number, spacing: number): Promise<Set<string>> {
        const cells = new Set<string>();
        try {
            const r = await this.registry.sendCommand(
                masterPid,
                "getNearbyPlayers",
                { maxDistance: 600 },
                4000
            );
            if (!r.ok || !r.detail) return cells;
            const arr = JSON.parse(r.detail);
            if (!Array.isArray(arr)) return cells;
            for (const p of arr) {
                if (typeof p?.x === "number" && typeof p?.y === "number") {
                    cells.add(cellKey(p.x, p.y, spacing));
                }
            }
        } catch (e: any) {
            console.warn(`[sync] getNearbyPlayers pid=${masterPid} failed: ${e.message}`);
        }
        return cells;
    }

    private handleDialogSelect(ev: DialogSelectEvent): void {
        const group = this.groupByMaster(ev.characterName);
        if (!group || !group.enabled || !group.mirrorDialog) return;

        const pids = this.onlineSlavePids(group);
        if (pids.length === 0) return;

        console.log(
            `[sync] group "${group.name}" master ${ev.characterName} dialogSelect npc=${ev.npc} opt=${ev.opt} sub=${ev.sub} → ${pids.length} slave(s)`
        );
        for (const pid of pids) {
            this.registry
                .sendCommand(
                    pid,
                    "sendDialogSelectRaw",
                    { npcId: ev.npc, option: ev.opt, sub: ev.sub },
                    5000
                )
                .catch((e: any) => {
                    console.warn(`[sync] dialogSelect replay pid=${pid} failed: ${e.message}`);
                });
        }
    }

    private handleTeleport(ev: TeleportEvent): void {
        const group = this.groupByMaster(ev.characterName);
        if (!group || !group.enabled || !group.mirrorTeleport) return;

        const pids = this.onlineSlavePids(group);
        if (pids.length === 0) return;

        console.log(
            `[sync] group "${group.name}" master ${ev.characterName} teleport destId=${ev.destId} → ${pids.length} slave(s)`
        );
        // destId 直接重放:副角色走 teleport handler 的 destId 分支(绕等级/金钱闸,
        // 服务端仍校验)。主角色没动跟随会很快把队形重新对齐。
        for (const pid of pids) {
            this.registry
                .sendCommand(pid, "teleport", { destId: ev.destId }, 5000)
                .catch((e: any) => {
                    console.warn(`[sync] teleport replay pid=${pid} failed: ${e.message}`);
                });
        }
        // 传送后位置会突变,清掉该组去重缓存,下一拍重新对齐位置跟随。
        this.lastPushedPos.delete(group.id);
    }

    // ---------------- HTTP 用的 CRUD ----------------

    getConfig(): SyncConfig {
        return this.config;
    }

    setConfig(body: Partial<SyncConfig>): SyncConfig {
        const next = normalizeConfig({ ...this.config, ...body });
        const intervalChanged = next.followIntervalMs !== this.config.followIntervalMs;
        this.config = next;
        // 分组/主副改了就清掉所有去重缓存,下一拍重新对齐。
        this.lastPushedPos.clear();
        this.save();
        if (intervalChanged) this.start();
        return this.config;
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
        this.registry.off("dialogSelect", this.onDialogSelect);
        this.registry.off("teleport", this.onTeleport);
    }
}

function defaultConfig(): SyncConfig {
    return {
        followIntervalMs: DEFAULT_FOLLOW_MS,
        posEpsilon: DEFAULT_POS_EPSILON,
        followJitterMs: DEFAULT_FOLLOW_JITTER_MS,
        pathRandomize: false,
        pathWaypoints: DEFAULT_PATH_WAYPOINTS,
        pathOffsetMax: DEFAULT_PATH_OFFSET_MAX,
        pathSegDelayMs: DEFAULT_PATH_SEG_DELAY_MS,
        groups: [],
    };
}

function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

// 构造从 (sx,sy) 到 (tx,ty) 之间的随机途经点(不含起点、不含终点)。沿直线均匀取
// waypoints 个内点,每个内点在「垂直于起点→终点方向」上加随机偏移(±offsetMax 内,
// 中段偏移最大、两端收窄,形如轻微弓形绕行)。这些是纯几何点,可能落在障碍里,
// 由调用方交给 DLL 的 snapPath 校验吸附。起终点几乎重合时返回空数组。
function buildRandomWaypoints(
    sx: number, sy: number, tx: number, ty: number,
    waypoints: number, offsetMax: number
): { x: number; y: number }[] {
    const dx = tx - sx;
    const dy = ty - sy;
    const len = Math.hypot(dx, dy);
    const n = Math.max(0, Math.floor(waypoints));
    if (len < 1e-3 || n === 0 || offsetMax <= 0) return [];
    // 单位法向量(把方向向量旋 90°)。途经点沿这个方向左右偏。
    const nxRaw = -dy / len;
    const nyRaw = dx / len;
    const out: { x: number; y: number }[] = [];
    for (let i = 1; i <= n; i++) {
        const f = i / (n + 1);                      // 沿直线的插值比例 (0,1)
        const taper = Math.sin(f * Math.PI);        // 两端 0、中间 1,弓形收窄
        const off = (Math.random() * 2 - 1) * offsetMax * taper;
        out.push({ x: sx + dx * f + nxRaw * off, y: sy + dy * f + nyRaw * off });
    }
    return out;
}

let s_groupIdSeq = 0;
function genGroupId(): string {
    return `g${Date.now().toString(36)}${(s_groupIdSeq++).toString(36)}`;
}

// 方向枚举 → 单位向量。x+ = X 增,y- = Y 减,以此类推。
function directionVec(dir: "x+" | "x-" | "y+" | "y-"): [number, number] {
    switch (dir) {
        case "x-": return [-1, 0];
        case "y+": return [0, 1];
        case "y-": return [0, -1];
        case "x+":
        default:   return [1, 0];
    }
}

// 把世界坐标量化成「格子 key」,用于占用判重。按 spacing 取整,容差半格,
// 这样浮点坐标(玩家实际位置不会精确落在整数格)也能稳定归到同一格。
function cellKey(x: number, y: number, spacing: number): string {
    const s = spacing > 0 ? spacing : 1;
    return `${Math.round(x / s)},${Math.round(y / s)}`;
}

function normalizeConfig(c: any): SyncConfig {
    const followIntervalMs = Math.max(
        MIN_FOLLOW_MS,
        Number(c?.followIntervalMs ?? DEFAULT_FOLLOW_MS) || DEFAULT_FOLLOW_MS
    );
    const posEpsilon = Number(c?.posEpsilon ?? DEFAULT_POS_EPSILON) || DEFAULT_POS_EPSILON;
    const followJitterMs = Math.max(0, Number(c?.followJitterMs ?? DEFAULT_FOLLOW_JITTER_MS) || 0);
    const pathRandomize = !!c?.pathRandomize;
    const pathWaypoints = clampInt(c?.pathWaypoints, DEFAULT_PATH_WAYPOINTS, 1, 5);
    const pathOffsetMax = Math.max(0, Number(c?.pathOffsetMax ?? DEFAULT_PATH_OFFSET_MAX) || 0);
    const pathSegDelayMs = Math.max(0, Number(c?.pathSegDelayMs ?? DEFAULT_PATH_SEG_DELAY_MS) || 0);

    // 旧格式迁移:顶层 masterName + slaveNames(单组)→ 一个分组。
    let rawGroups: any[] = Array.isArray(c?.groups) ? c.groups : [];
    if (rawGroups.length === 0 && (c?.masterName || (Array.isArray(c?.slaveNames) && c.slaveNames.length))) {
        rawGroups = [
            {
                name: "默认分组",
                masterName: c.masterName,
                slaveNames: c.slaveNames,
                mirrorPosition: c.mirrorPosition,
                mirrorDialog: c.mirrorDialog,
                mirrorTeleport: c.mirrorTeleport,
            },
        ];
    }

    // 一个角色名在所有分组里只能出现一次(主或副),按分组顺序抢占。
    const claimed = new Set<string>();
    const groups: SyncGroup[] = [];
    for (const g of rawGroups) {
        const id = typeof g?.id === "string" && g.id ? g.id : genGroupId();
        const name = typeof g?.name === "string" && g.name ? g.name : `分组 ${groups.length + 1}`;

        let masterName = typeof g?.masterName === "string" ? g.masterName : "";
        if (masterName && claimed.has(masterName)) masterName = ""; // 已被别组占用
        if (masterName) claimed.add(masterName);

        const slaveRaw = Array.isArray(g?.slaveNames) ? g.slaveNames : [];
        const slaveNames: string[] = [];
        for (const n of slaveRaw) {
            if (typeof n !== "string" || !n) continue;
            if (n === masterName) continue;     // 主不能同时是副
            if (claimed.has(n)) continue;        // 已被占用(本组主、或其它组)
            claimed.add(n);
            slaveNames.push(n);
        }

        groups.push({
            id,
            name,
            enabled: g?.enabled ?? true,
            masterName,
            slaveNames,
            mirrorPosition: g?.mirrorPosition ?? true,
            mirrorDialog: g?.mirrorDialog ?? true,
            mirrorTeleport: g?.mirrorTeleport ?? true,
            formation: g?.formation === "line" ? "line" : "overlap",
            lineDirection: ["x+", "x-", "y+", "y-"].includes(g?.lineDirection) ? g.lineDirection : "x+",
            lineSpacing: Number(g?.lineSpacing) > 0 ? Number(g.lineSpacing) : 1,
            lineAvoidPlayers: !!g?.lineAvoidPlayers,
        });
    }

    return {
        followIntervalMs,
        posEpsilon,
        followJitterMs,
        pathRandomize,
        pathWaypoints,
        pathOffsetMax,
        pathSegDelayMs,
        groups,
    };
}

// 取整 + 夹到 [min,max],非法值回落到 def。
function clampInt(v: any, def: number, min: number, max: number): number {
    const n = Math.floor(Number(v));
    if (!Number.isFinite(n)) return def;
    return Math.min(max, Math.max(min, n));
}
