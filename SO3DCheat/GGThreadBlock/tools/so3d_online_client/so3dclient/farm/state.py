"""FarmState 共享状态、背包/银行解析、update_farm_state、farm_send。"""
from __future__ import annotations

import argparse
import socket
import time

from ..runtime import bump_controller_stat
from ..logio import biz_log
from ..protocol import (
    BAG_ENTRY_BASE,
    BAG_MAIN_SLOTS,
    BANK_BULK_HEADER,
    BANK_ENTRY_STRIDE,
    CASH_SLOT_CAP,
    CRT_ENTRY_STRIDE,
    GC_CRT_DAMAGE,
    GC_CRT_DIE,
    GC_CRT_LOAD,
    GC_CRT_MOVE,
    GC_CRT_NEW,
    GC_CRT_REMOVE,
    INV_ENTRY_STRIDE,
    MONEY_WIRE_OFFSETS,
    SC_ATTACK_CRT2TARGET_CRIT,
    SC_ATTACK_CRT2TARGET_SUCC,
    SC_BAG_BULK,
    SC_BANK_BULK,
    SC_BANK_UPDATE,
    SC_BUFF_ADD,
    SC_BUFF_REMOVE,
    SC_BUY_ITEM_RESULT,
    SC_CASH_BAG_BULK,
    SC_CASH_SLOT_UPDATE,
    SC_CHARLOAD_MONEY_WIRE_OFFSET,
    SC_FISHING_CATCH,
    SC_LOCAL_MAP_STATE,
    SC_SELL_ITEM_RESULT,
    SC_STREETSTALL_ITEM_UPDATE,
    SC_STREETSTALL_SOLD,
    SUMMON_ITEM_ID,
    WIRE_SLOT_BASE,
    cash_buff_name,
    i64,
    proto_name,
    skill_cash_buff_name,
    u32,
    u64,
)
from ..tables import item_label, item_stackable
from ..packets import iter_framed_packets, send_game1841

from ..logio import log_print as print


# ======================== 离线挂机:钓鱼 / 贩卖 / 购买 ========================
# 纯 socket 客户端拿不到游戏内存,背包靠 recv 整桶包重建:
#   入场时 511591(主背包)/ 511324(cash 背包)各下发一次整桶快照,
#   之后 521102 收鱼包对单格做增量。维护 slot->(itemId,count)。
# 整桶只在入场发一次,故 GUI 看到的是入场快照;买卖/钓鱼后的变化要等收鱼增量或重连刷新。
# 坐标来自 511001 spawn 包(为 0 会被服务端踢,故必须有值)。


