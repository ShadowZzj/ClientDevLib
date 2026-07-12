"""自动卖/买/邮寄:摆摊会话、autosell/autobuy/automail。"""
from __future__ import annotations

import socket
import struct
import time

from ..runtime import bump_controller_stat
from ..logio import biz_log
from ..protocol import (
    BAG_MAIN_SLOTS,
    CG_SELL_ITEM,
    CG_SEND_MAIL,
    CG_USE_CASH_ITEM,
    CG_VENDOR_BUY,
    CG_VENDOR_CLOSE,
    CG_VENDOR_OPEN,
    MAIL_BODY_FIELD,
    MAIL_HEADER_CONST,
    MAIL_OP_ITEM,
    MAIL_OP_MONEY,
    MAIL_RECIPIENT_FIELD,
    SELL_TOKEN,
    SUMMON_ITEM_ID,
    SUMMON_VENDOR_ID,
    VENDOR2_SHOP_INDEX,
    VENDOR_MAX_PER_BUY,
    VENDOR_TOKEN,
    WIRE_SLOT_BASE,
)
from ..tables import item_label, item_sellable
from .state import FarmState, bag_count_item, bag_free_slots, farm_send, find_summon_slot

from ..logio import log_print as print


def vendor_open_session(state: FarmState, sock: socket.socket, *, reason: str) -> bool:
    """召唤摊贩呼叫卷(411156) -> 开摊贩(411455)。成功 True;找不到呼叫卷/cash 未同步 False。
    真实抓包顺序固定为:USE_CASH(召唤)->VENDOR_OPEN(开)->...卖/买...->VENDOR_CLOSE(关)。
    开摊贩后必须 sleep(autobuy_open_delay) 再发后续买/卖包——太快服务端会直接 RST 掉线。"""
    args = state.args
    summon_slot = find_summon_slot(state)
    if summon_slot is None:
        if not state.cash_observed:
            print(f"[摊贩]({reason})cash 背包尚未同步(511324),无法召唤摊贩")
        else:
            print(f"[摊贩]({reason})cash 背包内找不到摊贩呼叫卷({item_label(SUMMON_ITEM_ID)}),无法召唤")
        return False
    farm_send(state, sock, CG_USE_CASH_ITEM,
              struct.pack("<II", summon_slot + WIRE_SLOT_BASE, 0), "CG_USE_CASH_ITEM")
    time.sleep(max(0.0, args.autobuy_open_delay))
    farm_send(state, sock, CG_VENDOR_OPEN, struct.pack("<I", VENDOR_TOKEN), "CG_VENDOR_OPEN")
    print(f"[摊贩]({reason})已召唤并开摊贩(cash格={summon_slot})")
    biz_log(f"打开摊贩({reason})")
    bump_controller_stat("stat_summon_count")   # 每次召唤消耗一张摊贩呼叫券
    time.sleep(max(0.0, args.autobuy_open_delay))
    return True


def vendor_close_session(state: FarmState, sock: socket.socket, *, reason: str) -> None:
    farm_send(state, sock, CG_VENDOR_CLOSE, struct.pack("<I", VENDOR_TOKEN), "CG_VENDOR_CLOSE")
    print(f"[摊贩]({reason})已关摊贩")
    biz_log(f"关闭摊贩({reason})")


