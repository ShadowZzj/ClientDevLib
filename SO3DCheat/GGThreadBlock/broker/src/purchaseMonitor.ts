import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";
import { PaodianMonitor } from "./paodianMonitor";

// 监控购买:监控某游戏角色 cash 背包里某物品数量,低于阈值就用该角色对应的泡点账号
// (= broker 从窗口标题解析出的 accountName)去泡点网店买,买到的货进账号共享仓库(bank),
// 再开仓库把物品搬进 cash 背包。每个监控项有最小购买间隔(默认 1 天):一旦触发过购买/转移,
// 即便下次仍检测到不足,也要等间隔过去才会再次自动执行,防止误买真钱货币。
//
// itemId 口径:泡点 webshop 的 itemID == 游戏模板 itemId == cash/bank 的 itemId(同一 id 空间,
// 见 so3d_online_login 离线版 SUMMON_ITEM_ID=8036 两用)。所以全程用 itemId 串联最稳。

export interface PurchaseMonitorEntry {
    characterName: string;
    itemName: string;
    itemId?: number; // 泡点/游戏 itemId;留空则按 itemName 在泡点商品表里解析
    threshold: number; // cash 背包该物品数量 < threshold 时触发
    buyCount: number; // 每次从泡点买多少
    minIntervalMs: number; // 最小购买间隔(默认 1 天)
    enabled: boolean;
    lastTriggered: number; // epoch ms, 0 = never
    lastStatus: "" | "ok" | "error" | "running";
    lastResult: string; // 最近一次运行摘要(成功消息或错误)
    lastCashCount: number; // 最近一次探测读到的 cash 背包该物品总数
    lastChecked: number; // epoch ms, 0 = never;最近一次探测时刻
}

export interface PurchaseMonitorConfig {
    pollIntervalMs: number;
    autoEnabled: boolean;
}

export interface PurchaseStep {
    action: string;
    ok: boolean;
    detail?: string;
}

export interface PurchaseRunResult {
    ok: boolean;
    characterName: string;
    account: string;
    itemName: string;
    itemId: number;
    buyCount: number;
    moved: number; // 实际搬进 cash 的件数
    cashBefore: number;
    cashAfter: number;
    steps: PurchaseStep[];
    error?: string;
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "purchase_monitor.json"
);

const DAY_MS = 24 * 60 * 60 * 1000;
const DEFAULT_POLL_MS = 60_000;
const MIN_POLL_MS = 15_000;
const MAX_POLL_MS = DAY_MS;
const DEFAULT_MIN_INTERVAL_MS = DAY_MS;

// 角色刚登录时 cash 背包容器可能还没同步,getCashBagItems 会返回空(或读不到该物品),
// 跟"真的没货"无法区分。precheck 读到不足时不立刻买,隔 PRECHECK_RETRY_MS 重读,
// 连续 PRECHECK_MAX_TRIES 次都不足才认定真的缺货并触发购买。背包同步好后任一次读到
// 够阈值就退出,不会误买。
const PRECHECK_RETRY_MS = 3_000;
const PRECHECK_MAX_TRIES = 10;

// 开仓库后等 511320 整桶回流并搬运的时间参数。
const BANK_WAIT_TIMEOUT_MS = 30_000; // 买完到货进 bank 的总等待上限
const BANK_REOPEN_MS = 3_000; // 等待期间重发开仓库的频率
const CASH_VERIFY_TIMEOUT_MS = 8_000; // 搬运后等 cash 背包数量更新的上限

export class PurchaseMonitor {
    private entries: PurchaseMonitorEntry[] = [];
    private config: PurchaseMonitorConfig = { pollIntervalMs: DEFAULT_POLL_MS, autoEnabled: true };
    private timer: NodeJS.Timeout | null = null;
    private inFlight = new Set<string>();