class FarmState:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        # 服务器实际载入的角色名(LC_GSERV_CONNECT_SUCCESS 回显);单角色账号/配错名时会与所填不同
        self.real_character = ""
        # 本地玩家 userId(从 1842 包 body+12 解析,= 游戏内 CUser+0x70);0=尚未解析到
        self.player_id = 0
        # 抛竿坐标(511001 spawn)
        self.coord_x = 0
        self.coord_y = 0
        self.coord_seen = False
        # 背包快照:slot(0 基) -> (itemId, count)
        self.bag: dict[int, tuple[int, int]] = {}
        self.bag_observed = False     # 是否收到过任意背包格更新
        self.bait_observed = False    # 是否观测到过鱼饵格(autobuy 冷启动保护)
        # cash 背包快照(511324 整桶):slot(0 基) -> (itemId, count)
        self.cash_bag: dict[int, tuple[int, int]] = {}
        self.cash_observed = False
        # cash 状态 buff 快照(511337 加 / 511348 移除):type -> {"type","name","value","expiry"}。
        # expiry = 绝对 unix 秒(到期时间,0=未知);value = 数值(藥水加成 / 掉寶率%)。
        self.cash_buffs: dict[int, dict] = {}
        self.cash_buffs_observed = False
        # 进阶/技能型 cash buff(511337 disc!=0,走 BuffHelper,如进阶摆摊):buffId -> 同结构。
        # 与 cash_buffs 分开存(buffId 与 cash type 命名空间会撞,如 0=力量藥水 vs 0=自動撿取)。
        # expiry 由 duration(相对秒)在收到时换算成绝对 unix 秒。
        self.skill_buffs: dict[int, dict] = {}
        # 账号共享仓库(bank)快照(511320 整桶):唯一实例id -> (模板itemId, count)
        self.bank: dict[int, tuple[int, int]] = {}
        self.bank_observed = False
        # 自动补货状态机:idle -> waiting(已发购买+开 bank,等物品到 bank 再搬进 cash)
        self.restock_phase = "idle"
        self.restock_item = 0          # 目标背包模板itemId(买到后落 bank/cash 的 itemId,可能≠网店itemID)
        self.restock_target = 0        # 本次要搬进 cash 的件数
        self.restock_deadline = 0.0    # waiting 阶段超时(monotonic)
        self.restock_reopen_at = 0.0   # waiting 期间重发开 bank 的下次时间
        self.next_restock_at = 0.0     # 两轮补货最短间隔(monotonic)
        # 钓鱼状态机(开局抛一次竿,之后按间隔发中鱼包;抛竿绑定的饵格被钓空就自动重抛切下一格)
        self.session_started = False  # 本轮是否已抛过开局竿
        self.fishing_cast_slot = -1   # 本次抛竿绑定的鱼饵格(521102 会更新该格剩余;空了即重抛切格)
        self.reels_since_cast = 0     # 距上次抛竿已发的中鱼包数(仅统计/日志)
        self.reel_total = 0           # 累计发送的中鱼/收线包数(对照服务端返回收获次数)
        self.fish_next = 0.0          # 下一次中鱼/重抛动作的最早时间(monotonic)
        self.cast_count = 0
        self.catch_total = 0
        # 金币(U64,localUser+0x3498):从 511001/521191/511171/511175/511178 回流
        self.gold = 0
        self.gold_seen = False
        # 当前 HP / 最大 HP:511001(GC_CHAR_DATA_LOAD)wire+88=当前HP,登录瞬间被 CalcStatus
        # 置成与上限相等,故登录时当前=最大。wire+92 是本地玩家 actorId(IDA Packet_OpcodeToName
        # case 511001 已注释)——不是 MP,旧代码当成 MP 是错的。怪物攻击时血量靠
        # 521023/511131 的 wire+32 增量更新(见 update_farm_state),用 actor_id 认出"打的是自己"。
        self.hp = 0
        self.max_hp = 0
        self.actor_id = 0
        self.hp_seen = False
        # 所在地图 SLOT(511001 wire+20 = g_localMapId,需经 g_MapIdTable 转规范 id 再查名)
        self.map_slot = -1
        self.map_seen = False
        # 周围怪物实时快照:crtId -> {type, grade, x, y, hp, max_hp, last_seen}。
        # LOAD/NEW 加入并带满血(条目 +20=HP,实证:稻草人2亿/活动怪394万/小怪几十);
        # MOVE 更新坐标;DAMAGE 写当前 hp 并累计 max_hp;REMOVE / DIE / DAMAGE 后 hp<=0 删除。
        # worker 线程单写,GUI 线程 monster_snapshot 只读。+20=0 的非怪物(NPC/物件)hp 留 None。
        self.nearby_monsters: dict[int, dict] = {}
        self.monsters_observed = False
        # 贩卖 / 购买节流
        self.next_autosell_at = 0.0
        self.next_autobuy_at = 0.0
        # 购买后延迟汇报(等 521052 结果包被主循环处理再记背包鱼饵总数+金钱);0=无待汇报
        self.autobuy_log_at = 0.0
        self.autobuy_log_item = 0
        self.next_cash_check_at = 0.0
        # 定时 cash 物品的"已发包待确认 -1"表:itemId -> {"name","pre"(发包前数量),"retry_at"}。
        # 只有 511325 回包把数量真的 -1 才落地 lastUsed;否则到点重发。随 FarmState 每次重连重建。
        self.cash_pending: dict[int, dict] = {}
        # Buff 监控的每条规则下次探测时刻(monotonic 秒),按 buff 名键。随 FarmState 每次重连重建,
        # 首次探测在「上线 + interval」(默认5分钟后)才到点 —— 故重连相当于从 0 重新计时。
        self.buff_monitor_next: dict[str, float] = {}
        self.next_automail_at = 0.0
        self.warn_coord_at = 0.0
        self.warn_bait_at = 0.0
        self.warn_summon_at = 0.0
        # ---- 个人摆摊会话状态 ----
        # phase: "idle"(无摊) / "open"(已发开摊+上架包)。随 FarmState 每次重连重建,故重连即视为收摊。
        self.stall_phase = "idle"
        self.stall_name = ""
        # stallSlotIdx(0 基,= 411049 上架条目顺序) -> {item_id, remaining, qty0, price, bag_type, src_slot}。
        # 511110 按 stallSlotIdx 更新 remaining(绝对剩余);worker 线程单写、GUI 线程 dict() 拷贝只读。
        self.stall_slots: dict[int, dict] = {}
        # ---- 「补货并上架」一键工作流状态机 ----
        # idle -> waiting(已泡点买+开 bank,等货到 bank 再搬进 cash)-> 搬完/超时即从 cash 上架 -> idle。
        # 复用补货的 bank 搬运机制(bank_open/bank_move_to_cash),只是终点改成开摊上架而非补 cash。
        self.stall_prov_phase = "idle"
        self.stall_prov_name = ""        # 上架时用的摊位名
        self.stall_prov_deadline = 0.0   # waiting 阶段总超时(monotonic)
        self.stall_prov_reopen_at = 0.0  # waiting 期间重发开 bank 的下次时间
        # 待上架清单:[{template_id, want(目标件数), moved(已搬进 cash 件数), price(单价)}]
        self.stall_prov_pending: list[dict] = []


