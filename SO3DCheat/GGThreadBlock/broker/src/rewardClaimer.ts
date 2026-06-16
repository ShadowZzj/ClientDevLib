import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";

// rewardClaimer:对开启的角色,先用只读的 peekRewards 探「现在有没有可领奖励」(读 HUD 闹钟/红心的
// content+0x38 闪烁位,服务器登录即推送填好,不开窗不发包),只有探到可领才真正发 claimRewards 开窗领取
// (走 CLocalPlayer::ClaimDailyRewards:开本地 UI 窗 → 领 → 关窗)。
//
// 这样彻底丢掉了旧的「上线 10s + 每 3h 盲开窗」方案:peek 极便宜可以一直轮询,开窗只发生在「确有可领」时,
// 既不浪费开窗、也不会漏掉随在线时长逐档解锁(30/60/90/120/150/180 分钟)的在线奖励 —— 后面的档解锁后
// peek.online 会重新变 true,下一轮自动补领。
//
// peek 的 online/signin 只反映 status==1(可领)的档;商城道具那种永久 locked(status 0)不会置位,
// 所以不再需要旧版 ONLINE_PERMA_LOCKED_TIERS 的「永久锁」fudge。
//
// 领过一次后压一个 CLAIM_COOLDOWN_MS 冷却:万一某次领取没把 +0x38 清掉(领取失败 / 有领不掉的档),
// 不至于每轮都开窗死循环;冷却到了再按 peek 复查。doneDate 仅作 UI 展示用(最近探到「无可领」的本地日)。

export interface RewardClaimCharacterConfig {
    characterName: string;
    enabled: boolean;
    claimOnline: boolean; // 领在线奖励(默认 true)
    claimSignin: boolean; // 领签到奖励(默认 true)
    doneDate: string;     // 仅展示:最近一次 peek 探到「无可领」的本地日 "YYYY-MM-DD";探到可领即清空
    lastAttempt: number;  // 上次开窗领取的 epoch ms(展示用)
    lastResult: string;   // 上次领取结果的简短摘要(展示用)
}