    constructor(
        private readonly registry: InstanceRegistry,
        private readonly paodian: PaodianMonitor
    ) {
        this.load();
        this.start();
    }

    // ---------- 持久化 ----------

    private load(): void {
        try {
            if (!fs.existsSync(PERSIST_FILE)) return;
            const parsed = JSON.parse(fs.readFileSync(PERSIST_FILE, "utf8"));
            this.config = normalizeConfig(parsed?.config);
            const src = Array.isArray(parsed) ? parsed : parsed?.entries;
            if (Array.isArray(src)) {
                this.entries = src
                    .map((e: any) => normalizeEntry(e))
                    .filter((e: PurchaseMonitorEntry | null): e is PurchaseMonitorEntry => !!e);
            }
            console.log(`[purchaseMonitor] loaded ${this.entries.length} entries`);
        } catch (e: any) {
            console.warn(`[purchaseMonitor] load failed: ${e.message}`);
            this.entries = [];
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(
                PERSIST_FILE,
                JSON.stringify({ config: this.config, entries: this.entries }, null, 2),
                "utf8"
            );
        } catch (e: any) {
            console.warn(`[purchaseMonitor] save failed: ${e.message}`);
        }
    }

    private start(): void {
        if (this.timer) clearInterval(this.timer);
        this.timer = setInterval(() => {
            this.tick().catch((e) => console.warn(`[purchaseMonitor] tick failed: ${e.message}`));
        }, this.config.pollIntervalMs);
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }

    // ---------- 配置 / 条目 CRUD ----------

    getConfig(): PurchaseMonitorConfig {
        return { ...this.config };
    }

    setConfig(value: Partial<PurchaseMonitorConfig>): PurchaseMonitorConfig {
        this.config = normalizeConfig({ ...this.config, ...value });
        this.save();
        this.start();
        return this.getConfig();
    }

    getEntries(): PurchaseMonitorEntry[] {
        return this.entries.map((e) => ({ ...e }));
    }

    addOrUpdateEntry(raw: Partial<PurchaseMonitorEntry>): PurchaseMonitorEntry {
        const entry = normalizeEntry(raw);
        if (!entry) throw new Error("invalid entry: characterName and itemName required");
        const idx = this.entries.findIndex(
            (e) => e.characterName === entry.characterName && e.itemName === entry.itemName
        );
        if (idx >= 0) {
            // 保留运行态字段(lastTriggered/lastResult/探测态),只覆盖用户可编辑项。
            const prev = this.entries[idx];
            entry.lastTriggered = prev.lastTriggered;
            entry.lastStatus = prev.lastStatus;
            entry.lastResult = prev.lastResult;
            entry.lastCashCount = prev.lastCashCount;
            entry.lastChecked = prev.lastChecked;
            this.entries[idx] = entry;
        } else {
            this.entries.push(entry);
        }
        this.save();
        return { ...entry };
    }

    removeEntry(characterName: string, itemName: string): boolean {
        const before = this.entries.length;
        this.entries = this.entries.filter(
            (e) => !(e.characterName === characterName && e.itemName === itemName)
        );
        if (this.entries.length !== before) {
            this.save();
            return true;
        }
        return false;
    }

    // ---------- 手动触发(测试用,忽略间隔且不更新 lastTriggered) ----------

    async manualRun(req: {
        characterName: string;
        itemName: string;
        itemId?: number;
        count: number;
    }): Promise<PurchaseRunResult> {
        const characterName = String(req.characterName || "").trim();
        const itemName = String(req.itemName || "").trim();
        const count = Math.trunc(Number(req.count));
        if (!characterName) throw new Error("missing characterName");
        if (!itemName && !req.itemId) throw new Error("missing itemName or itemId");
        if (!Number.isFinite(count) || count <= 0) throw new Error("invalid count");
        return this.run({
            characterName,
            itemName: itemName || String(req.itemId),
            itemId: optionalPositiveInt(req.itemId),
            buyCount: count,
        });
    }

