"""个人摆摊封包构造/解析回归:与 shadowdance 真实抓包逐字节核对。

抓包来源:G:/GuGuSealKarel1120/GGConfig/shadowdance/net/sendlog & recvlog(摊位名 "xiaodian")。
直接 `python test_stall_packets.py` 运行;任何断言失败抛 SystemExit。

覆盖:
  SEND  411040 开摊 / 411049 上架 / 412050 终止 —— 构造的明文帧逐字节等于抓包。
  RECV  511110 剩余更新 / 521109 卖出回流 —— parse_* 解析字段,且 update_farm_state 正确改写状态。
  RECV  522105 关闭 ack —— 仅核对 proto 常量与帧头。
"""
from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from so3dclient.protocol import (  # noqa: E402
    CG_BANK_MOVE,
    CG_OPEN_STREETSTALL,
    CG_STREETSTALL_CLOSE,
    CG_STREETSTALL_ITEMS,
    CG_STREETSTALL_TERMINATE,
    STALL_BAG_CASH,
    SC_STREETSTALL_CLOSE,
    SC_STREETSTALL_ITEM_UPDATE,
    SC_STREETSTALL_SOLD,
    STALL_EMPTY_ENTRY,
    WIRE_SLOT_BASE,
    parse_streetstall_item_update,
    parse_streetstall_sold,
    streetstall_close_body,
    streetstall_items_body,
    streetstall_open_body,
    streetstall_terminate_body,
    u32,
    u64,
)
from so3dclient.tables import ITEM_STACKABLE  # noqa: E402
from so3dclient.farm.state import FarmState, update_farm_state  # noqa: E402
from so3dclient.farm.stall import (  # noqa: E402
    _provision_listings,
    normalize_listings,
    stall_check_sold_out,
    stall_open,
    stall_provision_tick,
    stall_status_lines,
)
from so3dclient.packets import game1841_decrypt_body  # noqa: E402


def _hx(s: str) -> bytes:
    return bytes.fromhex(s)


def _frame(proto: int, body: bytes) -> bytes:
    """明文帧 = [u32 len(含8字节头)][u32 proto][body]。"""
    return struct.pack("<II", len(body) + 8, proto) + body


def _check(cond: bool, msg: str) -> None:
    if not cond:
        raise SystemExit(f"FAIL: {msg}")


# ----- 真实抓包字节(明文 SEND / RECV)-----
# 411040 开摊 len=48:摊位名 "xiaodian" 补满 32B + a3=12 + type=2
CAP_411040 = _hx(
    "30 00 00 00 a0 45 06 00 "
    "78 69 61 6f 64 69 61 6e 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "0c 00 00 00 02 00 00 00"
)
# 411049 上架 len=396:count=16 + entry0(cash,wireSlot40,qty123,price123) + entry1(背包,wireSlot13,qty22,price88) + 14 空槽
_EMPTY_HEX = "00 00 00 00 00 00 00 00 e8 03 00 00 ff ff ff ff ff ff ff ff ff ff ff ff"
CAP_411049 = _hx(
    "8c 01 00 00 a9 45 06 00 "
    "10 00 00 00 "
    "01 00 00 00 00 00 00 00 28 00 00 00 7b 00 00 00 7b 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 0d 00 00 00 16 00 00 00 58 00 00 00 00 00 00 00 "
    + (_EMPTY_HEX + " ") * 14
)
# 412050 停止贩卖 len=20
CAP_412050 = _hx("14 00 00 00 92 49 06 00 01 00 00 00 00 00 00 00 00 00 00 00")
# 411041 关闭/离摊 len=12:body 单 u32=411041(自身 proto token)
CAP_411041 = _hx("0c 00 00 00 a1 45 06 00 a1 45 06 00")
# 511110 剩余更新 len=20:slotIdx=0 itemId=8036 remaining=100
CAP_511110 = _hx("14 00 00 00 86 cc 07 00 00 00 00 00 64 1f 00 00 64 00 00 00")
# 521109 卖出回流 len=40:op=1(cash) wireSlot=40 itemId=8036 packed=276 _u64=0 newGold=0x010594a0
CAP_521109 = _hx(
    "28 00 00 00 95 f3 07 00 01 00 00 00 28 00 00 00 "
    "64 1f 00 00 14 01 00 00 00 00 00 00 00 00 00 00 a0 94 05 01 00 00 00 00"
)
# 522105 关闭 ack len=12
CAP_522105 = _hx("0c 00 00 00 79 f7 07 00 78 01 00 00")