def autosell_run(state: FarmState, sock: socket.socket, *, reason: str = "自动",
                 manage_vendor: bool = True) -> int:
    """执行一轮贩卖:从 autosell_start_slot 卖到 autosell_end_slot,跳过 keep 列表
    以及 item_names.json 标记 sellable=false 的物品。另外 autosell_extra_item_ids 里的
    物品不分格、整背包一律卖(覆盖落在贩卖区间之前的靠前格;显式指定故无视 keep)。
    不做 enabled/节流/空格门槛(那些在 autosell_tick);手动触发直接调本函数。
    贩卖必须在摊贩开着时发包,否则服务端 RST;manage_vendor=True 时本函数自行
    召唤+开摊贩、卖完再关(真实抓包就是 开→卖→买→关 一个会话)。返回卖出格数。"""
    args = state.args
    keep = set(args.autosell_keep_item_ids or [])
    # 鱼饵是用来钓鱼/刚买进来的,绝不能卖掉(钓鱼饵 + autobuy 买的都算)
    for bait in (args.fishing_bait_item_id, args.autobuy_item_id):
        if bait:
            keep.add(bait)
    extra = set(args.autosell_extra_item_ids or [])
    lo = max(0, args.autosell_start_slot)
    hi = min(BAG_MAIN_SLOTS - 1, args.autosell_end_slot)

    # 先盘点这一轮要卖哪些格(开摊贩前算好,避免空开一轮摊贩)
    targets: list[tuple[int, int, int]] = []
    picked: set[int] = set()

    def _consider(slot: int, *, force: bool) -> None:
        """纳入卖出清单。force=True(额外贩卖名单)无视 keep——用户显式点名要卖;
        但 sellable=false 仍跳过(服务端不收,白发包)。已选过的格不重复加。"""
        if slot in picked:
            return
        item_id, count = state.bag.get(slot, (0, 0))
        if item_id == 0 or count <= 0:
            return
        if not force and item_id in keep:
            print(f"[贩卖]({reason})跳过保留物品 格={slot} 物品={item_label(item_id)} 数量={count}")
            return
        if not item_sellable(item_id):
            print(f"[贩卖]({reason})跳过不可贩卖 格={slot} 物品={item_label(item_id)} 数量={count}")
            return
        targets.append((slot, item_id, count))
        picked.add(slot)

    # 1) 配置的贩卖格区间 [lo, hi]
    for slot in range(lo, hi + 1):
        _consider(slot, force=False)
    # 2) 额外贩卖名单:不分格,整背包(含区间之前的靠前格)凡 itemId 命中就卖
    if extra:
        for slot in range(BAG_MAIN_SLOTS):
            if state.bag.get(slot, (0, 0))[0] in extra:
                _consider(slot, force=True)

    if not targets:
        print(f"[贩卖]({reason})区间[{lo}-{hi}]及额外名单内无可卖物品(已跳过保留/不可贩卖)")
        return 0

    opened_here = False
    if manage_vendor:
        if not vendor_open_session(state, sock, reason=reason):
            return 0
        opened_here = True

    sold = 0
    try:
        for slot, item_id, count in targets:
            body = struct.pack("<III", slot + WIRE_SLOT_BASE, count, SELL_TOKEN)
            farm_send(state, sock, CG_SELL_ITEM, body, "CG_SELL_ITEM")
            print(f"[贩卖]({reason})卖出格={slot} 物品={item_label(item_id)} 数量={count}")
            biz_log(f"贩卖 {item_label(item_id)} x{count}")
            state.bag[slot] = (0, 0)
            sold += 1
            time.sleep(max(0.0, args.autosell_per_packet_delay))
    finally:
        if opened_here:
            vendor_close_session(state, sock, reason=reason)
    if sold:
        print(f"[贩卖]({reason})本轮共卖出 {sold} 格")
        biz_log(f"本轮贩卖 {sold} 格")
    return sold