    // ---------- 自动轮询 ----------

    private async tick(): Promise<void> {
        if (!this.config.autoEnabled) return;
        const now = Date.now();
        const online = this.registry.list();
        const byChar = new Map<string, { pid: number; account?: string }>();
        for (const inst of online) {
            if (inst.characterName) {
                byChar.set(inst.characterName, { pid: inst.pid, account: inst.accountName });
            }
        }

        for (const entry of this.entries) {
            if (!entry.enabled) continue;
            const live = byChar.get(entry.characterName);
            if (!live) continue;

            const key = `${entry.characterName}::${entry.itemName}`;
            // confirmAndRun 正在跑(它自己会刷新探测数据),本轮跳过,避免并发读/重复触发。
            if (this.inFlight.has(key)) continue;

            // 解析 itemId(失败按"不确定"跳过本轮)。
            let resolvedId = optionalPositiveInt(entry.itemId);
            try {
                if (resolvedId === undefined) {
                    resolvedId = await this.resolvePaodianItemId(entry.itemName);
                }
            } catch (e: any) {
                console.warn(`[purchaseMonitor] resolve itemId failed for ${key}: ${e.message}`);
                continue;
            }
            const itemId = resolvedId;

            // 探测和购买解耦:每轮都读一次 cash 刷新显示(lastCashCount/lastChecked),
            // 即便仍在最小购买间隔内、不会触发购买,数量也要更新。
            let have: number;
            try {
                have = await this.countCashItem(live.pid, itemId, entry.itemName);
            } catch (e: any) {
                console.warn(`[purchaseMonitor] probe failed for ${key}: ${e.message}`);
                continue;
            }
            entry.lastCashCount = have;
            entry.lastChecked = Date.now();
            this.save();

            // 还在最小购买间隔内,只刷新数量,不进入购买流程。
            if (now - entry.lastTriggered < entry.minIntervalMs) continue;
            // 单次探测已够量,不必走多次确认。
            if (have >= entry.threshold) continue;

            // 单次读到缺货且间隔已过:异步多次确认(排除刚登录未同步)后购买。
            // 确认阶段隔几秒重读多次,不能在这里同步 await(会卡住其他 entry 本轮)。
            this.inFlight.add(key);
            void this.confirmAndRun(entry, live.pid, itemId, key, now);
        }
    }

    // 单次探测已判定缺货后,再连续 PRECHECK_MAX_TRIES 次读背包确认:排除"刚登录背包
    // 未同步"造成的空背包误判。任一次读到够阈值就放弃购买。读失败算"不确定",计入
    // 重试但不立即放弃。每次读都刷新 lastCashCount/lastChecked。
    private async confirmAndRun(
        entry: PurchaseMonitorEntry,
        pid: number,
        itemId: number | undefined,
        key: string,
        now: number
    ): Promise<void> {
        try {
            let confirmedShort = false;
            for (let attempt = 0; attempt < PRECHECK_MAX_TRIES; attempt++) {
                if (attempt > 0) await delay(PRECHECK_RETRY_MS);
                try {
                    const have = await this.countCashItem(pid, itemId, entry.itemName);
                    // 记录本次探测:供 web 显示 cash 当前总数 + 上次探测时间。
                    entry.lastCashCount = have;
                    entry.lastChecked = Date.now();
                    this.save();
                    if (have >= entry.threshold) {
                        confirmedShort = false;
                        break; // 背包已同步且够量,不买
                    }
                    if (attempt === PRECHECK_MAX_TRIES - 1) confirmedShort = true;
                } catch (e: any) {
                    console.warn(`[purchaseMonitor] precheck attempt ${attempt + 1} failed for ${key}: ${e.message}`);
                    if (attempt === PRECHECK_MAX_TRIES - 1) confirmedShort = true;
                }
            }
            if (!confirmedShort) return;

            // 触发前就把 lastTriggered 置为 now:即便购买/转移失败也要等满间隔再重试,
            // 避免在失败时反复刷真钱购买。手动按钮可用于失败后立即重试。
            entry.lastTriggered = now;
            entry.lastStatus = "running";
            this.save();

            try {
                const res = await this.run({
                    characterName: entry.characterName,
                    itemName: entry.itemName,
                    itemId,
                    buyCount: entry.buyCount,
                });
                entry.lastStatus = res.ok ? "ok" : "error";
                entry.lastResult = res.ok
                    ? `买${res.buyCount}/搬${res.moved} cash ${res.cashBefore}->${res.cashAfter}`
                    : res.error || "failed";
            } catch (e: any) {
                entry.lastStatus = "error";
                entry.lastResult = e?.message || String(e);
            } finally {
                this.save();
            }
        } finally {
            this.inFlight.delete(key);
        }
    }

