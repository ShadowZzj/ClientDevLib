"""点券道具使用与 buff 监控调度。"""
from __future__ import annotations

import json
import re
import socket
import struct
import threading
import time

from ..runtime import APP_DIR
from ..logio import biz_log
from ..protocol import CG_USE_CASH_ITEM, WIRE_SLOT_BASE
from ..tables import item_label, resolve_item_id
from .state import FarmState, cash_count_item, cash_find_slot, farm_send

from ..logio import log_print as print


# ---------- 定时使用 cash 背包物品 ----------
CASH_CHECK_INTERVAL = 5.0       # worker 主循环 0.25s tick,定时表检查再节流到 5s 一次
CASH_USE_RETRY_INTERVAL = 60.0  # 发了使用包但 cash 数量没确认 -1(丢包/没读到)时,隔多久重发
CASH_USE_STATE_PATH = APP_DIR / "cash_use_state.json"  # 每角色 lastUsed(运行态,不随 exe 打包)
_cash_state_lock = threading.Lock()
_cash_state_cache: "dict | None" = None


def _cash_state_load_locked() -> dict:
    global _cash_state_cache
    if _cash_state_cache is None:
        try:
            data = json.loads(CASH_USE_STATE_PATH.read_text(encoding="utf-8-sig"))
            _cash_state_cache = data if isinstance(data, dict) else {}
        except Exception:
            _cash_state_cache = {}
    return _cash_state_cache


def cash_last_used(character: str, item: str) -> float:
    """某角色某物品的上次使用时间(epoch 秒);无记录返回 0。多账号线程共享,加锁。"""
    with _cash_state_lock:
        d = _cash_state_load_locked()
        try:
            return float(d.get(character, {}).get(item, 0.0))
        except (TypeError, ValueError):
            return 0.0


def cash_mark_used(character: str, item: str, ts: float) -> None:
    """记录使用时间并落盘(read-modify-write,跨账号线程共享同一文件)。"""
    with _cash_state_lock:
        d = _cash_state_load_locked()
        d.setdefault(character, {})[item] = ts
        try:
            CASH_USE_STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
            CASH_USE_STATE_PATH.write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")
        except Exception as exc:
            print(f"[Cash定时] 保存使用时间失败:{exc}")


def parse_cash_schedule(value: object) -> list[tuple[str, int]]:
    """把定时表配置解析成 [(物品名, 间隔秒)]。接受:
      - list[dict] {"item","interval"}
      - list[str] / 多行字符串,每行 '物品名=秒'(=后为秒数)
    宽松解析(同一 tab 的补货/Buff 框都用冒号,用户常误填):全角 ＝/：当半角;
    没有 = 时也认 : 作分隔;秒数只取分隔后第一个数字(容忍 '名=秒:数量' 这种)。
    丢弃名字空或间隔<=0 的项。"""
    out: list[tuple[str, int]] = []
    if not value:
        return out
    items = value if isinstance(value, (list, tuple)) else str(value).splitlines()
    for it in items:
        if isinstance(it, dict):
            name = str(it.get("item", "")).strip()
            raw = it.get("interval", 0)
        else:
            s = str(it).strip().replace("＝", "=").replace("：", ":")
            if not s:
                continue
            sep = "=" if "=" in s else (":" if ":" in s else "")
            if not sep:
                continue
            name, _, raw = s.partition(sep)
            name = name.strip()
            raw = raw.strip()
        m = re.search(r"-?\d+(?:\.\d+)?", str(raw))
        if not m:
            continue
        interval = int(float(m.group()))
        if name and interval > 0:
            out.append((name, interval))
    return out


BUFF_MONITOR_DEFAULT_INTERVAL = 300  # 默认探测间隔(秒)= 5 分钟


def parse_buff_monitor_schedule(value: object) -> list[tuple[str, str, int]]:
    """把 buff 监控配置解析成 [(状态名, 物品名/ID, 间隔秒)]。接受:
      - list[dict] {"buff","item","interval"}
      - list[str] / 多行字符串,每行 '状态名=物品名:间隔秒'(:间隔秒 可省,默认 300)
    丢弃状态名或物品名为空的项;间隔<=0 或缺省一律回落 300。"""
    out: list[tuple[str, str, int]] = []
    if not value:
        return out
    items = value if isinstance(value, (list, tuple)) else str(value).splitlines()
    for it in items:
        if isinstance(it, dict):
            buff = str(it.get("buff", "")).strip()
            item = str(it.get("item", "")).strip()
            raw = it.get("interval", BUFF_MONITOR_DEFAULT_INTERVAL)
        else:
            s = str(it).strip()
            if not s or "=" not in s:
                continue
            buff, _, rest = s.partition("=")
            buff = buff.strip()
            item, sep, raw = rest.partition(":")
            item = item.strip()
            raw = raw.strip() if sep else ""
        try:
            interval = int(float(raw)) if str(raw).strip() != "" else BUFF_MONITOR_DEFAULT_INTERVAL
        except (TypeError, ValueError):
            interval = BUFF_MONITOR_DEFAULT_INTERVAL
        if interval <= 0:
            interval = BUFF_MONITOR_DEFAULT_INTERVAL
        if buff and item:
            out.append((buff, item, interval))
    return out


