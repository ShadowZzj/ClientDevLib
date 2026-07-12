"""SO3D 协议常量、opcode、名表、数据类与小工具(无内部依赖)。"""
from __future__ import annotations

import struct
from dataclasses import dataclass


CL_LOGIN = 111000
CL_GET_CHARINFO = 111003
CL_CREATE_CHARACTER = 111004
CL_GAMESERVER_CONNECT = 111006
CL_LIVE = 111008
CL_LOGIN_SECONDARY = 111011
LC_SERVER_LIST = 221001
LC_LOGIN_FAIL = 221002
LC_CHARINFO_SUCCESS = 221003
LC_CHARCREATE_SUCCESS = 221005
LC_GSERV_CONNECT_SUCCESS = 221009
LC_LOGIN_FAIL_ALT = 230010
LC_LOGIN_SECONDARY = 231002

# 创建角色默认外观:逐字节复刻手动抓包(_bootstrap 建号包,牧师/Cleric)。
# CL_CREATE_CHARACTER body = server_id(u32) + name(16s) + 下面 13 个 u32:
#   [const1, 头109, 身197, 腿241, 性别2, 外观槽 8/6/5/5/6/5, 发型0, 职业5]
CREATE_CHAR_APPEARANCE = (1, 109, 197, 241, 2, 8, 6, 5, 5, 6, 5, 0, 5)

CG_ENTER = 411005
CG_GAME_ALIVE = 411041
# 1842 世界服真正的保活包(真人客户端每 60s 发一次):proto=411013,body=8 字节全 0,
# 明文帧 10 00 00 00 85 45 06 00 00 00 00 00 00 00 00 00。
# 注意不是 CG_GAME_ALIVE(411041)——那个 proto/长度/body 都不对,服务器不当心跳认。
CG_HEARTBEAT = 411013
CG_INIT_411555 = 411555
CG_INIT_411589 = 411589
CG_INIT_411595 = 411595
CG_POST_ENTER_411570 = 411570
CG_POST_ENTER_411625 = 411625
CG_PLAY_START = 412039
SC_LOCAL_MAP_STATE = 511001
SC_MAP_HINT = 521051
# 怪物攻击玩家的成功/暴击包(IDA: Packet_OpcodeToName case 注释)。两者 44B 同构:
#   wire+8=攻击者(怪)actorId / wire+24=基础伤害 / wire+28=实际掉血 / wire+32=被打者【当前HP】
#   / wire+36=被打者 actorId。当 wire+36==本地 actorId(511001 wire+92)时 wire+32 就是玩家血量。
# 暴击会跳过 SUCC 流,必须两个一起处理才能让血量条同步(实测合并后单调递减:普攻-44,暴击-66)。
SC_ATTACK_CRT2TARGET_SUCC = 521023    # GC_ATTACK_CRT2TARGET_SUCC / 0x7F33F
SC_ATTACK_CRT2TARGET_CRIT = 511131    # GC_ATTACK_CRT2TARGET_CRITICAL / 0x7CC9B

# ---------- 周围怪物(CRT,recv,wire body 明文从帧 +8 起)----------
# IDA 核对 unpackd_so3d.exe 各 handler(已在 IDB 重命名 CMsg_OnCrt*),并与 recvlog 实帧逐字节对齐:
#   GC_CRT_LOAD 511029 (0x7CC35) CMsg_OnCrtLoad@0x89EDF0:进视野整桶快照。
#     body+0 count(u32),其后 count × 112B 条目。
#   GC_CRT_NEW  511030 (0x7CC36) CMsg_OnCrtNew@0x89F220:单只刷出,条目同 112B 结构。
#     条目布局:+0 crtId / +4 x / +8 y / +12 monsterType / +16 grade(≤8)/ +20 u64 / +28 u32
#               / +32/+36/+40 状态位掩码 / +44 appearance[68]。
#   GC_CRT_MOVE 511032 (0x7CC38) CMsg_OnCrtMove@0x89F110:+0 crtId / +4 x / +8 y(实时坐标)。
#   GC_CRT_REMOVE 511031 (0x7CC37) CMsg_OnCrtRemove@0x89F550:+0 crtId(出视野/移除)。
#   GC_CRT_DAMAGE 511423 (0x7CDBF) CMsg_OnCrtDamage@0x94F120:+0 crtId / +4 dmgFlag(i32) /
#     +8 curHP(i64,落 creature+800)/ +16 attackerId / +20 dmgAmount(i64)。curHP<=0 即死亡。
#   GC_CRT_DIE 511043 (0x7CC43):无独立 handler 注册,死亡靠 DAMAGE 的 curHP<=0 / REMOVE 体现。
# 注意:NEW/LOAD 不带 HP——客户端按 monsterType 从怪物模板库(creature+872→+176)填血。
# 纯 socket 端没有模板库,故只有【被攻击过】的怪才能从 DAMAGE 包拿到当前 HP,其余显示未知。
GC_CRT_LOAD = 511029
GC_CRT_NEW = 511030
GC_CRT_REMOVE = 511031
GC_CRT_MOVE = 511032
GC_CRT_DIE = 511043
GC_CRT_DAMAGE = 511423
CRT_ENTRY_STRIDE = 112                # LOAD/NEW 单条目字节数(44 字段 + 68 appearance)

