"""个人摆摊(street stall):开摊 411040 -> 上架 411049 -> 监控 511110/521109 -> 终止 412050。

纯 socket 客户端发包流程(对照 shadowdance 真实抓包,逐字节核对见 tests/test_stall_packets.py):
  1. 411040 开摊:body = [32B 摊位名][u32 a3=12][u32 type=2]
  2. 411049 上架:body = [u32 count=16] + 16 条 24B 条目;每条 [bagType, 0, wireSlot, qty, u64 price],
     不足 16 条用空槽哨兵补满。条目顺序下标即服务端回 511110 的 stallSlotIdx。
  3. 监控:511110 按 stallSlotIdx 更新各格绝对剩余;521109 卖出回流(刷源背包格 + 最新金币)。
     这两个 recv 在 update_farm_state 里处理,直接写 state.stall_slots。
  4. 412050 终止:body = [u32 1, u32 0, u32 0]。

发包必须在 worker 线程做(farm_send),GUI 线程只通过 action_queue 投递动作。
"""
from __future__ import annotations

import socket
import time

from ..logio import biz_log
from ..protocol import (
    CG_OPEN_STREETSTALL,
    CG_STREETSTALL_CLOSE,
    CG_STREETSTALL_ITEMS,
    CG_STREETSTALL_TERMINATE,
    STALL_BAG_CASH,
    STALL_BAG_NORMAL,
    STALL_MAX_SLOTS,
    WIRE_SLOT_BASE,
    streetstall_close_body,
    streetstall_items_body,
    streetstall_open_body,
    streetstall_terminate_body,
)
from ..tables import item_label, resolve_item_id
from .state import FarmState, cash_count_item, farm_send
from .restock import (
    RESTOCK_REOPEN_INTERVAL,
    RESTOCK_WAIT_TIMEOUT,
    bank_move_to_cash,
    bank_open,
    paodian_purchase,
    resolve_shop_id,
)

from ..logio import log_print as print


def _bag_snapshot(state: FarmState, bag_type: int) -> "dict[int, tuple[int, int]]":
    """按来源取背包快照:cash(1) -> state.cash_bag,主背包(0) -> state.bag。"""
    return state.cash_bag if bag_type == STALL_BAG_CASH else state.bag


def normalize_listings(state: FarmState, listings) -> "list[dict]":
    """把 GUI 传来的上架列表对照背包快照校验、归一化为可发包条目。

    listings 每项:(bag_type, src_slot, item_id, qty, price)。
      bag_type:0=主背包 / 1=cash 背包;src_slot 为内部 0 基格号;
      item_id 仅作一致性校验(防止背包在排队期间变动卖错物品);
      qty 取 min(请求量, 当前该格实际持有量);price 为单价(>=0)。
    返回 [{bag_type, src_slot, wire_slot, item_id, qty, price}],已剔除空格/数量为 0/超过 16 条的项。
    """
    out: list[dict] = []
    for item in listings:
        try:
            bag_type, src_slot, item_id, qty, price = item
        except (TypeError, ValueError):
            print(f"[摆摊] 跳过格式错误的上架项:{item!r}")
            continue
        bag_type = STALL_BAG_CASH if int(bag_type) == STALL_BAG_CASH else STALL_BAG_NORMAL
        src_slot = int(src_slot)
        want = int(item_id)
        qty = int(qty)
        price = max(0, int(price))
        if src_slot < 0 or qty <= 0:
            continue
        have_item, have_cnt = _bag_snapshot(state, bag_type).get(src_slot, (0, 0))
        if have_item == 0 or have_cnt <= 0:
            print(f"[摆摊] 跳过空格:{'cash' if bag_type == STALL_BAG_CASH else '背包'}格{src_slot}")
            continue
        if want and want != have_item:
            print(f"[摆摊] 跳过格{src_slot}:期望 {item_label(want)} 实为 {item_label(have_item)}(背包已变动)")
            continue
        real_qty = min(qty, have_cnt)
        out.append({
            "bag_type": bag_type,
            "src_slot": src_slot,
            "wire_slot": src_slot + WIRE_SLOT_BASE,
            "item_id": have_item,
            "qty": real_qty,
            "price": price,
        })
        if len(out) >= STALL_MAX_SLOTS:
            if len(listings) > STALL_MAX_SLOTS:
                print(f"[摆摊] 上架最多 {STALL_MAX_SLOTS} 件,多余的已忽略")
            break
    return out


