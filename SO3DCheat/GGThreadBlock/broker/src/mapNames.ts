const MAP_NAMES = require("../web-vue/src/data/mapNames.json") as Record<string, string>;

export function getMapName(mapId: unknown): string {
    const numericId = Number(mapId);
    if (!Number.isSafeInteger(numericId) || numericId <= 0) return "";
    return MAP_NAMES[String(numericId)] || "";
}