# ---------- 钓鱼 / 贩卖 / 购买 (send) ----------
CG_FISHING_CAST = 411047        # 抛竿: body=[invSlotEnc, actorId, coordX, coordY] len24
CG_FISHING_CAST_SPECIAL = 411174  # 特殊饵抛竿,body 同 411047
CG_FISHING_REEL = 412048        # 收杆/起钩: body=0 (16B tracked send)
CG_ACTION_CANCEL = 411023       # 取消动作: mode!=0 -> body=[mode] len12 (stop=1)
CG_SELL_ITEM = 411021           # 卖出: body=[bagId(slot+13), qty, token=0xFECD2408] len12
CG_VENDOR_BUY = 411020          # 购买: body=[vendorId, shopIndex, count, targetSlot, token] len28
CG_VENDOR_OPEN = 411455         # 开摊贩: body=[token]
CG_VENDOR_CLOSE = 411456        # 关摊贩: body=[token]
CG_USE_CASH_ITEM = 411156       # 用 cash 物品(召唤摊贩): body=[slot+13, 0]
CG_PLAYER_REVIVE = 412017       # 回城复活: body=[reviveMode] len12,= GGThreadBlock::ReviveToTown(玩家点死亡弹框「复活」同一 wire)
REVIVE_MODE_DEFAULT = 1         # reviveMode payload;1=默认(已抓包验证),去哪由服务端决定
# 回城落点由 reviveMode 决定(对齐 GGThreadBlock TeleportTownModule):乐园镇=1、狮子城=2
# (獅子城抓包确认 body=[02 00 00 00])。两地都发同一条 412017,仅这个 DWORD 不同。
REVIVE_MODE_PARADISE = 1        # 乐园镇(樂園鎮)
REVIVE_MODE_LIONCITY = 2        # 狮子城(獅子城)
# 有序 (城镇名, reviveMode);GUI 回城复活弹窗按此顺序建按钮。
REVIVE_TOWNS = (("乐园镇", REVIVE_MODE_PARADISE), ("狮子城", REVIVE_MODE_LIONCITY))
REVIVE_TOWN_NAMES = {mode: name for name, mode in REVIVE_TOWNS}  # reviveMode -> 城镇名(日志用)
# 邮寄金钱: MailBox::SendMoneyMail(proto 411524, op=0)。body 固定 77 字节(0x4D),布局经
# GGThreadBlock/CLocalPlayer.h 文档 + 实抓包逐字节核对(shadowdance 寄 1111 给 shadowsing):
#   [0..3]   const 3 / [4..19] 收件人 16B(编码后右补 0,不强制 NUL) / [20..23] op=0(金钱邮件)
#   [24..31] money int64 / [32..76] 邮件正文 45B(空则全 0)
CG_SEND_MAIL = 411524
MAIL_HEADER_CONST = 3           # body+0 固定头字段(抓包恒为 3)
MAIL_OP_MONEY = 0               # op=0 金钱邮件
# op=1 物品邮件。body 仍 77 字节,只是金钱的 int64[24..31] 拆成 [24..27]=itemBagId、[28..31]=itemCount:
#   [0..3]=3 / [4..19]=收件人16B / [20..23]=op=1 / [24..27]=itemBagId / [28..31]=itemCount / [32..76]=正文45B
# 引擎 MailBox::SendItemMail@0x7F4F80 逐字段核对:WriteU32(3)+WriteStr(name16)+WriteU32(op)+
#   WriteU32(itemBagId)+WriteU32(itemCount)+WriteStr(body),末尾 Net__SkillSendPackage(...,411524,0x4D)。
# itemBagId 口径同卖货/摆摊 = wireSlot = 内部slot+WIRE_SLOT_BASE(13):游戏邮件UI 传的是 send-box
#   物件+0x08(转存的源格 wireSlot),GGThreadBlock 绕过UI 直接把 BagItemInfo::bagId(=容器item+0x00=slot+13)
#   喂给同一引擎函数——两条路证实引擎参数就是 wireSlot,不是模板itemId、也不是 bank 那种唯一实例id。
MAIL_OP_ITEM = 1
MAIL_RECIPIENT_FIELD = 16       # 收件人字段宽(引擎 char[16],ASCII/Big5,超长会冲掉 op)
MAIL_BODY_FIELD = 45            # 正文字段宽,使 body 合计 4+16+4+8+45=77
# 账号共享仓库(item bank receipt)。泡点网店买的东西进 bank,再开 bank 拖到 cash 背包。
# 抓包实证(shadowdance):开 bank 发 411154[1]+411644[self];拖 bank→cash 发 411155。
CG_OPEN_BANK = 411154           # 开仓库: body=[1]
CG_BANK_KEEPALIVE = 411644      # 开仓库时随发的握手: body=[self proto](同 CG_GAME_ALIVE 套路)
CG_BANK_MOVE = 411155           # 仓库->cash: body=[destCashWireSlot(slot+13), bankUniqueId, 0, count]

# ---------- 背包 / 收鱼 (recv, wire body 明文,从帧 +8 起) ----------
# 入场时服务器整桶下发。每格 16 字节 = [u32 itemId, u32 count, u64 uniqueId],count==0 即空格。
# 布局经 IDA 核对真实 handler(unpackd_so3d.exe):
#   511591 主背包 handler sub_8BD150,body 三段固定:
#     body+0    : u32 itemCount 头
#     body+4    : 13 个装备格 ×16 字节(非主背包,跳过)
#     body+212  : 192 个主背包格 ×16 字节,内部 slot k -> wire slot k+13(handler 里 v15=k+13)
#     body+3284 : 205 个 u32 尾部(每格附加字段,非物品)  body 合计 4+208+3072+820=4104
#   511324 cash 背包 handler sub_8AD480,body = [{16字节格}*80],无内容头,从 body+0 直接起;
#     内部 slot i -> wire slot i+13(handler 里 v9[0].lo=i+13)。
SC_BAG_BULK = 511591            # 主背包整桶
SC_CASH_BAG_BULK = 511324       # cash 背包整桶(最多 80 格)
# 单格 cash 更新:用/买 cash 物品后回流一帧 511325,body+0 bagId(=slot+13)/+4 itemId/+8 packed count,
# 与整桶 511324 同物品结构口径(可堆叠真实数量=packed+1)。抓包实证:摊贩呼叫卷(8036)每次使用后
# 数量逐 1 递减(285→284→283→282);黃金釣鉤(8030)使用后回 count=298。itemId=0 表示该格已空。
SC_CASH_SLOT_UPDATE = 511325
# 仓库(bank)recv。开 bank 后回 511320 整桶;body+0 page、body+4 条目数,之后每条 24 字节:
#   +0  u32 唯一实例id(每条不同,搬运时 411155 引用这个) / +4 u32 0
#   +8  u32 模板itemId(item_names 里,会重复,匹配配置用这个) / +12 u32 packed count(可堆叠+1)
#   +16 u64 附加(0 / 0xFFFFFFFFFFFFFFFF)
# 搬运一格出 bank 后回 511322:body+4 模板itemId、body+20 该实例唯一id(据此把它从本地 bank 移除)。
SC_BANK_BULK = 511320
SC_BANK_UPDATE = 511322
BANK_ENTRY_STRIDE = 24          # 511320 每条目字节数
BANK_BULK_HEADER = 8            # body+0 page + body+4 count,条目从 body+8(帧+16)起
SC_FISHING_CATCH = 521102       # 收鱼(0x7F38E,IDA handler GC_OnFishingCatch_521102@0x891810):两段式,entry1=鱼饵剩余,entry2=钓到的鱼