interface PersistShape {
    characters: RewardClaimCharacterConfig[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "reward_claim.json"
);

// 角色上线后先等这么久再开始 peek,留时间给登录推送的奖励列表包(0x7CE76/0x7CE74)到达并建好 content。
const FIRST_DELAY_MS = 10_000;
// peek 轮询间隔。peek 只读不开窗、极便宜,但在线档每 30 分钟才解锁一档,所以每分钟探一次足够及时。
const PEEK_MS = 60_000;
// content 还没就绪(刚上线 / UI 未建,resolved=false)时的快速重试间隔。
const RETRY_NOT_READY_MS = 5_000;
// 领过一次后的开窗冷却:防「领取没清掉 +0x38」时每轮空开窗死循环。在线档每 30min 解锁一档,10min < 30min,
// 冷却到点后下一档也还没解锁,不会漏领。
const CLAIM_COOLDOWN_MS = 10 * 60_000;
// peekRewards 只读一两个字节,很快;给点余量。
const PEEK_TIMEOUT_MS = 5_000;
// claimRewards 在 DLL 里同步开窗 + 轮询 dialog 填充(最多 ~2×2s)+ 关窗,给足超时。
const CLAIM_TIMEOUT_MS = 30_000;

interface RewardTierResult {
    claimed: number;
    claimable: number;
    locked: number;
}
interface ClaimDetail {
    online: RewardTierResult;
    signin: RewardTierResult;
}

interface PeekResult {
    online: boolean;         // content62+0x38:在线奖励有可领档
    signin: boolean;         // content61+0x38:签到奖励有可领档
    onlineResolved: boolean; // GetUIContent(62) 取到 = content 已就绪
    signinResolved: boolean; // GetUIContent(61) 取到 = content 已就绪
}

export class RewardClaimer {
    private configs: RewardClaimCharacterConfig[] = [];
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;
    // 每角色「本次在线起点」:记 pid + since,pid 变了(重开游戏)就重新计时 FIRST_DELAY_MS。
    private onlineSince = new Map<string, { pid: number; since: number }>();
    private nextPeekAt = new Map<string, number>();
    // 领过后压的开窗冷却(到点前即使 peek 仍报可领也不开窗)。
    private nextClaimAt = new Map<string, number>();
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
                console.log(`[rewardClaimer] loaded ${this.configs.length} character configs`);
            }
        } catch (e: any) {
            console.warn(`[rewardClaimer] load failed: ${e.message}`);
            this.configs = [];
        }
    }

    private save(): void {
        try {
            const shape: PersistShape = { characters: this.configs };
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(shape, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[rewardClaimer] save failed: ${e.message}`);
        }
    }

    private start(): void {
        this.timer = setInterval(() => this.tick(), 1000);
    }

    private tick(): void {
        const now = Date.now();
        const nameToPid = new Map<string, number>();
        for (const inst of this.registry.list()) {
            if (inst.characterName) nameToPid.set(inst.characterName, inst.pid);
        }

        for (const cfg of this.configs) {
            if (!cfg.enabled) continue;

            const pid = nameToPid.get(cfg.characterName);
            if (pid === undefined) {
                // 离线:清掉在线计时,下次重开游戏重新走 FIRST_DELAY_MS。
                this.onlineSince.delete(cfg.characterName);
                continue;
            }

            // 记录/刷新在线起点;pid 变了说明重开了游戏,重新计时。
            let os = this.onlineSince.get(cfg.characterName);
            if (!os || os.pid !== pid) {
                os = { pid, since: now };
                this.onlineSince.set(cfg.characterName, os);
            }

            if (now - os.since < FIRST_DELAY_MS) continue; // 在线不足 FIRST_DELAY_MS
            if (this.inFlight.has(cfg.characterName)) continue;

            const nextPeek = this.nextPeekAt.get(cfg.characterName) ?? 0;
            if (now < nextPeek) continue;
            this.nextPeekAt.set(cfg.characterName, now + PEEK_MS);

            this.inFlight.add(cfg.characterName);
            this.runPeekAndClaim(cfg, pid).finally(() => {
                this.inFlight.delete(cfg.characterName);
            });
        }
    }

    // 先 peek,探到可领才开窗领。peek 不开窗不发包,代价极小。
    private async runPeekAndClaim(cfg: RewardClaimCharacterConfig, pid: number): Promise<void> {
        const now = Date.now();
        let peek: PeekResult;
        try {
            const r = await this.registry.sendCommand(pid, "peekRewards", {}, PEEK_TIMEOUT_MS);
            if (!r.ok) {
                console.warn(`[rewardClaimer] ${cfg.characterName} peek failed: ${r.detail}`);
                return;
            }
            peek = parsePeek(r.detail);
        } catch (e: any) {
            console.warn(`[rewardClaimer] ${cfg.characterName} peek error: ${e.message}`);
            return;
        }

        // content 未就绪(刚上线 / UI 未建):别据此判「无可领」,缩短间隔尽快重试。
        const onlineReady = !cfg.claimOnline || peek.onlineResolved;
        const signinReady = !cfg.claimSignin || peek.signinResolved;
        if (!onlineReady || !signinReady) {
            this.nextPeekAt.set(cfg.characterName, now + RETRY_NOT_READY_MS);
            return;
        }

        const wantOnline = cfg.claimOnline && peek.online;
        const wantSignin = cfg.claimSignin && peek.signin;

        if (!wantOnline && !wantSignin) {
            // 现在没有可领的:标「无可领」供 UI 展示(只在翻日时落盘),继续轮询(在线档后面会解锁)。
            const today = dayKey(now);
            if (cfg.doneDate !== today) {
                cfg.doneDate = today;
                this.save();
            }
            return;
        }

        // 有可领:开窗冷却内则先不开(防领取没清掉 +0x38 时每轮空开窗)。
        const nextClaim = this.nextClaimAt.get(cfg.characterName) ?? 0;
        if (now < nextClaim) return;
        this.nextClaimAt.set(cfg.characterName, now + CLAIM_COOLDOWN_MS);

        cfg.doneDate = ""; // 有可领,先清掉「无可领」展示(runClaim 内会落盘)
        await this.runClaim(cfg, pid, wantOnline, wantSignin);
        // 领完立刻复查真实状态:claim 内部已 sleep 等服务器回包刷新 +0x38,这里再 peek 一次,
        // 若已无可领就马上标「已领完」(否则要等下一轮 60s peek 才更新,看着像没领干净)。
        await this.confirmDone(cfg, pid);
    }

    // 领取后复查:再 peek 一次,若勾选的种类都已无可领,就把当天标成「已领完」。
    private async confirmDone(cfg: RewardClaimCharacterConfig, pid: number): Promise<void> {
        try {
            const r = await this.registry.sendCommand(pid, "peekRewards", {}, PEEK_TIMEOUT_MS);
            if (!r.ok) return;
            const peek = parsePeek(r.detail);
            const stillOnline = cfg.claimOnline && peek.online;
            const stillSignin = cfg.claimSignin && peek.signin;
            if (!stillOnline && !stillSignin) {
                cfg.doneDate = dayKey(Date.now());
                this.save();
            }
        } catch {
            /* 复查失败无所谓,下一轮 peek 会再判定 */
        }
    }

    // 真正开窗领取指定种类(doOnline/doSignin),更新展示字段并落盘。
    private async runClaim(
        cfg: RewardClaimCharacterConfig, pid: number, doOnline: boolean, doSignin: boolean): Promise<boolean> {
        const now = Date.now();
        try {
            const r = await this.registry.sendCommand(
                pid, "claimRewards", { online: doOnline, signin: doSignin }, CLAIM_TIMEOUT_MS);
            cfg.lastAttempt = now;
            if (!r.ok) {
                cfg.lastResult = `失败: ${r.detail ?? "unknown"}`;
                console.warn(`[rewardClaimer] ${cfg.characterName} claim failed: ${r.detail}`);
                this.save();
                return false;
            }
            const d = parseClaimDetail(r.detail);
            // 计数是「领取前快照」:claimable = 本次对 status==1 的档发起领取的数量(在线逐档发包 / 签到一次领),
            // 不是「还剩多少没领」。所以按「本次领 N」展示,别再用会误读成「剩 N」的 领/可/锁 旧标签。
            const parts: string[] = [];
            if (doOnline)
                parts.push(`在线 本次领${d.online.claimable}档`);
            if (doSignin)
                parts.push(`签到 本次领${d.signin.claimable}项`);
            cfg.lastResult = parts.join(" ") || "未选择任何奖励";
            console.log(`[rewardClaimer] ${cfg.characterName} claimed: ${cfg.lastResult}`);
            this.save();
            return true;
        } catch (e: any) {
            cfg.lastAttempt = now;
            cfg.lastResult = `异常: ${e.message}`;
            console.warn(`[rewardClaimer] ${cfg.characterName} claim error: ${e.message}`);
            this.save();
            return false;
        }
    }

    // ---------------- HTTP 用的 CRUD ----------------

    listConfigs(): RewardClaimCharacterConfig[] {
        return this.configs;
    }

    getConfig(characterName: string): RewardClaimCharacterConfig | undefined {
        return this.configs.find((c) => c.characterName === characterName);
    }

    setConfig(characterName: string, body: Partial<RewardClaimCharacterConfig>): RewardClaimCharacterConfig {
        const existing = this.configs.find((c) => c.characterName === characterName);
        const willEnable = body.enabled ?? existing?.enabled ?? false;
        const claimOnline = body.claimOnline ?? existing?.claimOnline ?? true;
        const claimSignin = body.claimSignin ?? existing?.claimSignin ?? true;
        // 重新开启,或改了「领哪种」,都当作「现在帮我重查一遍」:清在线计时 + peek/冷却,马上重走一轮 peek。
        const reEnabled = willEnable && !(existing?.enabled);
        const selectionChanged =
            !!existing && (existing.claimOnline !== claimOnline || existing.claimSignin !== claimSignin);
        const recheck = reEnabled || selectionChanged;
        const cfg: RewardClaimCharacterConfig = {
            characterName,
            enabled: willEnable,
            claimOnline,
            claimSignin,
            doneDate: recheck ? "" : (existing?.doneDate ?? ""),
            lastAttempt: existing?.lastAttempt ?? 0,
            lastResult: existing?.lastResult ?? "",
        };
        if (recheck) {
            this.onlineSince.delete(characterName);
            this.nextPeekAt.delete(characterName);
            this.nextClaimAt.delete(characterName);
        }
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
        this.onlineSince.delete(characterName);
        this.nextPeekAt.delete(characterName);
        this.nextClaimAt.delete(characterName);
        if (this.configs.length !== before) {
            this.save();
            return true;
        }
        return false;
    }

    // 立即领取(测试 / 手动用):无视 peek 与冷却,直接开窗按勾选领,角色须在线。
    async runNow(characterName: string): Promise<{ ok: boolean; detail?: string }> {
        const inst = this.registry.list().find((i) => i.characterName === characterName);
        if (!inst) return { ok: false, detail: "角色不在线" };
        if (this.inFlight.has(characterName)) return { ok: false, detail: "正在处理中" };

        let cfg = this.configs.find((c) => c.characterName === characterName);
        if (!cfg) {
            cfg = normalizeConfig({ characterName, enabled: false });
            this.configs.push(cfg);
        }
        this.inFlight.add(characterName);
        try {
            const ok = await this.runClaim(cfg, inst.pid, cfg.claimOnline, cfg.claimSignin);
            // 手动领过:压上冷却、复查真实状态(无可领即标已领完),让自动轮询从干净状态继续。
            this.nextClaimAt.set(characterName, Date.now() + CLAIM_COOLDOWN_MS);
            this.nextPeekAt.delete(characterName);
            cfg.doneDate = "";
            await this.confirmDone(cfg, inst.pid);
            return { ok, detail: cfg.lastResult };
        } finally {
            this.inFlight.delete(characterName);
        }
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
    }
}

function normalizeConfig(c: Partial<RewardClaimCharacterConfig>): RewardClaimCharacterConfig {
    return {
        characterName: typeof c.characterName === "string" ? c.characterName : "",
        enabled: c.enabled ?? false,
        claimOnline: c.claimOnline ?? true,
        claimSignin: c.claimSignin ?? true,
        doneDate: typeof c.doneDate === "string" ? c.doneDate : "",
        lastAttempt: Number(c.lastAttempt ?? 0) || 0,
        lastResult: typeof c.lastResult === "string" ? c.lastResult : "",
    };
}

function dayKey(ms: number): string {
    const d = new Date(ms);
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${y}-${m}-${day}`;
}

function parsePeek(detail?: string): PeekResult {
    try {
        const j = JSON.parse(detail || "{}");
        return {
            online: !!j.online,
            signin: !!j.signin,
            onlineResolved: !!j.onlineResolved,
            signinResolved: !!j.signinResolved,
        };
    } catch {
        return { online: false, signin: false, onlineResolved: false, signinResolved: false };
    }
}

function parseClaimDetail(detail?: string): ClaimDetail {
    const empty: RewardTierResult = { claimed: 0, claimable: 0, locked: 0 };
    try {
        const j = JSON.parse(detail || "{}");
        return {
            online: { ...empty, ...(j.online || {}) },
            signin: { ...empty, ...(j.signin || {}) },
        };
    } catch {
        return { online: { ...empty }, signin: { ...empty } };
    }
}
