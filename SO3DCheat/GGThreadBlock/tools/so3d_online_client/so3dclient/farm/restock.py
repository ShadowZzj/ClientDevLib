"""跑店补货:paodian HTTP、银行转仓、restock 调度。"""
from __future__ import annotations

import json
import re
import socket
import struct
import time
import urllib.error
import urllib.request

from ..runtime import bump_controller_stat
from ..logio import biz_log
from ..protocol import (
    CASH_SLOT_CAP,
    CG_BANK_KEEPALIVE,
    CG_BANK_MOVE,
    CG_OPEN_BANK,
    STACK_MAX,
    SUMMON_ITEM_ID,
    WIRE_SLOT_BASE,
)
from ..tables import item_label, item_stackable, resolve_item_id
from .state import FarmState, cash_count_item, farm_send

from ..logio import log_print as print


# ---------- 自动补货:泡点网店买 -> 进 bank -> 搬到 cash ----------
PAODIAN_BASE = "https://shop2.guguseal.com"
RESTOCK_WAIT_TIMEOUT = 30.0     # 买完等物品到 bank 并搬完的总超时(秒)
RESTOCK_REOPEN_INTERVAL = 3.0   # waiting 期间每隔多久重发一次开 bank(等到账)
RESTOCK_COOLDOWN = 60.0         # 两轮补货最短间隔(秒),防止失控连买
RESTOCK_FAIL_COOLDOWN = 300.0   # 购买失败(泡点不足/HTTP 400 等)后的重试间隔(秒)
RESTOCK_SCAN_INTERVAL = 5.0     # idle 扫描节流(秒);主循环 0.25s tick,不节流会每 tick 重扫
_paodian_shop_cache: dict[str, object] = {"at": 0.0, "items": {}}


def _paodian_headers() -> dict[str, str]:
    return {
        "accept": "*/*",
        "accept-language": "zh-CN,zh;q=0.9",
        "referer": PAODIAN_BASE + "/",
        "user-agent": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36"),
    }


