import fs from "fs/promises";
import path from "path";
import { InstanceRegistry } from "./instances";

export interface VendorItemConfig {
    itemName: string;
    aliases?: string[];
    itemId?: number;
    defaultCount: number;
    vendorId?: number;
    token?: string | number;
}

export interface VendorPurchaseConfig {
    summonItemName: string;
    summonAliases: string[];
    summonSlotIndex?: number;
    sendOpenPacket: boolean;
    sendClosePacket: boolean;
    openDelayMs: number;
    buyDelayMs: number;
    closeDelayMs: number;
    defaultVendorId: number;
    items: VendorItemConfig[];
}

interface BagItem {
    slotIndex: number;
    itemId?: number;
    count?: number;
    name: string;
}

interface CashBagItem {
    slotIndex: number;
    itemId?: number;
    count?: number;
    name: string;
}

export interface VendorShopItem {
    shopIndex: number;
    itemId: number;
    unitPrice?: number;
    name: string;
}

export interface VendorPurchaseRequest {
    itemName?: string;
    itemId?: number;
    shopIndex?: number;
    count?: number; // backward-compatible alias for groups.
    groups?: number;
    perGroupCount?: number;
    summonSlotIndex?: number;
    vendorId?: number;
    token?: string | number;
}

export interface VendorPurchaseResult {
    ok: boolean;
    pid: number;
    requestedName: string;
    itemName: string;
    itemId: number;
    shopIndex: number;
    count: number;
    groups: number;
    perGroupCount: number;
    vendorId: number;
    summonSlotIndex: number;
    buyDetail?: unknown;
    steps: Array<{ action: string; ok: boolean; detail?: string }>;
}

const CONFIG_FILE_NAME = "vendor_purchase.json";
const DEFAULT_TOKEN = "0xFECD2408";

export class VendorPurchaser {
    private readonly configFile: string;
    private config: VendorPurchaseConfig;

    constructor(private readonly registry: InstanceRegistry, dataDir: string) {
        this.configFile = path.resolve(dataDir, CONFIG_FILE_NAME);
        this.config = defaultConfig();
        void this.loadConfig();
    }

    getConfig(): VendorPurchaseConfig & { configFile: string } {
        return {
            ...this.config,
            summonAliases: [...this.config.summonAliases],
            items: this.config.items.map((it) => ({ ...it, aliases: [...(it.aliases || [])] })),
            configFile: this.configFile,
        };
    }

    async setConfig(raw: Partial<VendorPurchaseConfig>): Promise<VendorPurchaseConfig & { configFile: string }> {
        this.config = normalizeConfig({ ...this.config, ...raw }, this.config);
        await this.saveConfig();
        return this.getConfig();
    }