def stall_open(state: FarmState, sock: socket.socket, name: str, listings,
               *, reason: str = "手动") -> bool:
    """开摊并上架:411040 开摊 -> sleep(stall_open_delay) -> 411049 上架。

    name 为摊位名(按账号 encoding 编码,超 32 字节截断);listings 见 normalize_listings。
    成功(至少一件可上架)返回 True,并把 state.stall_phase 置 "open"、按上架顺序填 state.stall_slots
    (stallSlotIdx -> 该格元数据),供 511110 监控剩余量。无可上架物品则返回 False、不发开摊包。
    """
    if state.stall_phase == "open":
        print(f"[摆摊]({reason})已在摆摊中,先停止当前摊位再开新摊")
        return False
    rows = normalize_listings(state, listings)
    if not rows:
        print(f"[摆摊]({reason})没有可上架的物品(背包为空或都被跳过),取消开摊")
        return False

    args = state.args
    enc = getattr(args, "encoding", "cp950") or "cp950"
    stall_name = str(name or getattr(args, "stall_default_name", "") or "小店")
    delay = getattr(args, "stall_open_delay", 0.5)
    open_delay = max(0.0, float(0.5 if delay is None else delay))

    farm_send(state, sock, CG_OPEN_STREETSTALL,
              streetstall_open_body(stall_name, enc), "CG_OPEN_STREETSTALL")
    print(f"[摆摊]({reason})开摊「{stall_name}」,共 {len(rows)} 件待上架")
    time.sleep(open_delay)

    entries = [(r["bag_type"], r["wire_slot"], r["qty"], r["price"]) for r in rows]
    farm_send(state, sock, CG_STREETSTALL_ITEMS,
              streetstall_items_body(entries), "CG_STREETSTALL_ITEMS")

    slots: dict[int, dict] = {}
    for idx, r in enumerate(rows):
        slots[idx] = {
            "item_id": r["item_id"],
            "remaining": r["qty"],
            "qty0": r["qty"],
            "price": r["price"],
            "bag_type": r["bag_type"],
            "src_slot": r["src_slot"],
        }
        print(f"[摆摊] 上架格{idx}:{item_label(r['item_id'])} x{r['qty']} 单价 {r['price']} "
              f"(源{'cash' if r['bag_type'] == STALL_BAG_CASH else '背包'}格{r['src_slot']})")
        biz_log(f"摆摊上架 {item_label(r['item_id'])} x{r['qty']} @{r['price']}", ui=True)
    state.stall_phase = "open"
    state.stall_name = stall_name
    state.stall_slots = slots
    biz_log(f"开摊「{stall_name}」{len(rows)} 件", ui=True)
    return True


def stall_terminate(state: FarmState, sock: socket.socket, *, reason: str = "手动") -> None:
    """终止摆摊:412050 停售 -> sleep -> 411041 离摊,把会话状态收回 idle。

    只发 412050 服务端把摊位留在「准备中」,别人点开仍是准备中而非整个退出;必须再发 411041
    才真正关闭离摊(对照真实客户端 StreetStall_OnClose_Send411041)。两包都对当前会话发,
    重复终止也安全(无摊时发出去服务端按未摆摊忽略)。"""
    args = getattr(state, "args", None)
    delay = getattr(args, "stall_open_delay", 0.5)
    close_delay = max(0.0, float(0.5 if delay is None else delay))

    farm_send(state, sock, CG_STREETSTALL_TERMINATE,
              streetstall_terminate_body(), "CG_STREETSTALL_TERMINATE")
    time.sleep(close_delay)
    farm_send(state, sock, CG_STREETSTALL_CLOSE,
              streetstall_close_body(), "CG_STREETSTALL_CLOSE")
    name = state.stall_name or "摊位"
    print(f"[摆摊]({reason})已停售并离摊「{name}」")
    biz_log(f"停止摆摊「{name}」", ui=True)
    state.stall_phase = "idle"
    state.stall_name = ""
    state.stall_slots = {}