def bag_count_item(state: FarmState, item_id: int) -> int:
    if not item_id:
        return 0
    return sum(cnt for (iid, cnt) in state.bag.values() if iid == item_id)


def bag_free_slots(state: FarmState, start: int, end: int) -> int:
    lo = max(0, start)
    hi = min(BAG_MAIN_SLOTS - 1, end)
    if hi < lo:
        return 0
    span = hi - lo + 1
    occupied = sum(1 for s in range(lo, hi + 1) if state.bag.get(s, (0, 0))[0] != 0)
    return span - occupied


def resolve_bait_slot(state: FarmState) -> int | None:
    """按 fishing_bait_item_id 在背包里找第一个有货的鱼饵格(0 基)。"""
    bait_id = state.args.fishing_bait_item_id
    if not bait_id:
        return None
    for slot, (iid, cnt) in sorted(state.bag.items()):
        if iid == bait_id and cnt > 0:
            return slot
    return None


def find_summon_slot(state: FarmState) -> int | None:
    """在 cash 背包里找「摊贩呼叫卷」(itemId 写死 SUMMON_ITEM_ID)的格(0 基)。"""
    for slot, (iid, cnt) in sorted(state.cash_bag.items()):
        if iid == SUMMON_ITEM_ID and cnt > 0:
            return slot
    return None


def cash_count_item(snapshot: dict[int, tuple[int, int]], item_id: int) -> int:
    """cash 背包快照里某 itemId 的总数量(传 dict 快照,GUI 线程读时先 dict() 拷贝)。"""
    if not item_id:
        return 0
    return sum(cnt for (iid, cnt) in snapshot.values() if iid == item_id)


def cash_find_slot(snapshot: dict[int, tuple[int, int]], item_id: int) -> "int | None":
    """cash 背包快照里第一个有货的该 itemId 格(0 基);无则 None。"""
    if not item_id:
        return None
    for slot, (iid, cnt) in sorted(snapshot.items()):
        if iid == item_id and cnt > 0:
            return slot
    return None