def use_cash_item(state: FarmState, sock: socket.socket, name_or_id: object, *, reason: str) -> bool:
    """按物品名/ID 在 cash 背包里找格并发使用包(411156, body=[slot+13,0])。
    找不到该物品 / cash 未同步 -> False(调用方据此不更新 lastUsed,下轮重试)。"""
    item_id = resolve_item_id(name_or_id, 0)
    if not item_id:
        return False
    slot = cash_find_slot(state.cash_bag, item_id)
    if slot is None:
        return False
    farm_send(state, sock, CG_USE_CASH_ITEM,
              struct.pack("<II", slot + WIRE_SLOT_BASE, 0), "CG_USE_CASH_ITEM")
    print(f"[Cash定时]({reason})使用 {item_label(item_id)} cash格={slot}")
    biz_log(f"定时使用 {item_label(item_id)}")
    return True


def cash_use_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """定时使用 cash 背包物品:每 CASH_CHECK_INTERVAL 秒检查一次定时表,到点(按角色
    持久化的 lastUsed 算)且角色活着就用;死亡时延后(不更新时间,活过来即补)。

    关键:发使用包后【不立刻】记 lastUsed,而是挂 pending(记下发包前数量 pre)。要等
    511325 回包把 cash 背包该物品数量真的 -1 才落地 lastUsed,这次才算"用成功"。超过
    CASH_USE_RETRY_INTERVAL 还没见 -1(丢包 / 当时掉线没读到)就重发,直到确认 -1。
    掉线场景天然兜底:重连后 FarmState(含 cash_pending)重建、lastUsed 仍是旧值,下轮自动重用。
    now 是 monotonic(节流 / 重试计时用);间隔判定用 time.time() 墙钟,与持久化 epoch 对齐。"""
    args = state.args
    schedule = parse_cash_schedule(getattr(args, "cash_use_schedule", None))
    if not schedule:
        return
    if now < state.next_cash_check_at:
        return
    state.next_cash_check_at = now + CASH_CHECK_INTERVAL
    if not state.cash_observed:
        return
    if state.hp_seen and state.hp == 0:   # 死着不用,下个周期再问;活过来立刻补
        return
    character = getattr(args, "character", "") or ""
    wall = time.time()
    for name, interval in schedule:
        item_id = resolve_item_id(name, 0)
        if not item_id:
            continue
        pending = state.cash_pending.get(item_id)
        if pending is not None:
            cur = cash_count_item(state.cash_bag, item_id)
            if cur <= pending["pre"] - 1:
                # 回包确认数量已 -1,这次使用才算成功 -> 落地 lastUsed,清 pending
                cash_mark_used(character, name, wall)
                state.cash_pending.pop(item_id, None)
                print(f"[Cash定时] 确认 {item_label(item_id)} 已消耗(剩 {cur})")
                biz_log(f"确认使用 {item_label(item_id)}(剩 {cur})")
            elif now >= pending["retry_at"]:
                # 超时仍没见 -1(丢包 / 没读到):重发使用包,继续等确认
                pending["retry_at"] = now + CASH_USE_RETRY_INTERVAL
                if use_cash_item(state, sock, name, reason="重试·未确认-1"):
                    print(f"[Cash定时] {item_label(item_id)} 未确认 -1,已重发使用包")
                else:
                    # cash 里已找不到该物品(数量为 0,可能确已耗尽)-> 放弃 pending,交回间隔逻辑
                    state.cash_pending.pop(item_id, None)
            continue
        if wall - cash_last_used(character, name) < interval:
            continue
        pre = cash_count_item(state.cash_bag, item_id)
        if use_cash_item(state, sock, name, reason=f"间隔{interval}s"):
            state.cash_pending[item_id] = {"name": name, "pre": pre,
                                           "retry_at": now + CASH_USE_RETRY_INTERVAL}


def buff_present(state: FarmState, name: str) -> bool:
    """角色身上是否有名为 name 的状态。在 cash_buffs(disc==0 现金状态)和 skill_buffs
    (disc!=0 技能型,如进阶摆摊)两张快照里按名匹配:精确相等或 name 是状态名的子串
    (cash 名带模板文本如「掉寶率提高 50%」时,填短而独特的关键词即可命中)。"""
    key = (name or "").strip()
    if not key:
        return False
    for snap in (state.cash_buffs, state.skill_buffs):
        for info in snap.values():
            nm = str(info.get("name", ""))
            if key == nm or key in nm:
                return True
    return False


def buff_monitor_tick(state: FarmState, sock: socket.socket, now: float) -> None:
    """Buff 监控:每条规则按自己的间隔探测一次,角色身上没有指定状态就用配置的 cash 物品。
    now 是 monotonic 秒(节流/计时用)。计时随 FarmState 每次重连重建 ——
    首次探测排在「上线 + interval」(默认 5 分钟后),所以重连=从 0 重新计时,不会一上线就用。"""
    args = state.args
    schedule = parse_buff_monitor_schedule(getattr(args, "buff_monitor_schedule", None))
    if not schedule:
        return
    alive = not (state.hp_seen and state.hp == 0)
    for buff_name, item, interval in schedule:
        nxt = state.buff_monitor_next.get(buff_name)
        if nxt is None:
            # 上线后这条规则第一次探测要等满 interval(默认5分钟)才发生
            state.buff_monitor_next[buff_name] = now + interval
            continue
        if now < nxt:
            continue
        state.buff_monitor_next[buff_name] = now + interval  # 重排下一次探测
        if not state.cash_observed or not alive:
            continue  # 还没进游戏/死着:本轮不动作,等下个周期
        if buff_present(state, buff_name):
            continue  # 状态还在,不用补
        use_cash_item(state, sock, item, reason=f"buff监控·缺[{buff_name}]")
