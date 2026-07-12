"""钓鱼:抛竿/收线/复活与钓鱼 tick。"""
from __future__ import annotations

import random
import socket
import struct
import time

from ..logio import biz_log
from ..protocol import (
    CG_ACTION_CANCEL,
    CG_FISHING_CAST,
    CG_FISHING_CAST_SPECIAL,
    CG_FISHING_REEL,
    CG_PLAYER_REVIVE,
    CG_STREETSTALL_WARP,
    REVIVE_MODE_DEFAULT,
    REVIVE_TOWN_NAMES,
    WIRE_SLOT_BASE,
    streetstall_warp_body,
)
from ..tables import item_label
from .state import FarmState, farm_send, resolve_bait_slot

from ..logio import log_print as print


def fishing_send_cast(state: FarmState, sock: socket.socket, bait_slot: int, actor_id: int) -> None:
    proto = CG_FISHING_CAST_SPECIAL if state.args.fishing_special_bait else CG_FISHING_CAST
    wire_slot = bait_slot + WIRE_SLOT_BASE
    body = struct.pack("<IIII", wire_slot, actor_id, state.coord_x, state.coord_y)
    farm_send(state, sock, proto, body, "CG_FISHING_CAST")


def fishing_send_reel(state: FarmState, sock: socket.socket) -> None:
    farm_send(state, sock, CG_FISHING_REEL, struct.pack("<I", 0), "CG_FISHING_REEL")


def fishing_send_stop(state: FarmState, sock: socket.socket) -> None:
    farm_send(state, sock, CG_ACTION_CANCEL, struct.pack("<I", 1), "CG_ACTION_CANCEL")


def send_revive_to_town(state: FarmState, sock: socket.socket,
                        revive_mode: int = REVIVE_MODE_DEFAULT) -> None:
    """回城复活:发 CG_PLAYER_REVIVE(412017),body=单 DWORD reviveMode
    (对齐 GGThreadBlock::ReviveToTown / 玩家手动点死亡弹框「复活」)。
    落点由 reviveMode 决定:乐园镇=1、狮子城=2(见 protocol.REVIVE_TOWNS)。"""
    farm_send(state, sock, CG_PLAYER_REVIVE, struct.pack("<I", int(revive_mode)), "CG_PLAYER_REVIVE")
    town = REVIVE_TOWN_NAMES.get(int(revive_mode), f"mode={revive_mode}")
    biz_log(f"回城复活(手动)-> {town}")


def send_warp_to_coord(state: FarmState, sock: socket.socket, x: int, y: int) -> None:
    """坐标传送:发 CG_STREETSTALL_WARP(411597),body=[u32 x][u32 y],瞬移到地图格 (x, y)
    (复刻街摊「传送到摊主」warp 包 / GGThreadBlock::WarpToCoordinate)。x/y 为整数地图格坐标。"""
    farm_send(state, sock, CG_STREETSTALL_WARP, streetstall_warp_body(x, y), "CG_STREETSTALL_WARP")
    biz_log(f"坐标传送 -> ({x}, {y})")


def fishing_schedule_next(state: FarmState, now: float) -> None:
    # 对齐 AutoFishingModule::ScheduleNext:间隔 + 0~3 秒抖动。
    interval = max(5.0, float(state.args.fishing_reel_interval))
    state.fish_next = now + interval + random.uniform(0.0, 3.0)


def fishing_do_cast(state: FarmState, sock: socket.socket, actor_id: int, now: float,
                    *, stop_delay: float = 0.05) -> bool:
    bait_slot = resolve_bait_slot(state)
    if bait_slot is None:
        if now >= state.warn_bait_at:
            print(f"[钓鱼] 找不到鱼饵(物品={item_label(state.args.fishing_bait_item_id)}),暂停抛竿")
            state.warn_bait_at = now + 5.0
        state.fish_next = now + 2.0
        return False
    # 先取消当前动作再抛竿(AutoFishingModule::SendCast 同序);stop_delay 是停竿到重抛的间隔。
    fishing_send_stop(state, sock)
    time.sleep(max(0.0, stop_delay))
    fishing_send_cast(state, sock, bait_slot, actor_id)
    state.cast_count += 1
    state.reels_since_cast = 0
    state.fishing_cast_slot = bait_slot   # 绑定本次抛竿用的饵格;钓空了就靠它判定并重抛切格
    print(f"[钓鱼] 第 {state.cast_count} 次抛竿:饵格={bait_slot} 坐标=({state.coord_x},{state.coord_y})")
    biz_log(f"抛竿 #{state.cast_count}")
    return True