    async purchase(pid: number, request: VendorPurchaseRequest): Promise<VendorPurchaseResult> {
        const requestedName = String(request.itemName || "").trim();
        const requestedItemId = optionalPositiveInt(request.itemId);
        const requestedShopIndex = optionalNonNegativeInt(request.shopIndex);
        if (!requestedName && !requestedItemId) throw new Error("missing itemName or itemId");

        const configured = requestedName ? this.findConfiguredItem(requestedName) : undefined;
        const legacyCount = optionalPositiveInt(request.count);
        const defaultPerGroupCount = configured?.defaultCount ?? 300;
        const groups = positiveInt(
            request.groups,
            legacyCount !== undefined && legacyCount <= 20 ? legacyCount : 1,
            1
        );
        const perGroupCount = positiveInt(
            request.perGroupCount,
            legacyCount !== undefined && legacyCount > 20 ? legacyCount : defaultPerGroupCount,
            1
        );
        const vendorId = positiveInt(request.vendorId, configured?.vendorId ?? this.config.defaultVendorId, 0);
        const token = request.token ?? configured?.token ?? DEFAULT_TOKEN;
        const summonSlotIndex = await this.resolveSummonSlot(pid, request.summonSlotIndex);
        const steps: VendorPurchaseResult["steps"] = [];
        let shouldClose = false;
        let closeAttempted = false;
        const usedTargetSlots: number[] = [];

        try {
            await this.runStep(pid, steps, "useCashItem", { slotIndex: summonSlotIndex }, 5000);
            shouldClose = true;
            await delay(this.config.openDelayMs);

            if (this.config.sendOpenPacket) {
                await this.runStep(pid, steps, "vendorOpen", tokenArgs(token), 5000);
                shouldClose = true;
                await delay(this.config.buyDelayMs);
            }

            let shopItem: VendorShopItem | undefined;
            if (requestedItemId && requestedShopIndex !== undefined) {
                shopItem = {
                    itemId: requestedItemId,
                    shopIndex: requestedShopIndex,
                    name: requestedName || configured?.itemName || String(requestedItemId),
                };
            } else {
                const shopItems = await this.getVendorShopItems(pid, vendorId);
                shopItem = requestedItemId
                    ? shopItems.find((it) => it.itemId === requestedItemId)
                    : this.resolveShopItem(requestedName, configured, shopItems);
                if (!shopItem) {
                    const known = shopItems.map((it) => `${it.name}(${it.itemId}/${it.shopIndex})`).join(", ");
                    const wanted = requestedItemId ? `${requestedName || requestedItemId}` : requestedName;
                    throw new Error(`vendor item not found: "${wanted}"${known ? `, vendor has: ${known}` : ""}`);
                }
            }

            const buyDetails: unknown[] = [];
            let detailObj: Record<string, unknown> = {};
            let confirmedCount = await this.getBagItemCount(pid, shopItem.itemId);
            steps.push({
                action: "bagItemSnapshot",
                ok: true,
                detail: JSON.stringify({ itemId: shopItem.itemId, count: confirmedCount }),
            });
            for (let groupIndex = 0; groupIndex < groups; ++groupIndex) {
                const buyStep = await this.runStep(pid, steps, "vendorBuyItem", {
                    itemId: shopItem.itemId,
                    shopIndex: shopItem.shopIndex,
                    count: perGroupCount,
                    vendorId,
                    avoidTargetSlots: usedTargetSlots,
                    ...tokenArgs(token),
                }, 5000);
                const buyDetail = parseJson<unknown>(buyStep.detail, undefined);
                buyDetails.push(buyDetail);
                if (isRecord(buyDetail)) {
                    detailObj = buyDetail;
                    const targetSlot = numberField(buyDetail, "targetSlot", 0);
                    if (targetSlot > 0 && !usedTargetSlots.includes(targetSlot)) {
                        usedTargetSlots.push(targetSlot);
                    }
                }
                const expectedCount = confirmedCount + perGroupCount;
                confirmedCount = await this.waitForBagItemCount(
                    pid,
                    steps,
                    shopItem.itemId,
                    expectedCount,
                    8000,
                    groupIndex + 1
                );
            }

            if (this.config.sendClosePacket) {
                await delay(this.config.closeDelayMs);
                closeAttempted = true;
                await this.runStep(pid, steps, "vendorClose", tokenArgs(token), 5000);
            }

            return {
                ok: true,
                pid,
                requestedName: requestedName || String(shopItem.itemId),
                itemName: shopItem.name || configured?.itemName || requestedName || String(shopItem.itemId),
                itemId: numberField(detailObj, "itemId", shopItem.itemId),
                shopIndex: numberField(detailObj, "shopIndex", shopItem.shopIndex),
                count: groups * perGroupCount,
                groups,
                perGroupCount,
                vendorId: numberField(detailObj, "vendorId", vendorId),
                summonSlotIndex,
                buyDetail: groups === 1 ? buyDetails[0] : buyDetails,
                steps,
            };
        } catch (e) {
            if (this.config.sendClosePacket && shouldClose && !closeAttempted) {
                closeAttempted = true;
                await delay(this.config.closeDelayMs);
                try {
                    await this.runStep(pid, steps, "vendorClose", tokenArgs(token), 5000);
                } catch (closeError) {
                    if (!steps.some((step) => step.action === "vendorClose")) {
                        steps.push({
                            action: "vendorClose",
                            ok: false,
                            detail: closeError instanceof Error ? closeError.message : String(closeError),
                        });
                    }
                    // Preserve the original failure; close is best-effort here.
                }
            }
            throw attachSteps(e, steps);
        }
    }