def test_send_open() -> None:
    body = streetstall_open_body("xiaodian", "cp950")
    frame = _frame(CG_OPEN_STREETSTALL, body)
    _check(frame == CAP_411040, f"411040 mismatch:\n got={frame.hex(' ')}\n exp={CAP_411040.hex(' ')}")


def test_send_items() -> None:
    # entries = [(bag_type, wire_slot, qty, price)];抓包 entry0=cash(1),entry1=主背包(0)
    entries = [(1, 40, 123, 123), (0, 13, 22, 88)]
    body = streetstall_items_body(entries)
    frame = _frame(CG_STREETSTALL_ITEMS, body)
    _check(frame == CAP_411049, f"411049 mismatch:\n got={frame.hex(' ')}\n exp={CAP_411049.hex(' ')}")
    # 空槽哨兵也要与抓包一致(14 条)
    _check(STALL_EMPTY_ENTRY == _hx(_EMPTY_HEX), "STALL_EMPTY_ENTRY 与抓包空槽不符")
    _check(len(frame) == 396, f"411049 长度应为 396,实际 {len(frame)}")


def test_send_terminate() -> None:
    frame = _frame(CG_STREETSTALL_TERMINATE, streetstall_terminate_body())
    _check(frame == CAP_412050, f"412050 mismatch:\n got={frame.hex(' ')}\n exp={CAP_412050.hex(' ')}")


def test_send_close() -> None:
    frame = _frame(CG_STREETSTALL_CLOSE, streetstall_close_body())
    _check(frame == CAP_411041, f"411041 mismatch:\n got={frame.hex(' ')}\n exp={CAP_411041.hex(' ')}")


def test_parse_item_update() -> None:
    res = parse_streetstall_item_update(CAP_511110)
    _check(res == (0, 8036, 100), f"511110 parse 期望 (0,8036,100),实际 {res}")


def test_parse_sold() -> None:
    op, src_slot, item_id, packed, gold = parse_streetstall_sold(CAP_521109)
    _check(op == 1, f"521109 op 期望 1,实际 {op}")
    _check(src_slot == 40 - WIRE_SLOT_BASE == 27, f"521109 srcSlot 期望 27,实际 {src_slot}")
    _check(item_id == 8036, f"521109 itemId 期望 8036,实际 {item_id}")
    _check(packed == 276, f"521109 packed 期望 276,实际 {packed}")
    _check(gold == 0x010594A0, f"521109 gold 期望 {0x010594A0},实际 {gold}")
    # 与直接读字节一致(防解析偏移漂移)
    _check(gold == u64(CAP_521109, 32) and op == u32(CAP_521109, 8), "521109 解析与原始字节不一致")


def test_recv_state_update() -> None:
    # 8036(摊贩呼叫卷)在真实表里可能不可堆叠;为让 packed+1 还原确定,临时标记可堆叠(同 selftest 手法)。
    saved = ITEM_STACKABLE.get(8036)
    ITEM_STACKABLE[8036] = True
    try:
        args = argparse.Namespace(fishing_bait_item_id=0, debug_packets=False, _key_index=0, encoding="cp950")
        state = FarmState(args)
        state.stall_phase = "open"
        state.stall_name = "xiaodian"
        state.stall_slots = {0: {"item_id": 8036, "remaining": 123, "qty0": 123,
                                 "price": 123, "bag_type": 1, "src_slot": 27}}
        # 511110:格0 剩余更新为 100
        update_farm_state(state, CAP_511110)
        _check(state.stall_slots[0]["remaining"] == 100, f"511110 未更新剩余:{state.stall_slots[0]}")
        _check(state.stall_slots[0]["item_id"] == 8036, "511110 itemId 应保持 8036")
        # 521109:op=1 → 刷 cash 背包源格 27 = (8036, 276+1),并把金币置为 0x010594a0
        update_farm_state(state, CAP_521109)
        _check(state.gold == 0x010594A0 and state.gold_seen, f"521109 金币未更新:{state.gold}")
        _check(state.cash_bag.get(27) == (8036, 277), f"521109 cash 源格未更新:{state.cash_bag.get(27)}")
    finally:
        if saved is None:
            ITEM_STACKABLE.pop(8036, None)
        else:
            ITEM_STACKABLE[8036] = saved


def test_recv_close_ack() -> None:
    _check(u32(CAP_522105, 4) == SC_STREETSTALL_CLOSE == 522105, "522105 proto 常量不符")
    _check(SC_STREETSTALL_ITEM_UPDATE == 511110 and SC_STREETSTALL_SOLD == 521109, "RECV proto 常量漂移")


