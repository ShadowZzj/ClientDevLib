"""自检:加解密回归与 farm 解析回归。"""
from __future__ import annotations

import argparse
import struct
import zlib

from .protocol import (
    BAG_EQUIP_SLOTS,
    CG_ACTION_CANCEL,
    CG_FISHING_CAST,
    CG_FISHING_REEL,
    CHAT_LOGIN_OPCODE,
    INV_ENTRY_STRIDE,
    SC_BAG_BULK,
    SC_CASH_BAG_BULK,
    SC_FISHING_CATCH,
    SC_LOCAL_MAP_STATE,
    SELL_TOKEN,
    SUMMON_ITEM_ID,
    WIRE_SLOT_BASE,
    fixed_bytes,
    u32,
)
from .crypto import (
    CAPTURED_GAME1842_SELF_TEST,
    CAPTURED_GAME1842_SELF_TEST_PROTO,
    DATE_MAGIC,
    DATE_MAGIC_ENCRYPTED,
    game_des_encrypt,
)
from .tables import ITEM_STACKABLE
from .packets import (
    chat_login_packet,
    chat_status_packet,
    game1841_decrypt_body,
    game1841_packet,
)
from .farm.state import (
    FarmState,
    bag_count_item,
    bag_free_slots,
    find_summon_slot,
    resolve_bait_slot,
    update_farm_state,
)
from .farm.fishing import fishing_send_cast, fishing_send_reel, fishing_tick

from .logio import log_print as print


def self_test() -> None:
    got = game_des_encrypt(DATE_MAGIC)
    if got != DATE_MAGIC_ENCRYPTED:
        raise SystemExit(f"DES self-test failed: got {got.hex(' ')}, expected {DATE_MAGIC_ENCRYPTED.hex(' ')}")
    captured_body = struct.pack("<I", CAPTURED_GAME1842_SELF_TEST_PROTO)
    decrypted = game1841_decrypt_body(CAPTURED_GAME1842_SELF_TEST[8:])
    tick = u32(decrypted, 0)
    expected_crc = zlib.crc32(captured_body) & 0xFFFFFFFF
    if u32(decrypted, 4) != expected_crc or decrypted[8:12] != captured_body:
        raise SystemExit(f"1842 decrypt self-test failed: {decrypted.hex(' ')}")
    rebuilt = game1841_packet(CAPTURED_GAME1842_SELF_TEST_PROTO, captured_body, tick_ms=tick)
    if rebuilt != CAPTURED_GAME1842_SELF_TEST:
        raise SystemExit(f"1842 encrypt self-test failed: got {rebuilt.hex(' ')}, expected {CAPTURED_GAME1842_SELF_TEST.hex(' ')}")
    chinese_name = "\u9435\u5c0f\u5c0f"  # CP950 sample character name.
    chinese_raw = chinese_name.encode("cp950")
    if chinese_raw != bytes.fromhex("C5 4B A4 70 A4 70"):
        raise SystemExit(f"CP950 self-test failed: {chinese_raw.hex(' ')}")
    if fixed_bytes(chinese_name, 5, "cp950") != bytes.fromhex("C5 4B A4 70 00"):
        raise SystemExit("CP950 truncation self-test failed")
    chat = chat_login_packet("gongyu9011212", "901121", "shadowsing", "cp950", 0x7E)
    if len(chat) != 69 or struct.unpack_from("<H", chat, 0)[0] != 69 or struct.unpack_from("<I", chat, 2)[0] != CHAT_LOGIN_OPCODE:
        raise SystemExit(f"3000 chat login self-test failed: {chat.hex(' ')}")
    status = chat_status_packet("shadowdance", "cp950", 0x6D, 0xB7)
    expected_status = bytes.fromhex(
        "27 00 10 41 00 00 08 00 00 00 6D 00 00 00 "
        "73 68 61 64 6F 77 64 61 6E 63 65 00 00 00 00 00 "
        "00 00 00 00 00 B7 00 00 00"
    )
    if status != expected_status:
        raise SystemExit(f"3000 chat status self-test failed: got {status.hex(' ')}, expected {expected_status.hex(' ')}")
    _farm_self_test()
    print("self-test ok")


def _frame(proto: int, body: bytes) -> bytes:
    return struct.pack("<II", len(body) + 8, proto) + body