def _paodian_http(path: str, payload: "dict | None" = None, timeout: float = 15.0):
    url = PAODIAN_BASE + path
    headers = _paodian_headers()
    if payload is None:
        req = urllib.request.Request(url, headers=headers, method="GET")
    else:
        body = json.dumps(payload).encode("utf-8")
        headers["content-type"] = "application/json"
        headers["origin"] = PAODIAN_BASE
        req = urllib.request.Request(url, data=body, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", "replace"))


def _ensure_shop_cache() -> "dict[int, tuple[str, object]]":
    """返回 {网店itemID: (name, bubble_price)};/api/webshop-data 缓存 5 分钟。"""
    now = time.time()
    items = _paodian_shop_cache.get("items") or {}
    if now - float(_paodian_shop_cache.get("at", 0.0)) > 300 or not items:
        data = _paodian_http("/api/webshop-data")
        items = {}
        if isinstance(data, list):
            for it in data:
                try:
                    items[int(it.get("itemid"))] = (str(it.get("name", "")), it.get("bubble_price"))
                except (TypeError, ValueError):
                    continue
        _paodian_shop_cache["items"] = items
        _paodian_shop_cache["at"] = now
    return items


def paodian_shop_item(item_id: int) -> "tuple[str, object] | None":
    """返回网店 itemID 的 (name, bubble_price)。买货包要带这两项。"""
    return _ensure_shop_cache().get(int(item_id))


def paodian_shop_id_by_name(name: str) -> "int | None":
    """按网店【显示名】反查网店itemID(如「進階擺攤(7天)」-> 7899)。网店名常带 (7天) 这类后缀,
    游戏物品表 item_names.json 里没有,故只能查网店目录而不是 resolve_item_id。同名取首个。"""
    target = str(name).strip()
    if not target:
        return None
    for iid, (nm, _bubble) in _ensure_shop_cache().items():
        if str(nm).strip() == target:
            return iid
    return None


def resolve_shop_id(value: object, default: int = 0) -> int:
    """把【网店侧】配置值解析成网店itemID:数字 / 数字串(支持0x)/ '名字(123)' / 网店显示名。
    与游戏表 resolve_item_id 的区别:名字优先查【网店目录】(覆盖「進階擺攤(7天)」这种网店专属名),
    查不到再退回游戏表(网店itemID 与游戏 itemId 一致的物品,直接用游戏名也能命中)。
    注意:走到网店目录分支会触发一次 /api/webshop-data 拉取(5 分钟缓存)。"""
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
    m = re.search(r"\((\d+)\)\s*$", text)
    if m:
        return int(m.group(1))
    sid = paodian_shop_id_by_name(text)
    if sid:
        return sid
    return resolve_item_id(text, default)


def paodian_purchase(username: str, password: str, item_id: int, count: int) -> str:
    """泡点网店购买(HTTP,花泡点)。成功返回服务端 message;失败抛异常。买到的货进游戏 bank。"""
    info = paodian_shop_item(item_id)
    if not info:
        raise RuntimeError(f"网店里没有 itemID={item_id}")
    name, bubble = info
    data = _paodian_http("/api/purchase", {
        "username": username, "password": password,
        "itemID": int(item_id), "itemCount": str(int(count)),
        "itemname": name, "bubblePrice": bubble,
    })
    return str(data.get("message", "")) if isinstance(data, dict) else str(data)


def bank_open(state: FarmState, sock: socket.socket) -> None:
    """开账号共享仓库:411154[1] + 411644[self](抓包里这俩同时发)。bank 内容随后由 511320 回流。"""
    farm_send(state, sock, CG_OPEN_BANK, struct.pack("<I", 1), "CG_OPEN_BANK")
    farm_send(state, sock, CG_BANK_KEEPALIVE, struct.pack("<I", CG_BANK_KEEPALIVE), "CG_BANK_KEEPALIVE")


def cash_dest_slot(state: FarmState, item_id: int, move_n: int = 1) -> "int | None":
    """挑搬运目标 cash 格。可堆叠物品:优先已有同物品、且叠上 move_n 不超满摞(STACK_MAX)的格;
    否则第一个空格。不可堆叠物品(如艾彼雷歐寶石箱):每件必须独占一格,绝不叠到已有同物品格上
    ——服务端不接受把不可堆叠物品并进已占用格(搬运带的是显式目标 wireSlot),若还挑同物品格,
    本地会把 N 件乐观记到同一格、实际只进 1 件,补货判满 / 上架清单都会按这错误数量走(拖 N 件只上架 1 件)。
    满(80 格全占)返回 None;搬运方带显式 destWireSlot,只要挑的是空格服务端就照落,本地与服务端一致。
    叠满换空格的历史原因:服务端对可堆叠超摞搬运也是落到新空格的(抓包实证,且搬运不回 cash 包),
    本地若把乐观数量记到原格,会被该格消耗包(511325)冲掉,导致补货循环误判 cash 不足而无限买。"""
    if item_stackable(item_id):
        for slot, (iid, cnt) in sorted(state.cash_bag.items()):
            if iid == item_id and 0 < cnt and cnt + move_n <= STACK_MAX:
                return slot
    used = {s for s, (iid, _c) in state.cash_bag.items() if iid}
    for s in range(CASH_SLOT_CAP):
        if s not in used:
            return s
    return None


def bank_move_to_cash(state: FarmState, sock: socket.socket, uid: int, count: int, *, reason: str) -> int:
    """把 bank 里某唯一实例 uid 搬进 cash:411155[destWireSlot, uid, 0, count]。返回实际搬运件数。
    服务端搬运后不回 cash 更新包(只回 511322 bank 侧),故本地乐观把数量加进 cash_bag,
    否则补货循环判断 cash 数量一直不变会无限买。"""
    item_id, have = state.bank.get(uid, (0, 0))
    if not item_id:
        return 0
    move_n = min(count, have) if have else count
    if move_n <= 0:
        return 0
    dest = cash_dest_slot(state, item_id, move_n)
    if dest is None:
        print(f"[补货]({reason})cash 背包没有空格,无法搬运 {item_label(item_id)}")
        return 0
    prev = state.cash_bag.get(dest, (0, 0))
    base = prev[1] if prev[0] == item_id else 0
    move_n = min(move_n, STACK_MAX - base)   # 目标格剩余空间兜底,绝不叠爆满摞 300
    if move_n <= 0:
        return 0
    farm_send(state, sock, CG_BANK_MOVE,
              struct.pack("<IIII", dest + WIRE_SLOT_BASE, uid, 0, move_n), "CG_BANK_MOVE")
    state.cash_bag[dest] = (item_id, base + move_n)
    state.cash_observed = True
    state.bank.pop(uid, None)
    print(f"[补货]({reason})搬运 {item_label(item_id)} x{move_n}  bank(uid={uid}) -> cash格{dest}")
    biz_log(f"补货搬运 {item_label(item_id)} x{move_n}", ui=True)
    return move_n


def bank_pull_to_cash(state: FarmState, sock: socket.socket, template_id: int, want: int = 0,
                      *, reason: str = "手动") -> int:
    """把 bank 里模板id==template_id 的【所有】唯一实例逐个搬进 cash;want>0 时最多搬 want 件,
    want<=0 搬全部。不可堆叠物品(如寶石箱在 bank 占多格)每格一个 uid,这里逐 uid 搬,每件落
    独立 cash 空格;cash 满(某次 bank_move_to_cash 搬不动返回 0)即停,剩余留在 bank。返回实际搬入件数。
    供界面「银行拖到 cash」用,与自动补货 / 补货上架共用同一 bank_move_to_cash 搬运/落格逻辑。"""
    template_id = int(template_id)
    if not template_id:
        return 0
    limit = int(want) if want and int(want) > 0 else None
    total = 0
    for uid, (tmpl, cnt) in sorted(state.bank.items()):
        if tmpl != template_id:
            continue
        take = cnt if limit is None else min(cnt, limit - total)
        if take <= 0:
            break
        moved = bank_move_to_cash(state, sock, uid, take, reason=reason)
        if moved <= 0:
            break   # cash 满 / 搬不动,停止(剩余留在 bank)
        total += moved
        if limit is not None and total >= limit:
            break
    return total


def parse_restock_schedule(value: object) -> list[tuple[str, int, int, str]]:
    """解析自动补货表 -> [(背包物品文本, cash阈值, 每次买数量, 网店物品文本)]。接受:
      - list[dict] {"item","threshold","buy"[,"shop"]}
      - list[str]/多行字符串,每行 '背包名=阈值:数量' 或 '背包名@网店名=阈值:数量'
    item(背包侧)用 resolve_item_id 查游戏表,做 cash 计数 + bank 搬运匹配;
    shop(网店侧)用 resolve_shop_id 查网店目录,做泡点购买。shop 留空 = 与 item 相同
    (兼容旧写法:网店itemID 与游戏 itemId 一致的物品)。两边不同名的物品(如背包「進階擺攤」/
    网店「進階擺攤(7天)」)分别填:dict 给 "shop",或单行写成 '進階擺攤@進階擺攤(7天)=1:1'。
    物品保留为文本(名字/ID/'名字(123)'),解析交给调用方。丢弃物品为空或阈值/数量<=0 的项。"""
    out: list[tuple[str, int, int, str]] = []
    if not value:
        return out
    items = value if isinstance(value, (list, tuple)) else str(value).splitlines()
    for it in items:
        shop = ""
        try:
            if isinstance(it, dict):
                item = str(it.get("item", "")).strip()
                shop = str(it.get("shop", "") or "").strip()
                thr = int(it.get("threshold")); buy = int(it.get("buy"))
            else:
                s = str(it).strip()
                if not s or "=" not in s or ":" not in s:
                    continue
                left, _, right = s.partition("=")
                thr_s, _, buy_s = right.partition(":")
                item, _, shop = left.partition("@")
                item = item.strip(); shop = shop.strip()
                thr = int(thr_s.strip()); buy = int(buy_s.strip())
        except (TypeError, ValueError):
            continue
        if item and thr > 0 and buy > 0:
            out.append((item, thr, buy, shop))
    return out


def restock_kick(state: FarmState, sock: socket.socket, shop_id: int, buy_count: int,
                 *, bag_id: "int | None" = None, reason: str) -> bool:
    """启动一次补货:用网店itemID(shop_id)泡点购买(HTTP 阻塞),成功则开 bank 并进 waiting。
    买到的货以【背包模板id】(bag_id)落入 bank,后续 tick 由 restock_advance 按 bag_id 搬进 cash。
    bag_id 省略时默认 = shop_id(网店itemID 与游戏 itemId 一致的物品,兼容旧调用)。"""
    args = state.args
    user = str(getattr(args, "account", "") or "")
    pw = str(getattr(args, "password", "") or "")
    if not user or not pw:
        print("[补货]缺少账号/密码,无法泡点购买")
        return False
    template_id = int(bag_id) if bag_id else int(shop_id)
    try:
        msg = paodian_purchase(user, pw, shop_id, buy_count)
    except Exception as exc:
        print(f"[补货]({reason})泡点购买失败 网店id={shop_id}:{exc}")
        biz_log(f"补货购买失败 {item_label(template_id)}:{exc}", ui=True)
        return False
    label = item_label(template_id)
    if int(shop_id) != template_id:
        label += f"[网店id {shop_id}]"
    print(f"[补货]({reason})泡点购买 {label} x{buy_count}:{msg}")
    biz_log(f"泡点购买 {item_label(template_id)} x{buy_count}", ui=True)
    bank_open(state, sock)
    now = time.monotonic()
    state.restock_phase = "waiting"
    state.restock_item = template_id
    state.restock_target = int(buy_count)
    state.restock_deadline = now + RESTOCK_WAIT_TIMEOUT
    state.restock_reopen_at = now + RESTOCK_REOPEN_INTERVAL
    return True


def restock_advance(state: FarmState, sock: socket.socket, now: float) -> None:
    """waiting 阶段:bank 内容已由主循环的 update_farm_state(511320)刷新。把匹配模板id 的
    bank 条目搬进 cash,直到搬够 target;超时则放弃;期间定期重发开 bank 等到账。"""
    if state.restock_phase != "waiting":
        return
    for uid, (tmpl, cnt) in sorted(state.bank.items()):
        if state.restock_target <= 0:
            break
        if tmpl != state.restock_item:
            continue
        moved = bank_move_to_cash(state, sock, uid, min(cnt, state.restock_target), reason="自动补货")
        state.restock_target -= moved
        if moved and state.restock_item == SUMMON_ITEM_ID:
            bump_controller_stat("stat_summon_bought", moved)
    if state.restock_target <= 0:
        state.restock_phase = "idle"
        return
    if now >= state.restock_deadline:
        print(f"[补货]等待 {item_label(state.restock_item)} 到仓库超时,放弃(还差 {state.restock_target})")
        biz_log(f"补货超时 {item_label(state.restock_item)}(还差 {state.restock_target})", ui=True)
        state.restock_phase = "idle"
        return
    if now >= state.restock_reopen_at:
        bank_open(state, sock)
        state.restock_reopen_at = now + RESTOCK_REOPEN_INTERVAL


def restock_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """自动补货 tick。waiting 阶段无条件推进(手动测试也走这条);idle 阶段才看开关+定时表触发。"""
    if state.restock_phase != "idle":
        restock_advance(state, sock, now)
        return
    if not getattr(state.args, "restock_enabled", False):
        return
    schedule = parse_restock_schedule(getattr(state.args, "restock_schedule", None))
    if not schedule or not state.cash_observed:
        return
    if now < state.next_restock_at:
        return
    state.next_restock_at = now + RESTOCK_SCAN_INTERVAL   # 每次扫描都先节流到 N 秒后,避免每 tick 重扫/刷告警
    for item_text, threshold, buy, shop_text in schedule:
        bag_id = resolve_item_id(item_text, 0)   # 背包/cash 模板id:计数 + bank 搬运匹配(查 item_names.json)
        if not bag_id:
            continue
        total = cash_count_item(state.cash_bag, bag_id)
        if total < threshold:
            state.next_restock_at = now + RESTOCK_COOLDOWN   # 真触发了用更长冷却
            # 网店侧:留空=与背包同 id;填了就查网店目录(覆盖「進階擺攤(7天)」这种网店专属名)
            shop_id = resolve_shop_id(shop_text, 0) if shop_text else bag_id
            if not shop_id:
                print(f"[补货]网店物品 {shop_text!r} 在网店目录里查不到,跳过(背包侧={item_label(bag_id)})")
                break
            ok = restock_kick(state, sock, shop_id, buy, bag_id=bag_id,
                              reason=f"{item_text} cash{total}<{threshold}")
            if not ok:
                state.next_restock_at = now + RESTOCK_FAIL_COOLDOWN   # 购买失败(泡点不足/HTTP 400 等),拉长到 5 分钟再试
            break   # 一次只补一种,下一轮再看其它