def sell_single_slot(state: FarmState, sock: socket.socket, slot: int, *, reason: str = "手动单格") -> bool:
    """卖出指定的单个背包格(GUI 右键贩卖)。用户显式指定该格,故不走 keep / sellable 过滤
    (鱼饵也能被卖,由用户负责)。流程同 autosell_run:开摊贩 -> 卖该格 -> 关摊贩。
    数量 = state.bag 里的真实堆叠数(整桶解析已 +1 对齐游戏),与游戏 Item_GetSellQuantity 一致。"""
    item_id, count = state.bag.get(slot, (0, 0))
    if item_id == 0 or count <= 0:
        print(f"[贩卖]({reason})格={slot} 为空,跳过")
        return False
    if not vendor_open_session(state, sock, reason=reason):
        return False
    try:
        body = struct.pack("<III", slot + WIRE_SLOT_BASE, count, SELL_TOKEN)
        farm_send(state, sock, CG_SELL_ITEM, body, "CG_SELL_ITEM")
        print(f"[贩卖]({reason})卖出格={slot} 物品={item_label(item_id)} 数量={count}")
        biz_log(f"贩卖 {item_label(item_id)} x{count}")
        state.bag[slot] = (0, 0)
        time.sleep(max(0.0, state.args.autosell_per_packet_delay))
    finally:
        vendor_close_session(state, sock, reason=reason)
    return True


def autosell_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """背包将满时自动贩卖。autosell_run 会自行召唤+开摊贩、卖完再关,无需外部先开。"""
    args = state.args
    if not args.autosell_enabled or not state.bag_observed:
        return
    if now < state.next_autosell_at:
        return
    free_slots = bag_free_slots(state, args.autosell_start_slot, args.autosell_end_slot)
    if free_slots > args.autosell_min_free_slots:
        return
    autosell_run(state, sock, reason=f"自动·剩余空格{free_slots}")
    state.next_autosell_at = now + 3.0


def send_money_mail(state: FarmState, sock: socket.socket, recipient: str, money: int, *, reason: str) -> None:
    """寄金钱邮件(CG_SEND_MAIL 411524, op=0)。body 固定 77 字节,布局见常量处文档。
    收件人按账号 encoding(cp950/Big5)编码后塞进 16 字节字段(超 15 截断,留 NUL)。
    寄出后服务端回 521563 刷新钱包(已并入 MONEY_WIRE_OFFSETS),余额回落后自然停寄。"""
    enc = getattr(state.args, "encoding", "cp950") or "cp950"
    name_bytes = recipient.encode(enc, "ignore")[:MAIL_RECIPIENT_FIELD - 1].ljust(MAIL_RECIPIENT_FIELD, b"\x00")
    body = (struct.pack("<I", MAIL_HEADER_CONST) + name_bytes + struct.pack("<I", MAIL_OP_MONEY)
            + struct.pack("<q", int(money)) + b"\x00" * MAIL_BODY_FIELD)
    farm_send(state, sock, CG_SEND_MAIL, body, "CG_SEND_MAIL")
    print(f"[自动邮寄]({reason})寄 {money} 给 {recipient}")
    biz_log(f"邮寄 {money:,} → {recipient}")


def send_item_mail(state: FarmState, sock: socket.socket, recipient: str, slot: int, *,
                   expect_item_id: int = 0, count: "int | None" = None, reason: str) -> bool:
    """寄物品邮件(CG_SEND_MAIL 411524, op=1)。金钱的 int64 字段拆成 [itemBagId u32][itemCount u32]。
    itemBagId 口径同卖货/摆摊 = wireSlot = 内部slot+WIRE_SLOT_BASE(13)。count=None 整格全寄。
    expect_item_id 非 0 时校验该格确实是这件物品(防背包在排队期间挪动导致错寄)。"""
    iid, have = state.bag.get(slot, (0, 0))
    if iid == 0 or have <= 0:
        print(f"[物品邮寄]({reason})格={slot} 为空,跳过")
        return False
    if expect_item_id and iid != expect_item_id:
        print(f"[物品邮寄]({reason})格={slot} 现为 {item_label(iid)}(期望 {item_label(expect_item_id)}),跳过")
        return False
    send_count = have if count is None else min(int(count), have)
    if send_count <= 0:
        return False
    enc = getattr(state.args, "encoding", "cp950") or "cp950"
    name_bytes = recipient.encode(enc, "ignore")[:MAIL_RECIPIENT_FIELD - 1].ljust(MAIL_RECIPIENT_FIELD, b"\x00")
    body = (struct.pack("<I", MAIL_HEADER_CONST) + name_bytes + struct.pack("<I", MAIL_OP_ITEM)
            + struct.pack("<II", slot + WIRE_SLOT_BASE, send_count) + b"\x00" * MAIL_BODY_FIELD)
    farm_send(state, sock, CG_SEND_MAIL, body, "CG_SEND_MAIL")
    print(f"[物品邮寄]({reason})寄 {item_label(iid)} x{send_count}(格{slot}/wire{slot + WIRE_SLOT_BASE}) 给 {recipient}")
    biz_log(f"物品邮寄 {item_label(iid)} x{send_count} → {recipient}")
    if count is None or send_count >= have:
        state.bag[slot] = (0, 0)
    else:
        state.bag[slot] = (iid, have - send_count)
    time.sleep(0.3)
    return True


