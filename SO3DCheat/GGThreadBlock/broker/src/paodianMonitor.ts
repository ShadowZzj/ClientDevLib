import fs from "fs";
import path from "path";
import https from "https";

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "paodian_accounts.json"
);

// 网店商品列表(webshop-data)基本不变,落盘缓存:broker 重启后直接用,不必每次重拉。
// 过期(SHOP_ITEMS_CACHE_MS)或前端带 force 才重新请求网络。
const SHOP_CACHE_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "paodian_shop_cache.json"
);

// 购买记录(含批量)持久化:每条记录一次购买尝试的结果,重启后仍可在「购买记录」tab 看到。
const RECORDS_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "paodian_purchase_records.json"
);
const MAX_RECORDS = 2000; // 只保留最近这么多条,防止文件无限增长

const LOGIN_URL = "https://shop2.guguseal.com/api/login";
const WEBSHOP_DATA_URL = "https://shop2.guguseal.com/api/webshop-data";
const PURCHASE_URL = "https://shop2.guguseal.com/api/purchase";
const DEFAULT_REFRESH_INTERVAL_MS = 10 * 60 * 1000;
const MIN_REFRESH_INTERVAL_MS = 60 * 1000;
const MAX_REFRESH_INTERVAL_MS = 24 * 60 * 60 * 1000;
const SHOP_ITEMS_CACHE_MS = 24 * 60 * 60 * 1000; // 商品列表基本不变,缓存 24h;前端"刷新"按钮可强制重拉

interface PaodianAccount {
    username: string;
    password: string;
}

export interface PaodianConfig {
    refreshIntervalMs: number;
    autoRefreshEnabled: boolean;
}

export interface PaodianState {
    username: string;
    id: string;
    point: number;
    paodian: number;
    vip: number;
    paymoney: number;
    next_vip_level: number;
    next_vip_need: number;
    eps_endtime: string | null;
    status: "idle" | "loading" | "ok" | "error";
    lastUpdated: number;
    error: string;
}

export interface PaodianAccountView extends PaodianState {
    hasPassword: boolean;
}

export interface PaodianShopItem {
    itemid: number;
    price: number;
    bubble_price: number;
    pr: string;
    name: string;
    text: string;
    class: number;
    pd: number;
    qx: number;
}

export interface PaodianPurchaseResult {
    message: string;
    account: PaodianAccountView;
}

// 一条购买记录:单账号单次下单的结果(批量购买会为每个账号各写一条)。
export interface PaodianPurchaseRecord {
    id: string;          // 唯一 id(时间戳 + 随机后缀)
    time: number;        // 下单时间
    username: string;
    itemID: number;
    itemName: string;
    itemCount: number;
    bubblePrice: number; // 单价(下单时)
    totalBubble: number; // 本次合计泡点 = bubblePrice * itemCount
    batchId: string;     // 同一次批量购买共享;单账号购买也有,便于按批查看
    ok: boolean;
    message: string;     // 成功消息或错误信息
}

// 批量购买的整体结果。每个账号一条 PaodianPurchaseRecord。
export interface PaodianBatchPurchaseResult {
    batchId: string;
    requested: number;   // 请求的账号数
    ok: number;          // 成功数
    fail: number;        // 失败数
    records: PaodianPurchaseRecord[];
    accounts: PaodianAccountView[]; // 受影响账号刷新后的最新视图
}

function emptyState(username: string): PaodianState {
    return {
        username,
        id: username,
        point: 0,
        paodian: 0,
        vip: 0,
        paymoney: 0,
        next_vip_level: 0,
        next_vip_need: 0,
        eps_endtime: null,
        status: "idle",
        lastUpdated: 0,
        error: "",
    };
}

function asNumber(value: unknown, fallback = 0): number {
    const n = Number(value);
    return Number.isFinite(n) ? n : fallback;
}

// 把持久化文件里读回的 state 收敛成合法 PaodianState。重启读盘后直接拿来显示
// (泡点 / 刷新时间等),不用等下一次刷新。"loading" 是瞬时态,落盘的不应该有,
// 防御性地降级成 "idle"。
function coerceState(username: string, raw: any): PaodianState {
    if (!raw || typeof raw !== "object") return emptyState(username);
    const status: PaodianState["status"] =
        raw.status === "ok" || raw.status === "error" ? raw.status : "idle";
    return {
        username,
        id: String(raw.id ?? username),
        point: asNumber(raw.point),
        paodian: asNumber(raw.paodian),
        vip: asNumber(raw.vip),
        paymoney: asNumber(raw.paymoney),
        next_vip_level: asNumber(raw.next_vip_level),
        next_vip_need: asNumber(raw.next_vip_need),
        eps_endtime: raw.eps_endtime === null || raw.eps_endtime === undefined ? null : String(raw.eps_endtime),
        status,
        lastUpdated: asNumber(raw.lastUpdated),
        error: status === "error" ? String(raw.error ?? "") : "",
    };
}