def test_normalize_and_open() -> None:
    """normalize_listings 校验 + stall_open 全链路:用 FakeSock 解密回包,核对 worker 真正发出的 411040/411049。"""
    saved = ITEM_STACKABLE.get(8036)
    ITEM_STACKABLE[8036] = True
    try:
        args = argparse.Namespace(encoding="cp950", debug_packets=False, _key_index=0,
                                  stall_open_delay=0.0, stall_default_name="xiaodian")
        state = FarmState(args)
        # cash 格27=摊贩呼叫卷x123,主背包格0=某物x22(用 8036 占位计数,数量校验才是重点)
        state.cash_bag = {27: (8036, 200)}
        state.bag = {0: (8036, 50)}
        # 请求上架:cash格27 卖123、主背包格0 卖22(请求 200/50 时被 min 截断到实有,这里直接给 123/22)
        listings = [(1, 27, 8036, 123, 123), (0, 0, 8036, 22, 88)]
        rows = normalize_listings(state, listings)
        _check(len(rows) == 2, f"normalize 应得 2 条,实际 {len(rows)}")
        _check(rows[0]["wire_slot"] == 27 + WIRE_SLOT_BASE == 40, f"cash wireSlot 应 40,实际 {rows[0]['wire_slot']}")
        _check(rows[1]["wire_slot"] == 0 + WIRE_SLOT_BASE == 13, f"背包 wireSlot 应 13,实际 {rows[1]['wire_slot']}")
        # 数量超持有要被截断:请求 999 → 实有 50
        clip = normalize_listings(state, [(0, 0, 8036, 999, 88)])
        _check(clip[0]["qty"] == 50, f"超量未截断:{clip[0]['qty']}")
        # 空格要被跳过
        _check(normalize_listings(state, [(0, 191, 8036, 1, 1)]) == [], "空格未被跳过")

        rec: list[tuple[int, bytes]] = []

        class _FakeSock:
            def sendall(self, data: bytes) -> None:
                proto = u32(data, 4)
                body = game1841_decrypt_body(data[8:])[8:]  # 去 tick+crc
                rec.append((proto, body))

        ok = stall_open(state, _FakeSock(), "xiaodian", listings, reason="test")  # type: ignore[arg-type]
        _check(ok, "stall_open 应返回 True")
        _check(state.stall_phase == "open" and state.stall_name == "xiaodian", "开摊后状态未置 open")
        _check(set(state.stall_slots) == {0, 1}, f"stall_slots 应有 2 条:{state.stall_slots}")
        _check(state.stall_slots[0]["item_id"] == 8036 and state.stall_slots[0]["remaining"] == 123,
               f"stall_slots[0] 不对:{state.stall_slots[0]}")
        protos = [p for p, _ in rec]
        _check(protos == [CG_OPEN_STREETSTALL, CG_STREETSTALL_ITEMS],
               f"发包顺序应为 [开摊, 上架],实际 {protos}")
        # worker 实际发出的 body 必须与抓包一致。注意 send_game1841 走分组密码会在尾部补齐
        # (8 字节对齐填充),解密后 body 末尾带填充字节;真实 SEND 抓包是未填充明文,故比对有效前缀。
        open_body, items_body = CAP_411040[8:], CAP_411049[8:]
        _check(rec[0][1][:len(open_body)] == open_body,
               f"stall_open 的 411040 body 与抓包不符:\n got={rec[0][1].hex(' ')}\n exp={open_body.hex(' ')}")
        _check(rec[1][1][:len(items_body)] == items_body,
               f"stall_open 的 411049 body 与抓包不符:\n got={rec[1][1].hex(' ')}\n exp={items_body.hex(' ')}")
        # 状态行可读输出不报错且含摊位名
        lines = stall_status_lines(state)
        _check(any("xiaodian" in ln for ln in lines), f"状态行缺摊位名:{lines}")
    finally:
        if saved is None:
            ITEM_STACKABLE.pop(8036, None)
        else:
            ITEM_STACKABLE[8036] = saved


class _RecSock:
    """记录每个发包的 proto(读明文帧头 frame+4),不解密 body。用于核对工作流/收摊发了哪些包。"""
    def __init__(self) -> None:
        self.protos: list[int] = []

    def sendall(self, data: bytes) -> None:
        self.protos.append(u32(data, 4))


def _prov_args():
    return argparse.Namespace(encoding="cp950", debug_packets=False, _key_index=0,
                              stall_open_delay=0.0, stall_default_name="xiaodian")