def stall_status_lines(state: FarmState) -> "list[str]":
    """给 GUI/日志读的可读剩余量行(worker 写、GUI 线程读时先 dict() 拷贝 stall_slots)。"""
    if state.stall_phase != "open":
        return ["未摆摊"]
    slots = dict(state.stall_slots)
    if not slots:
        return [f"摆摊「{state.stall_name}」(等待剩余量回报)"]
    lines = [f"摆摊「{state.stall_name}」"]
    for idx in sorted(slots):
        e = slots[idx]
        sold = max(0, int(e.get("qty0", 0)) - int(e.get("remaining", 0)))
        lines.append(
            f"  格{idx} {item_label(e.get('item_id', 0))} "
            f"剩 {e.get('remaining', 0)}/{e.get('qty0', 0)}(已卖 {sold})单价 {e.get('price', 0)}"
        )
    return lines


def stall_check_sold_out(state: FarmState, sock: socket.socket) -> None:
    """摆摊中若所有格剩余都为 0(全部卖光),自动完整收摊(412050+411041)。
    每轮主循环调一次;只在 open 且确有上架格、且全部 remaining<=0 时触发一次(收摊后置 idle 不再重入)。
    开摊瞬间各格 remaining=qty0>0,故不会误触发。"""
    if state.stall_phase != "open":
        return
    slots = state.stall_slots
    if not slots:
        return
    if all(int(e.get("remaining", 0)) <= 0 for e in slots.values()):
        print("[摆摊] 上架物品已全部卖光,自动收摊")
        biz_log("摆摊售罄自动收摊", ui=True)
        stall_terminate(state, sock, reason="售罄自动")


# ---------- 「补货并上架」一键工作流:网店买 -> 进 bank -> 搬到 cash -> 从 cash 上架 ----------
def _provision_listings(state: FarmState, pending: "list[dict]") -> "list[tuple]":
    """按已搬进 cash 的件数,在 cash 背包里凑出上架清单 [(STALL_BAG_CASH, src_slot, item_id, qty, price)]。
    一个物品可能跨多格(超 300 摞),逐格列;每物品只列到本轮实际搬入的 moved 件为止,
    不会把上线前 cash 里本来就有的同物品也一起挂上。stall_open 再用 normalize_listings 对快照复核。"""
    listings: list[tuple] = []
    for p in pending:
        item_id = int(p["template_id"])
        price = int(p["price"])
        remaining = int(p.get("moved", 0))
        if item_id <= 0 or remaining <= 0:
            continue
        for slot, (iid, cnt) in sorted(state.cash_bag.items()):
            if remaining <= 0:
                break
            if iid != item_id or cnt <= 0:
                continue
            take = min(cnt, remaining)
            listings.append((STALL_BAG_CASH, slot, item_id, take, price))
            remaining -= take
    return listings