function normalizeConfig(value: any): PaodianConfig {
    const raw = asNumber(value?.refreshIntervalMs, DEFAULT_REFRESH_INTERVAL_MS);
    const refreshIntervalMs = Math.min(
        MAX_REFRESH_INTERVAL_MS,
        Math.max(MIN_REFRESH_INTERVAL_MS, Math.round(raw))
    );
    const autoRefreshEnabled = typeof value?.autoRefreshEnabled === "boolean"
        ? value.autoRefreshEnabled
        : true;
    return { refreshIntervalMs, autoRefreshEnabled };
}

function commonHeaders(): Record<string, string> {
    return {
        "accept": "*/*",
        "accept-language": "zh-CN,zh;q=0.9",
        "cache-control": "no-cache",
        "pragma": "no-cache",
        "referer": "https://shop2.guguseal.com/",
        "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36",
    };
}

function getJson(urlText: string): Promise<any> {
    return new Promise((resolve, reject) => {
        const url = new URL(urlText);
        const req = https.request(
            {
                method: "GET",
                protocol: url.protocol,
                hostname: url.hostname,
                port: url.port || 443,
                path: url.pathname + url.search,
                headers: commonHeaders(),
            },
            (res) => {
                let text = "";
                res.setEncoding("utf8");
                res.on("data", (chunk) => (text += chunk));
                res.on("end", () => {
                    if (!res.statusCode || res.statusCode < 200 || res.statusCode >= 300) {
                        reject(new Error(`HTTP ${res.statusCode || 0}: ${text.trim()}`));
                        return;
                    }
                    try {
                        resolve(JSON.parse(text));
                    } catch {
                        reject(new Error(`invalid json: ${text.slice(0, 200)}`));
                    }
                });
            }
        );

        req.setTimeout(15000, () => req.destroy(new Error("request timeout")));
        req.on("error", reject);
        req.end();
    });
}

function postJson(urlText: string, payload: unknown): Promise<any> {
    return new Promise((resolve, reject) => {
        const url = new URL(urlText);
        const body = JSON.stringify(payload);
        const req = https.request(
            {
                method: "POST",
                protocol: url.protocol,
                hostname: url.hostname,
                port: url.port || 443,
                path: url.pathname + url.search,
                headers: {
                    ...commonHeaders(),
                    "content-type": "application/json",
                    "origin": "https://shop2.guguseal.com",
                    "content-length": Buffer.byteLength(body),
                },
            },
            (res) => {
                let text = "";
                res.setEncoding("utf8");
                res.on("data", (chunk) => (text += chunk));
                res.on("end", () => {
                    if (!res.statusCode || res.statusCode < 200 || res.statusCode >= 300) {
                        reject(new Error(`HTTP ${res.statusCode || 0}: ${text.trim()}`));
                        return;
                    }
                    try {
                        resolve(JSON.parse(text));
                    } catch {
                        reject(new Error(`invalid json: ${text.slice(0, 200)}`));
                    }
                });
            }
        );

        req.setTimeout(15000, () => req.destroy(new Error("request timeout")));
        req.on("error", reject);
        req.write(body);
        req.end();
    });
}

export class PaodianMonitor {
    private accounts: PaodianAccount[] = [];
    private config: PaodianConfig = normalizeConfig({});
    private states = new Map<string, PaodianState>();
    private refreshing = new Set<string>();
    private timer: NodeJS.Timeout | null = null;
    private shopItems: PaodianShopItem[] = [];
    private shopItemsFetchedAt = 0;
    private records: PaodianPurchaseRecord[] = [];

    constructor() {
        this.load();
        this.loadShopCache();
        this.loadRecords();
        this.start();
    }

