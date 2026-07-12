import fs from "fs";
import path from "path";

// 物品名表(itemId -> 名字),供拾取过滤页做名字搜索用。掉落物在 DLL 端只能稳定拿到
// itemId,所以过滤始终按 itemId 走;名字搜索只是给人选 id 的便利,全在 broker 侧完成。
//
// item_names.json 约 3.9MB / 3.8 万条,启动时一次性读进内存。找不到文件就降级成空表,
// 搜索返回 []、name() 返回 ""(前端仍可手填 itemId),不影响过滤功能本身。

export interface ItemNameEntry {
    itemId: number;
    name: string;
}

interface RawItemEntry {
    name?: string;
}

let s_byId = new Map<number, string>();
let s_loadedFrom = "";

// 候选路径,按优先级找第一个存在的:
//   1. 环境变量显式指定
//   2. broker 数据目录(exe 同级)
//   3. broker 根目录 / release 目录(打包时可放一份)
//   4. 仓库 tools/ 下的原始表(dev 直接跑 ts-node 时用)
function candidatePaths(dataDir: string): string[] {
    const here = __dirname; // dist/ 或 src/
    const out: string[] = [];
    if (process.env.GGTB_ITEM_NAMES_FILE) out.push(process.env.GGTB_ITEM_NAMES_FILE);
    out.push(path.resolve(dataDir, "item_names.json"));
    out.push(path.resolve(here, "..", "item_names.json"));
    out.push(path.resolve(here, "..", "release", "item_names.json"));
    out.push(path.resolve(here, "..", "..", "tools", "item_names.json"));
    return out;
}

export function loadItemNames(dataDir: string): void {
    for (const p of candidatePaths(dataDir)) {
        try {
            if (!fs.existsSync(p)) continue;
            const raw = fs.readFileSync(p, "utf8");
            const parsed = JSON.parse(raw) as Record<string, RawItemEntry>;
            const map = new Map<number, string>();
            for (const key of Object.keys(parsed)) {
                const id = Number(key);
                if (!Number.isInteger(id) || id <= 0) continue;
                const name = parsed[key]?.name;
                if (typeof name === "string" && name && name !== String(id)) {
                    map.set(id, name);
                }
            }
            s_byId = map;
            s_loadedFrom = p;
            console.log(`[itemNames] loaded ${s_byId.size} names from ${p}`);
            return;
        } catch (e: any) {
            console.warn(`[itemNames] failed to load ${p}: ${e.message}`);
        }
    }
    console.warn("[itemNames] no item_names.json found; name search disabled (manual itemId still works)");
}

export function itemName(itemId: number): string {
    return s_byId.get(itemId) || "";
}

export function isItemNamesLoaded(): boolean {
    return s_byId.size > 0;
}

// 按名字子串 / itemId 搜索,返回至多 limit 条。q 是纯数字时也按 itemId 精确/前缀命中。
export function searchItems(query: string, limit = 50): ItemNameEntry[] {
    const q = (query || "").trim();
    const out: ItemNameEntry[] = [];
    if (!q) return out;

    const asId = Number(q);
    const numeric = Number.isInteger(asId) && asId > 0;

    // 数字查询:先把精确命中的那条放最前。
    if (numeric && s_byId.has(asId)) {
        out.push({ itemId: asId, name: s_byId.get(asId) || "" });
    }

    const lower = q.toLowerCase();
    for (const [id, name] of s_byId) {
        if (out.length >= limit) break;
        if (numeric && id === asId) continue; // 已加过
        const hitName = name.toLowerCase().includes(lower);
        const hitId = numeric && String(id).startsWith(q);
        if (hitName || hitId) {
            out.push({ itemId: id, name });
        }
    }
    return out.slice(0, limit);
}

// 将名称关键字展开成 DLL 可识别的精确 itemId 集合。
export function matchItemIds(keywords: string[]): number[] {
    const needles = keywords
        .map((keyword) => keyword.trim().toLowerCase())
        .filter(Boolean);
    if (needles.length === 0) return [];

    const out: number[] = [];
    for (const [id, name] of s_byId) {
        const lower = name.toLowerCase();
        if (needles.some((needle) => lower.includes(needle))) out.push(id);
    }
    return out;
}

export function itemNamesStatus(): { loaded: boolean; count: number; source: string } {
    return { loaded: s_byId.size > 0, count: s_byId.size, source: s_loadedFrom };
}
