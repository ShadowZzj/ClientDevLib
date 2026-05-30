import fs from "fs";
import path from "path";
import https from "https";

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "paodian_accounts.json"
);

const LOGIN_URL = "https://shop2.guguseal.com/api/login";
const WEBSHOP_DATA_URL = "https://shop2.guguseal.com/api/webshop-data";
const PURCHASE_URL = "https://shop2.guguseal.com/api/purchase";
const DEFAULT_REFRESH_INTERVAL_MS = 10 * 60 * 1000;
const MIN_REFRESH_INTERVAL_MS = 60 * 1000;
const MAX_REFRESH_INTERVAL_MS = 24 * 60 * 60 * 1000;
const SHOP_ITEMS_CACHE_MS = 5 * 60 * 1000;

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

    constructor() {
        this.load();
        this.start();
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

            for (const account of this.accounts) {
                this.states.set(account.username, emptyState(account.username));
            }
            console.log(`[paodian] loaded ${this.accounts.length} accounts`);
        } catch (e: any) {
            console.warn(`[paodian] load failed: ${e.message}`);
            this.accounts = [];
            this.states.clear();
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(
                PERSIST_FILE,
                JSON.stringify({ accounts: this.accounts, config: this.config }, null, 2),
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
            this.refreshAll().catch((e) => console.warn(`[paodian] refreshAll failed: ${e.message}`));
        }, this.config.refreshIntervalMs);
        console.log(`[paodian] auto refresh interval=${this.config.refreshIntervalMs}ms`);
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

        const data = await postJson(PURCHASE_URL, {
            username: account.username,
            password: account.password,
            itemID: item.itemid,
            itemCount: String(cleanCount),
            itemname: item.name,
            bubblePrice: item.bubble_price,
        });

        const updated = await this.refresh(account.username);
        return {
            message: String(data?.message ?? "购买完成"),
            account: updated,
        };
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
        }

        return this.viewFor(account);
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }
}