    private loadShopCache(): void {
        try {
            if (!fs.existsSync(SHOP_CACHE_FILE)) return;
            const parsed = JSON.parse(fs.readFileSync(SHOP_CACHE_FILE, "utf8"));
            const items = Array.isArray(parsed?.items) ? parsed.items : null;
            if (!items) return;
            this.shopItems = items;
            this.shopItemsFetchedAt = asNumber(parsed?.fetchedAt);
            console.log(`[paodian] loaded ${this.shopItems.length} cached shop items (fetchedAt=${new Date(this.shopItemsFetchedAt).toISOString()})`);
        } catch (e: any) {
            console.warn(`[paodian] shop cache load failed: ${e.message}`);
        }
    }

    private saveShopCache(): void {
        try {
            fs.writeFileSync(
                SHOP_CACHE_FILE,
                JSON.stringify({ fetchedAt: this.shopItemsFetchedAt, items: this.shopItems }, null, 2),
                "utf8"
            );
        } catch (e: any) {
            console.warn(`[paodian] shop cache save failed: ${e.message}`);
        }
    }

    private loadRecords(): void {
        try {
            if (!fs.existsSync(RECORDS_FILE)) return;
            const parsed = JSON.parse(fs.readFileSync(RECORDS_FILE, "utf8"));
            const arr = Array.isArray(parsed?.records) ? parsed.records : Array.isArray(parsed) ? parsed : null;
            if (!arr) return;
            this.records = arr
                .filter((r: any) => r && typeof r === "object")
                .map((r: any) => ({
                    id: String(r.id ?? ""),
                    time: asNumber(r.time),
                    username: String(r.username ?? ""),
                    itemID: asNumber(r.itemID),
                    itemName: String(r.itemName ?? ""),
                    itemCount: asNumber(r.itemCount),
                    bubblePrice: asNumber(r.bubblePrice),
                    totalBubble: asNumber(r.totalBubble),
                    batchId: String(r.batchId ?? ""),
                    ok: !!r.ok,
                    message: String(r.message ?? ""),
                }))
                .filter((r: PaodianPurchaseRecord) => r.username.length > 0);
            console.log(`[paodian] loaded ${this.records.length} purchase records`);
        } catch (e: any) {
            console.warn(`[paodian] records load failed: ${e.message}`);
            this.records = [];
        }
    }

    private saveRecords(): void {
        try {
            fs.writeFileSync(
                RECORDS_FILE,
                JSON.stringify({ updated: Date.now(), records: this.records }, null, 2),
                "utf8"
            );
        } catch (e: any) {
            console.warn(`[paodian] records save failed: ${e.message}`);
        }
    }

    private addRecord(record: PaodianPurchaseRecord): void {
        this.records.unshift(record); // 最新的排前面
        if (this.records.length > MAX_RECORDS) this.records.length = MAX_RECORDS;
    }

    listRecords(): PaodianPurchaseRecord[] {
        return this.records.slice();
    }

    clearRecords(): number {
        const n = this.records.length;
        this.records = [];
        this.saveRecords();
        return n;
    }

    private load(): void {
        try {
            if (!fs.existsSync(PERSIST_FILE)) return;
            const raw = fs.readFileSync(PERSIST_FILE, "utf8");
            const parsed = JSON.parse(raw);
            const source = Array.isArray(parsed) ? parsed : parsed.accounts;
            this.config = normalizeConfig(Array.isArray(parsed) ? {} : parsed.config);
            if (!Array.isArray(source)) return;

            this.accounts = source
                .filter((a: any) => typeof a?.username === "string" && typeof a?.password === "string")
                .map((a: any) => ({
                    username: a.username.trim(),
                    password: a.password,
                }))
                .filter((a: PaodianAccount) => a.username.length > 0);

            // 上次缓存的各账号泡点 / 刷新时间。可能是数组(viewFor 导出的)或
            // username->state 的 map,两种都兼容。
            const savedStates = new Map<string, any>();
            const rawStates = Array.isArray(parsed) ? null : parsed.states;
            if (Array.isArray(rawStates)) {
                for (const s of rawStates) {
                    if (s && typeof s.username === "string") savedStates.set(s.username.trim(), s);
                }
            } else if (rawStates && typeof rawStates === "object") {
                for (const [k, v] of Object.entries(rawStates)) savedStates.set(k.trim(), v);
            }

            for (const account of this.accounts) {
                const saved = savedStates.get(account.username);
                this.states.set(account.username, saved ? coerceState(account.username, saved) : emptyState(account.username));
            }
            console.log(`[paodian] loaded ${this.accounts.length} accounts (${savedStates.size} cached states)`);
        } catch (e: any) {
            console.warn(`[paodian] load failed: ${e.message}`);
            this.accounts = [];
            this.states.clear();
        }
    }