# ---------- 个人摆摊 street stall (send + recv) ----------
# 流程(对照真实抓包 shadowdance,逐字节核对 IDA + sendlog/recvlog):
#   开摊 CG_SendOpenStreetStall_411040@0xB2C170:type∈{0,2,3} 时 body = [32B 摊位名][u32 a3][u32 type],
#     个人摊走 OpenStreetStall_Type2@0x9BFE80(a3=12,type=2);type 其它值时再追加 16B a5(本客户端不用)。
#   上架 CG_SendStreetStallItemList_411049@0x9C3A00:body = [u32 count=16] + 16×24B 条目,
#     条目 = [u32 bagType(0=主背包/1=cash), u32 subType(0), u32 wireSlot(=srcSlot+13), u32 qty, u64 price],
#     空槽哨兵 = [0, 0, 1000, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF]。条目顺序下标 == 服务端回 511110 的 stallSlotIdx。
#   终止 CG_SendStreetStallTerminate_412050@0x9C3BD0:body = [u32 1, u32 0, u32 0]。
# 监控(recv,GC_StreetStallRecvDispatch@0x9C04D0 分发):
#   511110 GC_OnStreetStallItemUpdate@0x9C0EB0:body+8 = [u32 stallSlotIdx, u32 itemId, u32 newRemaining],
#     newRemaining 是该格【绝对剩余量】(非增量),==0 即该格售罄;全 16 格空时服务端自动收摊。
#   521109 GC_OnStreetStallSold@0x9C0CB0:body+8 = [u32 op(1=cash/0=主背包), u32 wireSlot(=srcSlot+13),
#     u32 itemId, u32 newBagCount(packed,可堆叠真实=+1), u64 _(=0), u64 newGold]。★newGold 在 frame+32★
#     (中间夹了一个恒 0 的 u64),故读金币要 len>=40。卖出回流同时刷新源背包格数量 + 最新金币(=卖货收入)。
#   522105 摆摊关闭 ack(终止后回流,body+8 单 u32,抓包=376),仅作日志。
CG_OPEN_STREETSTALL = 411040
CG_STREETSTALL_ITEMS = 411049
CG_STREETSTALL_TERMINATE = 412050     # 停止贩卖:服务端把摊位置为「准备中」,并未让摊位整个退出
CG_STREETSTALL_CLOSE = 411041         # 关闭/离开摊位:摊位整个退出。body 单 u32=411041(自身 proto 作 token)
                                      # IDA: StreetStall_OnClose_Send411041 @0x9bfef0,Net__SendDword(buf,411041,411041);
                                      # 真实客户端关摊顺序 = 412050(停售)-> 411041(离摊)。两者都发才不会卡在「准备中」。
# 坐标传送:CG 411597(CG_REQ_STREETSTALL_WARP)。复刻街摊「搜索->传送到摊主」那条 warp 包
# (IDA CStreetStallSearch::SendWarpToDealer @0x9CCA00 / GGThreadBlock::WarpToCoordinate):
# 明文帧 [len16][411597][u32 x][u32 y],x/y 是整数地图格坐标(非世界浮点,后者布局 X,Z,Y)。
# 瞬移不走路,一次性动作。经 send_game1841 走 Blowfish/CBC(等价引擎最底层明文发送里的加密)。
CG_STREETSTALL_WARP = 411597
SC_STREETSTALL_ITEM_UPDATE = 511110   # 0x7CC86 摆摊剩余量更新(每格绝对剩余)
SC_STREETSTALL_SOLD = 521109          # 0x7F395 卖出回流(源格新数量 + 最新金币)
SC_STREETSTALL_CLOSE = 522105         # 0x7F779 摆摊关闭 ack
STALL_MAX_SLOTS = 16            # 上架格上限(411049 固定 16 条目)
STALL_ENTRY_STRIDE = 24         # 每条上架条目字节数
STALL_NAME_FIELD = 32           # 摊位名字段宽(411040)
STALL_TYPE_PERSONAL = 2         # 个人摆摊 type
STALL_OPEN_A3 = 12              # 411040 个人摊 a3 常量(OpenStreetStall_Type2 写死)
STALL_BAG_NORMAL = 0            # 上架来源:主背包
STALL_BAG_CASH = 1             # 上架来源:cash 背包
STALL_EMPTY_SLOT_SENTINEL = 1000  # 空槽 wireSlot 占位值
# 空槽条目:bagType=0, subType=0, wireSlot=1000, qty=0xFFFFFFFF, price=0xFFFFFFFFFFFFFFFF
STALL_EMPTY_ENTRY = struct.pack(
    "<IIIIQ", 0, 0, STALL_EMPTY_SLOT_SENTINEL, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
)


