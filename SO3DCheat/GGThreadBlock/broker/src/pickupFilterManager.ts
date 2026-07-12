import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";
import { itemName, matchItemIds } from "./itemNames";

// pickupFilterManager:自动拾取过滤的远控。全局一份配置(所有角色共用同一个 mode + 白名单),
// 不分角色。配置改了推给所有在线实例;新实例上线时把当前配置补推一次。
//
// 实际拾取仍在 DLL 的 AutoPickupModule worker 里跑(实时扫掉落、发拾取包),broker 只下发
// setPickupFilter,DLL 的 PickupFilter 单例收下,worker 每轮发包前查 ShouldPick。
//
// 过滤始终按 itemId(掉落物 DLL 端只能稳定拿到 itemId)。items 里的 name 只是给 web 展示用的
// 缓存标签,wire 上只把 itemIds 数组下发给 DLL。

export interface PickupFilterItem {
    itemId: number;
    name: string; // 展示用标签,可空
}

export interface PickupFilterConfig {
    pickupExecutionMode: "autoPickup" | "nativeGame";
    mode: "all" | "whitelist"; // all = 全部拾取(默认);whitelist = 仅捡列表内
    items: PickupFilterItem[];
    fuzzyKeywords: string[];
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "pickup_filter.json"
);

export class PickupFilterManager {
    private config: PickupFilterConfig = defaultConfig();
    private registry: InstanceRegistry;
    // 已经推过当前配置的在线 pid。新 pid 出现就补推,pid 下线就移除。
    private pushedPids = new Set<number>();
    private onChange: () => void;

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.onChange = () => this.syncNewInstances();
        this.registry.on("change", this.onChange);
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                this.config = normalizeConfig(JSON.parse(raw));
                console.log(
                    `[pickupFilter] loaded config execution=${this.config.pickupExecutionMode} ` +
                    `mode=${this.config.mode} items=${this.config.items.length}`
                );
            }
        } catch (e: any) {
            console.warn(`[pickupFilter] load failed: ${e.message}`);
            this.config = defaultConfig();
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.config, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[pickupFilter] save failed: ${e.message}`);
        }
    }

    // 只把 DLL 需要的字段下发:mode + itemId 数组(名字不上 wire)。
    getWirePayload(pid?: number) {
        const itemIds = new Set(this.config.items.map((i) => i.itemId));
        for (const itemId of matchItemIds(this.config.fuzzyKeywords)) itemIds.add(itemId);
        const supportsNative = pid === undefined ||
            this.registry.list().some((instance) =>
                instance.pid === pid && instance.nativeDropPickupReady === true
            );
        if (this.config.pickupExecutionMode === "nativeGame" && !supportsNative) {
            return {
                pickupExecutionMode: this.config.pickupExecutionMode,
                mode: "whitelist" as const,
                itemIds: [] as number[],
            };
        }
        return {
            pickupExecutionMode: this.config.pickupExecutionMode,
            mode: this.config.mode,
            itemIds: [...itemIds],
        };
    }

    // 新连上的实例补推当前配置(也清掉已下线 pid 的记录)。
    private syncNewInstances(): void {
        const online = new Set<number>();
        for (const inst of this.registry.list()) online.add(inst.pid);

        for (const pid of this.pushedPids) {
            if (!online.has(pid)) this.pushedPids.delete(pid);
        }
        for (const pid of online) {
            if (!this.pushedPids.has(pid)) this.pushTo(pid);
        }
    }

    private pushTo(pid: number): void {
        this.pushedPids.add(pid); // 乐观标记,避免 change 风暴里重复发
        this.registry
            .sendCommand(pid, "setPickupFilter", this.getWirePayload(pid), 5000)
            .then((result) => {
                if (!result.ok) throw new Error(result.detail || "setPickupFilter rejected");
            })
            .catch((e: any) => {
                this.pushedPids.delete(pid); // 失败回退,下一次 change 再补推
                console.warn(`[pickupFilter] push pid=${pid} failed: ${e.message}`);
            });
    }

    private pushAll(): void {
        this.pushedPids.clear();
        for (const inst of this.registry.list()) this.pushTo(inst.pid);
    }

    // ---------------- HTTP 用的 CRUD ----------------

    getConfig(): PickupFilterConfig {
        return this.config;
    }

    setConfig(body: Partial<PickupFilterConfig>): PickupFilterConfig {
        this.config = normalizeConfig({ ...this.config, ...body });
        this.save();
        this.pushAll();
        return this.config;
    }

    destroy(): void {
        this.registry.off("change", this.onChange);
    }
}

function defaultConfig(): PickupFilterConfig {
    return { pickupExecutionMode: "autoPickup", mode: "all", items: [], fuzzyKeywords: [] };
}

function normalizeConfig(c: any): PickupFilterConfig {
    const pickupExecutionMode = c?.pickupExecutionMode === "nativeGame"
        ? "nativeGame"
        : "autoPickup";
    const mode = c?.mode === "whitelist" ? "whitelist" : "all";

    const seen = new Set<number>();
    const items: PickupFilterItem[] = [];
    const rawItems = Array.isArray(c?.items) ? c.items : [];
    for (const it of rawItems) {
        const itemId = Math.floor(Number(it?.itemId));
        if (!Number.isInteger(itemId) || itemId <= 0 || seen.has(itemId)) continue;
        seen.add(itemId);
        // 名字优先用前端传来的;没有就回查 item_names 表;再没有就留空。
        const name =
            typeof it?.name === "string" && it.name ? it.name : itemName(itemId);
        items.push({ itemId, name });
    }

    const fuzzyKeywords: string[] = [];
    const seenKeywords = new Set<string>();
    const rawKeywords = Array.isArray(c?.fuzzyKeywords) ? c.fuzzyKeywords : [];
    for (const value of rawKeywords) {
        const keyword = String(value ?? "").trim();
        const key = keyword.toLowerCase();
        if (!keyword || seenKeywords.has(key)) continue;
        seenKeywords.add(key);
        fuzzyKeywords.push(keyword);
    }

    return { pickupExecutionMode, mode, items, fuzzyKeywords };
}