    // ---------- 核心流程:购买 -> 开仓库 -> 等 511320 -> 搬运 -> 验证 cash ----------

    private async run(req: {
        characterName: string;
        itemName: string;
        itemId?: number;
        buyCount: number;
    }): Promise<PurchaseRunResult> {
        const steps: PurchaseStep[] = [];
        const live = this.findLive(req.characterName);
        const result: PurchaseRunResult = {
            ok: false,
            characterName: req.characterName,
            account: "",
            itemName: req.itemName,
            itemId: req.itemId ?? 0,
            buyCount: req.buyCount,
            moved: 0,
            cashBefore: 0,
            cashAfter: 0,
            steps,
        };

        try {
            // 0. 从游戏内存读登录账号/密码(私服里 = 泡点网店账号),不依赖 paodian 文件手填密码。
            const cred = await this.getLoginCredentials(live.pid);
            result.account = cred.account;
            steps.push({
                action: "loginCredentials",
                ok: true,
                detail: `account=${cred.account} pwdLen=${cred.password.length}`,
            });

            // 1. 解析 itemId(泡点商品表)。
            const itemId = req.itemId ?? (await this.resolvePaodianItemId(req.itemName));
            result.itemId = itemId;
            steps.push({ action: "resolveItemId", ok: true, detail: String(itemId) });

            // 2. 记录购买前 cash 数量。
            result.cashBefore = await this.countCashItem(live.pid, itemId, req.itemName);
            steps.push({ action: "cashBefore", ok: true, detail: String(result.cashBefore) });

            // 3. 泡点网店购买(用游戏读出的账号密码;货进账号共享仓库)。
            const buy = await this.paodian.purchaseWithPassword(
                cred.account, cred.password, itemId, req.buyCount);
            steps.push({ action: "paodianPurchase", ok: true, detail: buy.message });

            // 4. 开仓库,等 511320 整桶里出现该 itemId,搬进 cash。无论成败都要关仓库,
            //    否则角色会卡在"仓库交互中"无法移动(开仓库走的是本地 UI call)。
            let moved = 0;
            try {
                moved = await this.drainBankToCash(live.pid, itemId, steps);
            } finally {
                try {
                    const close = await this.registry.sendCommand(live.pid, "closeBank", {}, 5000);
                    steps.push({ action: "closeBank", ok: close.ok, detail: close.detail });
                } catch (e) {
                    steps.push({ action: "closeBank", ok: false, detail: e instanceof Error ? e.message : String(e) });
                }
            }
            result.moved = moved;

            // 5. 验证 cash 数量上升。
            result.cashAfter = await this.waitCashCount(
                live.pid,
                itemId,
                req.itemName,
                result.cashBefore + Math.min(moved, req.buyCount),
                steps
            );

            result.ok = moved > 0;
            if (!result.ok) result.error = "bank 里没找到该物品(可能尚未到货或 itemId 不符)";
            return result;
        } catch (e: any) {
            result.ok = false;
            result.error = e?.message || String(e);
            steps.push({ action: "error", ok: false, detail: result.error });
            return result;
        }
    }