    private save(): void {
        try {
            // states 一并落盘:重启后能先显示上次的泡点 / 刷新时间,刷新后再覆盖。
            const states = this.accounts
                .map((a) => this.states.get(a.username))
                .filter((s): s is PaodianState => !!s);
            fs.writeFileSync(
                PERSIST_FILE,
                JSON.stringify({ accounts: this.accounts, config: this.config, states }, null, 2),
                "utf8"
            );
        } catch (e: any) {
            console.warn(`[paodian] save failed: ${e.message}`);
        }
    }

    private start(): void {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
        if (!this.config.autoRefreshEnabled) {
            console.log("[paodian] auto refresh disabled");
            return;
        }
        this.timer = setInterval(() => {
            this.refreshOldest().catch((e) => console.warn(`[paodian] refreshOldest failed: ${e.message}`));
        }, this.config.refreshIntervalMs);
        console.log(`[paodian] auto refresh interval=${this.config.refreshIntervalMs}ms (one account per tick)`);
    }

    // 每个 tick 只刷新一个账号:挑 lastUpdated 最早(最旧)的那个。这样 N 个账号摊到
    // N*interval 才轮一遍,避免每次都把全部账号一起打到登录接口。正在刷新中的跳过。
    private async refreshOldest(): Promise<void> {
        let target: PaodianAccount | null = null;
        let oldest = Infinity;
        for (const account of this.accounts) {
            if (this.refreshing.has(account.username)) continue;
            const ts = this.states.get(account.username)?.lastUpdated ?? 0;
            if (ts < oldest) {
                oldest = ts;
                target = account;
            }
        }
        if (target) {
            await this.refresh(target.username);
        }
    }

    private viewFor(account: PaodianAccount): PaodianAccountView {
        return {
            ...emptyState(account.username),
            ...(this.states.get(account.username) || {}),
            username: account.username,
            hasPassword: account.password.length > 0,
        };
    }

    list(): PaodianAccountView[] {
        return this.accounts.map((account) => this.viewFor(account));
    }

    private findAccount(username: string): PaodianAccount {
        const cleanUsername = String(username || "").trim();
        const account = this.accounts.find((a) => a.username === cleanUsername);
        if (!account) {
            throw new Error("account not found");
        }
        return account;
    }

    getConfig(): PaodianConfig {
        return { ...this.config };
    }

    setConfig(value: Partial<PaodianConfig>): PaodianConfig {
        this.config = normalizeConfig({ ...this.config, ...value });
        this.save();
        this.start();
        return this.getConfig();
    }

    async getShopItems(force = false): Promise<PaodianShopItem[]> {
        const now = Date.now();
        if (!force && this.shopItems.length > 0 && now - this.shopItemsFetchedAt < SHOP_ITEMS_CACHE_MS) {
            return this.shopItems;
        }

        const data = await getJson(WEBSHOP_DATA_URL);
        if (!Array.isArray(data)) {
            throw new Error("invalid webshop-data response");
        }
        this.shopItems = data
            .map((item: any) => ({
                itemid: asNumber(item.itemid),
                price: asNumber(item.price),
                bubble_price: asNumber(item.bubble_price),
                pr: String(item.pr ?? ""),
                name: String(item.name ?? ""),
                text: String(item.text ?? ""),
                class: asNumber(item.class),
                pd: asNumber(item.pd),
                qx: asNumber(item.qx),
            }))
            .filter((item: PaodianShopItem) => item.itemid > 0 && item.name.length > 0);
        this.shopItemsFetchedAt = now;
        this.saveShopCache();
        return this.shopItems;
    }

    async purchase(username: string, itemID: number, itemCount: number): Promise<PaodianPurchaseResult> {
        const account = this.findAccount(username);
        const cleanItemID = Math.trunc(Number(itemID));
        const cleanCount = Math.trunc(Number(itemCount));
        if (!Number.isFinite(cleanItemID) || cleanItemID <= 0) {
            throw new Error("invalid itemID");
        }
        if (!Number.isFinite(cleanCount) || cleanCount <= 0) {
            throw new Error("invalid itemCount");
        }

        const items = await this.getShopItems();
        const item = items.find((x) => x.itemid === cleanItemID);
        if (!item) {
            throw new Error("item not found");
        }

        const batchId = this.newId();
        const result = await this.purchaseOne(account, item, cleanCount, batchId);
        if (!result.ok) {
            throw new Error(result.record.message || "购买失败");
        }
        return {
            message: result.record.message,
            account: result.account,
        };
    }