def fishing_tick(state: FarmState, sock: socket.socket, player_id: int, now: float) -> None:
    """钓鱼模型:开局抛一次竿(411023 取消 + 411047/174 抛竿)开钓,之后每隔 reel_interval 秒
    发一个中鱼包(412048)。抛竿时绑定一个鱼饵格,收线由 521102 回包扣该格;一旦该格被钓空,
    自动重抛(resolve_bait_slot 选下一个有饵的格)切到新格继续——不需要固定次数重抛配置。"""
    args = state.args
    if not args.fishing_enabled:
        return
    actor_id = player_id
    if not actor_id:
        if now >= state.warn_coord_at:
            print("[钓鱼] 等待本地玩家ID,暂不抛竿")
            state.warn_coord_at = now + 5.0
        return
    if not state.coord_seen:
        if now >= state.warn_coord_at:
            print("[钓鱼] 等待抛竿坐标(511001 spawn),暂不抛竿")
            state.warn_coord_at = now + 5.0
        return

    # 开局:抛一次竿开钓。
    if not state.session_started:
        if fishing_do_cast(state, sock, actor_id, now):
            state.session_started = True
            fishing_schedule_next(state, now)
        return

    if now < state.fish_next:
        return

    # 当前抛竿绑定的鱼饵格被钓空(521102 回包已把该格扣到 0,或该格已不是鱼饵)-> 自动重抛,
    # resolve_bait_slot 会选下一个有饵的格;全空则 fishing_do_cast 内部告警并稍后重试。
    cast_iid, cast_cnt = state.bag.get(state.fishing_cast_slot, (0, 0))
    if cast_iid != args.fishing_bait_item_id or cast_cnt <= 0:
        if fishing_do_cast(state, sock, actor_id, now):
            fishing_schedule_next(state, now)
        return

    fishing_send_reel(state, sock)
    state.reels_since_cast += 1
    state.reel_total += 1
    print(f"[钓鱼] 发送中鱼/收线包(累计第 {state.reel_total} 次,本轮抛竿后第 {state.reels_since_cast} 次,"
          f"坐标=({state.coord_x},{state.coord_y}))")
    biz_log(f"收线 #{state.reel_total}")
    fishing_schedule_next(state, now)


def fishing_start(state: FarmState, sock: socket.socket, player_id: int, *, reason: str = "手动") -> bool:
    """手动开钓:停竿 -> 等 1s -> 重新抛竿 -> 打开 fishing_enabled 让 fishing_tick 之后按间隔定时发中鱼包。
    需要本地玩家ID与抛竿坐标(511001 spawn)就绪;鱼饵格由 resolve_bait_slot 选,无饵则不开。
    会把 fishing_enabled 置 True、session_started 置 True,接力交给 fishing_tick 发周期性收线包。"""
    if not player_id:
        print("[钓鱼] 还没拿到本地玩家ID,无法开始钓鱼")
        return False
    if not state.coord_seen:
        print("[钓鱼] 还没拿到抛竿坐标(511001 spawn),无法开始钓鱼")
        return False
    now = time.monotonic()
    if not fishing_do_cast(state, sock, player_id, now, stop_delay=1.0):
        return False
    state.session_started = True
    state.args.fishing_enabled = True
    fishing_schedule_next(state, now)
    print(f"[钓鱼]({reason})已开始,后续按间隔自动发中鱼/收线包")
    biz_log("手动开始钓鱼")
    return True


def fishing_stop(state: FarmState, sock: socket.socket, *, reason: str = "手动") -> None:
    """手动停钓:关掉 fishing_enabled(session 主循环据此不再调 fishing_tick,停发中鱼/收线包),
    发一次停竿(411023)取消当前抛竿动作,并复位 session_started 以便下次开钓重新抛竿。"""
    state.args.fishing_enabled = False
    state.session_started = False
    fishing_send_stop(state, sock)
    print(f"[钓鱼]({reason})已停止")
    biz_log("手动停止钓鱼")