def streetstall_open_body(name: str, encoding: str, *,
                          stall_type: int = STALL_TYPE_PERSONAL, a3: int = STALL_OPEN_A3) -> bytes:
    """411040 开摊 body:[STALL_NAME_FIELD 字节摊位名][u32 a3][u32 type]。个人摊 type=2、a3=12。"""
    return fixed_bytes(name, STALL_NAME_FIELD, encoding) + struct.pack("<II", a3, stall_type)


def streetstall_items_body(entries) -> bytes:
    """411049 上架 body:[u32 count=STALL_MAX_SLOTS] + STALL_MAX_SLOTS 条 24B 条目。
    entries:可迭代 (bag_type, wire_slot, qty, price);不足 16 条用空槽哨兵补满,超出报错。
    条目顺序下标即服务端回 511110 的 stallSlotIdx,调用方据此映射监控剩余量。"""
    parts = [struct.pack("<I", STALL_MAX_SLOTS)]
    used = 0
    for bag_type, wire_slot, qty, price in entries:
        if used >= STALL_MAX_SLOTS:
            raise ValueError(f"摆摊上架最多 {STALL_MAX_SLOTS} 件")
        parts.append(struct.pack("<IIIIQ", int(bag_type), 0, int(wire_slot), int(qty), int(price)))
        used += 1
    parts.append(STALL_EMPTY_ENTRY * (STALL_MAX_SLOTS - used))
    return b"".join(parts)


def streetstall_terminate_body() -> bytes:
    """412050 停止贩卖 body:[u32 1, u32 0, u32 0]。仅停售,摊位仍处「准备中」。"""
    return struct.pack("<III", 1, 0, 0)


def streetstall_close_body() -> bytes:
    """411041 关闭/离开摊位 body:[u32 411041]。客户端把自身 proto 当作 token 再写一遍
    (Net__SendDword(buf, 411041, 411041)),发完摊位整个退出。"""
    return struct.pack("<I", CG_STREETSTALL_CLOSE)


def streetstall_warp_body(x: int, y: int) -> bytes:
    """411597 坐标传送 body:[u32 x][u32 y]。x/y 为整数地图格坐标(非世界浮点)。
    与引擎 [len16][411597][x][y] 明文帧的 body 段一致(帧头 8 字节由 send_game1841 补)。"""
    return struct.pack("<ii", int(x), int(y))


def parse_streetstall_item_update(frame: bytes):
    """解析 511110:返回 (stallSlotIdx, itemId, newRemaining)。剩余为绝对量,0=该格售罄。帧太短返回 None。"""
    if len(frame) < 20:
        return None
    return u32(frame, 8), u32(frame, 12), u32(frame, 16)


def parse_streetstall_sold(frame: bytes):
    """解析 521109:返回 (op, srcSlot, itemId, packedCount, newGold)。
    op:1=cash 背包/0=主背包;srcSlot 已减去 WIRE_SLOT_BASE;packedCount 为源格新数量(可堆叠真实=+1);
    newGold 为最新金币 U64(位于 frame+32,前面夹一个恒 0 的 u64)。帧太短返回 None。"""
    if len(frame) < 40:
        return None
    return (u32(frame, 8), u32(frame, 12) - WIRE_SLOT_BASE,
            u32(frame, 16), u32(frame, 20), u64(frame, 32))


# ---------- Cash 状态 buff (recv) ----------
# 角色死亡無懲罰、各属性藥水、掉寶率提高… 这类 cash 状态,与技能 buff 共用同一对「加/移除」包:
#   511337(0x0007CD69)加,511348(0x0007CD74)移除。body 第一个 u32 是判别符 disc:
#     disc != 0 → 技能 buff(buffId=disc),游戏交给 BuffHelper 处理,本客户端不解析。
#     disc == 0 → cash 状态(就是 web「buff守护」里那一栏)。
# 真实抓包(recvlog.1)+ IDA 双核对,disc==0 时帧布局(帧头8 = len4+proto4,body 从帧+8 起):
#   ADD  511337 帧长 0x18=24:  frame+8 disc(0) / +12 type / +16 expiryUnix(绝对秒) / +20 value
#   RM   511348 帧长 0x14=20:  frame+8 disc(0) / +12 type / +16 secs(剩余秒,移除只按 type 删)
# IDA:GC_OnBuffAdd_511337@0x8B4AE0 读掉 disc,为 0 时再读 3 个 u32 交 GameHUD_AddCashEffect
#     (type, expiryUnix, value)@0x7C5BB0;GameHUD_RemoveCashEffect@0x7C59E0 按 type 删。
# type→名称【不在包里】(游戏按 type 查本地 Big5 字符串表),纯 socket 端只能照下表硬编码。
# 下表已用 dump_cash_buffs.py 直接读在线游戏 GameHUD cash vector(@0xED40F0+0x25C0,记录
# +0x10 为 Big5 名字)核实,权威 —— 不是推断。type 5/6/9 当时无激活样本,未收录(落未知分支;
# 其中 9 与 8 同为掉寶率,由 cash_buff_name 动态命名)。
SC_BUFF_ADD = 511337
SC_BUFF_REMOVE = 511348
# cash 状态 buff 的 type → 名字。全部由 GameHUD_AddCashEffect(IDA 0x7C5BB0)各 case 的
# `push imm32; call sub_5BF8E0/610` 真值(=Big5 字符串表索引)读出,再用 dump_buff_names.py
# 读在线 SO3DPlus.exe 字符串表逐条核实(权威,非推测)。
# ★坑:Hex-Rays 把 sub_5BF8E0 首参当 char,伪代码只显示 stringId 低字节(1838→46=0x2E),
#   必须看反汇编 push 真值;详见 tools/dump_buff_names.py。
# 8/9 名称随 value 变(见 cash_buff_name);24/65560 名称随 value 选宠物运功散名。
CASH_BUFF_TYPE_NAMES = {
    0: "力量藥水",
    1: "魔力藥水",
    2: "防禦力藥水",
    3: "閃避藥水",
    4: "命中藥水",
    5: "移動速度上升",
    7: "攻擊速度上升",
    10: "角色死亡無懲罰狀態",
    11: "黃金釣鉤",
    12: "元氣飯糰",
    13: "寵物便當盒",
    14: "天神的守護",
    15: "情同意合-情侶組隊經驗值增加",
    16: "形影相依-情侶組隊移動速度增加",
    17: "兩情相悅-情侶組隊攻擊速度增加",
    18: "愛情騎士-承受情侶所受到的損傷",
    19: "你儂我儂-將藥水的效果分享給情侶",
    20: "愛情盲目-犧牲自身獲得的經驗值給情侶",
    21: "情侶力量套裝-提高攻擊力",
    22: "情侶攻速套裝-提高攻擊速度",
    23: "情侶防禦套裝-提高防禦力",
    26: "保命達P-HP最大值增加2000",
    27: "轟趴包",
    1000: "額外掉落率100%網咖效果",
    1001: "額外經驗值100%網咖效果",
    1004: "戀曲轟趴-經驗值+100%",
    1005: "戀曲轟趴-掉落率+100%",
    1006: "戀曲轟趴-經驗值+200%",
    1007: "戀曲轟趴-掉落率+200%",
    65551: "情同意合-跟情侶組隊經驗值增加",
    65552: "形影相依-跟情侶組隊移動速度增加",
    65553: "兩情相悅-跟情侶組隊攻擊速度增加",
    65554: "愛情騎士-情侶正在承受自己的損傷中",
    65555: "你儂我儂-分享情侶的藥水效果中",
    65556: "愛情盲目-從情侶那邊獲得一部分經驗值",
    65557: "情侶力量套裝-提高攻擊力",
}