def _load_bulk_inventory(
    target: dict[int, tuple[int, int]], frame: bytes, *, entry_base: int, slot_cap: int
) -> None:
    """把整桶背包帧按 16 字节/格解析进 target(内部 slot -> (itemId, count)),itemId==0 视为空格(不收录)。
    数量字段(off+4)是 packed,拆法严格对齐游戏 Item_SetSlotFromStruct@0x7A5120 的按类型分支:
      可堆叠(Item_IsStackable) -> 真实堆叠数 = packed + 1(游戏 v26[4]=a3[2]+1;wire 上存的是堆叠数-1,
        所以 wire=0 其实是 1 件,满摞 300 在 wire 上是 299);
      不可堆叠 -> 低16=属性、高16=耐久,整段当数量会得到 0x30000=196608 之类垃圾值,故固定按 1 件计。
    卖货发包(CG_SELL_ITEM 411021)的 qty 字段同样取这个真实数量:游戏 Item_GetSellQuantity@0x79B850 在
    可堆叠时返回 slot+0x10(=packed+1),不可堆叠时返回 1,故此处与 autosell 的卖出数量天然一致。
    entry_base = 条目在帧内的起始偏移:主背包 BAG_ENTRY_BASE=220(帧头8 + itemCount头4 + 13装备格×16),
    cash 8(帧头8,无内容头,从 body+0 直接起)。内部 slot 即发送侧 wire_slot - WIRE_SLOT_BASE。"""
    target.clear()
    end = len(frame)
    off = entry_base
    slot = 0
    while off + INV_ENTRY_STRIDE <= end and slot < slot_cap:
        item_id = u32(frame, off)
        if item_id != 0:
            count = (u32(frame, off + 4) + 1) if item_stackable(item_id) else 1
            target[slot] = (item_id, count)
        off += INV_ENTRY_STRIDE
        slot += 1


def _record_gold(state: FarmState, new_gold: int, source: str, *, count_income: bool = False) -> None:
    """更新金币快照并写业务日志。首次只记当前值;之后记 delta(变了才记)。
    主 .log 也打一行 [金币] 便于排查;干净的「当前金币/获得金币」进 .biz.log。"""
    if not state.gold_seen:
        state.gold = new_gold
        state.gold_seen = True
        print(f"[金币] 当前金币={new_gold}(来源 {source})")
        biz_log(f"当前金币 {new_gold:,}")
        return
    delta = new_gold - state.gold
    if delta == 0:
        return
    state.gold = new_gold
    print(f"[金币] {'+' if delta > 0 else ''}{delta} -> {new_gold}(来源 {source})")
    if delta > 0:
        biz_log(f"获得金币 +{delta:,}(当前 {new_gold:,})")
        if count_income:
            bump_controller_stat("stat_sell_income", delta)
    else:
        biz_log(f"金币变动 {delta:,}(当前 {new_gold:,})")


def _crt_upsert(
    state: FarmState,
    crt_id: int,
    *,
    mtype: int | None = None,
    grade: int | None = None,
    x: int | None = None,
    y: int | None = None,
    hp: int | None = None,
) -> dict:
    """按 crtId 取/建怪物条目并就地更新已知字段(传 None 的字段保持不变)。
    MOVE 可能先于 LOAD/NEW 到(我们进图时已有怪在动),此时建条目 type/grade=0(未知)。"""
    m = state.nearby_monsters.get(crt_id)
    if m is None:
        m = {"type": 0, "grade": 0, "x": 0, "y": 0, "hp": None, "max_hp": None}
        state.nearby_monsters[crt_id] = m
    if mtype is not None:
        m["type"] = mtype
    if grade is not None:
        m["grade"] = grade
    if x is not None:
        m["x"] = x
    if y is not None:
        m["y"] = y
    if hp:
        # 刷出包带的满血:hp 当前值,max 取见过的最大值(被打后 DAMAGE 会把当前压低)。
        m["hp"] = hp
        m["max_hp"] = hp if m["max_hp"] is None else max(m["max_hp"], hp)
    m["last_seen"] = time.monotonic()
    return m


def monster_snapshot(state: FarmState) -> list[dict]:
    """GUI 线程读怪物快照:返回按与本地坐标距离排序的浅拷贝列表(无本地坐标则按 crtId)。
    每项 {crt_id, type, grade, x, y, hp, max_hp, dist}。dist 为到玩家(coord_x/y)的曼哈顿距离。"""
    cx, cy = state.coord_x, state.coord_y
    have_self = state.coord_seen
    out: list[dict] = []
    for crt_id, m in state.nearby_monsters.items():
        dist = abs(m["x"] - cx) + abs(m["y"] - cy) if have_self else None
        out.append({
            "crt_id": crt_id,
            "type": m["type"],
            "grade": m["grade"],
            "x": m["x"],
            "y": m["y"],
            "hp": m["hp"],
            "max_hp": m["max_hp"],
            "dist": dist,
        })
    out.sort(key=lambda e: (e["dist"] if e["dist"] is not None else e["crt_id"]))
    return out