def automail_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """自动邮寄金钱:每 automail_poll_interval 秒检查一次,钱包>阈值就寄固定额给收件人。
    寄出后 521563 回流把钱包刷低,余额回落到阈值以下即自然停寄(故无需额外防重发逻辑;
    轮询间隔本身也兼当两封最短间隔,够 ack 回到再判)。"""
    args = state.args
    if not getattr(args, "automail_enabled", False):
        return
    # 白名单(逗号分隔的账号)内的账号不自动邮寄,留着自己用钱。
    whitelist = {a.strip() for a in str(getattr(args, "automail_whitelist", "") or "").split(",") if a.strip()}
    if str(getattr(args, "account", "") or "").strip() in whitelist:
        return
    recipient = str(getattr(args, "automail_recipient", "") or "").strip()
    threshold = int(getattr(args, "automail_money_threshold", 0) or 0)
    amount = int(getattr(args, "automail_amount", 0) or 0)
    if not recipient or threshold <= 0 or amount <= 0:
        return
    if now < state.next_automail_at:
        return
    poll = max(2.0, float(getattr(args, "automail_poll_interval", 10.0) or 10.0))
    state.next_automail_at = now + poll
    if not state.gold_seen or state.gold <= threshold:
        return
    send_money_mail(state, sock, recipient, min(amount, state.gold), reason=f"钱{state.gold}>阈值{threshold}")


def find_buy_target_slot(state: FarmState, item_id: int):
    """计算买货落点 targetSlot(wire = 内部slot+WIRE_SLOT_BASE),镜像 DLL FindVendorTargetSlot:
    优先堆到已有同 itemId 的格,否则用第一个空格;背包满返回 None。"""
    for slot in range(BAG_MAIN_SLOTS):
        iid, cnt = state.bag.get(slot, (0, 0))
        if iid == item_id and cnt > 0:
            return slot + WIRE_SLOT_BASE
    for slot in range(BAG_MAIN_SLOTS):
        iid, cnt = state.bag.get(slot, (0, 0))
        if iid == 0 or cnt <= 0:
            return slot + WIRE_SLOT_BASE
    return None