# type 24/65560:名字按 value(子型)选宠物运功散名;65560 是「未啟動」版本。
CASH_BUFF_SUBTYPE_NAMES = {
    0: "寵物運功散_黑暗菲尼克斯",
    1: "寵物運功散_光明巴哈姆特",
    2: "寵物運功散_正義火雞女王",
    3: "寵物運功散_邪惡火雞王",
    4: "寵物運功散_光之蔬果女妖精",
    5: "寵物運功散_闇之蔬果男妖精",
    8837: "寵物運功散_正義火雞女王",
    8838: "寵物運功散_邪惡火雞王",
    8839: "寵物運功散_光之蔬果女妖精",
    8840: "寵物運功散_闇之蔬果男妖精",
    18110: "寵物運功散_黑暗菲尼克斯",
    18111: "寵物運功散_光明巴哈姆特",
}


def cash_buff_name(btype: int, value: int) -> str:
    """cash 状态 type → 显示名。8=掉寶率/9=經驗值 带 value%;24/65560 按 value 选宠物名;
    表里没有的 type 显示原始编号。映射全部经 dump_buff_names.py 读在线字符串表核实。"""
    if btype == 8:
        return f"掉寶率提高{value}%"
    if btype == 9:
        return f"經驗值提高{value}%"
    if btype in (24, 65560):
        name = CASH_BUFF_SUBTYPE_NAMES.get(value, f"寵物運功散(value={value})")
        return f"{name}(未啟動)" if btype == 65560 else name
    return CASH_BUFF_TYPE_NAMES.get(btype, f"未知状态(type={btype})")


# ---------- 进阶/技能型 cash buff(disc!=0,走 BuffHelper)----------
# 进阶摆摊这类状态【不是】disc==0 的 cash 效果,而是 511337 里 disc!=0 的「技能 buff」:
#   GC_OnBuffAdd_511337@0x8B4AE0 → BuffHelper_TryParseAddBuff 读首个 u32=disc,disc!=0 时
#   再读 buffId/duration/value 交 BuffHelper(链表存储),布局(body 从帧+8 起):
#     ADD  frame+8 disc(!=0) / +12 buffId / +16 duration(剩余秒,相对!) / +20 value
#     RM   frame+8 disc(!=0) / +12 buffId
# 游戏内 cash 状态栏(RenderCashBuffBar)同时收 disc==0 的 cash 效果【和】BuffHelper 里
# desc.category∈{4,0} 的 buff —— 后者就是这一组。包里【只带 buffId 不带名】,且 BuffDesc
# 里存的是英文内部 key(不是 Big5 显示名),故下表 buffId→中文是按内部 key 人工翻译(用
# tools/dump_buffdesc.py 读在线 BuffDescTable @ *0xDE355C+0xAF*id 的 category/name 核出的
# 白名单,非游戏本地化原文,可按需修正)。只有列在这里的 buffId 才记进 cash 视图,普通技能
# buff(category 1/2/5)一律忽略。159=premiumselling=进阶摆摊 已实测确认。
SKILL_CASH_BUFF_NAMES = {
    0:   "自動撿取範圍",            # itemautopickuparea (cat0)
    139: "組隊祝福信",              # partyblessletter
    140: "組隊祝福信1",
    141: "組隊祝福信2",
    142: "組隊祝福信3",
    143: "組隊祝福信4",
    144: "組隊祝福信5",
    145: "組隊祝福信6",
    152: "副本入場券狀態",          # insdnticket
    153: "元氣飯糰(gatorade)",      # gatorade
    154: "自動狩獵",                # autohunt
    156: "現金變身",                # cashpolymorph
    159: "進階擺攤",                # premiumselling ★实测确认
    175: "黃金釣鉤(現金釣魚)",      # cashfishing
    209: "寵物現金經驗+100%",       # bpetcashexpup100
    210: "寵物現金經驗+50%",        # bpetcashexpup50
    211: "寵物現金經驗+150%",       # bpetcashexpup150
    214: "自動狩獵(道具)",          # itembuffautohunt
    215: "自動狩獵範圍(道具)",      # itembuffautohuntarea
    216: "技能欄位增加(道具)",      # itembuffskillslot
    217: "攻擊技能欄位增加",        # itemattackskillslot
    218: "寵物自動欄位",            # bpetcashautoslot
    436: "組隊祝福信B",             # partyblessletterb
    437: "組隊祝福信B1",
    438: "組隊祝福信B2",
    439: "組隊祝福信B3",
    440: "組隊祝福信B4",
    441: "組隊祝福信B5",
    442: "組隊祝福信B6",
    444: "組隊祝福信S",             # partyblessletters
    445: "組隊祝福信S1",
    446: "組隊祝福信S2",
    447: "組隊祝福信S3",
    448: "組隊祝福信S4",
    449: "組隊祝福信S5",
    450: "組隊祝福信S6",
    463: "寵物經驗分享10%",         # bpetcashexpshare10
    464: "寵物經驗分享20%",         # bpetcashexpshare20
    465: "寵物經驗分享50%",         # bpetcashexpshare50
    466: "寵物忠誠+100%",           # bpetloyaltyup100
    467: "test(測試)",             # test
    514: "自動撿取(道具)",          # itemautopickup
    515: "自動撿取範圍(道具)",      # itemautopickuparea (cat4)
}