    // 批量购买:对多个账号执行同一商品 + 同一数量。先做余额预检,任一账号泡点不足则
    // 整体不执行(抛错,列出不足的账号),满足"不够就提示且不执行"。预检通过后逐个下单,
    // 每个账号写一条购买记录。账号离线也能买(走 HTTP 登录态下单)。
    async purchaseBatch(
        usernames: string[],
        itemID: number,
        itemCount: number
    ): Promise<PaodianBatchPurchaseResult> {
        const cleanItemID = Math.trunc(Number(itemID));
        const cleanCount = Math.trunc(Number(itemCount));
        if (!Number.isFinite(cleanItemID) || cleanItemID <= 0) throw new Error("invalid itemID");
        if (!Number.isFinite(cleanCount) || cleanCount <= 0) throw new Error("invalid itemCount");

        const names = Array.from(
            new Set((Array.isArray(usernames) ? usernames : []).map((u) => String(u || "").trim()).filter(Boolean))
        );
        if (names.length === 0) throw new Error("no accounts selected");

        const accounts = names.map((n) => this.findAccount(n)); // 任一不存在直接抛错

        const items = await this.getShopItems();
        const item = items.find((x) => x.itemid === cleanItemID);
        if (!item) throw new Error("item not found");

        const needed = item.bubble_price * cleanCount;

        // 余额预检:有不足的就整体拒绝。用 viewFor 读已缓存的泡点(离线账号也有上次余额)。
        const insufficient: string[] = [];
        for (const account of accounts) {
            const have = this.viewFor(account).paodian;
            if (have < needed) {
                insufficient.push(`${account.username}(${have}/${needed})`);
            }
        }
        if (insufficient.length > 0) {
            throw new Error(
                `泡点不足,已取消(单价${item.bubble_price}×${cleanCount}=需${needed}):` +
                insufficient.join("、")
            );
        }

        const batchId = this.newId();
        const records: PaodianPurchaseRecord[] = [];
        const accountViews: PaodianAccountView[] = [];
        let okCount = 0;
        let failCount = 0;

        // 串行下单,避免同时打爆登录/购买接口;预检已通过,这里只关心实际下单结果。
        for (const account of accounts) {
            const r = await this.purchaseOne(account, item, cleanCount, batchId);
            records.push(r.record);
            accountViews.push(r.account);
            if (r.ok) okCount++;
            else failCount++;
        }

        return {
            batchId,
            requested: accounts.length,
            ok: okCount,
            fail: failCount,
            records,
            accounts: accountViews,
        };
    }

    // 单账号单次下单 + 写记录 + 刷新余额。purchase / purchaseBatch 共用。
    // 不抛错:失败也返回 ok=false 的记录,方便批量里继续处理其它账号。
    private async purchaseOne(
        account: PaodianAccount,
        item: PaodianShopItem,
        count: number,
        batchId: string
    ): Promise<{ ok: boolean; record: PaodianPurchaseRecord; account: PaodianAccountView }> {
        const base = {
            id: this.newId(),
            time: Date.now(),
            username: account.username,
            itemID: item.itemid,
            itemName: item.name,
            itemCount: count,
            bubblePrice: item.bubble_price,
            totalBubble: item.bubble_price * count,
            batchId,
        };
        try {
            const data = await postJson(PURCHASE_URL, {
                username: account.username,
                password: account.password,
                itemID: item.itemid,
                itemCount: String(count),
                itemname: item.name,
                bubblePrice: item.bubble_price,
            });
            const message = String(data?.message ?? "购买完成");
            const record: PaodianPurchaseRecord = { ...base, ok: true, message };
            this.addRecord(record);
            this.saveRecords();
            const account2 = await this.refresh(account.username);
            return { ok: true, record, account: account2 };
        } catch (e: any) {
            const message = e?.message || String(e);
            const record: PaodianPurchaseRecord = { ...base, ok: false, message };
            this.addRecord(record);
            this.saveRecords();
            return { ok: false, record, account: this.viewFor(account) };
        }
    }