def autobuy_run(state: FarmState, sock: socket.socket, *, reason: str = "自动", count: "int | None" = None) -> bool:
    """执行一次购买:用「摊贩呼叫卷」召唤摊贩(411156)->开(411455)->买(411020)->关(411456)。
    不做 enabled/节流/min_bait 门槛(那些在 autobuy_tick);手动触发直接调本函数。
    count=None 用配置的 autobuy_count;手动测试可传入要买的数量覆盖。
    未配置要买的 itemId、或 cash 背包内找不到摊贩呼叫卷,则返回 False。"""
    args = state.args
    bait_id = args.autobuy_item_id or args.fishing_bait_item_id
    if not bait_id:
        print(f"[购买]({reason})未配置要购买的 itemId(autobuy_item_id),放弃")
        return False
    shop_index = VENDOR2_SHOP_INDEX.get(bait_id)
    if shop_index is None:
        print(f"[购买]({reason})物品={item_label(bait_id)} 不在 vendorId=2 商品表里,无法确定 shopIndex,放弃下单")
        return False
    have = bag_count_item(state, bait_id)
    print(f"[购买]({reason})物品={item_label(bait_id)} 现有={have},准备召唤摊贩购买")
    if not vendor_open_session(state, sock, reason=reason):
        return False
    try:
        target_slot = find_buy_target_slot(state, bait_id)
        if target_slot is None:
            print(f"[购买]({reason})主背包已满,无处存放 物品={item_label(bait_id)},放弃下单")
            return False
        total = max(0, int(count if count is not None else args.autobuy_count))
        if total <= 0:
            print(f"[购买]({reason})购买数量为 0,放弃下单")
            return False
        # 单包数量上限 VENDOR_MAX_PER_BUY=300,超出按 300 一批拆成多次下单(都落同一 targetSlot,服务端堆叠)。
        batches = [min(VENDOR_MAX_PER_BUY, total - i) for i in range(0, total, VENDOR_MAX_PER_BUY)]
        print(f"[购买]({reason})物品={item_label(bait_id)} 共需 {total},分 {len(batches)} 批(每批≤{VENDOR_MAX_PER_BUY}):{batches}")
        for idx, qty in enumerate(batches, 1):
            # 买货包字段顺序经 IDA(CG_SendBuyItem_411020)+真实抓包核对:[vendorId, shopIndex, count, targetSlot, token]
            buy_body = struct.pack("<IIIII", SUMMON_VENDOR_ID, shop_index,
                                   qty, target_slot, VENDOR_TOKEN)
            farm_send(state, sock, CG_VENDOR_BUY, buy_body, "CG_VENDOR_BUY")
            print(f"[购买] 已下单(第{idx}/{len(batches)}批):vendorId={SUMMON_VENDOR_ID} shopIndex={shop_index} "
                  f"物品={item_label(bait_id)} 数量={qty} targetSlot={target_slot}")
            biz_log(f"购买 {item_label(bait_id)} x{qty}")
            time.sleep(max(0.0, args.autobuy_buy_delay))
    finally:
        vendor_close_session(state, sock, reason=reason)
    print(f"[购买]({reason})本轮购买流程结束")
    # 1.5s 后由主循环汇报背包鱼饵总数+金钱(等买货结果包 521052 回流并被处理);手动/自动都走这里
    state.autobuy_log_at = time.monotonic() + 1.5
    state.autobuy_log_item = bait_id
    return True


def autobuy_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """鱼饵不足时自动购买。"""
    args = state.args
    if not args.autobuy_enabled:
        return
    if now < state.next_autobuy_at:
        return
    bait_id = args.autobuy_item_id or args.fishing_bait_item_id
    if not bait_id:
        return
    # 冷启动:未观测到背包前不误判鱼饵为 0
    if not state.bait_observed and not state.bag_observed:
        return
    have = bag_count_item(state, bait_id)
    if have >= args.autobuy_min_bait:
        return
    # 找不到摊贩呼叫卷时降频告警(等 511324 同步到),避免刷屏
    if find_summon_slot(state) is None:
        if now >= state.warn_summon_at:
            if not state.cash_observed:
                print("[购买] 等待 cash 背包同步(511324),暂不召唤摊贩")
            else:
                print(f"[购买] cash 背包内找不到摊贩呼叫卷({item_label(SUMMON_ITEM_ID)}),无法召唤")
            state.warn_summon_at = now + 10.0
        state.next_autobuy_at = now + 5.0
        return
    print(f"[购买] 鱼饵不足(物品={item_label(bait_id)} 现有={have} < {args.autobuy_min_bait}),触发自动购买")
    autobuy_run(state, sock, reason="自动")
    # 用 run 结束后的时刻算冷却:autobuy_run 要跑好几秒,若用开头的 now 冷却会瞬间失效
    state.next_autobuy_at = time.monotonic() + 5.0
