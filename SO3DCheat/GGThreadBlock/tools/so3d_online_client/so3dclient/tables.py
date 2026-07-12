"""物品/地图静态表加载与名称<->ID 解析(item_names/map_*.json)。"""
from __future__ import annotations

import json
import re

from .runtime import APP_DIR

from .logio import log_print as print


def _load_item_table() -> tuple[dict[int, str], dict[int, bool], dict[int, bool]]:
    """从程序目录(APP_DIR:开发=脚本目录,打包=exe 目录)item_names.json 读取
    itemId -> (中文名, 是否可贩卖, 是否可堆叠)。
    新结构每条是 {"name","type","sellable","stackable"};缺失则空表(降级为只显示 ID、不做可卖过滤)。"""
    try:
        path = APP_DIR / "item_names.json"
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}, {}, {}
    names: dict[int, str] = {}
    sellable: dict[int, bool] = {}
    stackable: dict[int, bool] = {}
    for k, v in data.items():
        try:
            iid = int(k)
        except (TypeError, ValueError):
            continue
        if isinstance(v, dict):
            names[iid] = str(v.get("name", ""))
            sellable[iid] = bool(v.get("sellable", True))
            stackable[iid] = bool(v.get("stackable", True))
        else:
            # 兼容旧的扁平结构 {"3982": "紅標槍"}
            names[iid] = str(v)
            sellable[iid] = True
            stackable[iid] = True
    return names, sellable, stackable


ITEM_NAMES, ITEM_SELLABLE, ITEM_STACKABLE = _load_item_table()
# 名字 -> itemId;同名取首个出现的 id(itemId 升序)。供按名字配置时反查。
ITEM_IDS_BY_NAME: dict[str, int] = {}
for _iid in sorted(ITEM_NAMES):
    ITEM_IDS_BY_NAME.setdefault(ITEM_NAMES[_iid], _iid)


def item_name(item_id: object) -> str:
    try:
        return ITEM_NAMES.get(int(item_id), "")
    except (TypeError, ValueError):
        return ""


def item_sellable(item_id: object) -> bool:
    """item_names.json 标记是否可贩卖;表里没有的 itemId 默认允许卖(保守:不误跳)。"""
    try:
        return ITEM_SELLABLE.get(int(item_id), True)
    except (TypeError, ValueError):
        return True


def item_stackable(item_id: object) -> bool:
    """item_names.json 标记是否可堆叠(= 游戏 Item_IsStackable: itemEntry+276 != 0)。
    可堆叠物品 wire 数量字段低 16 位即堆叠数(高 16 位为 0);不可堆叠物品该字段是 packed
    (低16=属性/0,高16=耐久),整段读会得到 196608=0x30000 这种垃圾值,故按 1 件计。
    表里没有的 itemId 默认按可堆叠(取低 16 位),避免对计数类物品低报。"""
    try:
        return ITEM_STACKABLE.get(int(item_id), True)
    except (TypeError, ValueError):
        return True


def item_label(item_id: object) -> str:
    """展示用:'名字(itemId)';无名字时只给 itemId。"""
    name = item_name(item_id)
    return f"{name}({item_id})" if name else str(item_id)


def _load_map_tables() -> tuple[list[int], dict[int, str]]:
    """加载 slot->规范地图 id(map_id_table.json)与 id->地图名(map_names.json)。
    511001 的 wire+20 是地图 SLOT(g_localMapId,游戏内 g_MapIdTable 下标),不是可显示的地图 id;
    需经 g_MapIdTable[slot] 转成规范 id 再查名(headless 客户端没注入,故把表导出成 json 离线查)。
    路径用 APP_DIR(开发=脚本目录,打包=exe 目录),和账号配置一致。"""
    base = APP_DIR
    slot_to_id: list[int] = []
    id_to_name: dict[int, str] = {}
    try:
        mt = json.loads((base / "map_id_table.json").read_text(encoding="utf-8"))
        slot_to_id = [int(x) for x in mt.get("slot_to_id", [])]
    except Exception:
        pass
    try:
        mn = json.loads((base / "map_names.json").read_text(encoding="utf-8"))
        for m in mn.get("maps", []):
            try:
                id_to_name[int(m["id"])] = str(m.get("name", ""))
            except (TypeError, ValueError, KeyError):
                continue
    except Exception:
        pass
    return slot_to_id, id_to_name


MAP_SLOT_TO_ID, MAP_ID_TO_NAME = _load_map_tables()


def _load_monster_table() -> dict[int, str]:
    """读取 monster_names.json:怪物 type(CRT entry +12 monsterType)-> 中文名。
    从游戏 CSealTableManager 表 24 整表 dump 而来,已剔除「待新增」等占位条目;
    缺失则空表(降级为只显示 type id)。"""
    try:
        data = json.loads((APP_DIR / "monster_names.json").read_text(encoding="utf-8"))
    except Exception:
        return {}
    names: dict[int, str] = {}
    for k, v in data.items():
        try:
            names[int(k)] = str(v)
        except (TypeError, ValueError):
            continue
    return names


MONSTER_NAMES = _load_monster_table()


def monster_name(monster_type: object) -> str:
    try:
        return MONSTER_NAMES.get(int(monster_type), "")
    except (TypeError, ValueError):
        return ""


def resolve_map_name(slot: object) -> str:
    """地图 SLOT(511001 wire+20)-> 规范地图 id -> 中文名;查不到退化为'地图<id>'。"""
    try:
        s = int(slot)
    except (TypeError, ValueError):
        return ""
    mid = MAP_SLOT_TO_ID[s] if 0 <= s < len(MAP_SLOT_TO_ID) else s
    name = MAP_ID_TO_NAME.get(mid, "")
    return name if name else f"地图{mid}"


def resolve_item_id(value: object, default: int = 0, *, warn: bool = True) -> int:
    """把配置值解析成 itemId:接受数字 / 数字串(支持 0x)/ 物品名 / '名字(123)'。
    未知名字打印告警并回退 default(warn=False 时静默;用于"游戏表查不到再退网店目录"这种链式解析)。"""
    if value is None or isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if not text:
        return default
    try:
        return int(text, 0)
    except ValueError:
        pass
    # 先按完整物品名精确匹配:有的物品名本身就以「(数字)」结尾(如「錢包(5500)」=itemId 9248),
    # 必须先查名表,否则会被下面「(id)后缀」启发式误当成 itemId 5500(=浮游石)。
    iid = ITEM_IDS_BY_NAME.get(text)
    if iid is not None:
        return iid
    # 退而求其次:'名字(123)' 这种 item_label 展示格式(末尾括号是 itemId),取括号里的数字。
    # 注意 item_label(9248)='錢包(5500)(9248)',末尾括号仍是真 id,上面精确匹配失败才走到这。
    m = re.search(r"\((\d+)\)\s*$", text)
    if m:
        return int(m.group(1))
    if warn:
        print(f"[配置] 未知物品名 {text!r}(不是数字 itemId 也不在 item_names.json),按 {default} 处理")
    return default


def resolve_item_id_list(value: object) -> list[int]:
    """逗号/分号分隔,每项可为 itemId 或物品名;返回 itemId 列表(丢弃解析为 0 的空项)。"""
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        items = list(value)
    else:
        items = str(value).replace(";", ",").split(",")
    out: list[int] = []
    for it in items:
        if it is None:
            continue
        if isinstance(it, str) and not it.strip():
            continue
        iid = resolve_item_id(it, 0)
        if iid:
            out.append(iid)
    return out