    private newId(): string {
        return `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;
    }

    // 用显式账号/密码购买,不查 paodian_accounts.json、不刷新余额。供"监控购买"用从游戏
    // 内存读出的登录凭据直接下单,免去在账号文件里手填密码。
    async purchaseWithPassword(
        username: string,
        password: string,
        itemID: number,
        itemCount: number
    ): Promise<{ message: string }> {
        const cleanUsername = String(username || "").trim();
        const cleanItemID = Math.trunc(Number(itemID));
        const cleanCount = Math.trunc(Number(itemCount));
        if (!cleanUsername) throw new Error("missing username");
        if (!password) throw new Error("missing password (游戏内未读到密码)");
        if (!Number.isFinite(cleanItemID) || cleanItemID <= 0) throw new Error("invalid itemID");
        if (!Number.isFinite(cleanCount) || cleanCount <= 0) throw new Error("invalid itemCount");

        const items = await this.getShopItems();
        const item = items.find((x) => x.itemid === cleanItemID);
        if (!item) throw new Error("item not found");

        const data = await postJson(PURCHASE_URL, {
            username: cleanUsername,
            password,
            itemID: item.itemid,
            itemCount: String(cleanCount),
            itemname: item.name,
            bubblePrice: item.bubble_price,
        });
        return { message: String(data?.message ?? "购买完成") };
    }

    addOrUpdate(username: string, password: string): PaodianAccountView {
        const cleanUsername = String(username || "").trim();
        const cleanPassword = String(password || "");
        if (!cleanUsername || !cleanPassword) {
            throw new Error("missing username or password");
        }

        const idx = this.accounts.findIndex((a) => a.username === cleanUsername);
        if (idx >= 0) {
            this.accounts[idx] = { username: cleanUsername, password: cleanPassword };
        } else {
            this.accounts.push({ username: cleanUsername, password: cleanPassword });
        }

        if (!this.states.has(cleanUsername)) {
            this.states.set(cleanUsername, emptyState(cleanUsername));
        }
        this.save();

        this.refresh(cleanUsername).catch((e) => {
            console.warn(`[paodian] refresh after add failed for ${cleanUsername}: ${e.message}`);
        });

        return this.viewFor({ username: cleanUsername, password: cleanPassword });
    }

    remove(username: string): boolean {
        const cleanUsername = String(username || "").trim();
        const before = this.accounts.length;
        this.accounts = this.accounts.filter((a) => a.username !== cleanUsername);
        this.states.delete(cleanUsername);
        this.refreshing.delete(cleanUsername);
        if (this.accounts.length !== before) {
            this.save();
            return true;
        }
        return false;
    }

    async refreshAll(): Promise<PaodianAccountView[]> {
        await Promise.all(this.accounts.map((a) => this.refresh(a.username)));
        return this.list();
    }

    async refresh(username: string): Promise<PaodianAccountView> {
        const cleanUsername = String(username || "").trim();
        const account = this.accounts.find((a) => a.username === cleanUsername);
        if (!account) {
            throw new Error("account not found");
        }
        if (this.refreshing.has(cleanUsername)) {
            return this.viewFor(account);
        }

        this.refreshing.add(cleanUsername);
        this.states.set(cleanUsername, {
            ...emptyState(cleanUsername),
            ...(this.states.get(cleanUsername) || {}),
            status: "loading",
            error: "",
        });

        try {
            const data = await postJson(LOGIN_URL, {
                username: account.username,
                password: account.password,
            });
            const state: PaodianState = {
                username: account.username,
                id: String(data.id ?? account.username),
                point: asNumber(data.point),
                paodian: asNumber(data.paodian),
                vip: asNumber(data.vip),
                paymoney: asNumber(data.paymoney),
                next_vip_level: asNumber(data.next_vip_level),
                next_vip_need: asNumber(data.next_vip_need),
                eps_endtime: data.eps_endtime === null || data.eps_endtime === undefined ? null : String(data.eps_endtime),
                status: "ok",
                lastUpdated: Date.now(),
                error: "",
            };
            this.states.set(cleanUsername, state);
            console.log(`[paodian] ${cleanUsername}: paodian=${state.paodian}`);
        } catch (e: any) {
            const prev = this.states.get(cleanUsername) || emptyState(cleanUsername);
            this.states.set(cleanUsername, {
                ...prev,
                status: "error",
                lastUpdated: Date.now(),
                error: e.message || String(e),
            });
            console.warn(`[paodian] ${cleanUsername} refresh failed: ${e.message || e}`);
        } finally {
            this.refreshing.delete(cleanUsername);
            // 刷新结果(泡点 / 刷新时间 / status)落盘,重启后可直接读回。
            this.save();
        }

        return this.viewFor(account);
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }
}