def skill_cash_buff_name(buff_id: int) -> str | None:
    """disc!=0 技能型 buff 的 buffId → 显示名。不在 cash 白名单(普通技能 buff)返回 None。"""
    return SKILL_CASH_BUFF_NAMES.get(buff_id)


# ---------- 金币 (recv) ----------
# 金币 = 小端 U64,游戏内存里在 localUser+0x3498(IDA unpackd_so3d.exe 核对)。
# 下列 SC 包都会带最新金币;body 偏移 = wire 帧内偏移 - 8(帧头 8 字节)。
#   521191 GC_OnMoneyUpdate      : money U64 @ body+0  —— 纯金币广播,卖/买后服务端推这个(主路径)
#   511171 GC_OnShopMoneyResult  : money U64 @ body+20(前 20 字节为商店结果头)
#   511175 GC_OnHairShopMoneyFail: money U64 @ body+0
#   511178 GC_OnHairShopMoney    : money U64 @ body+0
#   511001 char-data-load        : money U64 @ body+44(= wire+52),登录整桶带初始金币(与坐标同一包)
#   521054 GC_OnSellItemResult_07F35E(IDA sub_8A7160@0x8A7160):★卖货真正的回流包★ 每卖一格推一包,
#          同时带【单格背包更新】+【最新金币】:body+0 bagId(slot+13)/body+4 itemId/body+8 packedCount/
#          body+12 u64 uid/body+20 u64 money。卖空时 itemId=0→清该格。bot 之前漏解析此包,导致卖货后
#          金币不刷新、要重登靠 511001 才更新——本包单独走解析分支(既刷金币又刷该格)。
SC_MONEY_UPDATE = 521191
SC_SHOP_MONEY = 511171
SC_HAIRSHOP_MONEY_FAIL = 511175
SC_HAIRSHOP_MONEY = 511178
SC_SELL_ITEM_RESULT = 521054    # 0x7F35E 卖货结果包(单格更新+金币),money U64 @ wire+28(body+20)
# 买货结果包(0x7F35C):每买一笔回一帧,结构与 521054 完全一致 ——
#   body+0 bagId(slot+13)/body+4 itemId/body+8 packedCount(该格新总数,可堆叠+1)/body+12 u64 uid/body+20 u64 money。
# 抓包实证:手动买 紅標槍(3982) x300 后回 521052 body=[173, 3982, 299, ...],299→真实 300,money=新钱包。
# bot 之前只解 521054(卖)不解 521052(买),买完背包计数不回升 → autobuy 无限买(本次修复点)。
SC_BUY_ITEM_RESULT = 521052
# 邮件结果 ack(0x7F55B):寄/收金钱邮件后服务端回流,body+8(wire+16)= 更新后的钱包 U64。
# 实抓包核对:寄 1111 后回 wire 帧 18 00 00 00 5B F5 07 00 03..00..(body+8)=0x03A9AB98=61,317,528。
# 不处理它的话钱包不刷新 → 自动邮寄会误判余额仍超阈值而反复寄,故必须当金币更新包解析。
SC_MAIL_RESULT = 521563
# proto -> 金币 U64 在 wire 帧内的字节偏移(= body 偏移 + 8)
MONEY_WIRE_OFFSETS = {
    SC_MONEY_UPDATE: 8,         # body+0
    SC_SHOP_MONEY: 28,          # body+20
    SC_HAIRSHOP_MONEY_FAIL: 8,  # body+0
    SC_HAIRSHOP_MONEY: 8,       # body+0
    SC_MAIL_RESULT: 16,         # body+8(邮件 ack 回流的新钱包)
}
SC_CHARLOAD_MONEY_WIRE_OFFSET = 52  # 511001 char-data-load: money U64 @ body+44 = wire+52