    private async loadConfig(): Promise<void> {
        try {
            const raw = await fs.readFile(this.configFile, "utf8");
            this.config = normalizeConfig(JSON.parse(raw), this.config);
        } catch (e: any) {
            if (e?.code !== "ENOENT") {
                console.warn(`[vendorPurchaser] config read failed: ${e.message || String(e)}`);
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

    private findConfiguredItem(itemName: string): VendorItemConfig | undefined {
        const needle = normalizeName(itemName);
        return this.config.items.find((item) => {
            const names = [item.itemName, ...(item.aliases || [])];
            return names.some((name) => normalizeName(name) === needle);
        });
    }

    private resolveShopItem(
        requestedName: string,
        configured: VendorItemConfig | undefined,
        shopItems: VendorShopItem[]
    ): VendorShopItem | undefined {
        if (configured?.itemId) {
            const byId = shopItems.find((it) => it.itemId === configured.itemId);
            if (byId) return byId;
        }

        const names = [requestedName, configured?.itemName, ...(configured?.aliases || [])]
            .filter((it): it is string => !!it && it.trim().length > 0)
            .map(normalizeName);
        return shopItems.find((it) => {
            const shopName = normalizeName(it.name || "");
            return names.some((name) => shopName === name || shopName.includes(name) || name.includes(shopName));
        });
    }

    private async getVendorShopItems(pid: number, vendorId: number): Promise<VendorShopItem[]> {
        const r = await this.registry.sendCommand(pid, "getVendorShopItems", { vendorId }, 5000);
        if (!r.ok) throw new Error(`getVendorShopItems failed: ${r.detail || "unknown"}`);
        return parseJson<VendorShopItem[]>(r.detail, []);
    }

    private async getBagItemCount(pid: number, itemId: number): Promise<number> {
        const r = await this.registry.sendCommand(pid, "getBagItems", {}, 5000);
        if (!r.ok) throw new Error(`getBagItems failed: ${r.detail || "unknown"}`);
        const items = parseJson<BagItem[]>(r.detail, []);
        return items
            .filter((it) => numberField(it as unknown as Record<string, unknown>, "itemId", 0) === itemId)
            .reduce((sum, it) => sum + positiveInt(it.count, 1, 1), 0);
    }

    private async waitForBagItemCount(
        pid: number,
        steps: VendorPurchaseResult["steps"],
        itemId: number,
        expectedCount: number,
        timeoutMs: number,
        group: number
    ): Promise<number> {
        const deadline = Date.now() + timeoutMs;
        let lastCount = 0;
        let lastError = "";

        while (Date.now() <= deadline) {
            try {
                lastCount = await this.getBagItemCount(pid, itemId);
                if (lastCount >= expectedCount) {
                    steps.push({
                        action: "waitBagItem",
                        ok: true,
                        detail: JSON.stringify({ itemId, group, expectedCount, count: lastCount }),
                    });
                    return lastCount;
                }
            } catch (e) {
                lastError = e instanceof Error ? e.message : String(e);
            }
            await delay(100);
        }

        const detail = JSON.stringify({ itemId, group, expectedCount, count: lastCount, lastError });
        steps.push({ action: "waitBagItem", ok: false, detail });
        throw new Error(`waitBagItem failed: ${detail}`);
    }

    private async resolveSummonSlot(pid: number, overrideSlot?: number): Promise<number> {
        const configuredSlot = optionalNonNegativeInt(this.config.summonSlotIndex);
        const slot = optionalNonNegativeInt(overrideSlot);
        if (slot !== undefined) return slot;
        if (configuredSlot !== undefined) return configuredSlot;

        const r = await this.registry.sendCommand(pid, "getCashBagItems", {}, 5000);
        if (!r.ok) throw new Error(`getCashBagItems failed: ${r.detail || "unknown"}`);
        const items = parseJson<CashBagItem[]>(r.detail, []);
        const found = items.find((it) => this.isSummonItem(it.name));
        if (!found) {
            const names = [this.config.summonItemName, ...this.config.summonAliases].filter(Boolean).join("/");
            throw new Error(`summon item not found in cash bag (${names})`);
        }
        return found.slotIndex;
    }

    private isSummonItem(name: string): boolean {
        const needle = normalizeName(name);
        const configured = [this.config.summonItemName, ...this.config.summonAliases]
            .filter(Boolean)
            .map(normalizeName);
        return configured.some((n) => needle.includes(n) || n.includes(needle));
    }

    private async runStep(
        pid: number,
        steps: VendorPurchaseResult["steps"],
        action: string,
        args: Record<string, unknown>,
        timeoutMs: number
    ): Promise<{ ok: boolean; detail?: string }> {
        const r = await this.registry.sendCommand(pid, action, args, timeoutMs);
        steps.push({ action, ok: r.ok, detail: r.detail });
        if (!r.ok) throw new Error(`${action} failed: ${r.detail || "unknown"}`);
        return r;
    }
}

function defaultConfig(): VendorPurchaseConfig {
    return {
        summonItemName: "摊贩",
        summonAliases: ["攤販", "呼叫卷", "呼叫券", "摊贩呼叫卷", "攤販呼叫券"],
        sendOpenPacket: true,
        sendClosePacket: true,
        openDelayMs: 300,
        buyDelayMs: 150,
        closeDelayMs: 150,
        defaultVendorId: 2,
        items: [
            {
                itemName: "红标枪",
                aliases: ["紅標槍"],
                defaultCount: 300,
                vendorId: 2,
                token: DEFAULT_TOKEN,
            },
        ],
    };
}

function normalizeConfig(raw: any, fallback: VendorPurchaseConfig): VendorPurchaseConfig {
    const items = Array.isArray(raw?.items)
        ? raw.items.map((it: any) => normalizeItem(it)).filter((it: VendorItemConfig | null): it is VendorItemConfig => !!it)
        : fallback.items;
    return {
        summonItemName: stringValue(raw?.summonItemName, fallback.summonItemName),
        summonAliases: stringArray(raw?.summonAliases, fallback.summonAliases),
        summonSlotIndex: optionalNonNegativeInt(raw?.summonSlotIndex),
        sendOpenPacket: booleanValue(raw?.sendOpenPacket, fallback.sendOpenPacket),
        sendClosePacket: booleanValue(raw?.sendClosePacket, fallback.sendClosePacket),
        openDelayMs: positiveInt(raw?.openDelayMs, fallback.openDelayMs, 0),
        buyDelayMs: positiveInt(raw?.buyDelayMs, fallback.buyDelayMs, 0),
        closeDelayMs: positiveInt(raw?.closeDelayMs, fallback.closeDelayMs, 0),
        defaultVendorId: positiveInt(raw?.defaultVendorId, fallback.defaultVendorId, 0),
        items,
    };
}

function normalizeItem(raw: any): VendorItemConfig | null {
    const itemName = stringValue(raw?.itemName, "").trim();
    if (!itemName) return null;
    return {
        itemName,
        aliases: stringArray(raw?.aliases, []),
        itemId: optionalPositiveInt(raw?.itemId),
        defaultCount: positiveInt(raw?.defaultCount ?? raw?.count, 300, 1),
        vendorId: optionalNonNegativeInt(raw?.vendorId),
        token: raw?.token,
    };
}

function tokenArgs(token: unknown): Record<string, unknown> {
    return token === undefined || token === "" ? {} : { token };
}

function normalizeName(value: string): string {
    return value
        .normalize("NFKC")
        .trim()
        .toLocaleLowerCase()
        .replace(/\s+/g, "")
        .replace(/紅/g, "红")
        .replace(/標/g, "标")
        .replace(/槍/g, "枪")
        .replace(/捲/g, "卷")
        .replace(/軸/g, "轴")
        .replace(/級/g, "级")
        .replace(/藍/g, "蓝")
        .replace(/綠/g, "绿");
}

function stringValue(value: unknown, fallback: string): string {
    return typeof value === "string" ? value : fallback;
}

function stringArray(value: unknown, fallback: string[]): string[] {
    if (!Array.isArray(value)) return [...fallback];
    return value.map((it) => String(it || "").trim()).filter(Boolean);
}

function booleanValue(value: unknown, fallback: boolean): boolean {
    return typeof value === "boolean" ? value : fallback;
}

function optionalNonNegativeInt(value: unknown): number | undefined {
    const n = Number(value);
    return Number.isFinite(n) && n >= 0 ? Math.floor(n) : undefined;
}

function optionalPositiveInt(value: unknown): number | undefined {
    const n = Number(value);
    return Number.isFinite(n) && n > 0 ? Math.floor(n) : undefined;
}

function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === "object" && value !== null && !Array.isArray(value);
}

function numberField(obj: Record<string, unknown>, name: string, fallback: number): number {
    const n = Number(obj[name]);
    return Number.isFinite(n) ? Math.floor(n) : fallback;
}

function positiveInt(value: unknown, fallback: number, min: number): number {
    const n = Number(value);
    return Number.isFinite(n) && n >= min ? Math.floor(n) : fallback;
}

function parseJson<T>(raw: unknown, fallback: T): T {
    try {
        if (typeof raw !== "string") return fallback;
        return JSON.parse(raw) as T;
    } catch {
        return fallback;
    }
}

function attachSteps(e: unknown, steps: VendorPurchaseResult["steps"]): Error & { steps?: VendorPurchaseResult["steps"] } {
    const err: Error & { steps?: VendorPurchaseResult["steps"] } = e instanceof Error ? e : new Error(String(e));
    err.steps = steps;
    return err;
}

function delay(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, Math.max(0, ms)));
}