    private async drainBankToCash(
        pid: number,
        itemId: number,
        steps: PurchaseStep[]
    ): Promise<number> {
        const deadline = Date.now() + BANK_WAIT_TIMEOUT_MS;
        const movedUids = new Set<number>();
        let moved = 0;
        let lastReopen = 0;

        while (Date.now() <= deadline) {
            const now = Date.now();
            if (now - lastReopen >= BANK_REOPEN_MS) {
                const open = await this.registry.sendCommand(pid, "openBank", {}, 5000);
                steps.push({ action: "openBank", ok: open.ok, detail: open.detail });
                lastReopen = now;
            }

            const bank = await this.getBankItems(pid);
            const matches = bank.filter((b) => b.itemId === itemId && !movedUids.has(b.uid));
            for (const m of matches) {
                const r = await this.registry.sendCommand(
                    pid,
                    "bankMoveToCash",
                    { uid: m.uid, itemId, count: m.count },
                    5000
                );
                steps.push({
                    action: "bankMoveToCash",
                    ok: r.ok,
                    detail: r.ok ? r.detail : `uid=${m.uid} ${r.detail || "failed"}`,
                });
                if (r.ok) {
                    movedUids.add(m.uid);
                    moved += m.count;
                }
            }
            if (moved > 0 && matches.length === 0) break; // 该 itemId 已全部搬完
            await delay(500);
        }

        if (moved === 0) {
            steps.push({ action: "drainBank", ok: false, detail: "timeout, no matching bank item" });
        }
        return moved;
    }

    // ---------- 辅助 ----------

    private findLive(characterName: string): { pid: number; account?: string } {
        const inst = this.registry.list().find((i) => i.characterName === characterName);
        if (!inst) throw new Error(`角色 ${characterName} 不在线`);
        return { pid: inst.pid, account: inst.accountName };
    }

    // 从游戏内存(localUser)读登录账号/密码。私服里这套 = 泡点网店账号,直接用于购买。
    private async getLoginCredentials(pid: number): Promise<{ account: string; password: string }> {
        const r = await this.registry.sendCommand(pid, "getLoginCredentials", {}, 5000);
        if (!r.ok) throw new Error(`读取游戏登录凭据失败: ${r.detail || "unknown"}`);
        const c = parseJson<{ account?: string; password?: string }>(r.detail, {});
        if (!c.account) throw new Error("游戏登录账号为空(未登录?)");
        return { account: c.account, password: c.password || "" };
    }

    async resolvePaodianItemId(itemName: string): Promise<number> {
        const items = await this.paodian.getShopItems();
        const needle = normalizeName(itemName);
        const found = items.find((it) => {
            const n = normalizeName(it.name || "");
            return n === needle || n.includes(needle) || needle.includes(n);
        });
        if (!found) throw new Error(`泡点商品表里找不到 "${itemName}"`);
        return found.itemid;
    }

    private async countCashItem(pid: number, itemId: number | undefined, itemName: string): Promise<number> {
        const r = await this.registry.sendCommand(pid, "getCashBagItems", {}, 5000);
        if (!r.ok) throw new Error(`getCashBagItems failed: ${r.detail || "unknown"}`);
        const items = parseJson<Array<{ itemId?: number; count?: number; name?: string }>>(r.detail, []);
        const needle = normalizeName(itemName);
        return items
            .filter((it) =>
                itemId !== undefined && itemId > 0
                    ? Number(it.itemId) === itemId
                    : normalizeName(it.name || "") === needle
            )
            .reduce((sum, it) => sum + Math.max(1, Math.trunc(Number(it.count) || 1)), 0);
    }