def stall_provision_kick(state: FarmState, sock: socket.socket, name: str, rows,
                         *, reason: str = "手动") -> bool:
    """启动一键工作流:逐项泡点购买(HTTP 阻塞),成功则开 bank 并进 waiting,后续 tick 搬运+上架。
    rows 每项:(shop_text, buy_count, price)。shop_text 走 resolve_shop_id 查网店目录;
    背包/cash 模板 id 默认 = 网店 id(网店 itemID 与游戏 itemId 一致的可直接转卖物品)。
    已在摆摊或工作流进行中则拒绝(返回 False)。至少买成 1 项才返回 True。"""
    if state.stall_phase == "open":
        print(f"[摆摊]({reason})已在摆摊中,先停止当前摊位再补货上架")
        return False
    if state.stall_prov_phase != "idle":
        print(f"[摆摊]({reason})上一轮补货上架还没跑完,稍后再试")
        return False

    args = state.args
    user = str(getattr(args, "account", "") or "")
    pw = str(getattr(args, "password", "") or "")
    if not user or not pw:
        print("[摆摊]缺少账号/密码,无法泡点购买")
        return False

    pending: list[dict] = []
    for row in rows:
        try:
            shop_text, buy_count, price = row
        except (TypeError, ValueError):
            print(f"[摆摊] 跳过格式错误的补货项:{row!r}")
            continue
        buy_count = int(buy_count)
        price = max(0, int(price))
        shop_id = resolve_shop_id(shop_text, 0)
        if not shop_id or buy_count <= 0:
            print(f"[摆摊] 跳过无效补货项:{shop_text!r} x{buy_count}")
            continue
        template_id = shop_id   # 网店 id 即背包模板 id(转卖物品两者一致)
        try:
            msg = paodian_purchase(user, pw, shop_id, buy_count)
        except Exception as exc:
            print(f"[摆摊]({reason})泡点购买失败 网店id={shop_id}:{exc}")
            biz_log(f"补货上架购买失败 {item_label(template_id)}:{exc}", ui=True)
            continue
        print(f"[摆摊]({reason})泡点购买 {item_label(template_id)} x{buy_count}:{msg}")
        biz_log(f"补货上架购买 {item_label(template_id)} x{buy_count} @{price}", ui=True)
        # 同物品多次买合并到一条 pending(want 累加),上架时跨格列
        for p in pending:
            if p["template_id"] == template_id and p["price"] == price:
                p["want"] += buy_count
                break
        else:
            pending.append({"template_id": template_id, "want": buy_count, "moved": 0, "price": price})

    if not pending:
        print(f"[摆摊]({reason})没有买成任何物品,取消补货上架")
        return False

    bank_open(state, sock)
    now = time.monotonic()
    state.stall_prov_phase = "waiting"
    state.stall_prov_name = str(name or getattr(args, "stall_default_name", "") or "小店")
    state.stall_prov_pending = pending
    state.stall_prov_deadline = now + RESTOCK_WAIT_TIMEOUT
    state.stall_prov_reopen_at = now + RESTOCK_REOPEN_INTERVAL
    total = sum(p["want"] for p in pending)
    print(f"[摆摊]({reason})已下单 {len(pending)} 种共 {total} 件,等到货搬进 cash 后自动上架")
    return True


def stall_provision_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """waiting 阶段推进:bank 内容由主循环 update_farm_state(511320)刷新。把匹配模板的 bank 条目
    搬进 cash 累计 moved;搬够全部 want(或超时还有部分到货)就从 cash 上架并回到 idle;期间定期重发开 bank。"""
    if state.stall_prov_phase != "waiting":
        return
    pending = state.stall_prov_pending
    for uid, (tmpl, cnt) in sorted(state.bank.items()):
        target = next((p for p in pending if p["template_id"] == tmpl and p["moved"] < p["want"]), None)
        if target is None:
            continue
        moved = bank_move_to_cash(state, sock, uid, min(cnt, target["want"] - target["moved"]),
                                  reason="补货上架")
        target["moved"] += moved

    all_done = all(p["moved"] >= p["want"] for p in pending)
    timed_out = now >= state.stall_prov_deadline
    if not all_done and not timed_out:
        if now >= state.stall_prov_reopen_at:
            bank_open(state, sock)
            state.stall_prov_reopen_at = now + RESTOCK_REOPEN_INTERVAL
        return

    # 收尾:把搬进 cash 的货上架。无论搬齐还是超时,有多少挂多少。
    name = state.stall_prov_name
    listings = _provision_listings(state, pending)
    state.stall_prov_phase = "idle"
    state.stall_prov_pending = []
    if timed_out and not all_done:
        short = [f"{item_label(p['template_id'])}(到 {p['moved']}/{p['want']})"
                 for p in pending if p["moved"] < p["want"]]
        print(f"[摆摊] 等到货超时,按已到货上架;未到齐:{', '.join(short)}")
    if not listings:
        print("[摆摊] 没有任何货搬进 cash,取消上架")
        biz_log("补货上架:无到货,取消", ui=True)
        return
    stall_open(state, sock, name, listings, reason="补货上架")