def test_sold_out_autoclose() -> None:
    """所有上架格 remaining=0 → stall_check_sold_out 自动发 412050+411041 并回 idle。"""
    state = FarmState(_prov_args())
    state.stall_phase = "open"
    state.stall_name = "xiaodian"
    state.stall_slots = {
        0: {"item_id": 8036, "remaining": 0, "qty0": 50, "price": 99, "bag_type": 1, "src_slot": 0},
        1: {"item_id": 8036, "remaining": 0, "qty0": 22, "price": 88, "bag_type": 1, "src_slot": 1},
    }
    sock = _RecSock()
    stall_check_sold_out(state, sock)  # type: ignore[arg-type]
    _check(state.stall_phase == "idle", f"售罄后应收摊,实际 {state.stall_phase}")
    _check(CG_STREETSTALL_TERMINATE in sock.protos and CG_STREETSTALL_CLOSE in sock.protos,
           f"售罄收摊应发 412050+411041,实际 {sock.protos}")


def test_not_sold_out_keeps_open() -> None:
    """还有剩余就不收摊,且不发任何包。"""
    state = FarmState(_prov_args())
    state.stall_phase = "open"
    state.stall_slots = {
        0: {"item_id": 8036, "remaining": 0, "qty0": 50, "price": 99},
        1: {"item_id": 8036, "remaining": 3, "qty0": 22, "price": 88},
    }
    sock = _RecSock()
    stall_check_sold_out(state, sock)  # type: ignore[arg-type]
    _check(state.stall_phase == "open", "还有剩余不应收摊")
    _check(sock.protos == [], f"未售罄不应发包,实际 {sock.protos}")


def test_provision_listings_multislot() -> None:
    """已搬进 cash 的货跨多格凑上架清单:300+100=400(到 want 截断),非目标物品忽略。"""
    state = FarmState(_prov_args())
    state.cash_bag = {0: (8036, 300), 1: (8036, 150), 2: (9999, 10)}
    pending = [{"template_id": 8036, "want": 400, "moved": 400, "price": 50}]
    out = _provision_listings(state, pending)
    _check(out == [(STALL_BAG_CASH, 0, 8036, 300, 50), (STALL_BAG_CASH, 1, 8036, 100, 50)],
           f"跨格上架清单不对:{out}")


def test_provision_tick_lists_from_cash() -> None:
    """工作流 waiting:bank 到货 -> 搬进 cash(CG_BANK_MOVE)-> 自动开摊上架(411040/411049),回 idle。"""
    saved = ITEM_STACKABLE.get(8036)
    ITEM_STACKABLE[8036] = True
    try:
        state = FarmState(_prov_args())
        state.stall_prov_phase = "waiting"
        state.stall_prov_name = "xiaodian"
        state.stall_prov_pending = [{"template_id": 8036, "want": 50, "moved": 0, "price": 99}]
        now = time.monotonic()
        state.stall_prov_deadline = now + 100
        state.stall_prov_reopen_at = now + 100
        state.bank = {12345: (8036, 50)}   # 到货:bank 唯一实例 12345 = 8036 x50
        sock = _RecSock()
        stall_provision_tick(state, sock, now)  # type: ignore[arg-type]
        _check(state.stall_prov_phase == "idle", f"工作流应回 idle,实际 {state.stall_prov_phase}")
        _check(state.stall_phase == "open", f"应自动开摊,实际 {state.stall_phase}")
        _check(any(e["item_id"] == 8036 and e["remaining"] == 50 for e in state.stall_slots.values()),
               f"上架格不对:{state.stall_slots}")
        _check(CG_BANK_MOVE in sock.protos, f"应发过 bank 搬运包,实际 {sock.protos}")
        _check(CG_OPEN_STREETSTALL in sock.protos and CG_STREETSTALL_ITEMS in sock.protos,
               f"应发过开摊+上架,实际 {sock.protos}")
    finally:
        if saved is None:
            ITEM_STACKABLE.pop(8036, None)
        else:
            ITEM_STACKABLE[8036] = saved


def main() -> None:
    tests = [
        test_send_open,
        test_send_items,
        test_send_terminate,
        test_send_close,
        test_sold_out_autoclose,
        test_not_sold_out_keeps_open,
        test_provision_listings_multislot,
        test_provision_tick_lists_from_cash,
        test_parse_item_update,
        test_parse_sold,
        test_recv_state_update,
        test_recv_close_ack,
        test_normalize_and_open,
    ]
    for t in tests:
        t()
        print(f"  ok  {t.__name__}")
    print("摆摊封包自检全部通过 (13/13)")


if __name__ == "__main__":
    main()