    private async getBankItems(pid: number): Promise<Array<{ uid: number; itemId: number; count: number }>> {
        const r = await this.registry.sendCommand(pid, "getBankItems", {}, 5000);
        if (!r.ok) throw new Error(`getBankItems failed: ${r.detail || "unknown"}`);
        return parseJson<Array<{ uid: number; itemId: number; count: number }>>(r.detail, []);
    }

    private async waitCashCount(
        pid: number,
        itemId: number,
        itemName: string,
        expected: number,
        steps: PurchaseStep[]
    ): Promise<number> {
        const deadline = Date.now() + CASH_VERIFY_TIMEOUT_MS;
        let last = 0;
        while (Date.now() <= deadline) {
            try {
                last = await this.countCashItem(pid, itemId, itemName);
                if (last >= expected) {
                    steps.push({ action: "cashAfter", ok: true, detail: String(last) });
                    return last;
                }
            } catch {
                // ignore, retry
            }
            await delay(300);
        }
        steps.push({ action: "cashAfter", ok: false, detail: `count=${last} expected>=${expected}` });
        return last;
    }
}

// ---------- 归一化 / 工具 ----------

function normalizeConfig(raw: any): PurchaseMonitorConfig {
    const pollIntervalMs = clamp(
        Math.round(Number(raw?.pollIntervalMs)) || DEFAULT_POLL_MS,
        MIN_POLL_MS,
        MAX_POLL_MS
    );
    const autoEnabled = typeof raw?.autoEnabled === "boolean" ? raw.autoEnabled : true;
    return { pollIntervalMs, autoEnabled };
}

function normalizeEntry(raw: any): PurchaseMonitorEntry | null {
    const characterName = String(raw?.characterName || "").trim();
    const itemName = String(raw?.itemName || "").trim();
    if (!characterName || !itemName) return null;
    return {
        characterName,
        itemName,
        itemId: optionalPositiveInt(raw?.itemId),
        threshold: Math.max(0, Math.trunc(Number(raw?.threshold)) || 0),
        buyCount: Math.max(1, Math.trunc(Number(raw?.buyCount)) || 1),
        minIntervalMs: clamp(
            Math.round(Number(raw?.minIntervalMs)) || DEFAULT_MIN_INTERVAL_MS,
            60_000,
            30 * DAY_MS
        ),
        enabled: typeof raw?.enabled === "boolean" ? raw.enabled : true,
        lastTriggered: Math.max(0, Math.trunc(Number(raw?.lastTriggered)) || 0),
        lastStatus: raw?.lastStatus === "ok" || raw?.lastStatus === "error" ? raw.lastStatus : "",
        lastResult: typeof raw?.lastResult === "string" ? raw.lastResult : "",
        lastCashCount: Math.max(0, Math.trunc(Number(raw?.lastCashCount)) || 0),
        lastChecked: Math.max(0, Math.trunc(Number(raw?.lastChecked)) || 0),
    };
}

function optionalPositiveInt(value: unknown): number | undefined {
    const n = Number(value);
    return Number.isFinite(n) && n > 0 ? Math.trunc(n) : undefined;
}

function clamp(v: number, lo: number, hi: number): number {
    return Math.min(hi, Math.max(lo, v));
}

function parseJson<T>(raw: unknown, fallback: T): T {
    try {
        if (typeof raw !== "string") return fallback;
        return JSON.parse(raw) as T;
    } catch {
        return fallback;
    }
}

// 繁简 + 大小写 + 空白归一,跟 vendorPurchaser 同口径,便于物品名匹配。
function normalizeName(value: string): string {
    return value
        .normalize("NFKC")
        .trim()
        .toLocaleLowerCase()
        .replace(/\s+/g, "")
        .replace(/攤/g, "摊")
        .replace(/販/g, "贩")
        .replace(/呼叫卷/g, "呼叫券")
        .replace(/釣/g, "钓")
        .replace(/鉤/g, "钩")
        .replace(/黃/g, "黄");
}

function delay(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, Math.max(0, ms)));
}