VENDOR_TOKEN = 0xFECD2408       # 摊贩开/关/买 token
SELL_TOKEN = 0xFECD2408         # 卖出 token(与开/关/买同一个 token,= -20110328 的补码;IDA+真实抓包核对修正)
WIRE_SLOT_BASE = 13             # 背包 wire slot 编码 = slotIndex + 13
INV_ENTRY_STRIDE = 16           # 整桶背包每格字节数: [u32 itemId, u32 count, u64 uniqueId]
BAG_EQUIP_SLOTS = 13            # 511591 body 头部装备格数(跳过,不计入主背包)
BAG_MAIN_SLOTS = 192            # 主背包格数(511591 整桶)
# 主背包条目在帧内起始偏移 = 帧头8 + itemCount头4 + 装备区(13*16=208) = 220
BAG_ENTRY_BASE = 8 + 4 + BAG_EQUIP_SLOTS * INV_ENTRY_STRIDE
CASH_SLOT_CAP = 80              # cash 背包格上限(511324 整桶 80 格,从帧+8 直接起条目)
STACK_MAX = 300                 # 可堆叠物品满摞上限(同 wire 满摞 299+1);搬运叠满会溢出,服务端改落新空格
SUMMON_ITEM_ID = 8036           # 摊贩呼叫卷 itemId(写死);在 cash 背包按此 id 找召唤格
SUMMON_VENDOR_ID = 2            # 摊贩呼叫卷召唤出的摊贩 vendorId(写死)
VENDOR_MAX_PER_BUY = 300        # 单个买货包(411020)数量上限,超出按此值拆成多批发包
# vendorId=2(摊贩呼叫卷召唤出的摊贩)商品表 itemId -> shopIndex(0-based)。
# shopIndex 来自游戏本地静态商店表(ShopTableManager),不在任何网络包里,纯 socket 客户端
# 无法自动获取,故按游戏内实际列表写死。买货包(411020)用 shopIndex 而非 itemId 指定买哪一项。
VENDOR2_SHOP_INDEX = {
    95: 0, 96: 1, 112: 2, 115: 3, 125: 4, 126: 5, 130: 6, 131: 7,
    1000: 8, 3676: 9, 6080: 10, 3982: 11, 3983: 12, 3984: 13, 3985: 14,
    3986: 15, 3987: 16, 3988: 17, 6834: 18, 6835: 19, 6836: 20, 6837: 21,
    6838: 22, 6839: 23, 6840: 24, 26656: 25, 26657: 26, 26658: 27,
    26731: 28, 29568: 29,
}

# 离线挂机配置字段:(配置键, 中文标签, 类型)。类型 bool/int/float 决定 GUI 控件与解析。
# 供 GUI「挂机设置」弹窗与 default_accounts_defaults() 复用,避免两处重复列举。
FISHING_ON_MARK = "✓ 开"
FISHING_OFF_MARK = "✗ 关"
FARM_FIELD_SPECS = (
    ("fishing_enabled", "启用自动钓鱼", "bool"),
    ("fishing_bait_item_id", "鱼饵(物品名或ID,按ID自动找格)", "itemid"),
    ("fishing_special_bait", "使用特殊饵(411174)", "bool"),
    ("fishing_reel_interval", "中鱼间隔(秒,建议30-45)", "float"),
    ("fishing_stall_timeout", "无鱼获超时重连(秒,0关)", "float"),
    ("autosell_enabled", "启用背包将满自动贩卖", "bool"),
    ("autosell_min_free_slots", "剩余空格≤此值即贩卖", "int"),
    ("autosell_start_slot", "贩卖起始格(0基)", "int"),
    ("autosell_end_slot", "贩卖结束格(含)", "int"),
    ("autosell_keep_item_ids", "不卖的物品(名或ID,逗号分隔)", "itemidlist"),
    ("autosell_extra_item_ids", "额外贩卖的物品(名或ID,逗号分隔;不分格全卖)", "itemidlist"),
    ("autosell_per_packet_delay", "每个卖出包间隔(秒)", "float"),
    ("autobuy_enabled", "启用鱼饵不足自动购买", "bool"),
    ("autobuy_min_bait", "鱼饵<此值即购买", "int"),
    ("autobuy_item_id", "购买鱼饵(物品名或ID,空=同钓鱼饵)", "itemid"),
    ("autobuy_count", "每次购买数量", "int"),
    ("autobuy_open_delay", "开摊贩后到下单间隔(秒)", "float"),
    ("autobuy_buy_delay", "下单后到关摊贩间隔(秒)", "float"),
    ("cash_low_stock_threshold", "Cash定时物品低库存阈值(<此值告警)", "int"),
)