def _farm_self_test() -> None:
    # 自测用的合成 itemId(4001/5001/6001/7777/8036)在真实 item_names.json 里多为不可堆叠,
    # 而解析器对可堆叠物按 packed+1 还原真实数量。为了让本测试只验证解析逻辑、不受物品表漂移影响,
    # 这里把这些合成 id 临时标记成可堆叠,wire 上按 (真实数量-1) 打包,断言仍写人类可读的真实数量。
    _stack_saved = {iid: ITEM_STACKABLE.get(iid) for iid in (4001, 5001, 6001, 7777, 8036)}
    for _iid in _stack_saved:
        ITEM_STACKABLE[_iid] = True
    try:
        _farm_self_test_body()
    finally:
        for _iid, _v in _stack_saved.items():
            if _v is None:
                ITEM_STACKABLE.pop(_iid, None)
            else:
                ITEM_STACKABLE[_iid] = _v


def _farm_self_test_body() -> None:
    args = argparse.Namespace(
        fishing_enabled=True, fishing_bait_item_id=4001,
        fishing_special_bait=False, fishing_reel_interval=35.0,
        autosell_enabled=True, autosell_min_free_slots=2, autosell_start_slot=0,
        autosell_end_slot=5, autosell_keep_item_ids=[4001], autosell_per_packet_delay=0.0,
        autobuy_enabled=True, autobuy_min_bait=50, autobuy_item_id=4001,
        autobuy_count=300, autobuy_open_delay=0.0, autobuy_buy_delay=0.0,
        debug_packets=False, _key_index=0,
    )
    state = FarmState(args)
    # 511001 spawn -> 坐标
    state_chunk = _frame(SC_LOCAL_MAP_STATE, struct.pack("<IIIIII", 0, 0xABCD, 0, 7, 1000, 2000))
    update_farm_state(state, state_chunk)
    if not (state.coord_seen and state.coord_x == 1000 and state.coord_y == 2000):
        raise SystemExit(f"farm coord self-test failed: {state.coord_x},{state.coord_y}")
    # 主背包整桶 511591(布局同真实 handler sub_8BD150):itemCount头 + 13装备格 + 主背包格。
    # 主背包 slot0=鱼饵(4001 x100),slot1=杂物(5001 x1);装备区填空,验证解析器正确跳过它。
    # wire 上数量按 packed=真实数量-1(可堆叠口径),解析后还原回真实数量。
    bag_body = struct.pack("<I", 2) \
        + b"\x00" * (BAG_EQUIP_SLOTS * INV_ENTRY_STRIDE) \
        + struct.pack("<IIQ", 4001, 99, 0) \
        + struct.pack("<IIQ", 5001, 0, 0)
    update_farm_state(state, _frame(SC_BAG_BULK, bag_body))
    if bag_count_item(state, 4001) != 100:
        raise SystemExit("farm bait-count self-test failed")
    if resolve_bait_slot(state) != 0:
        raise SystemExit("farm bait-slot self-test failed")
    # 收鱼包 521102(两段式,见 update_farm_state 注释):entry1=被消耗的鱼饵(body+8 bag/+12 item/+16 count),
    # entry2=钓到的鱼(body+28 bag/+32 item)。鱼获 count 字段是 packed 垃圾值,解析器对新落格的鱼恒按
    # 1 条计(真实数量随后由背包同步包补正)。这里鱼饵留在 slot0、鱼落到 slot2。
    catch_body = (
        struct.pack("<II", 0, 0)                       # entry1 头部(itemtype/属性占位)
        + struct.pack("<II", 0 + WIRE_SLOT_BASE, 4001)  # body+8 bait_bag, body+12 bait_item
        + struct.pack("<I", 99)                         # body+16 bait_count(packed,可堆叠口径=真实-1)
        + struct.pack("<II", 0, 0)                       # body+20 entry1 uid 占位
        + struct.pack("<II", 2 + WIRE_SLOT_BASE, 6001)   # body+28 fish_bag, body+32 fish_item
        + struct.pack("<II", 0x30000, 0)                 # body+36 fish packed count + uid 占位
    )
    update_farm_state(state, _frame(SC_FISHING_CATCH, catch_body))
    if state.catch_total != 1 or state.bag.get(2) != (6001, 1):
        raise SystemExit(f"farm catch self-test failed: {state.bag.get(2)}")
    # 0..5 共 6 格,占用 3 格(slot0/1/2),空 3 格 > 阈值2 -> 不卖
    if bag_free_slots(state, 0, 5) != 3:
        raise SystemExit(f"farm free-slot self-test failed: {bag_free_slots(state, 0, 5)}")
    # cash 整桶 511324:无内容头,16字节/格;slot0=杂物(7777 x1),slot1=摊贩呼叫卷(8036 x2)。
    # wire 上 packed=真实数量-1(可堆叠口径)。
    cash_body = struct.pack("<IIQ", 7777, 0, 0) \
        + struct.pack("<IIQ", SUMMON_ITEM_ID, 1, 0)
    update_farm_state(state, _frame(SC_CASH_BAG_BULK, cash_body))
    if not state.cash_observed or state.cash_bag.get(1) != (SUMMON_ITEM_ID, 2):
        raise SystemExit(f"farm cash-bag self-test failed: {state.cash_bag}")
    if find_summon_slot(state) != 1:
        raise SystemExit(f"farm summon-slot self-test failed: {find_summon_slot(state)}")
    # send helper 线格编码验证
    rec: list[tuple[int, bytes]] = []
    class _FakeSock:
        def sendall(self, data: bytes) -> None:
            proto = u32(data, 4)
            body = game1841_decrypt_body(data[8:])[8:]  # 去 tick+crc
            rec.append((proto, body))
    fake = _FakeSock()
    fishing_send_cast(state, fake, 0, 0x1234)  # type: ignore[arg-type]
    proto, body = rec[-1]
    if proto != CG_FISHING_CAST or struct.unpack("<IIII", body[:16]) != (13, 0x1234, 1000, 2000):
        raise SystemExit(f"farm cast-body self-test failed: {proto} {body.hex(' ')}")
    fishing_send_reel(state, fake)  # type: ignore[arg-type]
    if rec[-1][0] != CG_FISHING_REEL or u32(rec[-1][1], 0) != 0:
        raise SystemExit("farm reel-body self-test failed")
    # 钓鱼模型:首 tick 抛一次竿(411023+411047)并绑定饵格,后续按间隔发中鱼包(412048);
    # 抛竿绑定的饵格被钓空后,下一 tick 自动重抛(切到下一个有饵的格)。
    rec.clear()
    fstate = FarmState(args)
    fstate.coord_x, fstate.coord_y, fstate.coord_seen = 1000, 2000, True
    fstate.bag[0] = (4001, 100)
    fishing_tick(fstate, fake, 0x1234, 100.0)  # type: ignore[arg-type]
    protos = [p for p, _ in rec]
    if protos != [CG_ACTION_CANCEL, CG_FISHING_CAST] or not fstate.session_started or fstate.fishing_cast_slot != 0:
        raise SystemExit(f"farm fishing open-cast self-test failed: {protos} slot={fstate.fishing_cast_slot}")
    rec.clear()
    fishing_tick(fstate, fake, 0x1234, fstate.fish_next)  # 饵格0仍有货 -> 中鱼  # type: ignore[arg-type]
    if [p for p, _ in rec] != [CG_FISHING_REEL]:
        raise SystemExit(f"farm fishing reel self-test failed: {[p for p,_ in rec]}")
    rec.clear()
    # 模拟饵格0钓空(521102 会把该格扣到 0),并在格5补一摞饵 -> 下一 tick 应自动重抛切到格5
    fstate.bag[0] = (4001, 0)
    fstate.bag[5] = (4001, 80)
    fishing_tick(fstate, fake, 0x1234, fstate.fish_next)  # type: ignore[arg-type]
    if [p for p, _ in rec] != [CG_ACTION_CANCEL, CG_FISHING_CAST] or fstate.fishing_cast_slot != 5:
        raise SystemExit(f"farm fishing auto-recast(bait depleted) self-test failed: {[p for p,_ in rec]} slot={fstate.fishing_cast_slot}")
    rec.clear()
    body = struct.pack("<III", 3 + WIRE_SLOT_BASE, 7, SELL_TOKEN)
    if struct.unpack("<III", body) != (16, 7, SELL_TOKEN):
        raise SystemExit("farm sell-body self-test failed")
