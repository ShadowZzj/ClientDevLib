// 地图 id → 名称查表。数据来源 tools/map_names.json,只保留有名字的地图
// (117 条),编译进 bundle。getStatus / status 帧里的 mapId 是 canonical id,
// 可直接当 key 用。
import mapNamesRaw from "../data/mapNames.json";

const MAP_NAMES = mapNamesRaw as Record<string, string>;

// 拿地图名;查不到返回空串(调用方自行决定 fallback)。
export function getMapName(mapId: number | null | undefined): string {
    if (mapId === null || mapId === undefined) return "";
    return MAP_NAMES[String(mapId)] || "";
}

// 「名称 (id)」/「未知地图 (id)」的展示文本;mapId 无效时返回空串。
export function formatMapLabel(mapId: number | null | undefined): string {
    if (mapId === null || mapId === undefined || !Number.isFinite(mapId)) return "";
    const name = getMapName(mapId);
    return name ? `${name} (${mapId})` : `未知地图 (${mapId})`;
}