PROTO_NAMES = {
    CL_LOGIN: "CL_LOGIN",
    CL_GET_CHARINFO: "CL_GET_CHARINFO",
    CL_CREATE_CHARACTER: "CL_CREATE_CHARACTER",
    CL_GAMESERVER_CONNECT: "CL_GAMESERVER_CONNECT",
    CL_LIVE: "CL_LIVE",
    CL_LOGIN_SECONDARY: "CL_LOGIN_SECONDARY",
    LC_SERVER_LIST: "LC_SERVER_LIST",
    LC_LOGIN_FAIL: "LC_LOGIN_FAIL",
    LC_CHARINFO_SUCCESS: "LC_CHARINFO_SUCCESS",
    LC_CHARCREATE_SUCCESS: "LC_CHARCREATE_SUCCESS",
    LC_GSERV_CONNECT_SUCCESS: "LC_GSERV_CONNECT_SUCCESS",
    LC_LOGIN_FAIL_ALT: "LC_LOGIN_FAIL_ALT",
    LC_LOGIN_SECONDARY: "LC_LOGIN_SECONDARY",
    CG_ENTER: "CG_ENTER",
    CG_GAME_ALIVE: "CG_GAME_ALIVE",
    CG_HEARTBEAT: "CG_HEARTBEAT",
    CG_INIT_411555: "CG_INIT_411555",
    CG_INIT_411589: "CG_INIT_411589",
    CG_INIT_411595: "CG_INIT_411595",
    CG_POST_ENTER_411570: "CG_POST_ENTER_411570",
    CG_POST_ENTER_411625: "CG_POST_ENTER_411625",
    CG_PLAY_START: "CG_PLAY_START",
    SC_LOCAL_MAP_STATE: "SC_LOCAL_MAP_STATE",
    SC_MAP_HINT: "SC_MAP_HINT",
    SC_ATTACK_CRT2TARGET_SUCC: "SC_ATTACK_CRT2TARGET_SUCC",
    SC_ATTACK_CRT2TARGET_CRIT: "SC_ATTACK_CRT2TARGET_CRIT",
    GC_CRT_LOAD: "GC_CRT_LOAD",
    GC_CRT_NEW: "GC_CRT_NEW",
    GC_CRT_REMOVE: "GC_CRT_REMOVE",
    GC_CRT_MOVE: "GC_CRT_MOVE",
    GC_CRT_DIE: "GC_CRT_DIE",
    GC_CRT_DAMAGE: "GC_CRT_DAMAGE",
    CG_FISHING_CAST: "CG_FISHING_CAST",
    CG_FISHING_CAST_SPECIAL: "CG_FISHING_CAST_SPECIAL",
    CG_FISHING_REEL: "CG_FISHING_REEL",
    CG_ACTION_CANCEL: "CG_ACTION_CANCEL",
    CG_SELL_ITEM: "CG_SELL_ITEM",
    CG_VENDOR_BUY: "CG_VENDOR_BUY",
    CG_VENDOR_OPEN: "CG_VENDOR_OPEN",
    CG_VENDOR_CLOSE: "CG_VENDOR_CLOSE",
    CG_USE_CASH_ITEM: "CG_USE_CASH_ITEM",
    CG_PLAYER_REVIVE: "CG_PLAYER_REVIVE",
    CG_SEND_MAIL: "CG_SEND_MAIL",
    SC_MAIL_RESULT: "SC_MAIL_RESULT",
    CG_OPEN_BANK: "CG_OPEN_BANK",
    CG_BANK_KEEPALIVE: "CG_BANK_KEEPALIVE",
    CG_BANK_MOVE: "CG_BANK_MOVE",
    SC_BANK_BULK: "SC_BANK_BULK",
    SC_BANK_UPDATE: "SC_BANK_UPDATE",
    SC_BAG_BULK: "SC_BAG_BULK",
    SC_CASH_BAG_BULK: "SC_CASH_BAG_BULK",
    SC_CASH_SLOT_UPDATE: "SC_CASH_SLOT_UPDATE",
    SC_BUY_ITEM_RESULT: "SC_BUY_ITEM_RESULT",
    SC_FISHING_CATCH: "SC_FISHING_CATCH",
    CG_OPEN_STREETSTALL: "CG_OPEN_STREETSTALL",
    CG_STREETSTALL_ITEMS: "CG_STREETSTALL_ITEMS",
    CG_STREETSTALL_TERMINATE: "CG_STREETSTALL_TERMINATE",
    CG_STREETSTALL_CLOSE: "CG_STREETSTALL_CLOSE",
    CG_STREETSTALL_WARP: "CG_STREETSTALL_WARP",
    SC_STREETSTALL_ITEM_UPDATE: "SC_STREETSTALL_ITEM_UPDATE",
    SC_STREETSTALL_SOLD: "SC_STREETSTALL_SOLD",
    SC_STREETSTALL_CLOSE: "SC_STREETSTALL_CLOSE",
    SC_MONEY_UPDATE: "SC_MONEY_UPDATE",
    SC_SHOP_MONEY: "SC_SHOP_MONEY",
    SC_HAIRSHOP_MONEY_FAIL: "SC_HAIRSHOP_MONEY_FAIL",
    SC_HAIRSHOP_MONEY: "SC_HAIRSHOP_MONEY",
}

GAME_LOCAL_ID_PROTOS = {
    511000,  # 0x0007CC18, movement/state rows include actor id at body[0]
    511096,  # 0x0007CC78, server id confirmation rows include actor id at body[0]
}

CHAT_LOGIN_OPCODE = 0x4100
CHAT_STATUS_OPCODE = 0x4110
CHAT_LOGIN_MAGIC = 0x0001AE1F
CHAT_STATUS_KIND = 8

LOGIN_FATAL_PROTOS = {LC_LOGIN_FAIL, LC_LOGIN_FAIL_ALT}


@dataclass
class GameServerInfo:
    host: str
    port: int
    character: str
    enter_seed: int
    current_server_type: int
    channel_id: int


@dataclass
class ClientFileInfo:
    filetime_high: int
    filetime_low: int
    size: int


@dataclass
class ChatStatus:
    player_id: int = 0
    map_id: int = 0


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def i64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<q", data, offset)[0]


def proto_name(proto: int) -> str:
    return PROTO_NAMES.get(proto, str(proto))


def short_hex(data: bytes, limit: int = 160) -> str:
    text = data[:limit].hex(" ")
    if len(data) > limit:
        text += f" ...(+{len(data) - limit} bytes)"
    return text


def parse_int_config(value: object, default: int = 0) -> int:
    if value is None:
        return default
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if not text:
        return default
    return int(text, 0)


def _coerce_int_list(value: object) -> list[int]:
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        items = value
    else:
        items = str(value).replace(";", ",").split(",")
    result: list[int] = []
    for item in items:
        text = str(item).strip()
        if not text:
            continue
        result.append(int(text, 0))
    return result


def ascii_preview(data: bytes, limit: int = 48) -> str:
    out = []
    for value in data[:limit]:
        out.append(chr(value) if 32 <= value <= 126 else ".")
    if len(data) > limit:
        out.append("...")
    return "".join(out)


def c_string(data: bytes) -> str:
    raw = data.split(b"\x00", 1)[0].rstrip(b" ")
    for enc in ("ascii", "cp950", "big5", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            pass
    return raw.hex(" ")


def fixed_bytes(text: str, size: int, encoding: str) -> bytes:
    raw = bytearray()
    for ch in text:
        part = ch.encode(encoding)
        if len(raw) + len(part) > size:
            break
        raw.extend(part)
    return bytes(raw) + b"\x00" * (size - len(raw))