def update_farm_state(state: FarmState, chunk: bytes) -> None:
    """从一段(已跨 recv 重组、按帧对齐的)数据更新坐标 / 背包 / cash 背包 / 收鱼 / 金币状态。"""
    for frame in iter_framed_packets(chunk):
        if len(frame) < 8:
            continue
        proto = u32(frame, 4)
        if proto == SC_LOCAL_MAP_STATE and len(frame) >= 32:
            cx = u32(frame, 24)
            cy = u32(frame, 28)
            if cx or cy:
                state.coord_x = cx
                state.coord_y = cy
                state.coord_seen = True
            # 地图 SLOT(wire+20 = g_localMapId);经 g_MapIdTable 转规范 id 再查名
            if len(frame) >= 24:
                state.map_slot = u32(frame, 20)
                state.map_seen = True
            # 当前 HP(wire+88)/ 本地玩家 actorId(wire+92)。max HP 取历史最大值:登录瞬间
            # 当前=上限,之后被怪打掉的话 511001 不再下发,只能靠 521023/511131 增量,故上限以
            # 首次(满血)那次为准,用 max() 防止后续帧把上限压低。actor_id 用来在战斗包里认本人。
            # 注意:GC_CHAR_DATA_LOAD 同结构也会被【附近其他实体的 spawn】复用(实测进图后会再来
            # 一个 wire+92 为别人 actorId、HP 低得多的 511001)。已锁定本人 actorId 后,只认 wire+92
            # 等于本人的那帧,否则会把别人的血量画到自己头上。首次(还没锁 actorId)无条件采纳=登录帧。
            if len(frame) >= 96:
                frame_actor = u32(frame, 92)
                if state.actor_id == 0 or frame_actor == state.actor_id:
                    hp = u32(frame, 88)
                    state.hp = hp
                    state.max_hp = max(state.max_hp, hp)
                    state.actor_id = frame_actor
                    state.hp_seen = True
            # 同一包(char-data-load)在 body+44 带初始金币
            if len(frame) >= SC_CHARLOAD_MONEY_WIRE_OFFSET + 8:
                _record_gold(state, u64(frame, SC_CHARLOAD_MONEY_WIRE_OFFSET), "char-load")
        elif proto in (SC_ATTACK_CRT2TARGET_SUCC, SC_ATTACK_CRT2TARGET_CRIT) and len(frame) >= 40:
            # 怪打人:wire+36=被打者 actorId,wire+32=被打者当前 HP。只认"被打者==本人"的包,
            # 否则会把附近其他玩家/宠物的血量误当成自己的。本地 actorId 来自登录的 511001 wire+92。
            # max_hp 兜底:万一登录没拿到上限(hp_seen=False),用首个战斗血量当上限,避免 0/0 显示。
            target_actor = u32(frame, 36)
            if state.actor_id and target_actor == state.actor_id:
                hp = u32(frame, 32)
                if not state.hp_seen or state.max_hp == 0:
                    state.max_hp = max(state.max_hp, hp)
                state.hp = hp
                state.hp_seen = True
        elif proto == GC_CRT_LOAD and len(frame) >= 12:
            # 进视野整桶:body+0 count,其后 count × 112B 条目(crtId@0/x@4/y@8/type@12/grade@16/hp@20)。
            # +20 是怪物满血(实证:稻草人 2 亿、活动元素 394 万、小怪几十,和 DAMAGE 的当前 HP 同口径)。
            count = u32(frame, 8)
            off = 12
            n = 0
            while n < count and off + CRT_ENTRY_STRIDE <= len(frame):
                crt_id = u32(frame, off)
                if crt_id:
                    _crt_upsert(
                        state, crt_id,
                        mtype=u32(frame, off + 12), grade=u32(frame, off + 16),
                        x=u32(frame, off + 4), y=u32(frame, off + 8),
                        hp=u32(frame, off + 20),
                    )
                off += CRT_ENTRY_STRIDE
                n += 1
            state.monsters_observed = True
        elif proto == GC_CRT_NEW and len(frame) >= 28:
            # 单只刷出:body 同 112B 条目结构(crtId@8/x@12/y@16/type@20/grade@24/hp@28 of frame)。
            crt_id = u32(frame, 8)
            if crt_id:
                _crt_upsert(
                    state, crt_id,
                    mtype=u32(frame, 20), grade=u32(frame, 24),
                    x=u32(frame, 12), y=u32(frame, 16),
                    hp=u32(frame, 28) if len(frame) >= 32 else None,
                )
                state.monsters_observed = True
        elif proto == GC_CRT_MOVE and len(frame) >= 20:
            # 实时移动:crtId@8/x@12/y@16(frame 偏移)。
            crt_id = u32(frame, 8)
            if crt_id:
                _crt_upsert(state, crt_id, x=u32(frame, 12), y=u32(frame, 16))
                state.monsters_observed = True
        elif proto in (GC_CRT_REMOVE, GC_CRT_DIE) and len(frame) >= 12:
            # 出视野/死亡:body+0 crtId,直接移除。
            state.nearby_monsters.pop(u32(frame, 8), None)
        elif proto == GC_CRT_DAMAGE and len(frame) >= 24:
            # 怪物受击:crtId@8 / dmgFlag@12(i32)/ curHP@16(i64)/ attackerId@24。curHP<=0 即死亡移除。
            crt_id = u32(frame, 8)
            if crt_id:
                cur_hp = i64(frame, 16)
                if cur_hp <= 0:
                    state.nearby_monsters.pop(crt_id, None)
                else:
                    m = _crt_upsert(state, crt_id)
                    m["hp"] = cur_hp
                    m["max_hp"] = cur_hp if m["max_hp"] is None else max(m["max_hp"], cur_hp)
                state.monsters_observed = True
        elif proto in MONEY_WIRE_OFFSETS:
            off = MONEY_WIRE_OFFSETS[proto]
            if len(frame) >= off + 8:
                _record_gold(state, u64(frame, off), proto_name(proto))
        elif proto == SC_BAG_BULK:
            _load_bulk_inventory(state.bag, frame, entry_base=BAG_ENTRY_BASE, slot_cap=BAG_MAIN_SLOTS)
            state.bag_observed = True
            bait = getattr(state.args, "fishing_bait_item_id", 0)
            if bait and any(iid == bait for iid, _ in state.bag.values()):
                state.bait_observed = True
        elif proto == SC_CASH_BAG_BULK:
            _load_bulk_inventory(state.cash_bag, frame, entry_base=8, slot_cap=CASH_SLOT_CAP)
            state.cash_observed = True
        elif proto == SC_BANK_BULK and len(frame) >= 16:
            # 仓库整桶:body+4 条目数,每条 24B(+0 唯一id/+8 模板itemId/+12 packed count)。
            # 重建 state.bank(唯一id -> (模板itemId, 真实数量))。空条目(itemId=0)跳过。
            count = u32(frame, 12)
            state.bank.clear()
            off = 8 + BANK_BULK_HEADER   # 帧+8 是 body 起点,+BANK_BULK_HEADER 跳过 page+count
            n = 0
            while off + BANK_ENTRY_STRIDE <= len(frame) and n < count:
                uid = u32(frame, off)
                tmpl = u32(frame, off + 8)
                if uid and tmpl:
                    packed = u32(frame, off + 12)
                    state.bank[uid] = (tmpl, (packed + 1) if item_stackable(tmpl) else 1)
                off += BANK_ENTRY_STRIDE
                n += 1
            state.bank_observed = True
        elif proto == SC_BANK_UPDATE and len(frame) >= 32:
            # 单条仓库更新(搬出一格后回流):body+20 是该实例唯一id。把它从本地 bank 移除,
            # 避免补货状态机重复搬同一格(真实是否清空由服务端决定,这里保守删除)。
            uid = u32(frame, 28)
            if uid:
                state.bank.pop(uid, None)
        elif proto == SC_CASH_SLOT_UPDATE and len(frame) >= 20:
            # 单格 cash 背包更新(用/买 cash 物品后回流):body+0 bagId(=slot+13)/+4 itemId/+8 packed count。
            # 与整桶 511324、卖货 521054 同一物品结构口径:可堆叠真实数量=packed+1,itemId=0 清空该格。
            # 没这个 handler 时 cash 数量会一直停在进场快照,定时使用后界面/告警都不会减(本次修复的就是这个)。
            slot = u32(frame, 8) - WIRE_SLOT_BASE
            if slot >= 0:
                cash_item = u32(frame, 12)
                if cash_item == 0:
                    state.cash_bag.pop(slot, None)
                else:
                    packed = u32(frame, 16)
                    state.cash_bag[slot] = (cash_item, (packed + 1) if item_stackable(cash_item) else 1)
                state.cash_observed = True
        elif proto == SC_BUFF_ADD and len(frame) >= 24:
            # 加 buff:frame+8 disc。disc==0=cash 状态(+12 type/+16 expiryUnix 绝对秒/+20 value);
            # disc!=0=技能 buff(+12 buffId/+16 duration 相对秒/+20 value),只收白名单里的「cash 栏」
            # 技能 buff(category 4/0,如进阶摆摊),其余普通技能 buff 忽略。
            disc = u32(frame, 8)
            if disc == 0:
                btype = u32(frame, 12)
                value = u32(frame, 20)
                state.cash_buffs[btype] = {
                    "type": btype,
                    "name": cash_buff_name(btype, value),
                    "value": value,
                    "expiry": u32(frame, 16),   # 绝对 unix 秒
                }
                state.cash_buffs_observed = True
            else:
                buff_id = u32(frame, 12)
                name = skill_cash_buff_name(buff_id)
                if name is not None:
                    duration = u32(frame, 16)   # 相对剩余秒,换算成绝对到期
                    state.skill_buffs[buff_id] = {
                        "type": buff_id,
                        "name": name,
                        "value": u32(frame, 20),
                        "expiry": int(time.time()) + duration if duration else 0,
                    }
                    state.cash_buffs_observed = True
        elif proto == SC_BUFF_REMOVE and len(frame) >= 16:
            # 移除 buff:frame+8 disc / +12 type|buffId。disc==0 删 cash 状态,disc!=0 删技能 buff。
            if u32(frame, 8) == 0:
                state.cash_buffs.pop(u32(frame, 12), None)
            else:
                state.skill_buffs.pop(u32(frame, 12), None)
            state.cash_buffs_observed = True
        elif proto in (SC_SELL_ITEM_RESULT, SC_BUY_ITEM_RESULT) and len(frame) >= 36:
            # 521054 卖货 / 521052 买货 结果包,结构相同:每卖/买一笔推一包,
            # 同时带【单格背包更新(该格新总数)】+【最新金币】。买货也走这里——之前只解卖货,
            # 导致买完 bag_count 不回升、autobuy 无限买。
            #   body+0 bagId(=slot+13) / body+4 itemId / body+8 packedCount / body+12 u64 uid / body+20 u64 money
            # 卖空时 itemId=0 → 清空该格(游戏 Item_SetSlotFromStruct 同样清);可堆叠 count=packed+1。
            slot = u32(frame, 8) - WIRE_SLOT_BASE
            if slot >= 0:
                slot_item = u32(frame, 12)
                if slot_item == 0:
                    state.bag.pop(slot, None)
                else:
                    packed = u32(frame, 16)
                    state.bag[slot] = (slot_item, (packed + 1) if item_stackable(slot_item) else 1)
                state.bag_observed = True
            _record_gold(state, u64(frame, 28), proto_name(proto),
                         count_income=(proto == SC_SELL_ITEM_RESULT))
        elif proto == SC_FISHING_CATCH and len(frame) >= 44:
            # 521102 两段式(IDA handler GC_OnFishingCatch_521102@0x891810 核对):
            #   entry1 frame+16/20/24 = 鱼饵被消耗后的剩余(itemId=鱼饵,count 每次递减)
            #   entry2 frame+36/40    = 真正钓到的鱼(itemId 每次不同,落入新背包格)
            # 游戏自身弹"(鱼名) ..."聊天用的就是 entry2 槽位物品;旧代码错读了 entry1
            # 把鱼饵当成鱼获。entry2 的 count 字段恒为 0x00030000(低16=0,真实堆叠量随后
            # 由 510006/510008 背包同步包补正),故每个 521102 事件按"钓到 1 条"计。
            # entry1 鱼饵的 count 同样是 packed:handler 把它喂给 Item_SetSlotFromStruct,
            # 可堆叠物品真实剩余 = packed + 1(游戏 v26[4]=a3[2]+1),与整桶解析口径一致。
            bait_bag = u32(frame, 16)
            bait_item = u32(frame, 20)
            bait_count = (u32(frame, 24) + 1) if item_stackable(bait_item) else 1
            bait_slot = bait_bag - WIRE_SLOT_BASE
            if bait_slot >= 0:
                # 该格最后一个鱼饵被这次抛竿消耗光时,服务器回的 entry1 itemId=0(空格),
                # count 字段仍是 1。必须按空格处理清掉,否则背包会残留 itemId=0/数量1 的幽灵格。
                if bait_item == 0:
                    state.bag.pop(bait_slot, None)
                    bait_count = 0
                else:
                    state.bag[bait_slot] = (bait_item, bait_count)
                state.bag_observed = True
            fish_bag = u32(frame, 36)
            fish_item = u32(frame, 40)
            fish_slot = fish_bag - WIRE_SLOT_BASE
            if fish_slot >= 0:
                if fish_item == 0:
                    state.bag.pop(fish_slot, None)
                else:
                    prev = state.bag.get(fish_slot)
                    new_count = (prev[1] + 1) if (prev and prev[0] == fish_item) else 1
                    state.bag[fish_slot] = (fish_item, new_count)
                state.bag_observed = True
            state.catch_total += 1
            bump_controller_stat("stat_catch_total")
            bait_desc = "该格已用尽" if bait_item == 0 else f"{item_label(bait_item)}={bait_count}"
            print(f"[钓鱼] 钓到鱼获(第 {state.catch_total} 次,已发中鱼 {state.reel_total} 次):"
                  f"鱼={item_label(fish_item)} 背包格={fish_slot} | 鱼饵剩余 {bait_desc}")
            biz_log(f"钓到 {item_label(fish_item)}(累计 {state.catch_total} 次,鱼饵剩 {'该格用尽' if bait_item == 0 else bait_count})")
        elif proto == SC_STREETSTALL_ITEM_UPDATE and len(frame) >= 20:
            # 511110 个人摆摊剩余量更新:body+8 = [stallSlotIdx, itemId, newRemaining(绝对剩余,0=该格售罄)]。
            # stallSlotIdx 对应开摊时 411049 的上架条目顺序(stall_open 已据此填 stall_slots)。
            slot_idx = u32(frame, 8)
            upd_item = u32(frame, 12)
            remaining = u32(frame, 16)
            entry = state.stall_slots.get(slot_idx)
            if entry is not None:
                entry["remaining"] = remaining
                if upd_item:
                    entry["item_id"] = upd_item
            else:
                # 没有本地登记(如重连后服务端补发)也记下,保证 GUI 能看到剩余量。
                state.stall_slots[slot_idx] = {
                    "item_id": upd_item, "remaining": remaining, "qty0": remaining,
                    "price": 0, "bag_type": 0, "src_slot": -1,
                }
            print(f"[摆摊] 剩余更新 格{slot_idx} {item_label(upd_item)} 剩 {remaining}")
        elif proto == SC_STREETSTALL_SOLD and len(frame) >= 40:
            # 521109 卖出回流:body+8 = [op(1=cash/0=主背包), wireSlot, itemId, newBagCount(packed), u64 _, u64 newGold]。
            # 既刷新源背包格(卖出后源格数量下降),又带最新金币(=卖货收入,经 _record_gold 计入会话统计)。
            op = u32(frame, 8)
            sold_slot = u32(frame, 12) - WIRE_SLOT_BASE
            sold_item = u32(frame, 16)
            if sold_slot >= 0:
                target = state.cash_bag if op == 1 else state.bag
                if sold_item == 0:
                    target.pop(sold_slot, None)
                else:
                    packed = u32(frame, 20)
                    target[sold_slot] = (sold_item, (packed + 1) if item_stackable(sold_item) else 1)
                if op == 1:
                    state.cash_observed = True
                else:
                    state.bag_observed = True
            _record_gold(state, u64(frame, 32), proto_name(SC_STREETSTALL_SOLD), count_income=True)
            print(f"[摆摊] 卖出 {item_label(sold_item)}(源{'cash' if op == 1 else '背包'}格{sold_slot}),金币刷新")
            biz_log(f"摆摊卖出 {item_label(sold_item)}", ui=True)


# __FARM_SEND_PLACEHOLDER__


def farm_send(state: FarmState, sock: socket.socket, proto: int, body: bytes, label: str | None = None) -> None:
    args = state.args
    send_game1841(sock, proto, body, label, key_index=args._key_index, debug=bool(args.debug_packets))
