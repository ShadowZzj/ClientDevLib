#!/usr/bin/env python3
"""Minimal SO3D online login client.

This is not a log replay. It regenerates the login packet from account,
password and character parameters, then keeps the session alive with the
same lobby/bill/game alive packets used by the client.
"""

from __future__ import annotations

import argparse
import builtins
import json
import queue
import random
import re
import select
import socket
import struct
import sys
import threading
import time
import traceback
import urllib.request
import urllib.error
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    from Crypto.Cipher import Blowfish as PyCryptoBlowfish
except Exception:  # pragma: no cover - optional dependency fallback
    PyCryptoBlowfish = None

# 正式服(server_mode=prod)需要的 123.dll layer-1 解密桥;测试服不依赖它,
# 缺失也不影响现有流程。延迟到 prod 实际使用时才报错。
try:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import bridge_client
except Exception:  # pragma: no cover - bridge optional for test mode
    bridge_client = None


_PRINT_LOCK = threading.Lock()
_LOG_CONTEXT = threading.local()
_BUILTIN_PRINT = builtins.print
STOP_EVENT = threading.Event()
_SOCKET_LOCK = threading.RLock()
_ACTIVE_SOCKETS: set[socket.socket] = set()
_LOG_HANDLER = None
_STATUS_HANDLER = None
_CONTROLLER_CONTEXT = threading.local()


class AccountController:
    """Per-account session control: its own stop flag and socket set.

    Lets the GUI start/stop a single account while others keep running,
    instead of the old global STOP_EVENT that tore everything down at once.
    """

    def __init__(self, account: str, character: str) -> None:
        self.account = account
        self.character = character
        self.stop_event = threading.Event()
        self.thread: "threading.Thread | None" = None
        self._sockets: set[socket.socket] = set()
        self._lock = threading.RLock()
        # run() 线程每次上线时把当前 FarmState 挂到这里,供 GUI 线程读背包快照。
        # 读取方在 CPython 下用 dict(...) 一次性拷贝即可,无需额外锁。
        self.farm: "FarmState | None" = None
        # GUI 起账号时把该账号实时的 args Namespace 挂这里,供「挂机设置」热更新就地改写。
        # 这正是 worker 主循环每 0.25s tick 现读的同一对象(FarmState.args 也指向它),
        # 改它立即生效;account_args 跨重连复用同一对象,改动也会保留。
        self.args: "argparse.Namespace | None" = None
        # GUI 线程把手动动作放进来,worker 主循环取出后在自己线程里执行。元素可为:
        #   "sell" 整体贩卖 / "buy" 买鱼饵 / ("sell_slot", slot) 右键单格贩卖。
        # 关键:发包只能在 worker 线程做——两个线程同时 sendall 同一 game socket 会撕裂 TCP 帧。
        self.action_queue: "queue.Queue[object]" = queue.Queue()
        # 会话级累计统计:run() 每次重连都重建 FarmState(catch_total 等会清零),
        # 所以"自启动到现在"的累计挂在跨重连存活的 controller 上。worker 线程单写、
        # GUI 线程只读,CPython 下对单一写者的 int 自增安全,无需加锁。
        self.stat_start_at = time.monotonic()
        self.stat_catch_total = 0       # 累计钓获次数
        self.stat_summon_count = 0      # 召唤摊贩次数(每次消耗一张摊贩呼叫券)
        self.stat_summon_bought = 0     # 累计补货买入的摊贩呼叫券张数
        self.stat_sell_income = 0       # 累计贩卖获得的金币(卖货回包正向 delta 之和)

    def add_socket(self, sock: socket.socket) -> None:
        with self._lock:
            self._sockets.add(sock)

    def discard_socket(self, sock: socket.socket) -> None:
        with self._lock:
            self._sockets.discard(sock)

    def take_sockets(self) -> list[socket.socket]:
        with self._lock:
            sockets = list(self._sockets)
            self._sockets.clear()
        return sockets


def current_controller() -> "AccountController | None":
    return getattr(_CONTROLLER_CONTEXT, "controller", None)


def bump_controller_stat(attr: str, delta: int = 1) -> None:
    """worker 线程累加当前账号 controller 的会话统计;无 controller(如 self-test)则忽略。"""
    controller = current_controller()
    if controller is not None:
        setattr(controller, attr, getattr(controller, attr, 0) + delta)


def current_stop_event() -> threading.Event:
    controller = current_controller()
    if controller is not None:
        return controller.stop_event
    return STOP_EVENT


def app_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


APP_DIR = app_dir()


def set_log_handler(handler) -> None:
    global _LOG_HANDLER
    _LOG_HANDLER = handler


def set_status_handler(handler) -> None:
    global _STATUS_HANDLER
    _STATUS_HANDLER = handler


def report_account_status(account: str, character: str, status: str) -> None:
    handler = _STATUS_HANDLER
    if handler is None:
        return
    try:
        handler(account, character, status)
    except Exception:
        pass


def _emit_line(line: str, *, console: bool) -> None:
    with _PRINT_LOCK:
        if console:
            try:
                _BUILTIN_PRINT(line, flush=True)
            except Exception:
                pass
        handler = _LOG_HANDLER
        if handler is not None:
            try:
                handler(line)
            except Exception:
                pass


def _timestamp() -> str:
    now = time.time()
    stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now))
    return f"{stamp}.{int((now % 1) * 1000):03d}"


def _log_print(*values: object, sep: str = " ", end: str = "\n", file=None, flush: bool = False) -> None:
    if file is not None:
        _BUILTIN_PRINT(*values, sep=sep, end=end, file=file, flush=flush)
        return

    text = sep.join(str(value) for value in values)
    line = f"{_timestamp()} | {text}"
    stream = getattr(_LOG_CONTEXT, "stream", None)
    console = bool(getattr(_LOG_CONTEXT, "console", True))
    with _PRINT_LOCK:
        if stream is not None:
            stream.write(line + end)
            stream.flush()
    if console:
        _emit_line(line, console=True)


print = _log_print


def biz_log(message: str) -> None:
    """写一行干净的业务事件到独立的 <account>_<character>.biz.log。
    只进 biz 流,不进主 .log、不进 console —— 主 .log 保留全部噪音不变。
    biz_stream 未设置时(self-test / 未上线)静默跳过。"""
    stream = getattr(_LOG_CONTEXT, "biz_stream", None)
    if stream is None:
        return
    line = f"{_timestamp()} | {message}\n"
    with _PRINT_LOCK:
        try:
            stream.write(line)
            stream.flush()
        except Exception:
            pass


def main_log(message: str) -> None:
    _emit_line(f"{_timestamp()} | {message}", console=True)


def register_socket(sock: socket.socket) -> socket.socket:
    with _SOCKET_LOCK:
        _ACTIVE_SOCKETS.add(sock)
    controller = current_controller()
    if controller is not None:
        controller.add_socket(sock)
    return sock


def close_socket(sock: socket.socket | None) -> None:
    if sock is None:
        return
    with _SOCKET_LOCK:
        _ACTIVE_SOCKETS.discard(sock)
    controller = current_controller()
    if controller is not None:
        controller.discard_socket(sock)
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    try:
        sock.close()
    except OSError:
        pass


def close_active_sockets() -> None:
    with _SOCKET_LOCK:
        sockets = list(_ACTIVE_SOCKETS)
        _ACTIVE_SOCKETS.clear()
    for sock in sockets:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            sock.close()
        except OSError:
            pass


LOGIN_HOST = "165.154.194.121"
LOGIN_PORT = 1818
BILL_HOST = "123.58.197.240"
BILL_PORT = 1838
DEFAULT_GAME_HOST_MAP = {
    "127.1.5.41": "128.14.237.136",
    "127.1.5.42": "165.154.194.124",
}

# 正式服(prod):游戏端的所有连接都打到本机代理进程(它再转发到真服),
# 代理监听的环回别名固定为 127.2.57.25(取自 GGConfig/.../net/sendlog 抓包)。
# 端口对齐表(对照测试服):login 10002(测试服 1818)、bill 1838、chat 3000、
# game 1841/1842。9999(最先发的认证大包)由代理自己处理,客户端跳过,直接从
# 10002 登录。正式服不做 IP 映射(host 已是本地代理),只保留端口对齐。
# 登录 socket(10002)的 recv 是 123.dll layer-1 密文,需要先经注入进游戏进程里
# 的 GGThreadBlock LoginBridge 解密(用活连接的 session key),再走 layer-2 xor。
PROD_PROXY_HOST = "127.2.57.25"
PROD_LOGIN_PORT = 10002
PROD_BILL_PORT = 1838
PROD_CHAT_PORT = 3000

# Edit this block for normal use, then run:
#   python SO3DCheat\GGThreadBlock\tools\so3d_online_login_client.py
CONFIG = {
    "accounts_file": str(APP_DIR / "so3d_online_accounts.json"),
    "defaults_file": str(APP_DIR / "so3d_online_defaults.json"),
    "log_dir": str(APP_DIR / "so3d_online_logs"),
    "log_max_mb": 50,
    "log_max_files": 3,
    "retry_delay": 10.0,
    "max_retries": 0,  # 0 = retry forever
    "failure_window_seconds": 300.0,
    "max_failures_per_window": 10,
    "start_stagger": 2.0,
    "account": "gongyu9011212",
    "password": "901121",
    "character": "shadowsing",
    "server_id": 2,
    "channel_id": 1,
    # "test" = 直连公网真服(无 123.dll 保护,现有逻辑,默认);
    # "prod" = 全部连本地代理 PROD_PROXY_HOST,登录 socket recv 走 bridge 解密。
    "server_mode": "test",
    "login_host": LOGIN_HOST,
    "login_port": LOGIN_PORT,
    "bill_host": BILL_HOST,
    "bill_port": BILL_PORT,
    "chat_host": BILL_HOST,
    "chat_port": 3000,
    "chat_enabled": True,
    "chat_player_id": 0,  # 0 = auto-detect from early 1842 packets
    "chat_map_id": 0,  # 0 = auto-detect from 1842 map packets
    "chat_status_enabled": True,
    "chat_player_id_wait": 2.0,
    "chat_watchdog_seconds": 180.0,
    # The lobby packet may return a virtual 127.x address. Keep game_host empty
    # to resolve it through game_host_map; set game_host only for a hard override.
    "game_host": "",
    "game_host_map": DEFAULT_GAME_HOST_MAP,
    "game_port": 0,  # 0 = use the port returned by lobby
    # In the no-123.dll flow, WIRE_RECV == RECV for the lobby socket, so normal
    # XOR proto decoding is enough for 1818/10002.
    "enter_seed": "",
    "client_exe": "",  # e.g. r"G:\path\to\SO3DPlus.exe"; empty uses captured fallback values
    "encoding": "cp950",
    "xor_index": "auto",
    "timeout": 8.0,
    "run_seconds": 0.0,  # 0 = run until Ctrl+C
    "secondary_login_delay": 5.0,
    "cl_live_interval": 180.0,
    "bill_alive_interval": 870.0,
    "game_alive_interval": 60.0,
    "post_enter_init_delay": 1.0,
    "debug_packets": True,
    # ---------- 自动钓鱼 ----------
    # 开局抛一次竿(411047),之后每隔 reel_interval 秒发一个中鱼包(412048);
    # 抛竿绑定一个鱼饵格,该格被钓空(521102 扣到 0)就自动重抛切到下一个有饵的格,无需配置重抛次数。
    "fishing_enabled": False,        # 总开关
    "fishing_bait_item_id": 0,       # 鱼饵 itemId(数字),0 = 必填才生效;按 itemId 在背包里自动找格
    "fishing_special_bait": False,   # True 用 411174(特殊饵),False 用 411047
    "fishing_reel_interval": 35.0,   # 中鱼间隔(秒),建议 30-45;真实间隔会加 0-3 秒抖动
    # ---------- 背包将满自动贩卖 ----------
    "autosell_enabled": False,       # 总开关
    "autosell_min_free_slots": 5,    # 剩余空格 <= 此值即触发贩卖
    "autosell_start_slot": 0,        # 从该背包格(0 基)开始向后卖
    "autosell_end_slot": 191,        # 卖到该格为止(含),背包上限 192 格
    "autosell_keep_item_ids": [],    # 这些 itemId 不卖(鱼饵等)
    "autosell_per_packet_delay": 0.12,  # 每个卖出包之间的间隔(秒)
    # ---------- 鱼饵不足自动购买 ----------
    # 召唤摊贩用「摊贩呼叫卷」(itemId 写死 SUMMON_ITEM_ID=8036),程序在 cash 背包
    # (511324 整桶同步包)里自动找到它所在格,无需手填 cash 格号。
    "autobuy_enabled": False,        # 总开关
    "autobuy_min_bait": 50,          # 鱼饵数量 < 此值即触发购买
    "autobuy_item_id": 0,            # 购买项 itemId(数字),应等于 fishing_bait_item_id
    "autobuy_count": 300,            # 每次购买数量
    "autobuy_open_delay": 0.3,       # 开摊贩后到下购买包的间隔(秒)
    "autobuy_buy_delay": 0.15,       # 购买后到关摊贩的间隔(秒)
    # ---------- 定时使用 cash 背包物品 ----------
    # 每组 {item:物品名/ID, interval:间隔秒};到点且角色活着就扫 cash 背包找该物品发
    # 使用包(411156, body=[slot+13,0])。每角色 lastUsed 持久化到 cash_use_state.json,
    # 跨重启/重连不丢。物品名经 item_names.json 反查 itemId。
    "cash_use_schedule": [],         # 例:[{"item":"经验药水","interval":600}]
    "cash_low_stock_threshold": 10,  # 定时物品在 cash 背包 < 此值(或缺货)即在界面醒目告警
    # ---------- 自动邮寄金钱 ----------
    # 镜像 GGThreadBlock/AutoMailModule:轮询钱包,gold>threshold 就把固定额寄给收件人(411524 op0)。
    # 寄出后 521563 回流更新钱包(已并入 MONEY_WIRE_OFFSETS),余额回落后自然停寄。
    "automail_enabled": False,       # 总开关
    "automail_recipient": "",        # 收件人角色名(ASCII/Big5),空=不寄
    "automail_money_threshold": 0,   # 钱 > 此值即触发;0=不生效
    "automail_amount": 0,            # 每次寄出金额;0=不生效
    "automail_poll_interval": 10.0,  # 轮询/两封最短间隔(秒)
    "automail_whitelist": "",        # 白名单账号(逗号分隔):列表内账号不自动邮寄
    # ---------- 自动补货(泡点网店买->进bank->搬到cash) ----------
    # 每组 {item:网店itemID, threshold:cash该物品总和<此值即触发, buy:每次买几个}。
    # 默认关闭(会花泡点);开启前请先用「测试补货」按钮跑一遍验证。
    "restock_enabled": False,
    "restock_schedule": [],          # 例:[{"item":25824,"threshold":10,"buy":5}]
}

CL_LOGIN = 111000
CL_GET_CHARINFO = 111003
CL_GAMESERVER_CONNECT = 111006
CL_LIVE = 111008
CL_LOGIN_SECONDARY = 111011
LC_SERVER_LIST = 221001
LC_LOGIN_FAIL = 221002
LC_CHARINFO_SUCCESS = 221003
LC_GSERV_CONNECT_SUCCESS = 221009
LC_LOGIN_FAIL_ALT = 230010
LC_LOGIN_SECONDARY = 231002

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
# 邮寄金钱: MailBox::SendMoneyMail(proto 411524, op=0)。body 固定 77 字节(0x4D),布局经
# GGThreadBlock/CLocalPlayer.h 文档 + 实抓包逐字节核对(shadowdance 寄 1111 给 shadowsing):
#   [0..3]   const 3 / [4..19] 收件人 16B(编码后右补 0,不强制 NUL) / [20..23] op=0(金钱邮件)
#   [24..31] money int64 / [32..76] 邮件正文 45B(空则全 0)
CG_SEND_MAIL = 411524
MAIL_HEADER_CONST = 3           # body+0 固定头字段(抓包恒为 3)
MAIL_OP_MONEY = 0               # op=0 金钱邮件(op=1 物品邮件,本客户端暂不做)
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
    ("autosell_enabled", "启用背包将满自动贩卖", "bool"),
    ("autosell_min_free_slots", "剩余空格≤此值即贩卖", "int"),
    ("autosell_start_slot", "贩卖起始格(0基)", "int"),
    ("autosell_end_slot", "贩卖结束格(含)", "int"),
    ("autosell_keep_item_ids", "不卖的物品(名或ID,逗号分隔)", "itemidlist"),
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
    CL_GAMESERVER_CONNECT: "CL_GAMESERVER_CONNECT",
    CL_LIVE: "CL_LIVE",
    CL_LOGIN_SECONDARY: "CL_LOGIN_SECONDARY",
    LC_SERVER_LIST: "LC_SERVER_LIST",
    LC_LOGIN_FAIL: "LC_LOGIN_FAIL",
    LC_CHARINFO_SUCCESS: "LC_CHARINFO_SUCCESS",
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

XOR_KEYS = [
    bytes.fromhex("40 23 24 25"),
    bytes.fromhex("23 24 25 26"),
    bytes.fromhex("24 25 26 2a"),
    bytes.fromhex("25 26 2a 2b"),
    bytes.fromhex("26 2a 2b 21"),
    bytes.fromhex("2a 2b 21 40"),
    bytes.fromhex("2b 21 40 23"),
    bytes.fromhex("21 40 23 24"),
]
DEFAULT_XOR_INDEX = 7

KNOWN_ACCOUNT_XOR_INDEX = {
    # Fallback only. In normal "auto" mode the key is derived from the lobby
    # handshake for this TCP connection.
    "gongyu901121": 3,   # shadowpope sample, 2026-06-04 20:21
    "gongyu9011212": 4,  # shadowsing sample, 2026-06-04 22:27
    "gongyu9011213": 7,  # shadowdance sample, 2026-06-04 18:08
}

DATE_MAGIC = bytes.fromhex("32 30 31 37 31 31 31 37 00 00 00 00 67 5f 70 4c")
DATE_MAGIC_ENCRYPTED = bytes.fromhex("1b 35 be b7 4f 8d 24 39 10 5f 32 85 22 87 d2 2e")

# These are the observed SO3DPlus.exe values from the captured login. Passing
# --client-exe is better because the game sends the local file time/size.
FALLBACK_FILETIME_HIGH = 0x01DC3DEE
FALLBACK_FILETIME_LOW = 0x76664113
FALLBACK_CLIENT_SIZE = 0x007CC5C8

BILL_OPEN_DWORDS = (0x001AE890, 0x00B2B937, 0x00CDCEF0, 0x001AE828)

SO3D_GAME_BLOWFISH_KEY = bytes.fromhex(
    "74 6A 71 6A 73 6A 61 6E 61 6B 73 67 64 6B 64 79 "
    "21 5F 40 29 23 28 24 31 32 33 00"
)
SO3D_BLOWFISH_IV = b"\x00" * 8

CAPTURED_GAME1842_SELF_TEST = bytes.fromhex(
    "18 00 00 00 A1 45 06 00 "
    "E4 8C 74 AA 2A EB FE 5D 34 B3 B4 0C 1E DB FB 03"
)
CAPTURED_GAME1842_SELF_TEST_PROTO = 411041

IP = [
    58, 50, 42, 34, 26, 18, 10, 2,
    60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,
    64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1,
    59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,
    63, 55, 47, 39, 31, 23, 15, 7,
]
FP = [
    40, 8, 48, 16, 56, 24, 64, 32,
    39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30,
    37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28,
    35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26,
    33, 1, 41, 9, 49, 17, 57, 25,
]
EXP = [
    32, 1, 2, 3, 4, 5,
    4, 5, 6, 7, 8, 9,
    8, 9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32, 1,
]
PBOX = [
    16, 7, 20, 21, 29, 12, 28, 17,
    1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9,
    19, 13, 30, 6, 22, 11, 4, 25,
]
SBOX = [
    [14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 2, 14, 10, 0, 6, 13],
    [15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9],
    [10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12],
    [7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14],
    [2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3],
    [4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12],
    [12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13],
    [13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11],
]

K_ENC_BITS = (
    "001010010011111101010001101100110111111110111110"
    "001110110101000000011100100111110111111111010111"
    "000011000000000111111100101111111110001111110101"
    "100101100100100000111101111100111110111111000111"
    "100011110010101100100000111111101010011110011111"
    "100010100011111010101101111111110111011111001111"
    "110110010011011001001000011111101111001111101011"
    "010000001101111011101000111101101111110101101111"
    "101101001010000010110000011011111101110111011111"
    "100101100000111000110110110011111111010111111011"
    "111011100011001000010100111011111101111101101101"
    "000011101001011001101100110110101101111111111110"
    "110010100101000001111010110111111101111110111101"
    "101011001100101101101000110110110111111111111001"
    "100000100111101100001011111110111111101100111101"
    "001111100000001011000111101111111100111011111111"
)
K_ENC = [1 if ch == "1" else 0 for ch in K_ENC_BITS]


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


def proto_name(proto: int) -> str:
    return PROTO_NAMES.get(proto, str(proto))


def resolve_xor_index(account: str, requested: object) -> int | None:
    if isinstance(requested, int):
        value = requested
    else:
        text = str(requested or "auto").strip().lower()
        if text in ("", "auto"):
            return None
        else:
            value = int(text, 0)
    if not 0 <= value < len(XOR_KEYS):
        raise ValueError(f"xor_index must be 0..{len(XOR_KEYS) - 1}, got {value}")
    return value


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


def resolve_map_name(slot: object) -> str:
    """地图 SLOT(511001 wire+20)-> 规范地图 id -> 中文名;查不到退化为'地图<id>'。"""
    try:
        s = int(slot)
    except (TypeError, ValueError):
        return ""
    mid = MAP_SLOT_TO_ID[s] if 0 <= s < len(MAP_SLOT_TO_ID) else s
    name = MAP_ID_TO_NAME.get(mid, "")
    return name if name else f"地图{mid}"


def resolve_item_id(value: object, default: int = 0) -> int:
    """把配置值解析成 itemId:接受数字 / 数字串(支持 0x)/ 物品名 / '名字(123)'。
    未知名字打印告警并回退 default。"""
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
    # '名字(123)' 这种带 id 后缀的展示格式,优先取括号里的数字
    m = re.search(r"\((\d+)\)\s*$", text)
    if m:
        return int(m.group(1))
    iid = ITEM_IDS_BY_NAME.get(text)
    if iid is not None:
        return iid
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


def _as_bool(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() not in ("", "0", "false", "no", "off")
    return bool(value)


def parse_host_map(value: object) -> dict[str, str]:
    if value is None:
        return {}
    if isinstance(value, dict):
        return {
            str(src).strip(): str(dst).strip()
            for src, dst in value.items()
            if str(src).strip() and str(dst).strip()
        }

    text = str(value).strip()
    if not text:
        return {}

    result: dict[str, str] = {}
    for part in text.split(","):
        item = part.strip()
        if not item:
            continue
        if "=" not in item:
            raise ValueError(f"game host map item must be src=dst, got {item!r}")
        src, dst = item.split("=", 1)
        src = src.strip()
        dst = dst.strip()
        if src and dst:
            result[src] = dst
    return result


def sanitize_log_name(value: object) -> str:
    text = str(value or "").strip()
    text = re.sub(r"[^0-9A-Za-z._-]+", "_", text)
    text = text.strip("._-")
    return text or "account"


def config_defaults() -> dict[str, object]:
    return {
        "login_host": CONFIG["login_host"],
        "login_port": CONFIG["login_port"],
        "bill_host": CONFIG["bill_host"],
        "bill_port": CONFIG["bill_port"],
        "chat_host": CONFIG["chat_host"],
        "chat_port": CONFIG["chat_port"],
        "chat_enabled": CONFIG["chat_enabled"],
        "chat_player_id": CONFIG["chat_player_id"],
        "chat_map_id": CONFIG["chat_map_id"],
        "chat_status_enabled": CONFIG["chat_status_enabled"],
        "chat_player_id_wait": CONFIG["chat_player_id_wait"],
        "chat_watchdog_seconds": CONFIG["chat_watchdog_seconds"],
        "failure_window_seconds": CONFIG["failure_window_seconds"],
        "max_failures_per_window": CONFIG["max_failures_per_window"],
        "server_id": CONFIG["server_id"],
        "channel_id": CONFIG["channel_id"],
        "server_mode": CONFIG["server_mode"],
        "game_host": CONFIG["game_host"],
        "game_host_map": CONFIG["game_host_map"],
        "game_port": CONFIG["game_port"],
        "client_exe": CONFIG["client_exe"],
        "encoding": CONFIG["encoding"],
        "xor_index": CONFIG["xor_index"],
        "timeout": CONFIG["timeout"],
        "run_seconds": CONFIG["run_seconds"],
        "secondary_login_delay": CONFIG["secondary_login_delay"],
        "cl_live_interval": CONFIG["cl_live_interval"],
        "bill_alive_interval": CONFIG["bill_alive_interval"],
        "game_alive_interval": CONFIG["game_alive_interval"],
        "post_enter_init_delay": CONFIG["post_enter_init_delay"],
        "debug_packets": CONFIG["debug_packets"],
        "log_max_mb": CONFIG["log_max_mb"],
        "log_max_files": CONFIG["log_max_files"],
        "fishing_enabled": CONFIG["fishing_enabled"],
        "fishing_bait_item_id": CONFIG["fishing_bait_item_id"],
        "fishing_special_bait": CONFIG["fishing_special_bait"],
        "fishing_reel_interval": CONFIG["fishing_reel_interval"],
        "autosell_enabled": CONFIG["autosell_enabled"],
        "autosell_min_free_slots": CONFIG["autosell_min_free_slots"],
        "autosell_start_slot": CONFIG["autosell_start_slot"],
        "autosell_end_slot": CONFIG["autosell_end_slot"],
        "autosell_keep_item_ids": CONFIG["autosell_keep_item_ids"],
        "autosell_per_packet_delay": CONFIG["autosell_per_packet_delay"],
        "autobuy_enabled": CONFIG["autobuy_enabled"],
        "autobuy_min_bait": CONFIG["autobuy_min_bait"],
        "autobuy_item_id": CONFIG["autobuy_item_id"],
        "autobuy_count": CONFIG["autobuy_count"],
        "autobuy_open_delay": CONFIG["autobuy_open_delay"],
        "autobuy_buy_delay": CONFIG["autobuy_buy_delay"],
        "cash_use_schedule": CONFIG["cash_use_schedule"],
        "cash_low_stock_threshold": CONFIG["cash_low_stock_threshold"],
        "automail_enabled": CONFIG["automail_enabled"],
        "automail_recipient": CONFIG["automail_recipient"],
        "automail_money_threshold": CONFIG["automail_money_threshold"],
        "automail_amount": CONFIG["automail_amount"],
        "automail_poll_interval": CONFIG["automail_poll_interval"],
        "automail_whitelist": CONFIG["automail_whitelist"],
        "restock_enabled": CONFIG["restock_enabled"],
        "restock_schedule": CONFIG["restock_schedule"],
    }


def read_defaults(defaults_file: Path | None, accounts_file: Path | None, *, builtin_fallback: bool) -> dict[str, object]:
    """读取独立的全局默认配置 so3d_online_defaults.json。
    缺失时回退到旧版账号文件里内联的 defaults(迁移兼容),再不行才用内置默认(GUI)或空 dict(CLI)。
    文件内容既支持裸对象 {...},也支持旧版整体包了一层 {"defaults":{...}}。"""
    if defaults_file is not None and defaults_file.exists():
        data = json.loads(defaults_file.read_text(encoding="utf-8-sig"))
        if not isinstance(data, dict):
            raise ValueError("defaults config must be a JSON object")
        inner = data["defaults"] if isinstance(data.get("defaults"), dict) else data
        return dict(inner)
    if accounts_file is not None and accounts_file.exists():
        try:
            adata = json.loads(accounts_file.read_text(encoding="utf-8-sig"))
        except Exception:
            adata = None
        if isinstance(adata, dict) and isinstance(adata.get("defaults"), dict):
            return dict(adata["defaults"])
    return default_accounts_defaults() if builtin_fallback else {}


def load_accounts_config(path: Path, defaults_file: Path | None = None) -> tuple[dict[str, object], list[dict[str, object]]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(data, list):
        accounts = data
    elif isinstance(data, dict):
        accounts = data.get("accounts", [])
    else:
        raise ValueError("accounts config must be a JSON array or object")

    if not isinstance(accounts, list):
        raise ValueError("accounts must be an array")

    defaults = read_defaults(defaults_file, path, builtin_fallback=False)

    clean_accounts: list[dict[str, object]] = []
    for index, item in enumerate(accounts):
        if not isinstance(item, dict):
            raise ValueError(f"accounts[{index}] must be an object")
        enabled = item.get("enabled", True)
        if isinstance(enabled, str):
            enabled = enabled.strip().lower() not in ("0", "false", "no", "off")
        if not enabled:
            continue
        clean_accounts.append(dict(item))
    return defaults, clean_accounts


def namespace_from_config(values: dict[str, object]) -> argparse.Namespace:
    return argparse.Namespace(**values)


def account_namespace(base: argparse.Namespace, global_defaults: dict[str, object], account: dict[str, object]) -> argparse.Namespace:
    merged = config_defaults()
    merged.update(vars(base))
    merged.update(global_defaults)
    merged.update(account)

    for key in (
        "login_port",
        "bill_port",
        "chat_port",
        "server_id",
        "channel_id",
        "game_port",
        "log_max_files",
        "max_failures_per_window",
        "autosell_min_free_slots",
        "autosell_start_slot",
        "autosell_end_slot",
        "autobuy_min_bait",
        "autobuy_count",
        "cash_low_stock_threshold",
        "automail_money_threshold",
        "automail_amount",
    ):
        merged[key] = parse_int_config(merged.get(key), int(config_defaults()[key]))
    for key in (
        "timeout",
        "run_seconds",
        "secondary_login_delay",
        "cl_live_interval",
        "bill_alive_interval",
        "game_alive_interval",
        "post_enter_init_delay",
        "chat_player_id_wait",
        "chat_watchdog_seconds",
        "failure_window_seconds",
        "log_max_mb",
        "fishing_reel_interval",
        "autosell_per_packet_delay",
        "autobuy_open_delay",
        "autobuy_buy_delay",
        "automail_poll_interval",
    ):
        merged[key] = float(merged.get(key, config_defaults()[key]))
    for key in (
        "chat_enabled",
        "chat_status_enabled",
        "debug_packets",
        "fishing_enabled",
        "fishing_special_bait",
        "autosell_enabled",
        "autobuy_enabled",
        "automail_enabled",
        "restock_enabled",
    ):
        value = merged.get(key)
        if isinstance(value, str):
            merged[key] = value.strip().lower() not in ("0", "false", "no", "off")

    # 这三个字段支持按物品名配置(item_names.json 反查),也兼容数字 itemId
    merged["fishing_bait_item_id"] = resolve_item_id(merged.get("fishing_bait_item_id"), 0)
    merged["autobuy_item_id"] = resolve_item_id(merged.get("autobuy_item_id"), 0)
    merged["autosell_keep_item_ids"] = resolve_item_id_list(merged.get("autosell_keep_item_ids"))

    return namespace_from_config(merged)


def account_log_path(log_dir: Path, args: argparse.Namespace) -> Path:
    name = sanitize_log_name(f"{args.account}_{args.character}")
    return log_dir / f"{name}.log"


def account_biz_log_path(log_dir: Path, args: argparse.Namespace) -> Path:
    name = sanitize_log_name(f"{args.account}_{args.character}")
    return log_dir / f"{name}.biz.log"


class RotatingTextLog:
    def __init__(self, path: Path, *, max_bytes: int, max_files: int) -> None:
        self.path = path
        self.max_bytes = max(1024, int(max_bytes))
        self.max_files = max(1, int(max_files))
        self._stream = None
        self._size = 0

    def __enter__(self) -> "RotatingTextLog":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._open()
        if self._size >= self.max_bytes:
            self._rotate()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def write(self, text: str) -> int:
        if not isinstance(text, str):
            text = str(text)
        encoded_size = len(text.encode("utf-8", errors="replace"))
        if self._size > 0 and self._size + encoded_size > self.max_bytes:
            self._rotate()
        assert self._stream is not None
        written = self._stream.write(text)
        self._size += encoded_size
        return written

    def flush(self) -> None:
        if self._stream is not None:
            self._stream.flush()

    def close(self) -> None:
        if self._stream is not None:
            self._stream.close()
            self._stream = None

    def _open(self) -> None:
        self._size = self.path.stat().st_size if self.path.exists() else 0
        self._stream = self.path.open("a", encoding="utf-8", buffering=1)

    def _backup_path(self, index: int) -> Path:
        return self.path.with_name(f"{self.path.name}.{index}")

    def _rotate(self) -> None:
        self.close()
        if self.max_files <= 1:
            self.path.unlink(missing_ok=True)
            self._open()
            return

        last = self.max_files - 1
        self._backup_path(last).unlink(missing_ok=True)
        for index in range(last - 1, 0, -1):
            src = self._backup_path(index)
            if src.exists():
                src.replace(self._backup_path(index + 1))
        if self.path.exists():
            self.path.replace(self._backup_path(1))
        self._open()


def log_max_bytes(args: argparse.Namespace) -> int:
    max_mb = float(getattr(args, "log_max_mb", CONFIG["log_max_mb"]))
    return int(max_mb * 1024 * 1024)


def log_max_files(args: argparse.Namespace) -> int:
    return int(getattr(args, "log_max_files", CONFIG["log_max_files"]))


def open_account_log(log_dir: Path, args: argparse.Namespace) -> RotatingTextLog:
    return RotatingTextLog(
        account_log_path(log_dir, args),
        max_bytes=log_max_bytes(args),
        max_files=log_max_files(args),
    )


def open_account_biz_log(log_dir: Path, args: argparse.Namespace) -> RotatingTextLog:
    return RotatingTextLog(
        account_biz_log_path(log_dir, args),
        max_bytes=log_max_bytes(args),
        max_files=log_max_files(args),
    )


def run_with_log(args: argparse.Namespace, log_dir: Path, *, console: bool) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    path = account_log_path(log_dir, args)
    with open_account_log(log_dir, args) as stream, open_account_biz_log(log_dir, args) as biz_stream:
        old_stream = getattr(_LOG_CONTEXT, "stream", None)
        old_console = getattr(_LOG_CONTEXT, "console", True)
        old_biz = getattr(_LOG_CONTEXT, "biz_stream", None)
        _LOG_CONTEXT.stream = stream
        _LOG_CONTEXT.console = console
        _LOG_CONTEXT.biz_stream = biz_stream
        try:
            print(f"[log] writing {path}")
            run(args)
        finally:
            _LOG_CONTEXT.stream = old_stream
            _LOG_CONTEXT.console = old_console
            _LOG_CONTEXT.biz_stream = old_biz


def run_forever(
    args: argparse.Namespace,
    log_dir: Path,
    *,
    retry_delay: float,
    max_retries: int,
    failure_window_seconds: float,
    max_failures_per_window: int,
    console: bool,
    controller: "AccountController | None" = None,
) -> None:
    if controller is not None:
        _CONTROLLER_CONTEXT.controller = controller
    stop_event = current_stop_event()
    attempt = 0
    failure_window_seconds = max(1.0, float(failure_window_seconds or 0.0))
    max_failures_per_window = int(max_failures_per_window or 0)
    failure_window_start = time.monotonic()
    failure_count_in_window = 0
    while not stop_event.is_set():
        now = time.monotonic()
        if now - failure_window_start >= failure_window_seconds:
            failure_window_start = now
            failure_count_in_window = 0

        if max_failures_per_window > 0 and failure_count_in_window >= max_failures_per_window:
            wait_seconds = max(0.1, failure_window_seconds - (now - failure_window_start))
            main_log(
                f"[{args.account}/{args.character}] failure limit reached: "
                f"{failure_count_in_window}/{max_failures_per_window} in "
                f"{failure_window_seconds:g}s; pause {wait_seconds:.1f}s"
            )
            if stop_event.wait(wait_seconds):
                return
            failure_window_start = time.monotonic()
            failure_count_in_window = 0
            continue

        attempt += 1
        try:
            main_log(f"[{args.account}/{args.character}] 第 {attempt} 次启动")
            report_account_status(args.account, args.character, "登录中")
            run_with_log(args, log_dir, console=console)
            report_account_status(args.account, args.character, "离线")
            if args.run_seconds and args.run_seconds > 0:
                main_log(f"[{args.account}/{args.character}] 已达到运行时长，不再重试")
                return
            main_log(f"[{args.account}/{args.character}] 会话正常结束，准备重试")
        except KeyboardInterrupt:
            raise
        except Exception:
            report_account_status(args.account, args.character, "离线")
            failure_count_in_window += 1
            log_dir.mkdir(parents=True, exist_ok=True)
            path = account_log_path(log_dir, args)
            with open_account_log(log_dir, args) as stream:
                old_stream = getattr(_LOG_CONTEXT, "stream", None)
                old_console = getattr(_LOG_CONTEXT, "console", True)
                _LOG_CONTEXT.stream = stream
                _LOG_CONTEXT.console = console
                try:
                    print(f"[error] attempt {attempt} failed")
                    for line in traceback.format_exc().rstrip().splitlines():
                        print(line)
                finally:
                    _LOG_CONTEXT.stream = old_stream
                    _LOG_CONTEXT.console = old_console
            main_log(f"[{args.account}/{args.character}] 第 {attempt} 次失败，详情见 {path}")

        if max_retries > 0 and attempt >= max_retries:
            main_log(f"[{args.account}/{args.character}] 已达到最大重试次数 {max_retries}，停止")
            return
        stop_event.wait(max(0.1, retry_delay))


def stop_threads(threads: list[threading.Thread], reason: str) -> None:
    STOP_EVENT.set()
    close_active_sockets()
    main_log(reason)

    deadline = time.time() + 3.0
    for thread in threads:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        try:
            thread.join(timeout=min(0.5, remaining))
        except KeyboardInterrupt:
            close_active_sockets()
            break

    alive = [thread.name for thread in threads if thread.is_alive()]
    if alive:
        main_log("[主程序] 以下线程未等待完成，直接退出：" + ", ".join(alive))


def start_account_threads(
    args: argparse.Namespace,
    defaults: dict[str, object],
    accounts: list[dict[str, object]],
    log_dir: Path,
    *,
    console_account_logs: bool,
) -> list[threading.Thread]:
    STOP_EVENT.clear()
    close_active_sockets()
    threads: list[threading.Thread] = []
    for index, account in enumerate(accounts):
        if STOP_EVENT.is_set():
            break
        account_args = account_namespace(args, defaults, account)
        missing = [name for name in ("account", "password", "character") if not getattr(account_args, name, "")]
        if missing:
            raise ValueError(f"account #{index + 1} missing: " + ", ".join(missing))
        thread = threading.Thread(
            target=run_forever,
            args=(account_args, log_dir),
            kwargs={
                "retry_delay": float(getattr(account_args, "retry_delay", args.retry_delay)),
                "max_retries": int(getattr(account_args, "max_retries", args.max_retries)),
                "failure_window_seconds": float(
                    getattr(account_args, "failure_window_seconds", args.failure_window_seconds)
                ),
                "max_failures_per_window": int(
                    getattr(account_args, "max_failures_per_window", args.max_failures_per_window)
                ),
                "console": console_account_logs,
            },
            name=f"so3d-{account_args.account}-{account_args.character}",
            daemon=True,
        )
        thread.start()
        threads.append(thread)
        if index + 1 < len(accounts) and STOP_EVENT.wait(max(0.0, args.start_stagger)):
            break
    return threads


def wait_account_threads(threads: list[threading.Thread]) -> None:
    while any(thread.is_alive() for thread in threads):
        for thread in threads:
            thread.join(timeout=0.5)
        if STOP_EVENT.is_set():
            break


def resolve_game_host(returned_host: str, override_host: str, host_map: dict[str, str]) -> str:
    if override_host:
        print(f"[game] host override {returned_host} -> {override_host}")
        return override_host

    mapped = host_map.get(returned_host)
    if mapped:
        print(f"[game] host map {returned_host} -> {mapped}")
        return mapped

    if returned_host.startswith("127."):
        print(f"[warn] no public host mapping for virtual game host {returned_host}; connecting to it directly")
    return returned_host


def encrypted_body_size(size: int) -> int:
    # Game mode is CBC + PKCS-ish padding, and sub_5CBA50 adds one byte before
    # rounding so a naturally aligned body still gets a full padding block.
    adjusted = size + 1
    return ((adjusted + 7) // 8) * 8


def game1841_plain_body(body: bytes, tick_ms: int | None = None) -> bytes:
    tick = (int(time.monotonic() * 1000) if tick_ms is None else tick_ms) & 0xFFFFFFFF
    crc = zlib.crc32(body) & 0xFFFFFFFF
    plain_len = len(body) + 8
    padded_len = encrypted_body_size(plain_len)
    pad_value = padded_len - plain_len
    return struct.pack("<II", tick, crc) + body + bytes([pad_value]) * pad_value


def game1841_encrypt_body(body: bytes, tick_ms: int | None = None) -> bytes:
    plain = game1841_plain_body(body, tick_ms=tick_ms)
    if PyCryptoBlowfish is not None:
        cipher = PyCryptoBlowfish.new(SO3D_GAME_BLOWFISH_KEY, PyCryptoBlowfish.MODE_CBC, iv=SO3D_BLOWFISH_IV)
        return cipher.encrypt(plain)
    raise RuntimeError("pycryptodome is required for 1842 Blowfish/CBC packets; run: python -m pip install pycryptodome")


def game1841_decrypt_body(encrypted: bytes) -> bytes:
    if PyCryptoBlowfish is None:
        raise RuntimeError("pycryptodome is required for 1842 decrypt diagnostics")
    cipher = PyCryptoBlowfish.new(SO3D_GAME_BLOWFISH_KEY, PyCryptoBlowfish.MODE_CBC, iv=SO3D_BLOWFISH_IV)
    return cipher.decrypt(encrypted)


def game1841_packet(proto: int, body: bytes, tick_ms: int | None = None) -> bytes:
    encrypted = game1841_encrypt_body(body, tick_ms=tick_ms)
    return struct.pack("<II", len(encrypted) + 8, proto) + encrypted


def send_game1841(
    sock: socket.socket,
    proto: int,
    body: bytes,
    label: str | None = None,
    *,
    key_index: int,
    debug: bool,
) -> None:
    name = proto_name(proto) if label is None else label
    plain_packet = game_packet(proto, body)
    packet = game1841_packet(proto, body)
    debug_game_send(name, plain_packet, xor=False, key_index=key_index, enabled=debug)
    if debug:
        print(f"[debug][wire:{name}] len={len(packet)} raw={short_hex(packet)}")
    sock.sendall(packet)
    print(f"[game] send {name}")


def iter_framed_packets(data: bytes):
    offset = 0
    data_len = len(data)
    while offset + 8 <= data_len:
        size = u32(data, offset)
        if 8 <= size <= data_len - offset:
            frame = data[offset:offset + size]
            yield frame
            offset += size
            continue
        offset += 1


# 单帧上限:实测最大整桶包约 4112 字节,留足余量防对齐错乱时无限等待。
MAX_GAME_FRAME = 1 << 20


def take_complete_frames(buf: bytearray) -> bytes:
    """从累积缓冲里弹出所有完整帧(拼接成一段返回),把尾部不完整的帧留在 buf 里等下次 recv。
    game socket 自连接起就帧对齐,所以正常只会走"完整/等待"两条路;size 非法时滑 1 字节自愈。"""
    out = bytearray()
    offset = 0
    n = len(buf)
    while offset + 8 <= n:
        size = u32(buf, offset)
        if size < 8 or size > MAX_GAME_FRAME:
            offset += 1            # 对齐错乱,滑 1 字节重找
            continue
        if offset + size > n:
            break                  # 帧未收全,留到下次
        out += buf[offset:offset + size]
        offset += size
    del buf[:offset]
    return bytes(out)


def extract_local_player_id(data: bytes) -> int | None:
    status = extract_chat_status(data)
    if status and status.player_id:
        return status.player_id
    for frame in iter_framed_packets(data):
        if len(frame) < 12:
            continue
        proto = u32(frame, 4)
        if proto not in GAME_LOCAL_ID_PROTOS:
            continue
        candidate = u32(frame, 8)
        if 0 < candidate < 0x10000:
            return candidate
    return None


def extract_chat_status(data: bytes) -> ChatStatus | None:
    status = ChatStatus()
    for frame in iter_framed_packets(data):
        if len(frame) < 12:
            continue
        proto = u32(frame, 4)
        if proto == SC_LOCAL_MAP_STATE and len(frame) >= 24:
            player_id = u32(frame, 12)
            map_id = u32(frame, 20)
            if 0 < player_id < 0x100000:
                status.player_id = player_id
            if 0 < map_id < 0x10000:
                status.map_id = map_id
        elif proto == SC_MAP_HINT and len(frame) >= 12:
            map_id = u32(frame, 8)
            if 0 < map_id < 0x10000:
                status.map_id = map_id
    if status.player_id or status.map_id:
        return status
    return None


def merge_chat_status(current: ChatStatus, chunk: bytes, *, label: str = "") -> bool:
    found = extract_chat_status(chunk)
    if not found:
        return False
    changed = False
    if found.player_id and found.player_id != current.player_id:
        current.player_id = found.player_id
        changed = True
    if found.map_id and found.map_id != current.map_id:
        current.map_id = found.map_id
        changed = True
    if changed:
        suffix = f" from {label}" if label else ""
        print(f"[chat] status source{suffix}: player_id=0x{current.player_id:08X} map_id={current.map_id}")
    return changed


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
        # 账号共享仓库(bank)快照(511320 整桶):唯一实例id -> (模板itemId, count)
        self.bank: dict[int, tuple[int, int]] = {}
        self.bank_observed = False
        # 自动补货状态机:idle -> waiting(已发购买+开 bank,等物品到 bank 再搬进 cash)
        self.restock_phase = "idle"
        self.restock_item = 0          # 目标模板itemId(=网店itemID)
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
        # 当前 HP / 最大 HP(511001 char-load wire+88=当前HP,登录时当前=最大;MP 在 wire+92)
        self.hp = 0
        self.max_hp = 0
        self.mp = 0
        self.hp_seen = False
        # 所在地图 SLOT(511001 wire+20 = g_localMapId,需经 g_MapIdTable 转规范 id 再查名)
        self.map_slot = -1
        self.map_seen = False
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
        self.next_automail_at = 0.0
        self.warn_coord_at = 0.0
        self.warn_bait_at = 0.0
        self.warn_summon_at = 0.0


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
            s = str(it).strip()
            if not s or "=" not in s:
                continue
            name, _, raw = s.partition("=")
            name = name.strip()
            raw = raw.strip()
        try:
            interval = int(float(raw))
        except (TypeError, ValueError):
            continue
        if name and interval > 0:
            out.append((name, interval))
    return out


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
            # 当前 HP(wire+88,登录时当前=最大)/ MP(wire+92);max HP 是派生属性,
            # 登录瞬间被 CalcStatus 置成与当前相等,headless 钓鱼 bot 不打怪,直接拿这值当上限。
            if len(frame) >= 96:
                hp = u32(frame, 88)
                state.hp = hp
                state.max_hp = hp
                state.mp = u32(frame, 92)
                state.hp_seen = True
            # 同一包(char-data-load)在 body+44 带初始金币
            if len(frame) >= SC_CHARLOAD_MONEY_WIRE_OFFSET + 8:
                _record_gold(state, u64(frame, SC_CHARLOAD_MONEY_WIRE_OFFSET), "char-load")
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


# __FARM_SEND_PLACEHOLDER__


def farm_send(state: FarmState, sock: socket.socket, proto: int, body: bytes, label: str | None = None) -> None:
    args = state.args
    send_game1841(sock, proto, body, label, key_index=args._key_index, debug=bool(args.debug_packets))


def fishing_send_cast(state: FarmState, sock: socket.socket, bait_slot: int, actor_id: int) -> None:
    proto = CG_FISHING_CAST_SPECIAL if state.args.fishing_special_bait else CG_FISHING_CAST
    wire_slot = bait_slot + WIRE_SLOT_BASE
    body = struct.pack("<IIII", wire_slot, actor_id, state.coord_x, state.coord_y)
    farm_send(state, sock, proto, body, "CG_FISHING_CAST")


def fishing_send_reel(state: FarmState, sock: socket.socket) -> None:
    farm_send(state, sock, CG_FISHING_REEL, struct.pack("<I", 0), "CG_FISHING_REEL")


def fishing_send_stop(state: FarmState, sock: socket.socket) -> None:
    farm_send(state, sock, CG_ACTION_CANCEL, struct.pack("<I", 1), "CG_ACTION_CANCEL")


def send_revive_to_town(state: FarmState, sock: socket.socket) -> None:
    """回城复活:发 CG_PLAYER_REVIVE(412017),body=单 DWORD reviveMode=1
    (对齐 GGThreadBlock::ReviveToTown / 玩家手动点死亡弹框「复活」)。"""
    farm_send(state, sock, CG_PLAYER_REVIVE, struct.pack("<I", REVIVE_MODE_DEFAULT), "CG_PLAYER_REVIVE")
    biz_log("回城复活(手动)")


def fishing_schedule_next(state: FarmState, now: float) -> None:
    # 对齐 AutoFishingModule::ScheduleNext:间隔 + 0~3 秒抖动。
    interval = max(5.0, float(state.args.fishing_reel_interval))
    state.fish_next = now + interval + random.uniform(0.0, 3.0)


def fishing_do_cast(state: FarmState, sock: socket.socket, actor_id: int, now: float) -> bool:
    bait_slot = resolve_bait_slot(state)
    if bait_slot is None:
        if now >= state.warn_bait_at:
            print(f"[钓鱼] 找不到鱼饵(物品={item_label(state.args.fishing_bait_item_id)}),暂停抛竿")
            state.warn_bait_at = now + 5.0
        state.fish_next = now + 2.0
        return False
    # 先取消当前动作再抛竿(AutoFishingModule::SendCast 同序)。
    fishing_send_stop(state, sock)
    time.sleep(0.05)
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
    以及 item_names.json 标记 sellable=false 的物品。不做 enabled/节流/空格门槛
    (那些在 autosell_tick);手动触发直接调本函数。
    贩卖必须在摊贩开着时发包,否则服务端 RST;manage_vendor=True 时本函数自行
    召唤+开摊贩、卖完再关(真实抓包就是 开→卖→买→关 一个会话)。返回卖出格数。"""
    args = state.args
    keep = set(args.autosell_keep_item_ids or [])
    # 鱼饵是用来钓鱼/刚买进来的,绝不能卖掉(钓鱼饵 + autobuy 买的都算)
    for bait in (args.fishing_bait_item_id, args.autobuy_item_id):
        if bait:
            keep.add(bait)
    lo = max(0, args.autosell_start_slot)
    hi = min(BAG_MAIN_SLOTS - 1, args.autosell_end_slot)

    # 先盘点这一轮要卖哪些格,跳过保留 ID 和不可贩卖物品(开摊贩前算好,避免空开一轮摊贩)
    targets: list[tuple[int, int, int]] = []
    for slot in range(lo, hi + 1):
        item_id, count = state.bag.get(slot, (0, 0))
        if item_id == 0 or count <= 0:
            continue
        if item_id in keep:
            print(f"[贩卖]({reason})跳过保留物品 格={slot} 物品={item_label(item_id)} 数量={count}")
            continue
        if not item_sellable(item_id):
            print(f"[贩卖]({reason})跳过不可贩卖 格={slot} 物品={item_label(item_id)} 数量={count}")
            continue
        targets.append((slot, item_id, count))

    if not targets:
        print(f"[贩卖]({reason})范围[{lo}-{hi}]内无可卖物品(已跳过保留/不可贩卖)")
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


# ---------- 自动补货:泡点网店买 -> 进 bank -> 搬到 cash ----------
PAODIAN_BASE = "https://shop2.guguseal.com"
RESTOCK_WAIT_TIMEOUT = 30.0     # 买完等物品到 bank 并搬完的总超时(秒)
RESTOCK_REOPEN_INTERVAL = 3.0   # waiting 期间每隔多久重发一次开 bank(等到账)
RESTOCK_COOLDOWN = 60.0         # 两轮补货最短间隔(秒),防止失控连买
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


def paodian_shop_item(item_id: int) -> "tuple[str, object] | None":
    """返回网店 itemID 的 (name, bubble_price);/api/webshop-data 缓存 5 分钟。买货包要带这两项。"""
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
    return items.get(int(item_id))


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


def cash_dest_slot(state: FarmState, item_id: int) -> "int | None":
    """挑搬运目标 cash 格:优先已有同物品的格(堆叠),否则第一个空格;满返回 None。"""
    for slot, (iid, cnt) in sorted(state.cash_bag.items()):
        if iid == item_id and cnt > 0:
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
    dest = cash_dest_slot(state, item_id)
    if dest is None:
        print(f"[补货]({reason})cash 背包没有空格,无法搬运 {item_label(item_id)}")
        return 0
    move_n = min(count, have) if have else count
    if move_n <= 0:
        return 0
    farm_send(state, sock, CG_BANK_MOVE,
              struct.pack("<IIII", dest + WIRE_SLOT_BASE, uid, 0, move_n), "CG_BANK_MOVE")
    prev = state.cash_bag.get(dest, (0, 0))
    base = prev[1] if prev[0] == item_id else 0
    state.cash_bag[dest] = (item_id, base + move_n)
    state.cash_observed = True
    state.bank.pop(uid, None)
    print(f"[补货]({reason})搬运 {item_label(item_id)} x{move_n}  bank(uid={uid}) -> cash格{dest}")
    biz_log(f"补货搬运 {item_label(item_id)} x{move_n}")
    return move_n


def parse_restock_schedule(value: object) -> list[tuple[str, int, int]]:
    """解析自动补货表 -> [(物品名或ID文本, cash阈值, 每次买数量)]。接受:
      - list[dict] {"item","threshold","buy"}
      - list[str]/多行字符串,每行 '物品名=阈值:数量'(cash 该物品总和<阈值 就买数量个)
    物品保留为文本(名字/ID/'名字(123)'),由调用方用 resolve_item_id 转成网店itemID。
    丢弃物品为空或阈值/数量<=0 的项。"""
    out: list[tuple[str, int, int]] = []
    if not value:
        return out
    items = value if isinstance(value, (list, tuple)) else str(value).splitlines()
    for it in items:
        try:
            if isinstance(it, dict):
                item = str(it.get("item", "")).strip(); thr = int(it.get("threshold")); buy = int(it.get("buy"))
            else:
                s = str(it).strip()
                if not s or "=" not in s or ":" not in s:
                    continue
                left, _, right = s.partition("=")
                thr_s, _, buy_s = right.partition(":")
                item = left.strip(); thr = int(thr_s.strip()); buy = int(buy_s.strip())
        except (TypeError, ValueError):
            continue
        if item and thr > 0 and buy > 0:
            out.append((item, thr, buy))
    return out


def restock_kick(state: FarmState, sock: socket.socket, item_id: int, buy_count: int, *, reason: str) -> bool:
    """启动一次补货:先泡点购买(HTTP 阻塞),成功则开 bank 并进 waiting,
    后续 tick 由 restock_advance 等物品到 bank 再搬进 cash。"""
    args = state.args
    user = str(getattr(args, "account", "") or "")
    pw = str(getattr(args, "password", "") or "")
    if not user or not pw:
        print("[补货]缺少账号/密码,无法泡点购买")
        return False
    try:
        msg = paodian_purchase(user, pw, item_id, buy_count)
    except Exception as exc:
        print(f"[补货]({reason})泡点购买失败 itemID={item_id}:{exc}")
        biz_log(f"补货购买失败 {item_label(item_id)}:{exc}")
        return False
    print(f"[补货]({reason})泡点购买 {item_label(item_id)} x{buy_count}:{msg}")
    biz_log(f"泡点购买 {item_label(item_id)} x{buy_count}")
    bank_open(state, sock)
    now = time.monotonic()
    state.restock_phase = "waiting"
    state.restock_item = int(item_id)
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
        biz_log(f"补货超时 {item_label(state.restock_item)}(还差 {state.restock_target})")
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
    for item_text, threshold, buy in schedule:
        item_id = resolve_item_id(item_text, 0)   # 名字/ID -> 网店itemID(查 item_names.json)
        if not item_id:
            continue
        total = cash_count_item(state.cash_bag, item_id)
        if total < threshold:
            state.next_restock_at = now + RESTOCK_COOLDOWN   # 真触发了用更长冷却
            restock_kick(state, sock, item_id, buy, reason=f"{item_text} cash{total}<{threshold}")
            break   # 一次只补一种,下一轮再看其它


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


def bytes_to_bits(block: bytes) -> list[int]:
    bits: list[int] = []
    for value in block:
        bits.extend((value >> shift) & 1 for shift in range(7, -1, -1))
    return bits


def bits_to_bytes(bits: list[int]) -> bytes:
    out = bytearray()
    for i in range(0, len(bits), 8):
        value = 0
        for bit in bits[i:i + 8]:
            value = (value << 1) | int(bit)
        out.append(value)
    return bytes(out)


def feistel_bits(right: list[int], round_no: int) -> list[int]:
    expanded = [right[i - 1] for i in EXP]
    mixed = [expanded[i] ^ K_ENC[round_no * 48 + i] for i in range(48)]
    sbox_bits: list[int] = []
    for box in range(8):
        base = box * 6
        index = (
            32 * mixed[base]
            + 16 * mixed[base + 5]
            + 8 * mixed[base + 1]
            + 4 * mixed[base + 2]
            + 2 * mixed[base + 3]
            + mixed[base + 4]
        )
        value = SBOX[box][index]
        sbox_bits.extend([(value >> 3) & 1, (value >> 2) & 1, (value >> 1) & 1, value & 1])
    return [sbox_bits[i - 1] for i in PBOX]


def encrypt_block(block: bytes) -> bytes:
    bits = bytes_to_bits(block)
    permuted = [bits[i - 1] for i in IP]
    left = permuted[:32]
    right = permuted[32:]

    for round_no in range(16):
        old_right = right[:]
        pbox = feistel_bits(right, round_no)
        right = [pbox[i] ^ left[i] for i in range(32)]
        left = old_right

    # This matches sub_5CC2E0's final stack layout after sub_5CCB60.
    preoutput = left + right
    return bits_to_bytes([preoutput[i - 1] for i in FP])


def decrypt_block(block: bytes) -> bytes:
    bits = bytes_to_bits(block)
    permuted = [bits[i - 1] for i in IP]
    left = permuted[:32]
    right = permuted[32:]

    for round_no in range(15, -1, -1):
        pbox = feistel_bits(left, round_no)
        previous_left = [right[i] ^ pbox[i] for i in range(32)]
        previous_right = left
        left = previous_left
        right = previous_right

    preoutput = left + right
    return bits_to_bytes([preoutput[i - 1] for i in FP])


def game_des_encrypt(data: bytes) -> bytes:
    out = bytearray()
    full = (len(data) // 8) * 8
    for offset in range(0, full, 8):
        out.extend(encrypt_block(data[offset:offset + 8]))
    out.extend(data[full:])
    return bytes(out)


def game_des_decrypt(data: bytes) -> bytes:
    out = bytearray()
    full = (len(data) // 8) * 8
    for offset in range(0, full, 8):
        out.extend(decrypt_block(data[offset:offset + 8]))
    out.extend(data[full:])
    return bytes(out)


def handshake_xor_index(raw: bytes) -> tuple[int, bytes]:
    if len(raw) < 12:
        raise ValueError(f"handshake packet too short: {len(raw)}")
    decoded = game_des_decrypt(raw[4:12])
    marker = u32(decoded, 0)
    key_seed = u32(decoded, 4)
    low_parity = sum((key_seed >> bit) & 1 for bit in range(0, 9)) & 1
    high_parity = sum((key_seed >> bit) & 1 for bit in range(16, 24)) & 1
    if low_parity != ((key_seed >> 24) & 1) or high_parity != ((key_seed >> 28) & 1):
        raise ValueError(f"bad lobby handshake key checksum: {decoded.hex(' ')}")
    key_index = (key_seed >> 25) & 7
    if key_index == 0:
        key_index = DEFAULT_XOR_INDEX
    if not 0 <= key_index < len(XOR_KEYS):
        raise ValueError(f"bad lobby handshake key index {key_index}")
    return key_index, decoded


def xor_payload(packet: bytes, key_index: int = DEFAULT_XOR_INDEX) -> bytes:
    key = XOR_KEYS[key_index]
    data = bytearray(packet)
    for offset in range(4, len(data)):
        data[offset] ^= key[(offset - 4) % 4]
    return bytes(data)


def game_packet(proto: int, body: bytes = b"", *, xor: bool = False, key_index: int = DEFAULT_XOR_INDEX) -> bytes:
    packet = struct.pack("<II", len(body) + 8, proto) + body
    return xor_payload(packet, key_index) if xor else packet


def debug_game_send(label: str, packet: bytes, *, xor: bool, key_index: int, enabled: bool) -> None:
    if not enabled:
        return
    decoded = xor_payload(packet, key_index) if xor else packet
    proto = u32(decoded, 4) if len(decoded) >= 8 else 0
    print(f"[debug][send:{label}] len={len(packet)} proto={proto_name(proto)} xor={xor}")
    print(f"[debug][send:{label}] raw={short_hex(packet)}")
    if xor:
        print(f"[debug][send:{label}] dec={short_hex(decoded)}")


def debug_game_recv(label: str, raw: bytes, decoded: bytes, *, key_index: int, enabled: bool) -> None:
    if not enabled:
        return
    proto = u32(decoded, 4) if len(decoded) >= 8 else 0
    print(f"[debug][recv:{label}] len={len(raw)} proto={proto_name(proto)} key={key_index}")
    print(f"[debug][recv:{label}] raw={short_hex(raw)}")
    print(f"[debug][recv:{label}] dec={short_hex(decoded)}")
    if proto not in PROTO_NAMES:
        known_candidates: list[str] = []
        print(f"[debug][recv:{label}] ascii={ascii_preview(decoded)}")
        for i in range(len(XOR_KEYS)):
            cand = xor_payload(raw, i)
            if len(cand) >= 8:
                cand_proto = u32(cand, 4)
                first_body_u32 = u32(cand, 8) if len(cand) >= 12 else 0
                name = proto_name(cand_proto)
                if cand_proto in PROTO_NAMES:
                    known_candidates.append(f"key={i} {name}")
                print(
                    f"[debug][recv:{label}] xor-candidate key={i} "
                    f"proto={name} firstBody=0x{first_body_u32:08X} "
                    f"body={short_hex(cand[8:40], 32)}"
                )
        if not known_candidates:
            print(
                f"[debug][recv:{label}] no XOR key produced a known SO3D proto; "
                "the SO3D client dispatcher would not handle this as a normal game packet"
            )
        else:
            print(f"[debug][recv:{label}] known-candidates {'; '.join(known_candidates)}")


def login_packet(account: str, password: str, encoding: str, key_index: int) -> bytes:
    account_block = fixed_bytes(account, 16, encoding)
    password_block = fixed_bytes(password, 16, encoding)
    body = (
        struct.pack("<I", 0)
        + game_des_encrypt(account_block)
        + game_des_encrypt(password_block)
        + game_des_encrypt(DATE_MAGIC)
    )
    return game_packet(CL_LOGIN, body, xor=True, key_index=key_index)


def login_secondary_packet(account: str, password: str, encoding: str, key_index: int) -> bytes:
    body = fixed_bytes(account, 16, encoding) + fixed_bytes(password, 16, encoding)
    return game_packet(CL_LOGIN_SECONDARY, body, xor=True, key_index=key_index)


def read_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("socket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def read_game_packet(sock: socket.socket, *, xor: bool, key_index: int, timeout: float, bridge=None) -> tuple[bytes, bytes]:
    old_timeout = sock.gettimeout()
    sock.settimeout(timeout)
    try:
        header = read_exact(sock, 4)
        size = u32(header, 0)
        if size < 4 or size > 0x4000:
            raise ValueError(f"bad packet size {size}")
        raw = header + read_exact(sock, size - 4)
    finally:
        sock.settimeout(old_timeout)
    # 正式服:从代理收到的 login(10002)wire,只有 body>0xC(总 len>12)的包带 123.dll
    # layer-1 密文;len=12 的 handshake/心跳类没有 layer-1。对带 layer-1 的包先经 bridge
    # 用游戏进程的活 session key 原地解密,得到与测试服等价的明文 frame,再走 layer-2 xor。
    if bridge is not None and len(raw) > 12:
        raw = bridge.decrypt_wire(raw)
    decoded = xor_payload(raw, key_index) if xor and len(raw) > 4 else raw
    return raw, decoded


def read_bill_packet(sock: socket.socket, timeout: float) -> bytes:
    old_timeout = sock.gettimeout()
    sock.settimeout(timeout)
    try:
        header = read_exact(sock, 2)
        size = struct.unpack("<H", header)[0]
        if size < 2 or size > 0x1000:
            raise ValueError(f"bad bill packet size {size}")
        return header + read_exact(sock, size - 2)
    finally:
        sock.settimeout(old_timeout)


def try_read_bill_packet(sock: socket.socket, timeout: float, label: str) -> bytes | None:
    try:
        data = read_bill_packet(sock, timeout)
        print(f"[bill] recv {label} {data.hex(' ')}")
        return data
    except Exception as exc:
        print(f"[bill] recv {label} skipped: {exc}")
        return None


def drain_socket(sock: socket.socket, label: str, *, enabled: bool, max_reads: int = 16) -> list[bytes]:
    stop_event = current_stop_event()
    chunks: list[bytes] = []
    for _ in range(max_reads):
        if stop_event.is_set():
            return chunks
        if sock.fileno() < 0:
            if stop_event.is_set():
                return chunks
            raise ConnectionError(f"{label} socket closed")
        try:
            readable, _writable, _errored = select.select([sock], [], [], 0)
        except ValueError as exc:
            if stop_event.is_set():
                return chunks
            raise ConnectionError(f"{label} socket closed") from exc
        if not readable:
            return chunks
        data = sock.recv(65536)
        if not data:
            raise ConnectionError(f"{label} socket closed")
        chunks.append(data)
        if not enabled:
            continue

        details = ""
        if len(data) >= 8:
            details = f" firstLen={u32(data, 0)} firstProto={proto_name(u32(data, 4))}"
        elif len(data) >= 4:
            details = f" firstLen={u32(data, 0)}"
        print(f"[debug][recv:{label}] chunk-len={len(data)}{details}")
        print(f"[debug][recv:{label}] raw={short_hex(data)}")
        print(f"[debug][recv:{label}] ascii={ascii_preview(data)}")
    return chunks


def recv_until_proto(
    sock: socket.socket,
    proto: int,
    *,
    key_index: int,
    timeout: float,
    debug: bool,
    fatal_protos: set[int] | None = None,
    return_protos: set[int] | None = None,
    bridge=None,
) -> bytes:
    deadline = time.monotonic() + timeout
    seen: list[int] = []
    fatal_protos = set() if fatal_protos is None else fatal_protos
    return_protos = set() if return_protos is None else return_protos
    while True:
        remaining = max(0.2, deadline - time.monotonic())
        try:
            raw, decoded = read_game_packet(sock, xor=True, key_index=key_index, timeout=remaining, bridge=bridge)
        except TimeoutError:
            suffix = ", ".join(proto_name(x) for x in seen[-8:]) or "none"
            extra = ""
            if seen and all(value not in PROTO_NAMES for value in seen):
                extra = "; all seen packets failed SO3D XOR proto decode, not a normal SO3D client-dispatch packet"
            raise TimeoutError(f"timed out waiting for proto {proto_name(proto)}; seen={suffix}{extra}") from None
        except ConnectionError as exc:
            suffix = ", ".join(proto_name(x) for x in seen[-8:]) or "none"
            raise ConnectionError(
                f"socket closed while waiting for proto {proto_name(proto)}; seen={suffix}"
            ) from exc
        debug_game_recv(proto_name(proto), raw, decoded, key_index=key_index, enabled=debug)
        if len(decoded) >= 8:
            got_proto = u32(decoded, 4)
            # layer-2 XOR key fallback:正式服里某些回包(如 LC_LOGIN_SECONDARY)用的 layer-2
            # key index 与 handshake 协商出来的不同。固定 key 解出的 proto 不是已知 SO3D 协议时,
            # 在已解过 layer-1 的 raw 上逐个试其它 key,命中已知 proto 就采用该包(不改全局 key_index)。
            if got_proto not in PROTO_NAMES and len(raw) > 4:
                for alt in range(len(XOR_KEYS)):
                    if alt == key_index:
                        continue
                    alt_decoded = xor_payload(raw, alt)
                    alt_proto = u32(alt_decoded, 4) if len(alt_decoded) >= 8 else None
                    if alt_proto in PROTO_NAMES:
                        if debug:
                            print(f"[10002] layer-2 key fallback {key_index}->{alt} "
                                  f"proto={proto_name(alt_proto)}")
                        decoded = alt_decoded
                        got_proto = alt_proto
                        break
            seen.append(got_proto)
            print(f"[10002] recv proto={proto_name(got_proto)} len={len(decoded)}")
            if got_proto == proto:
                return decoded
            if got_proto in return_protos:
                return decoded
            if got_proto in fatal_protos:
                fail_code = u32(decoded, 8) if len(decoded) >= 12 else None
                code_text = f" code={fail_code}" if fail_code is not None else ""
                raise RuntimeError(
                    f"received {proto_name(got_proto)}{code_text} while waiting for {proto_name(proto)}"
                )
        if time.monotonic() >= deadline:
            suffix = ", ".join(proto_name(x) for x in seen[-8:]) or "none"
            raise TimeoutError(f"timed out waiting for proto {proto_name(proto)}; seen={suffix}")


def send_bill(sock: socket.socket, data: bytes, label: str) -> None:
    sock.sendall(data)
    if len(data) >= 6:
        print(f"[bill] send {label} opcode={struct.unpack_from('<I', data, 2)[0]} len={len(data)}")
    else:
        print(f"[bill] send {label} len={len(data)}")


def chat_login_packet(account: str, password: str, character: str, encoding: str, player_id: int) -> bytes:
    return struct.pack(
        "<HI17s17s17sIII",
        69,
        CHAT_LOGIN_OPCODE,
        fixed_bytes(account, 17, encoding),
        fixed_bytes(password, 17, encoding),
        fixed_bytes(character, 17, encoding),
        1,
        CHAT_LOGIN_MAGIC,
        player_id & 0xFFFFFFFF,
    )


def chat_status_packet(character: str, encoding: str, map_id: int, player_id: int, extra_id: int = 0) -> bytes:
    return struct.pack(
        "<HIII16sxII",
        39,
        CHAT_STATUS_OPCODE,
        CHAT_STATUS_KIND,
        map_id & 0xFFFFFFFF,
        fixed_bytes(character, 16, encoding),
        extra_id & 0xFFFFFFFF,
        player_id & 0xFFFFFFFF,
    )


def send_chat(sock: socket.socket, data: bytes, label: str) -> None:
    sock.sendall(data)
    opcode = struct.unpack_from("<I", data, 2)[0] if len(data) >= 6 else 0
    print(f"[chat] send {label} opcode=0x{opcode:08X} len={len(data)} raw={short_hex(data)}")


def send_chat_status_if_ready(
    sock: socket.socket | None,
    status: ChatStatus,
    character: str,
    encoding: str,
    last_sent: tuple[int, int] | None,
) -> tuple[int, int] | None:
    if sock is None or not status.player_id or not status.map_id:
        return last_sent
    current = (status.map_id, status.player_id)
    if current == last_sent:
        return last_sent
    send_chat(
        sock,
        chat_status_packet(character, encoding, status.map_id, status.player_id),
        f"map-status map={status.map_id} player=0x{status.player_id:08X}",
    )
    return current


def bill_op1(account: str, encoding: str) -> bytes:
    return struct.pack("<HI16sIIII", 38, 1, fixed_bytes(account, 16, encoding), *BILL_OPEN_DWORDS)


def bill_account_op(opcode: int, account: str, encoding: str) -> bytes:
    return struct.pack("<HI16s", 22, opcode, fixed_bytes(account, 16, encoding))


def bill_u32_op(opcode: int, value: int) -> bytes:
    return struct.pack("<HII", 10, opcode, value)


def get_client_file_info(path: Path | None) -> ClientFileInfo:
    if path and path.exists():
        stat = path.stat()
        filetime = int(stat.st_mtime_ns // 100 + 116444736000000000)
        return ClientFileInfo((filetime >> 32) & 0xFFFFFFFF, filetime & 0xFFFFFFFF, stat.st_size)
    return ClientFileInfo(FALLBACK_FILETIME_HIGH, FALLBACK_FILETIME_LOW, FALLBACK_CLIENT_SIZE)


def parse_game_server_info(decoded: bytes) -> GameServerInfo:
    body = decoded[8:]
    if len(body) < 52:
        raise ValueError(f"LC_GSERV_CONNECT_SUCCESS body too short: {len(body)}")
    character = c_string(body[4:20])
    host = c_string(body[20:36])
    return GameServerInfo(
        host=host,
        port=u32(body, 36),
        character=character,
        enter_seed=u32(body, 40),
        current_server_type=u32(body, 44),
        channel_id=u32(body, 48),
    )


def parse_charinfo(decoded: bytes) -> tuple[int, str]:
    """Return (character_count, first_character_name) from LC_CHARINFO_SUCCESS.

    Body layout after the 8-byte len+proto header: server(u32), count(u32),
    flag(u32), then the first character's 16-byte name. This mirrors the client
    handler (sub_8CDD00), which reads the count at body+4 (g_LobbyCharacterSize)
    and the first name at body+12.
    """
    body = decoded[8:]
    if len(body) < 28:
        return 0, ""
    count = u32(body, 4)
    first_name = c_string(body[12:28])
    return count, first_name


def cg_enter_packet(character: str, encoding: str, server: GameServerInfo, file_info: ClientFileInfo) -> bytes:
    return game_packet(CG_ENTER, cg_enter_body(character, encoding, server, file_info))


def cg_enter_body(character: str, encoding: str, server: GameServerInfo, file_info: ClientFileInfo) -> bytes:
    return struct.pack(
        "<IIIIIII16s",
        server.enter_seed,
        file_info.filetime_high,
        file_info.filetime_low,
        file_info.size,
        0,
        0,
        0,
        fixed_bytes(character, 16, encoding),
    )


def socket_connect(host: str, port: int, timeout: float) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    register_socket(sock)
    print(f"[net] connected {host}:{port}")
    return sock


def setup_server_mode(args: argparse.Namespace):
    """正式服(prod)前置:对齐到本地代理的 host/端口,关闭 IP 映射,并连接一个
    就绪的 123.dll 解密 bridge。返回 bridge(prod)或 None(test)。

    prod 探测不到可用 bridge 时抛 BridgeUnavailable —— 调用方据此中止本次登录。"""
    mode = str(getattr(args, "server_mode", "test") or "test").strip().lower()
    if mode != "prod":
        return None
    if bridge_client is None:
        raise RuntimeError("正式服模式需要 bridge_client 模块,但未能导入")

    # 端口对齐到本地代理(代理再转发到真服)。host 已是本地代理,不做 IP 映射。
    args.login_host = PROD_PROXY_HOST
    args.login_port = PROD_LOGIN_PORT
    args.bill_host = PROD_PROXY_HOST
    args.bill_port = PROD_BILL_PORT
    args.chat_host = PROD_PROXY_HOST
    args.chat_port = PROD_CHAT_PORT
    # 游戏服地址由 lobby 下发,prod 下也指向本地代理:固定 host,清空虚拟->公网映射。
    args.game_host = PROD_PROXY_HOST
    args.game_host_map = {}

    bridge = bridge_client.connect_ready_bridge()
    print(f"[prod] 已连接 123.dll 解密 bridge (pid={bridge.pid}); 代理 host={PROD_PROXY_HOST} "
          f"login={args.login_port} bill={args.bill_port} chat={args.chat_port}")
    return bridge


def run(args: argparse.Namespace) -> None:
    encoding = args.encoding
    configured_key_index = resolve_xor_index(args.account, args.xor_index)
    key_index = configured_key_index
    debug = bool(args.debug_packets)
    login_bridge = setup_server_mode(args)
    client_info = get_client_file_info(Path(args.client_exe) if args.client_exe else None)
    print(f"[client] account={args.account!r} character={args.character!r}")
    print(
        "[client] file info "
        f"high=0x{client_info.filetime_high:08X} low=0x{client_info.filetime_low:08X} "
        f"size=0x{client_info.size:08X}"
    )

    login = socket_connect(args.login_host, args.login_port, args.timeout)
    try:
        try:
            raw, decoded = read_game_packet(login, xor=False, key_index=DEFAULT_XOR_INDEX, timeout=2.0, bridge=login_bridge)
            handshake_key_index, handshake_decoded = handshake_xor_index(raw)
            if key_index is None:
                key_index = handshake_key_index
            debug_game_recv("handshake", raw, decoded, key_index=key_index, enabled=debug)
            if debug:
                print(f"[debug][recv:handshake] des-dec={short_hex(handshake_decoded)}")
            print(f"[10002] handshake xor key index={handshake_key_index} key={XOR_KEYS[handshake_key_index].hex(' ')}")
            print(f"[10002] initial handshake len={len(raw)}")
        except Exception as exc:
            print(f"[10002] no initial handshake before login: {exc}")
            if key_index is None:
                key_index = KNOWN_ACCOUNT_XOR_INDEX.get(args.account, DEFAULT_XOR_INDEX)
                print(f"[10002] fallback xor key index={key_index} key={XOR_KEYS[key_index].hex(' ')}")

        if key_index is None:
            key_index = DEFAULT_XOR_INDEX
        print(f"[client] active xor key index={key_index} key={XOR_KEYS[key_index].hex(' ')}")

        packet = login_packet(args.account, args.password, encoding, key_index)
        debug_game_send("CL_LOGIN", packet, xor=True, key_index=key_index, enabled=debug)
        login.sendall(packet)
        print("[10002] send CL_LOGIN")
        first_login_reply = recv_until_proto(
            login,
            LC_SERVER_LIST,
            key_index=key_index,
            timeout=args.timeout,
            debug=debug,
            fatal_protos=LOGIN_FATAL_PROTOS,
            return_protos={LC_LOGIN_SECONDARY},
            bridge=login_bridge,
        )
        if len(first_login_reply) >= 8 and u32(first_login_reply, 4) == LC_LOGIN_SECONDARY:
            if args.secondary_login_delay > 0:
                print(f"[10002] wait {args.secondary_login_delay:g}s before CL_LOGIN_SECONDARY")
                time.sleep(args.secondary_login_delay)
            packet = login_secondary_packet(args.account, args.password, encoding, key_index)
            debug_game_send("CL_LOGIN_SECONDARY", packet, xor=True, key_index=key_index, enabled=debug)
            login.sendall(packet)
            print("[10002] send CL_LOGIN_SECONDARY")

            raw, decoded = read_game_packet(login, xor=False, key_index=key_index, timeout=args.timeout, bridge=login_bridge)
            handshake_key_index, handshake_decoded = handshake_xor_index(raw)
            key_index = handshake_key_index
            debug_game_recv("secondary-handshake", raw, decoded, key_index=key_index, enabled=debug)
            if debug:
                print(f"[debug][recv:secondary-handshake] des-dec={short_hex(handshake_decoded)}")
            print(
                f"[10002] secondary handshake xor key index={key_index} "
                f"key={XOR_KEYS[key_index].hex(' ')}"
            )

            packet = login_packet(args.account, args.password, encoding, key_index)
            debug_game_send("CL_LOGIN", packet, xor=True, key_index=key_index, enabled=debug)
            login.sendall(packet)
            print("[10002] send CL_LOGIN after secondary handshake")
            recv_until_proto(
                login,
                LC_SERVER_LIST,
                key_index=key_index,
                timeout=args.timeout,
                debug=debug,
                fatal_protos=LOGIN_FATAL_PROTOS,
                bridge=login_bridge,
            )

        bill = socket_connect(args.bill_host, args.bill_port, args.timeout)
        try:
            send_bill(bill, bill_op1(args.account, encoding), "open")
            try_read_bill_packet(bill, min(args.timeout, 3.0), "open-ack")
            send_bill(bill, bill_account_op(2, args.account, encoding), "account")
            send_bill(bill, struct.pack("<HI", 6, 3), "ready")
            for index in range(2):
                if try_read_bill_packet(bill, min(args.timeout, 3.0), f"post-ready-{index + 1}") is None:
                    break

            packet = game_packet(CL_GET_CHARINFO, struct.pack("<II", args.server_id, args.channel_id), xor=True, key_index=key_index)
            debug_game_send("CL_GET_CHARINFO", packet, xor=True, key_index=key_index, enabled=debug)
            login.sendall(packet)
            print(f"[10002] send CL_GET_CHARINFO server={args.server_id} channel={args.channel_id}")
            charinfo = recv_until_proto(login, LC_CHARINFO_SUCCESS, key_index=key_index, timeout=args.timeout, debug=debug, bridge=login_bridge)
            char_count, first_char_name = parse_charinfo(charinfo)
            character = args.character
            if char_count == 1 and first_char_name:
                if first_char_name != character:
                    print(
                        f"[char] account has a single character {first_char_name!r}; "
                        f"using it instead of configured {character!r}"
                    )
                character = first_char_name
            else:
                print(f"[char] account has {char_count} character(s); using configured character {character!r}")
                if fixed_bytes(character, 16, encoding).rstrip(b"\0") not in charinfo:
                    print(f"[warn] character {character!r} was not found plainly in LC_CHARINFO_SUCCESS")

            send_bill(bill, bill_u32_op(4, args.server_id), "select-server")
            # 1838 计费/认证连接只在登录-选服阶段用;握手包发完即关,进世界后不再持有、
            # 不 drain、不发 alive 保活(否则空闲会被对端关闭→误判整轮重连)。
            close_socket(bill)
            bill = None

            packet = game_packet(
                CL_GAMESERVER_CONNECT,
                struct.pack("<II16s", args.server_id, args.channel_id, fixed_bytes(character, 16, encoding)),
                xor=True,
                key_index=key_index,
            )
            debug_game_send("CL_GAMESERVER_CONNECT", packet, xor=True, key_index=key_index, enabled=debug)
            login.sendall(packet)
            print(f"[10002] send CL_GAMESERVER_CONNECT character={character!r}")
            gs_packet = recv_until_proto(login, LC_GSERV_CONNECT_SUCCESS, key_index=key_index, timeout=args.timeout, debug=debug, bridge=login_bridge)
            game_server = parse_game_server_info(gs_packet)
            print(
                "[game] lobby returned "
                f"{game_server.host}:{game_server.port} character={game_server.character!r} "
                f"seed=0x{game_server.enter_seed:08X}"
            )
            game_host_map = parse_host_map(args.game_host_map)
            game_server.host = resolve_game_host(game_server.host, str(args.game_host or "").strip(), game_host_map)
            if args.game_port:
                game_server.port = args.game_port
            print(
                "[game] effective server "
                f"{game_server.host}:{game_server.port} character={game_server.character!r} "
                f"seed=0x{game_server.enter_seed:08X}"
            )

            game = socket_connect(game_server.host, game_server.port, args.timeout)
            chat: socket.socket | None = None
            # 游戏 socket 跨 recv 重组缓冲。整桶背包帧(511591≈4112B / 511324)在 CG_ENTER 后的入场
            # 初始化阶段就被下发,那段 drain 只喂 chat_status 会把整桶包丢掉;这里从连接起就累积所有
            # game chunk,等 farm 建好后先 update_farm_state 一次,再交给主循环继续消费。
            game_buf = bytearray()
            try:
                play_body = struct.pack("<I", CG_PLAY_START)
                send_game1841(game, CG_PLAY_START, play_body, key_index=key_index, debug=debug)

                enter_body = cg_enter_body(character, encoding, game_server, client_info)
                send_game1841(game, CG_ENTER, enter_body, key_index=key_index, debug=debug)

                chat_status = ChatStatus(
                    player_id=parse_int_config(args.chat_player_id, 0),
                    map_id=parse_int_config(args.chat_map_id, 0),
                )
                player_id = chat_status.player_id
                if args.post_enter_init_delay > 0:
                    deadline = time.monotonic() + args.post_enter_init_delay
                    while time.monotonic() < deadline:
                        for chunk in drain_socket(game, "game", enabled=debug):
                            game_buf.extend(chunk)
                            if merge_chat_status(chat_status, chunk, label="post-enter"):
                                player_id = chat_status.player_id or player_id
                            found_id = extract_local_player_id(chunk)
                            if found_id and not player_id:
                                player_id = found_id
                                chat_status.player_id = found_id
                                print(f"[game] detected local player id=0x{player_id:08X}")
                        time.sleep(0.05)

                send_game1841(
                    game,
                    CG_INIT_411555,
                    struct.pack("<HII", 0, 0xFFFFFFFF, 0xFFFFFFFF),
                    key_index=key_index,
                    debug=debug,
                )
                send_game1841(
                    game,
                    CG_INIT_411589,
                    struct.pack("<III", 0, 0xFFFFFFFF, 0xFFFFFFFF),
                    key_index=key_index,
                    debug=debug,
                )
                send_game1841(
                    game,
                    CG_INIT_411595,
                    struct.pack("<III", 0, 0xFFFFFFFF, 0xFFFFFFFF),
                    key_index=key_index,
                    debug=debug,
                )
                time.sleep(0.02)
                send_game1841(game, CG_PLAY_START, play_body, "CG_PLAY_START_2", key_index=key_index, debug=debug)
                time.sleep(0.02)
                send_game1841(
                    game,
                    CG_POST_ENTER_411570,
                    struct.pack("<I", 4),
                    key_index=key_index,
                    debug=debug,
                )

                if args.chat_enabled:
                    if not player_id and args.chat_player_id_wait > 0:
                        deadline = time.monotonic() + args.chat_player_id_wait
                        while not player_id and time.monotonic() < deadline:
                            for chunk in drain_socket(game, "game", enabled=debug):
                                game_buf.extend(chunk)
                                if merge_chat_status(chat_status, chunk, label="chat-wait"):
                                    player_id = chat_status.player_id or player_id
                                    break
                                found_id = extract_local_player_id(chunk)
                                if found_id:
                                    player_id = found_id
                                    chat_status.player_id = found_id
                                    print(f"[game] detected local player id=0x{player_id:08X}")
                                    break
                            if not player_id:
                                time.sleep(0.05)
                    if not player_id:
                        print("[warn] local player id was not detected; chat login will use id=0")
                    chat = socket_connect(args.chat_host, args.chat_port, args.timeout)
                    send_chat(
                        chat,
                        chat_login_packet(args.account, args.password, character, encoding, player_id),
                        "login",
                    )
                    initial_chat_chunks = drain_socket(chat, "chat", enabled=debug)
                    last_chat_rx = time.monotonic()
                    if initial_chat_chunks:
                        print(f"[chat] received {len(initial_chat_chunks)} initial packet chunk(s)")
                else:
                    last_chat_rx = time.monotonic()
                last_chat_status: tuple[int, int] | None = None
                if args.chat_status_enabled:
                    # The real client sends 0x4110 from the map-load path. This
                    # standalone client opens the chat socket slightly later, so
                    # replay the already observed map state once the socket exists.
                    last_chat_status = send_chat_status_if_ready(
                        chat,
                        chat_status,
                        character,
                        encoding,
                        last_chat_status,
                    )

                time.sleep(0.25)
                send_game1841(
                    game,
                    CG_POST_ENTER_411625,
                    struct.pack("<I", 0),
                    key_index=key_index,
                    debug=debug,
                )

                next_cl_live = time.monotonic() + args.cl_live_interval
                next_game_alive = time.monotonic() + args.game_alive_interval
                stop_at = time.monotonic() + args.run_seconds if args.run_seconds > 0 else None
                print("[loop] online simulation is running; Ctrl+C to stop")
                report_account_status(args.account, args.character, "在线")
                main_log(f"[{args.account}/{args.character}] 登录成功，已上线")
                stop_event = current_stop_event()
                args._key_index = key_index
                farm = FarmState(args)
                farm.real_character = game_server.character   # 服务器回显的真实登录角色
                biz_log(f"上线 {args.account}/{args.character}")
                # 先把入场阶段(post-enter/chat-wait)攒下的整桶背包帧消化掉,否则它们已被 drain 走、
                # 主循环再也收不到,GUI「查看背包」就会空。
                enter_framed = take_complete_frames(game_buf)
                if enter_framed:
                    update_farm_state(farm, enter_framed)
                _controller = current_controller()
                if _controller is not None:
                    _controller.farm = farm
                if (getattr(args, "fishing_enabled", False)
                        or getattr(args, "autosell_enabled", False)
                        or getattr(args, "autobuy_enabled", False)):
                    print(f"[挂机] 钓鱼={args.fishing_enabled} 贩卖={args.autosell_enabled} "
                          f"购买={args.autobuy_enabled}")
                while True:
                    if stop_event.is_set():
                        print("[loop] stop requested; closing session")
                        biz_log(f"下线 {args.account}/{args.character}")
                        break
                    now = time.monotonic()
                    if stop_at is not None and now >= stop_at:
                        print(f"[loop] run_seconds reached ({args.run_seconds:g}s); stopping")
                        biz_log(f"下线 {args.account}/{args.character}(到时)")
                        break
                    drain_socket(login, "lobby", enabled=debug)
                    for chunk in drain_socket(game, "game", enabled=debug):
                        game_buf.extend(chunk)
                    framed = take_complete_frames(game_buf)
                    if framed:
                        update_farm_state(farm, framed)
                        if merge_chat_status(chat_status, framed, label="loop"):
                            player_id = chat_status.player_id or player_id
                            last_chat_status = send_chat_status_if_ready(
                                chat,
                                chat_status,
                                character,
                                encoding,
                                last_chat_status,
                            )
                    if chat is not None:
                        chat_chunks = drain_socket(chat, "chat", enabled=debug)
                        if chat_chunks:
                            last_chat_rx = now
                        chat_watchdog_seconds = float(getattr(args, "chat_watchdog_seconds", 0.0) or 0.0)
                        if chat_watchdog_seconds > 0 and now - last_chat_rx > chat_watchdog_seconds:
                            raise ConnectionError(
                                f"chat socket stale for {now - last_chat_rx:.1f}s "
                                f"(watchdog={chat_watchdog_seconds:g}s)"
                            )
                    if now >= next_cl_live:
                        packet = game_packet(CL_LIVE, struct.pack("<I", CL_LIVE), xor=True, key_index=key_index)
                        debug_game_send("CL_LIVE", packet, xor=True, key_index=key_index, enabled=debug)
                        login.sendall(packet)
                        print("[10002] send CL_LIVE")
                        next_cl_live = now + args.cl_live_interval
                    if now >= next_game_alive:
                        # 真人客户端在 1842 上发的保活是 CG_HEARTBEAT(411013)、body 8 字节全 0。
                        send_game1841(game, CG_HEARTBEAT, b"\x00" * 8, key_index=key_index, debug=debug)
                        next_game_alive = now + args.game_alive_interval
                    # GUI 手动动作(卖货/买鱼饵):必须在 worker 线程发包,执行后背包变化由同步包回流。
                    if _controller is not None:
                        while True:
                            try:
                                action = _controller.action_queue.get_nowait()
                            except queue.Empty:
                                break
                            if action == "sell":
                                autosell_run(farm, game, reason="手动")
                            elif action == "buy":
                                autobuy_run(farm, game, reason="手动")
                            elif isinstance(action, tuple) and action and action[0] == "buy":
                                autobuy_run(farm, game, reason="手动", count=int(action[1]))
                            elif action == "revive":
                                send_revive_to_town(farm, game)
                            elif isinstance(action, tuple) and action and action[0] == "sell_slot":
                                sell_single_slot(farm, game, int(action[1]), reason="手动单格")
                            elif isinstance(action, tuple) and action and action[0] == "mail":
                                send_money_mail(farm, game, str(action[1]), int(action[2]), reason="手动测试")
                            elif isinstance(action, tuple) and action and action[0] == "restock":
                                restock_kick(farm, game, int(action[1]), int(action[2]), reason="手动测试")
                    # 购买后延迟汇报:无条件检查(手动买/自动买都要报),不受 autobuy_enabled 影响。
                    # 等 521052 买货结果包被主循环 update_farm_state 处理后,记一行背包鱼饵总数+金钱。
                    if farm.autobuy_log_at and now >= farm.autobuy_log_at:
                        _item = farm.autobuy_log_item
                        biz_log(f"购买后 {item_label(_item)} 背包共 {bag_count_item(farm, _item)} 个，当前金钱 {farm.gold:,}")
                        farm.autobuy_log_at = 0.0
                    # 先贩卖再购买:背包将满时若先买,find_buy_target_slot 没空格会买不进去;
                    # 先卖腾出格子(autosell_run 卖后会就地清空 state.bag 对应格),再买货才有落点。
                    if args.autosell_enabled:
                        autosell_tick(farm, game, now)
                    if args.autobuy_enabled:
                        autobuy_tick(farm, game, now)
                    if args.fishing_enabled:
                        fishing_tick(farm, game, player_id, now)
                    cash_use_tick(farm, game, now)
                    automail_tick(farm, game, now)
                    restock_tick(farm, game, now)
                    time.sleep(0.25)
            finally:
                if chat is not None:
                    close_socket(chat)
                close_socket(game)
        finally:
            if bill is not None:
                close_socket(bill)
    finally:
        close_socket(login)
        if login_bridge is not None:
            login_bridge.close()


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


def default_accounts_defaults() -> dict[str, object]:
    keys = (
        "server_id",
        "channel_id",
        "debug_packets",
        "log_max_mb",
        "log_max_files",
        "retry_delay",
        "max_retries",
        "run_seconds",
        "chat_enabled",
        "chat_status_enabled",
        "encoding",
        "game_host_map",
        "cash_use_schedule",
        "automail_enabled",
        "automail_recipient",
        "automail_money_threshold",
        "automail_amount",
        "automail_poll_interval",
        "automail_whitelist",
        "restock_enabled",
        "restock_schedule",
    )
    result = {key: CONFIG[key] for key in keys}
    for key, _label, _kind in FARM_FIELD_SPECS:
        result[key] = CONFIG[key]
    return result


def load_accounts_document(path: Path, defaults_file: Path | None = None) -> tuple[dict[str, object], list[dict[str, object]]]:
    if not path.exists():
        defaults = read_defaults(defaults_file, None, builtin_fallback=True)
        return defaults, [
            {
                "account": CONFIG["account"],
                "password": CONFIG["password"],
                "character": CONFIG["character"],
            }
        ]

    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(data, list):
        accounts = data
    elif isinstance(data, dict):
        accounts = data.get("accounts", [])
    else:
        raise ValueError("accounts config must be a JSON array or object")

    defaults = read_defaults(defaults_file, path, builtin_fallback=True)

    clean_accounts: list[dict[str, object]] = []
    if isinstance(accounts, list):
        for item in accounts:
            if isinstance(item, dict):
                clean_accounts.append(dict(item))
    return defaults, clean_accounts


def save_accounts_document(path: Path, accounts: list[dict[str, object]]) -> None:
    """只写账号列表(含密码),全局默认配置另存于 so3d_online_defaults.json。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {"accounts": accounts}
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def save_defaults_document(path: Path, defaults: dict[str, object]) -> None:
    """写独立的全局默认配置文件(打包时随 exe 分发)。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(defaults, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class OnlineLoginGui:
    def __init__(self, root, args: argparse.Namespace) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = root
        self.args = args
        self.accounts_file = Path(args.accounts_file)
        self.defaults_file = Path(args.defaults_file)
        self.log_dir = Path(args.log_dir)
        self.log_queue: queue.Queue[str] = queue.Queue()
        self.status_queue: queue.Queue[tuple[str, str, str]] = queue.Queue()
        # One controller per running account row, keyed by its tree iid. This is
        # only ever touched on the Tk thread, so no extra lock is needed.
        self.controllers: dict[str, AccountController] = {}

        set_log_handler(self.log_queue.put)
        set_status_handler(
            lambda account, character, status: self.status_queue.put((account, character, status))
        )
        self.root.title("SO3D 在线登录")
        self.root.geometry("900x620")
        self.root.minsize(780, 520)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        self.defaults: dict[str, object] = {}
        self.build_ui()
        self.reload_accounts_from_json()
        self.drain_logs()

    def build_ui(self) -> None:
        tk = self.tk
        ttk = self.ttk

        outer = ttk.Frame(self.root, padding=10)
        outer.pack(fill=tk.BOTH, expand=True)

        list_frame = ttk.LabelFrame(outer, text="账号列表")
        list_frame.pack(fill=tk.BOTH, expand=False)

        columns = ("account", "password", "character", "online", "fishing", "hp", "location", "cash_warn", "real_char")
        self.tree = ttk.Treeview(list_frame, columns=columns, show="headings", height=8)
        self.tree.heading("account", text="账号")
        self.tree.heading("password", text="密码")
        self.tree.heading("character", text="角色名(配置)")
        self.tree.heading("online", text="在线状态")
        self.tree.heading("fishing", text="自动钓鱼")
        self.tree.heading("hp", text="当前HP/最大HP")
        self.tree.heading("location", text="所在位置")
        self.tree.heading("cash_warn", text="Cash定时")
        self.tree.heading("real_char", text="登录角色")
        self.tree.column("account", width=150, anchor=tk.W)
        self.tree.column("password", width=140, anchor=tk.W)
        self.tree.column("character", width=130, anchor=tk.W)
        self.tree.column("online", width=90, anchor=tk.CENTER)
        self.tree.column("fishing", width=90, anchor=tk.CENTER)
        self.tree.column("hp", width=120, anchor=tk.CENTER)
        self.tree.column("location", width=120, anchor=tk.CENTER)
        self.tree.column("cash_warn", width=150, anchor=tk.W)
        self.tree.column("real_char", width=130, anchor=tk.W)
        self.tree.tag_configure("online", foreground="#1a7f37")
        self.tree.tag_configure("offline", foreground="#999999")
        self.tree.tag_configure("connecting", foreground="#b8860b")
        # cash 定时物品缺货/低库存:红底深红字,盖过在线/离线配色,醒目
        self.tree.tag_configure("cash_alert", background="#ffd6d6", foreground="#b00020")
        self.tree.bind("<Button-1>", self.on_tree_click)
        self.tree.bind("<Button-3>", self.on_tree_right_click)
        self.tree.bind("<<TreeviewSelect>>", self.on_tree_select)
        self.row_menu = tk.Menu(self.tree, tearoff=0)
        self.row_menu.add_command(label="启动该账号", command=self.start_selected)
        self.row_menu.add_command(label="停止该账号", command=self.stop_selected)
        self.row_menu.add_separator()
        self.row_menu.add_command(label="回城复活", command=self.revive_selected)
        scroll = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.grid(row=0, column=0, sticky="nsew")
        scroll.grid(row=0, column=1, sticky="ns")
        list_frame.columnconfigure(0, weight=1)
        list_frame.rowconfigure(0, weight=1)

        detail_frame = ttk.LabelFrame(outer, text="选中账号统计(本次会话)")
        detail_frame.pack(fill=tk.X, pady=(8, 0))
        self.detail_var = tk.StringVar(value="（在上方列表选中一个账号查看统计）")
        ttk.Label(detail_frame, textvariable=self.detail_var, justify=tk.LEFT,
                  anchor=tk.W, padding=(8, 6)).pack(fill=tk.X)

        edit = ttk.Frame(outer)
        edit.pack(fill=tk.X, pady=(8, 6))
        self.account_var = tk.StringVar()
        self.password_var = tk.StringVar()
        self.character_var = tk.StringVar()
        ttk.Label(edit, text="账号").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(edit, textvariable=self.account_var, width=20).grid(row=1, column=0, sticky="ew", padx=(0, 8))
        ttk.Label(edit, text="密码").grid(row=0, column=1, sticky=tk.W)
        ttk.Entry(edit, textvariable=self.password_var, width=20).grid(row=1, column=1, sticky="ew", padx=(0, 8))
        ttk.Label(edit, text="角色名").grid(row=0, column=2, sticky=tk.W)
        ttk.Entry(edit, textvariable=self.character_var, width=20).grid(row=1, column=2, sticky="ew", padx=(0, 8))
        self.add_button = ttk.Button(edit, text="添加", command=self.add_account)
        self.add_button.grid(row=1, column=3, padx=(0, 6))
        self.delete_button = ttk.Button(edit, text="删除选中", command=self.delete_selected)
        self.delete_button.grid(row=1, column=4, padx=(0, 6))
        self.start_button = ttk.Button(edit, text="全部启动", command=self.start)
        self.start_button.grid(row=1, column=5, padx=(0, 6))
        self.stop_button = ttk.Button(edit, text="全部停止", command=self.stop)
        self.stop_button.grid(row=1, column=6, padx=(0, 6))
        self.start_sel_button = ttk.Button(edit, text="启动选中", command=self.start_selected)
        self.start_sel_button.grid(row=1, column=7, padx=(0, 6))
        self.stop_sel_button = ttk.Button(edit, text="停止选中", command=self.stop_selected)
        self.stop_sel_button.grid(row=1, column=8, padx=(0, 6))
        self.farm_button = ttk.Button(edit, text="挂机设置", command=self.open_farm_settings)
        self.farm_button.grid(row=1, column=9, padx=(0, 6))
        self.bag_button = ttk.Button(edit, text="查看背包", command=self.open_bag_view)
        self.bag_button.grid(row=1, column=10)

        # 服务器模式:test=直连公网真服(现有逻辑); prod=连本地代理 + 123.dll bridge 解密。
        mode_init = str(getattr(self.args, "server_mode", "test") or "test").strip().lower()
        if mode_init not in ("test", "prod"):
            mode_init = "test"
        self.server_mode_var = tk.StringVar(value="正式服" if mode_init == "prod" else "测试服")
        ttk.Label(edit, text="服务器").grid(row=2, column=0, sticky=tk.W, pady=(6, 0))
        self.server_mode_combo = ttk.Combobox(
            edit, textvariable=self.server_mode_var, state="readonly",
            values=("测试服", "正式服"), width=8,
        )
        self.server_mode_combo.grid(row=2, column=1, sticky=tk.W, pady=(6, 0))
        self.server_mode_combo.bind("<<ComboboxSelected>>", self.on_server_mode_change)
        # 初始化一次,保证 self.args.server_mode 与下拉一致
        self.args.server_mode = mode_init

        for index in range(3):
            edit.columnconfigure(index, weight=1)

        log_frame = ttk.LabelFrame(outer, text="日志")
        log_frame.pack(fill=tk.BOTH, expand=True)
        self.log_text = tk.Text(log_frame, height=18, wrap=tk.NONE, state=tk.DISABLED)
        yscroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        xscroll = ttk.Scrollbar(log_frame, orient=tk.HORIZONTAL, command=self.log_text.xview)
        self.log_text.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

    def insert_account(self, account: dict[str, object]) -> str:
        fishing_on = _as_bool(account.get("fishing_enabled", False))
        return self.tree.insert(
            "",
            self.tk.END,
            values=(
                str(account.get("account") or ""),
                str(account.get("password") or ""),
                str(account.get("character") or ""),
                "",
                FISHING_ON_MARK if fishing_on else FISHING_OFF_MARK,
                "",
                "",
                "",
                "",
            ),
            tags=("offline",),
        )

    def clear_accounts(self) -> None:
        for iid in self.tree.get_children():
            self.tree.delete(iid)

    def reload_accounts_from_json(self) -> list[dict[str, object]]:
        self.defaults, accounts = load_accounts_document(self.accounts_file, self.defaults_file)
        self.clear_accounts()
        for account in accounts:
            self.insert_account(account)
        return accounts

    def save_accounts_from_tree(self) -> None:
        save_accounts_document(self.accounts_file, self.account_rows())
        main_log(f"[界面] 已保存账号配置到 {self.accounts_file}")

    def save_defaults_to_file(self) -> None:
        save_defaults_document(self.defaults_file, self.defaults)
        main_log(f"[界面] 已保存挂机默认配置到 {self.defaults_file}")

    def account_rows(self) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for iid in self.tree.get_children():
            values = self.tree.item(iid, "values")
            account, password, character = values[0], values[1], values[2]
            fishing_on = len(values) >= 5 and values[4] == FISHING_ON_MARK
            rows.append({
                "account": account,
                "password": password,
                "character": character,
                "fishing_enabled": fishing_on,
            })
        return rows

    def on_tree_click(self, event) -> None:
        # 仅「自动钓鱼」列(#5)的单击切换勾选,其它列保持默认选中行为。
        if self.tree.identify_region(event.x, event.y) != "cell":
            return
        if self.tree.identify_column(event.x) != "#5":
            return
        iid = self.tree.identify_row(event.y)
        if not iid:
            return
        values = list(self.tree.item(iid, "values"))
        while len(values) < 5:
            values.append("")
        values[4] = FISHING_OFF_MARK if values[4] == FISHING_ON_MARK else FISHING_ON_MARK
        self.tree.item(iid, values=values)
        self.save_accounts_from_tree()
        if iid in self.controllers:
            main_log("[界面] 自动钓鱼开关已改，对该账号重启后生效")

    def add_account(self) -> None:
        account = self.account_var.get().strip()
        password = self.password_var.get().strip()
        character = self.character_var.get().strip()
        if not account or not password or not character:
            main_log("[界面] 账号、密码、角色名不能为空")
            return
        row = {"account": account, "password": password, "character": character}
        iid = self.insert_account(row)
        self.account_var.set("")
        self.password_var.set("")
        self.character_var.set("")
        self.save_accounts_from_tree()
        # New accounts auto-connect right away instead of waiting for a restart.
        self.start_account_row(iid)

    def delete_selected(self) -> None:
        for iid in self.tree.selection():
            # Deleting a row auto-disconnects its session if it is running.
            self.stop_account_row(iid)
            self.tree.delete(iid)
        self.save_accounts_from_tree()

    def current_server_mode(self) -> str:
        return "prod" if self.server_mode_var.get().strip() == "正式服" else "test"

    def on_server_mode_change(self, _event=None) -> None:
        mode = self.current_server_mode()
        self.args.server_mode = mode
        if mode == "prod":
            main_log("[界面] 已切换到正式服(连本地代理 127.2.57.25 + 123.dll bridge 解密)")
            # 切换即时探测一次,给用户提前反馈(不阻止切换)
            ok, detail = self._probe_prod_bridge()
            if ok:
                main_log(f"[界面] 检测到可用 bridge:{detail}")
            else:
                main_log(f"[界面] 警告:暂未检测到可用 bridge({detail});启动正式服前请先登录一个游戏进程")
        else:
            main_log("[界面] 已切换到测试服(直连公网真服)")

    def _probe_prod_bridge(self) -> tuple[bool, str]:
        """探测是否有就绪的 123.dll 解密 bridge。返回 (ok, 描述)。"""
        if bridge_client is None:
            return False, "bridge_client 模块未导入"
        try:
            bridge = bridge_client.connect_ready_bridge()
        except Exception as exc:
            return False, str(exc)
        pid = bridge.pid
        bridge.close()
        return True, f"pid={pid}"

    def _ensure_prod_bridge_or_warn(self) -> bool:
        """正式服启动前置检查:无可用 bridge 则弹窗提示并返回 False(中止本次登录)。"""
        if self.current_server_mode() != "prod":
            return True
        ok, detail = self._probe_prod_bridge()
        if ok:
            return True
        from tkinter import messagebox
        messagebox.showwarning(
            "正式服无法登录",
            "未检测到可用的 123.dll 解密 bridge,无法登录正式服。\n\n"
            "请先启动并登录一个注入了 GGThreadBlock 的游戏进程,\n"
            "进入游戏后再点击启动。\n\n"
            f"详情:{detail}",
        )
        main_log(f"[界面] 正式服启动中止:{detail}")
        return False

    def build_account_args(self, iid: str) -> argparse.Namespace:
        values = self.tree.item(iid, "values")
        account = {"account": values[0], "password": values[1], "character": values[2]}
        # 「自动钓鱼」列(values[4])是该账号开关的真实来源,必须并进 account dict,
        # 否则 account_namespace 里它会被全局默认 fishing_enabled 盖掉(行内关了仍会钓鱼)。
        if len(values) >= 5:
            account["fishing_enabled"] = values[4] == FISHING_ON_MARK
        # GUI 下拉的服务器模式优先于 defaults.json,强制写进 account 覆盖层。
        account["server_mode"] = self.current_server_mode()
        account_args = account_namespace(self.args, self.defaults, account)
        missing = [name for name in ("account", "password", "character") if not getattr(account_args, name, "")]
        if missing:
            raise ValueError("缺少字段：" + ", ".join(missing))
        return account_args

    def apply_farm_settings_to_running(self) -> int:
        """把刚改的全局挂机设置就地写进每个在线账号的实时 args。worker 主循环每 0.25s tick
        现读 args.fishing_enabled/autosell_*/autobuy_* 等,故无需重启即生效;account_args 跨
        重连复用同一对象,改动也会保留。用 build_account_args 重新合并,保证账号级覆盖仍优先于
        全局默认;只覆盖 FARM_FIELD_SPECS 的键,连接类参数(端口/server_id)不动。返回热更新账号数。"""
        count = 0
        for iid, controller in self.controllers.items():
            live = getattr(controller, "args", None)
            if live is None:
                continue
            try:
                rebuilt = self.build_account_args(iid)
            except Exception:
                continue
            for key, _label, _kind in FARM_FIELD_SPECS:
                if hasattr(rebuilt, key):
                    setattr(live, key, getattr(rebuilt, key))
            # 这些键不在 FARM_FIELD_SPECS(列表/字符串/自动邮寄 tab 自管),单独热更新。
            for extra in ("cash_use_schedule", "automail_enabled", "automail_recipient",
                          "automail_money_threshold", "automail_amount", "automail_poll_interval",
                          "automail_whitelist", "restock_enabled", "restock_schedule"):
                if hasattr(rebuilt, extra):
                    setattr(live, extra, getattr(rebuilt, extra))
            count += 1
        return count

    def start_account_row(self, iid: str) -> None:
        if iid in self.controllers:
            return
        try:
            account_args = self.build_account_args(iid)
        except Exception:
            main_log("[界面] 启动账号失败")
            for line in traceback.format_exc().rstrip().splitlines():
                main_log(line)
            return

        controller = AccountController(account_args.account, account_args.character)
        controller.args = account_args   # 「挂机设置」热更新就地改写这个对象
        self.controllers[iid] = controller

        def worker() -> None:
            try:
                run_forever(
                    account_args,
                    self.log_dir,
                    retry_delay=float(getattr(account_args, "retry_delay", self.args.retry_delay)),
                    max_retries=int(getattr(account_args, "max_retries", self.args.max_retries)),
                    failure_window_seconds=float(
                        getattr(account_args, "failure_window_seconds", self.args.failure_window_seconds)
                    ),
                    max_failures_per_window=int(
                        getattr(account_args, "max_failures_per_window", self.args.max_failures_per_window)
                    ),
                    console=False,
                    controller=controller,
                )
            except Exception:
                main_log(f"[界面] 账号 {account_args.account} 线程异常")
                for line in traceback.format_exc().rstrip().splitlines():
                    main_log(line)
            finally:
                self.root.after(0, lambda: self.controllers.pop(iid, None))

        controller.thread = threading.Thread(
            target=worker,
            name=f"so3d-{account_args.account}-{account_args.character}",
            daemon=True,
        )
        controller.thread.start()

    def stop_account_row(self, iid: str) -> None:
        controller = self.controllers.pop(iid, None)
        if controller is None:
            return
        main_log(f"[界面] 正在断开账号 {controller.account}")

        def closer() -> None:
            controller.stop_event.set()
            for sock in controller.take_sockets():
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass

        threading.Thread(target=closer, daemon=True).start()

    def start(self) -> None:
        rows = self.tree.get_children()
        if not rows:
            main_log("[界面] 没有可启动的账号")
            return
        if not self._ensure_prod_bridge_or_warn():
            return
        for iid in rows:
            self.start_account_row(iid)

    def stop(self) -> None:
        for iid in list(self.controllers.keys()):
            self.stop_account_row(iid)

    def selected_rows(self) -> list[str]:
        return list(self.tree.selection())

    def start_selected(self) -> None:
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        if not self._ensure_prod_bridge_or_warn():
            return
        for iid in rows:
            self.start_account_row(iid)

    def stop_selected(self) -> None:
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        for iid in rows:
            self.stop_account_row(iid)

    def revive_selected(self) -> None:
        """对选中(可多选)且在线的账号投递 revive 命令,worker 线程发 CG_PLAYER_REVIVE(412017)。"""
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        sent = 0
        for iid in rows:
            controller = self.controllers.get(iid)
            if controller is None or controller.farm is None:
                vals = self.tree.item(iid, "values")
                who = vals[0] if vals else iid
                main_log(f"[界面] 账号 {who} 未在线,无法回城复活")
                continue
            controller.action_queue.put("revive")
            sent += 1
        if sent:
            main_log(f"[界面] 已请求回城复活 x{sent}(发送 CG_PLAYER_REVIVE 412017)")

    def on_tree_right_click(self, event) -> None:
        iid = self.tree.identify_row(event.y)
        if not iid:
            return
        # 右键未落在已选区时,先把选中切到该行,菜单才作用于点中的账号。
        if iid not in self.tree.selection():
            self.tree.selection_set(iid)
        try:
            self.row_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.row_menu.grab_release()

    def drain_logs(self) -> None:
        try:
            while True:
                line = self.log_queue.get_nowait()
                self.log_text.configure(state=self.tk.NORMAL)
                self.log_text.insert(self.tk.END, line + "\n")
                self.log_text.see(self.tk.END)
                self.log_text.configure(state=self.tk.DISABLED)
        except queue.Empty:
            pass
        self.drain_statuses()
        self.refresh_farm_columns()
        self.refresh_detail_panel()
        self.root.after(100, self.drain_logs)

    @staticmethod
    def _status_tag(status_text: str) -> str:
        return {"在线": "online", "登录中": "connecting"}.get(status_text, "offline")

    @staticmethod
    def _cash_warn_text(farm) -> str:
        """扫 cash 背包快照,把定时表里缺货/低于阈值的物品汇成醒目提示;无则空串。
        cash 尚未同步则返回空(避免登录初期误报缺货)。GUI 线程调用,先 dict() 拷贝快照。"""
        if farm is None or not getattr(farm, "cash_observed", False):
            return ""
        schedule = parse_cash_schedule(getattr(farm.args, "cash_use_schedule", None))
        if not schedule:
            return ""
        try:
            threshold = int(getattr(farm.args, "cash_low_stock_threshold", 10))
        except (TypeError, ValueError):
            threshold = 10
        snap = dict(farm.cash_bag)
        parts: list[str] = []
        for name, _interval in schedule:
            iid = resolve_item_id(name, 0)
            if not iid:
                continue
            total = cash_count_item(snap, iid)
            if total == 0:
                parts.append(f"缺:{name}")
            elif total < threshold:
                parts.append(f"低:{name}({total})")
        return " | ".join(parts)

    def refresh_farm_columns(self) -> None:
        """把每行的 当前HP/最大HP、所在位置、Cash定时告警 刷成 controller.farm 的最新值。
        值/配色不变时跳过写入,避免每 100ms 重画整表;离线/未上线则留空。
        告警时整行套 cash_alert 红底(盖过在线配色);状态 tag 由 values[3] 反推,故本函数
        每周期兜底重算 tags,与 apply_status 的即时设置不冲突。"""
        for iid in self.tree.get_children():
            controller = self.controllers.get(iid)
            farm = controller.farm if controller is not None else None
            if farm is not None and getattr(farm, "hp_seen", False):
                hp_text = f"{farm.hp:,} / {farm.max_hp:,}"
            else:
                hp_text = ""
            if farm is not None and getattr(farm, "map_seen", False):
                loc_text = resolve_map_name(farm.map_slot)
            else:
                loc_text = ""
            warn_text = self._cash_warn_text(farm)
            real_char = getattr(farm, "real_character", "") if farm is not None else ""
            values = list(self.tree.item(iid, "values"))
            while len(values) < 9:
                values.append("")
            status_tag = self._status_tag(values[3] if len(values) > 3 else "")
            new_tags = (status_tag, "cash_alert") if warn_text else (status_tag,)
            cur_tags = tuple(self.tree.item(iid, "tags") or ())
            changed = (values[5] != hp_text or values[6] != loc_text
                       or values[7] != warn_text or values[8] != real_char)
            if changed or cur_tags != new_tags:
                values[5] = hp_text
                values[6] = loc_text
                values[7] = warn_text
                values[8] = real_char
                self.tree.item(iid, values=values, tags=new_tags)

    def on_tree_select(self, _event=None) -> None:
        self.refresh_detail_panel()

    def refresh_detail_panel(self) -> None:
        """把选中账号的会话统计汇成多行文本。累计项(钓获/用券/买券/运行时长)来自
        跨重连存活的 controller;实时项(金钱/鱼饵/Cash券余量)来自当前 farm 快照,离线显示 —。"""
        sel = self.tree.selection()
        if not sel:
            self.detail_var.set("（在上方列表选中一个账号查看统计）")
            return
        iid = sel[0]
        vals = list(self.tree.item(iid, "values"))
        who = ((vals[8] if len(vals) > 8 and vals[8] else "")
               or (vals[2] if len(vals) > 2 and vals[2] else "")
               or (vals[0] if vals else iid))
        controller = self.controllers.get(iid)
        if controller is None:
            self.detail_var.set(f"角色 {who}    （未运行，无会话统计）")
            return
        secs = max(0, int(time.monotonic() - controller.stat_start_at))
        run_hms = f"{secs // 3600:02d}:{secs % 3600 // 60:02d}:{secs % 60:02d}"
        catch = controller.stat_catch_total
        used = controller.stat_summon_count
        bought = controller.stat_summon_bought
        income = controller.stat_sell_income
        farm = controller.farm
        if farm is not None:
            gold_text = f"{farm.gold:,}" if getattr(farm, "gold_seen", False) else "—"
            bait_id = getattr(farm.args, "fishing_bait_item_id", 0)
            bait_text = f"{bag_count_item(farm, bait_id)} ({item_label(bait_id)})" if bait_id else "—"
            if getattr(farm, "cash_observed", False):
                ticket_left_text = f"{cash_count_item(dict(farm.cash_bag), SUMMON_ITEM_ID)} 张"
            else:
                ticket_left_text = "—"
        else:
            gold_text = bait_text = ticket_left_text = "—"
        self.detail_var.set(
            f"角色 {who}    运行 {run_hms}    钓获 {catch} 件\n"
            f"金钱 {gold_text}    剩余鱼饵 {bait_text}    累计卖货收入 +{income:,}\n"
            f"摊贩呼叫券:  使用 {used} 次  /  买入 {bought} 张  /  Cash 余 {ticket_left_text}"
        )

    def drain_statuses(self) -> None:
        try:
            while True:
                account, character, status = self.status_queue.get_nowait()
                self.apply_status(account, character, status)
        except queue.Empty:
            pass

    def apply_status(self, account: str, character: str, status: str) -> None:
        tag = self._status_tag(status)
        for iid in self.tree.get_children():
            values = list(self.tree.item(iid, "values"))
            if len(values) >= 3 and values[0] == account and values[2] == character:
                while len(values) < 9:
                    values.append("")
                values[3] = status
                # 仅设基础状态 tag;cash_alert 由 refresh_farm_columns 每 100ms 兜底补回。
                self.tree.item(iid, values=values, tags=(tag,))
                break

    def open_farm_settings(self) -> None:
        tk = self.tk
        ttk = self.ttk
        win = tk.Toplevel(self.root)
        win.title("挂机设置")
        win.transient(self.root)
        win.grab_set()

        defaults = self.defaults
        nb = ttk.Notebook(win)
        nb.grid(row=0, column=0, sticky="nsew")
        win.rowconfigure(0, weight=1)
        win.columnconfigure(0, weight=1)

        # 按字段前缀把 FARM_FIELD_SPECS 分流到各 tab:钓鱼+买饵一组、贩卖一组、cash 一组。
        tab_fishing = ttk.Frame(nb, padding=10)
        tab_sell = ttk.Frame(nb, padding=10)
        tab_cash = ttk.Frame(nb, padding=10)
        nb.add(tab_fishing, text="钓鱼 / 买饵")
        nb.add(tab_sell, text="贩卖")
        nb.add(tab_cash, text="Cash定时")

        def tab_for(key: str):
            if key.startswith("fishing_") or key.startswith("autobuy_"):
                return tab_fishing
            if key.startswith("autosell_"):
                return tab_sell
            return tab_cash   # cash_low_stock_threshold 及其它

        rows = {id(tab_fishing): 0, id(tab_sell): 0, id(tab_cash): 0}
        farm_vars: dict[str, object] = {}
        for key, label, kind in FARM_FIELD_SPECS:
            frame = tab_for(key)
            row = rows[id(frame)]
            rows[id(frame)] = row + 1
            current = defaults.get(key, CONFIG[key])
            if kind == "bool":
                var = tk.BooleanVar(value=_as_bool(current))
                ttk.Checkbutton(frame, text=label, variable=var).grid(
                    row=row, column=0, columnspan=2, sticky=tk.W, pady=2)
            else:
                ttk.Label(frame, text=label).grid(row=row, column=0, sticky=tk.W, pady=2)
                if kind == "itemidlist":
                    text = ",".join(item_name(v) or str(v) for v in resolve_item_id_list(current))
                elif kind == "intlist":
                    text = ",".join(str(v) for v in _coerce_int_list(current))
                elif kind == "itemid":
                    iid = resolve_item_id(current, 0)
                    text = (item_name(iid) or str(iid)) if iid else ""
                else:
                    text = str(current)
                var = tk.StringVar(value=text)
                ttk.Entry(frame, textvariable=var, width=18).grid(
                    row=row, column=1, sticky="ew", padx=(8, 0), pady=2)
            farm_vars[key] = var
        for frame in (tab_fishing, tab_sell, tab_cash):
            frame.columnconfigure(1, weight=1)

        # Cash tab 末尾追加「定时使用 cash 物品」多行编辑框(物品名=间隔秒,单值控件放不下)。
        cash_row = rows[id(tab_cash)]
        ttk.Label(tab_cash, text="定时使用Cash物品(每行一组:物品名=间隔秒)").grid(
            row=cash_row, column=0, columnspan=2, sticky=tk.W, pady=(10, 2))
        cash_text = tk.Text(tab_cash, width=34, height=6, wrap=tk.NONE)
        cash_text.grid(row=cash_row + 1, column=0, columnspan=2, sticky="ew", pady=(0, 4))
        for nm, iv in parse_cash_schedule(defaults.get("cash_use_schedule")):
            cash_text.insert(tk.END, f"{nm}={iv}\n")
        ttk.Label(tab_cash, text="例:经验药水=600(每600秒用一次);活着才用,缺货/不足阈值会在列表里红色告警").grid(
            row=cash_row + 2, column=0, columnspan=2, sticky=tk.W)

        # Cash tab 再追加「自动补货」(泡点网店买->进bank->搬到cash)。默认关闭,会花泡点。
        ttk.Separator(tab_cash, orient=tk.HORIZONTAL).grid(
            row=cash_row + 3, column=0, columnspan=2, sticky="ew", pady=(12, 6))
        restock_enabled = tk.BooleanVar(value=_as_bool(defaults.get("restock_enabled", False)))
        ttk.Checkbutton(tab_cash, text="启用自动补货(泡点购买，会花泡点)", variable=restock_enabled).grid(
            row=cash_row + 4, column=0, columnspan=2, sticky=tk.W, pady=2)
        ttk.Label(tab_cash, text="自动补货(每行一组:物品名=阈值:数量)").grid(
            row=cash_row + 5, column=0, columnspan=2, sticky=tk.W, pady=(6, 2))
        restock_text = tk.Text(tab_cash, width=34, height=5, wrap=tk.NONE)
        restock_text.grid(row=cash_row + 6, column=0, columnspan=2, sticky="ew", pady=(0, 4))
        for it, thr, buy in parse_restock_schedule(defaults.get("restock_schedule")):
            restock_text.insert(tk.END, f"{it}={thr}:{buy}\n")
        ttk.Label(
            tab_cash,
            text="例:攤販呼叫券=10:50(cash里该物品总和<10就泡点买50个,进bank后自动搬到cash)。\n"
                 "填物品名即可,程序自动查id。凭据=游戏账号。先用「查看背包」里的「测试补货」验证再开自动。",
            justify=tk.LEFT, foreground="#666666",
        ).grid(row=cash_row + 7, column=0, columnspan=2, sticky=tk.W)

        # ===== 自动邮寄金钱 tab =====
        tab2 = ttk.Frame(nb, padding=12)
        nb.add(tab2, text="自动邮寄")
        mail_enabled = tk.BooleanVar(value=_as_bool(defaults.get("automail_enabled", False)))
        mail_recipient = tk.StringVar(value=str(defaults.get("automail_recipient", "") or ""))
        mail_threshold = tk.StringVar(value=str(defaults.get("automail_money_threshold", 0)))
        mail_amount = tk.StringVar(value=str(defaults.get("automail_amount", 0)))
        mail_poll = tk.StringVar(value=str(defaults.get("automail_poll_interval", 10.0)))
        mail_whitelist = tk.StringVar(value=str(defaults.get("automail_whitelist", "") or ""))
        ttk.Checkbutton(tab2, text="启用自动邮寄金钱", variable=mail_enabled).grid(
            row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
        for r, (lab, var) in enumerate((
            ("寄送给哪个角色(收件人)", mail_recipient),
            ("钱 > 此值即寄(阈值)", mail_threshold),
            ("每次寄出金额", mail_amount),
            ("循环探测间隔(秒)", mail_poll),
            ("白名单账号(逗号分隔,不寄)", mail_whitelist),
        ), start=1):
            ttk.Label(tab2, text=lab).grid(row=r, column=0, sticky=tk.W, pady=3)
            ttk.Entry(tab2, textvariable=var, width=22).grid(row=r, column=1, sticky="ew", padx=(8, 0), pady=3)
        tab2.columnconfigure(1, weight=1)
        ttk.Label(
            tab2,
            text="说明:钱包>阈值时每「循环探测间隔」寄一次给收件人;寄出后服务端(521563)刷新钱包,\n"
                 "余额回落到阈值以下即自然停寄。建议金额<阈值,收件人别填自己。\n"
                 "白名单填账号(非角色名),逗号分隔;在白名单里的账号不自动邮寄。",
            justify=tk.LEFT, foreground="#666666",
        ).grid(row=6, column=0, columnspan=2, sticky=tk.W, pady=(10, 0))

        btns = ttk.Frame(win, padding=(10, 6))
        btns.grid(row=1, column=0, columnspan=2, sticky="ew")

        def on_save() -> None:
            try:
                for skey, _slabel, skind in FARM_FIELD_SPECS:
                    raw = farm_vars[skey].get()
                    if skind == "bool":
                        defaults[skey] = bool(raw)
                    elif skind == "int":
                        defaults[skey] = parse_int_config(raw, int(CONFIG[skey]))
                    elif skind == "float":
                        defaults[skey] = float(str(raw).strip() or CONFIG[skey])
                    elif skind == "itemid":
                        iid = resolve_item_id(raw, 0)
                        defaults[skey] = (item_name(iid) or iid) if iid else 0
                    elif skind == "itemidlist":
                        defaults[skey] = [item_name(i) or i for i in resolve_item_id_list(raw)]
                    elif skind == "intlist":
                        defaults[skey] = _coerce_int_list(raw)
                sched = parse_cash_schedule(cash_text.get("1.0", tk.END))
                defaults["cash_use_schedule"] = [{"item": nm, "interval": iv} for nm, iv in sched]
                defaults["automail_enabled"] = bool(mail_enabled.get())
                defaults["automail_recipient"] = str(mail_recipient.get()).strip()
                defaults["automail_money_threshold"] = parse_int_config(mail_threshold.get(), 0)
                defaults["automail_amount"] = parse_int_config(mail_amount.get(), 0)
                defaults["automail_poll_interval"] = float(str(mail_poll.get()).strip() or 10.0)
                defaults["automail_whitelist"] = ",".join(
                    a.strip() for a in str(mail_whitelist.get()).split(",") if a.strip())
                defaults["restock_enabled"] = bool(restock_enabled.get())
                rs = parse_restock_schedule(restock_text.get("1.0", tk.END))
                defaults["restock_schedule"] = [{"item": i, "threshold": t, "buy": b} for i, t, b in rs]
            except Exception as exc:
                main_log(f"[界面] 挂机设置保存失败：{exc}")
                return
            self.save_defaults_to_file()
            applied = self.apply_farm_settings_to_running()
            if applied:
                main_log(f"[界面] 挂机设置已保存，并热更新 {applied} 个在线账号（即时生效，无需重启）")
            else:
                main_log("[界面] 挂机设置已保存（下次启动账号生效）")
            win.destroy()

        ttk.Button(btns, text="保存", command=on_save).pack(side=tk.RIGHT)
        ttk.Button(btns, text="取消", command=win.destroy).pack(side=tk.RIGHT, padx=(0, 6))

    def _selected_controller(self) -> "AccountController | None":
        sel = self.tree.selection()
        iid = sel[0] if sel else None
        if not iid:
            return None
        return self.controllers.get(iid)

    def open_bag_view(self) -> None:
        tk = self.tk
        ttk = self.ttk
        controller = self._selected_controller()
        if controller is None:
            main_log("[界面] 请先在列表里选中一个【已在线】的账号再查看背包")
            return

        win = tk.Toplevel(self.root)
        win.title(f"背包查看 - {controller.account} / {controller.character}")
        win.geometry("680x560")
        win.transient(self.root)

        header = ttk.Frame(win, padding=(10, 8))
        header.pack(fill=tk.X)
        gold_var = tk.StringVar(value="金币 未知")
        ttk.Label(header, textvariable=gold_var, font=("", 10, "bold")).pack(side=tk.LEFT)
        auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(header, text="自动刷新(1s)", variable=auto_var).pack(side=tk.RIGHT)
        info_var = tk.StringVar(value="")
        ttk.Label(header, textvariable=info_var).pack(side=tk.RIGHT, padx=(0, 12))

        # 按钮栏:必须早于两个表格 pack、且 side=BOTTOM 钉到底部,否则表格 expand=True
        # 会占满 680x560,把最后 pack 的按钮栏挤出可视区(之前"看不到卖货按钮"的原因)。
        btns = ttk.Frame(win, padding=(10, 6))
        btns.pack(side=tk.BOTTOM, fill=tk.X)

        def make_table(parent, title: str):
            frame = ttk.LabelFrame(parent, text=title)
            frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 8))
            cols = ("slot", "item_id", "name", "count")
            tv = ttk.Treeview(frame, columns=cols, show="headings", height=10)
            tv.heading("slot", text="格子")
            tv.heading("item_id", text="物品ID")
            tv.heading("name", text="物品名")
            tv.heading("count", text="数量")
            tv.column("slot", width=60, anchor=tk.CENTER)
            tv.column("item_id", width=90, anchor=tk.CENTER)
            tv.column("name", width=170, anchor=tk.W)
            tv.column("count", width=90, anchor=tk.CENTER)
            sb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=tv.yview)
            tv.configure(yscrollcommand=sb.set)
            tv.grid(row=0, column=0, sticky="nsew")
            sb.grid(row=0, column=1, sticky="ns")
            frame.columnconfigure(0, weight=1)
            frame.rowconfigure(0, weight=1)
            return tv

        bag_tv = make_table(win, "背包(511591 整桶快照)")
        cash_tv = make_table(win, "Cash 背包(511324 整桶快照)")

        def fill(tv, snapshot: dict) -> int:
            tv.delete(*tv.get_children())
            shown = 0
            for slot in sorted(snapshot):
                iid_, cnt = snapshot[slot]
                if not iid_ and not cnt:
                    continue
                tv.insert("", tk.END, values=(slot, iid_, item_name(iid_) or "", cnt))
                shown += 1
            return shown

        def refresh() -> None:
            if not win.winfo_exists():
                return
            farm = controller.farm
            if farm is None:
                gold_var.set("金币 未知")
                info_var.set("账号未在线或尚未进入游戏,暂无背包数据")
                bag_tv.delete(*bag_tv.get_children())
                cash_tv.delete(*cash_tv.get_children())
            else:
                gold_var.set(f"金币 {farm.gold:,}" if farm.gold_seen else "金币 未知(尚未回流)")
                bag_snap = dict(farm.bag)
                cash_snap = dict(farm.cash_bag)
                nb = fill(bag_tv, bag_snap)
                nc = fill(cash_tv, cash_snap)
                info_var.set(f"背包 {nb} 项 / Cash {nc} 项"
                             + ("" if farm.cash_observed else "(Cash 尚未同步)"))

        timer = {"id": None}

        def cancel_timer() -> None:
            if timer["id"] is not None:
                try:
                    win.after_cancel(timer["id"])
                except Exception:
                    pass
                timer["id"] = None

        def tick() -> None:
            timer["id"] = None
            if not win.winfo_exists():
                return
            refresh()
            if auto_var.get():
                timer["id"] = win.after(1000, tick)

        def on_auto_toggle(*_args) -> None:
            cancel_timer()
            if auto_var.get():
                tick()

        auto_var.trace_add("write", on_auto_toggle)
        win.protocol("WM_DELETE_WINDOW", lambda: (cancel_timer(), win.destroy()))

        from tkinter import messagebox

        def _confirm_sell() -> None:
            # 手动卖货:按该账号配置的贩卖范围,确认框里先列出将卖出的具体格子(不可逆)。
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法卖货")
                return
            fargs = farm.args
            keep = set(getattr(fargs, "autosell_keep_item_ids", []) or [])
            lo = max(0, getattr(fargs, "autosell_start_slot", 0))
            hi = min(BAG_MAIN_SLOTS - 1, getattr(fargs, "autosell_end_slot", BAG_MAIN_SLOTS - 1))
            bag = dict(farm.bag)
            sell_list = [(s, bag[s][0], bag[s][1]) for s in range(lo, hi + 1)
                         if bag.get(s, (0, 0))[0] and bag[s][1] > 0 and bag[s][0] not in keep]
            if not sell_list:
                messagebox.showinfo("立即卖货",
                                    f"贩卖范围[{lo}-{hi}]内没有可卖物品(已跳过保留ID {sorted(keep)})")
                return
            preview = "\n".join(f"  格{s} {item_label(iid)} x{cnt}" for s, iid, cnt in sell_list[:12])
            more = "" if len(sell_list) <= 12 else f"\n  ...另外 {len(sell_list) - 12} 格"
            msg = (f"将真实卖出以下 {len(sell_list)} 格(范围[{lo}-{hi}],跳过保留ID {sorted(keep)}):\n"
                   f"{preview}{more}\n\n此操作不可逆!确定卖出?")
            if messagebox.askyesno("立即卖货 - 确认", msg):
                controller.action_queue.put("sell")
                main_log(f"[界面] 已请求立即卖货({len(sell_list)}格),执行后点「手动刷新」看背包变化")

        def _confirm_buy() -> None:
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法买鱼饵")
                return
            fargs = farm.args
            bait_id = getattr(fargs, "autobuy_item_id", 0) or getattr(fargs, "fishing_bait_item_id", 0)
            if not bait_id:
                messagebox.showwarning("立即买鱼饵",
                                       "该账号未配置要购买的 itemId(autobuy_item_id),请先在「挂机设置」里填写")
                return
            summon = find_summon_slot(farm)
            if summon is None:
                hint = ("cash 背包尚未同步(511324)" if not farm.cash_observed
                        else f"cash 背包内找不到摊贩呼叫卷({item_label(SUMMON_ITEM_ID)})")
                messagebox.showwarning("立即买鱼饵", f"无法召唤摊贩:{hint}")
                return
            # 弹框让用户输入数量(默认取配置的 autobuy_count),走和自动同一逻辑(autobuy_run)。
            dlg = tk.Toplevel(win)
            dlg.title("立即买鱼饵")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            qv = tk.StringVar(value=str(getattr(fargs, "autobuy_count", 0) or 300))
            ttk.Label(frm, text=f"物品:{item_label(bait_id)}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 4))
            ttk.Label(frm, text=f"现有:{bag_count_item(farm, bait_id)}  摊贩呼叫卷:cash格{summon}").grid(
                row=1, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            ttk.Label(frm, text="购买数量").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=qv, width=16).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            frm.columnconfigure(1, weight=1)

            def do_buy() -> None:
                try:
                    qty = int(str(qv.get()).strip())
                except ValueError:
                    messagebox.showwarning("立即买鱼饵", "数量必须是整数")
                    return
                if qty <= 0:
                    messagebox.showwarning("立即买鱼饵", "数量必须 > 0")
                    return
                controller.action_queue.put(("buy", qty))
                main_log(f"[界面] 已请求立即买鱼饵 {item_label(bait_id)} x{qty},执行后点「手动刷新」看背包变化")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="购买", command=do_buy).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        def _sell_one_slot(slot: int, iid: int, cnt: int) -> None:
            # 右键单格贩卖:用户显式指定该格,不走 keep/sellable 过滤(鱼饵也能卖)。
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法卖货")
                return
            if messagebox.askyesno(
                    "右键卖出 - 确认",
                    f"将真实卖出单格:\n  格{slot} {item_label(iid)} x{cnt}\n\n此操作不可逆!确定卖出?"):
                controller.action_queue.put(("sell_slot", slot))
                main_log(f"[界面] 已请求卖出单格{slot}({item_label(iid)} x{cnt}),执行后点「手动刷新」看变化")

        def _test_mail() -> None:
            # 手动测试自动邮寄:弹框填收件人/金额(默认取该账号配置),确认后走 action_queue
            # 在 worker 线程发 411524。寄出后服务端回 521563 刷新钱包,点「手动刷新」即可看到金币减少。
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法邮寄")
                return
            fargs = farm.args
            gold_now = farm.gold if farm.gold_seen else None
            dlg = tk.Toplevel(win)
            dlg.title("测试邮寄金钱")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            rv = tk.StringVar(value=str(getattr(fargs, "automail_recipient", "") or ""))
            av = tk.StringVar(value=str(getattr(fargs, "automail_amount", 0) or ""))
            ttk.Label(frm, text=f"寄件人:{controller.character}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            ttk.Label(frm, text="收件人角色名").grid(row=1, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=rv, width=20).grid(row=1, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="金额").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=av, width=20).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text=(f"当前钱包:{gold_now:,}" if gold_now is not None else "当前钱包:未知(尚未回流)")).grid(
                row=3, column=0, columnspan=2, sticky=tk.W, pady=(6, 0))
            frm.columnconfigure(1, weight=1)

            def do_send() -> None:
                recipient = rv.get().strip()
                try:
                    amount = int(str(av.get()).strip())
                except ValueError:
                    messagebox.showwarning("测试邮寄", "金额必须是整数")
                    return
                if not recipient or amount <= 0:
                    messagebox.showwarning("测试邮寄", "收件人不能为空,金额必须 > 0")
                    return
                if recipient == controller.character and not messagebox.askyesno(
                        "测试邮寄", "收件人就是自己,确定继续?"):
                    return
                if gold_now is not None and amount > gold_now:
                    messagebox.showwarning("测试邮寄", f"金额 {amount:,} 超过当前钱包 {gold_now:,}")
                    return
                controller.action_queue.put(("mail", recipient, amount))
                main_log(f"[界面] 已请求测试邮寄 {amount:,} 给 {recipient}(寄出后点「手动刷新」看钱包变化)")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="发送", command=do_send).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        def _test_restock() -> None:
            # 手动测试自动补货:填网店itemID+买几个,确认后走 action_queue 在 worker 线程
            # 跑完整流程(泡点购买->开bank->搬到cash)。会真的花泡点!
            controller = self._selected_controller()
            if controller is None or controller.farm is None:
                main_log("[界面] 账号未在线,无法补货")
                return
            dlg = tk.Toplevel(win)
            dlg.title("测试补货(泡点购买)")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            iv = tk.StringVar(value="")
            cv = tk.StringVar(value="1")
            ttk.Label(frm, text=f"角色:{controller.character}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            ttk.Label(frm, text="物品名(或ID)").grid(row=1, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=iv, width=20).grid(row=1, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="购买数量").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=cv, width=20).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="会真的花泡点!买完自动开bank并搬到cash,看日志/手动刷新。",
                      foreground="#b00020").grid(row=3, column=0, columnspan=2, sticky=tk.W, pady=(6, 0))
            frm.columnconfigure(1, weight=1)

            def do_buy() -> None:
                item_id = resolve_item_id(str(iv.get()).strip(), 0)   # 名字/ID -> 网店itemID
                try:
                    count = int(str(cv.get()).strip())
                except ValueError:
                    messagebox.showwarning("测试补货", "数量必须是整数")
                    return
                if not item_id:
                    messagebox.showwarning("测试补货", "物品名/ID 解析不到,检查 item_names.json")
                    return
                if count <= 0:
                    messagebox.showwarning("测试补货", "数量必须 > 0")
                    return
                if not messagebox.askyesno("测试补货", f"将用泡点购买 {item_label(item_id)} x{count} 并搬到cash。\n会真的花泡点,确定?"):
                    return
                controller.action_queue.put(("restock", item_id, count))
                main_log(f"[界面] 已请求测试补货 {item_label(item_id)} x{count}(看日志,完成后手动刷新看cash)")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="购买", command=do_buy).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        bag_menu = tk.Menu(win, tearoff=0)

        def _bag_rclick(event) -> None:
            row = bag_tv.identify_row(event.y)
            if not row:
                return
            bag_tv.selection_set(row)
            vals = bag_tv.item(row, "values")  # (slot, item_id, name, count)
            try:
                slot, iid, cnt = int(vals[0]), int(vals[1]), int(vals[3])
            except (ValueError, IndexError):
                return
            bag_menu.delete(0, tk.END)
            bag_menu.add_command(label=f"卖出此格:{item_name(iid) or iid} x{cnt}",
                                 command=lambda: _sell_one_slot(slot, iid, cnt))
            bag_menu.tk_popup(event.x_root, event.y_root)

        bag_tv.bind("<Button-3>", _bag_rclick)

        ttk.Button(btns, text="整体卖货(范围)", command=_confirm_sell).pack(side=tk.LEFT)
        ttk.Button(btns, text="立即买鱼饵", command=_confirm_buy).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="测试邮寄", command=_test_mail).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="测试补货", command=_test_restock).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Label(btns, text="提示:背包格上右键可单格贩卖").pack(side=tk.LEFT, padx=(12, 0))
        ttk.Button(btns, text="手动刷新", command=refresh).pack(side=tk.RIGHT)
        ttk.Button(btns, text="关闭",
                   command=lambda: (cancel_timer(), win.destroy())).pack(side=tk.RIGHT, padx=(0, 6))

        tick()

    def on_close(self) -> None:
        for controller in self.controllers.values():
            controller.stop_event.set()
            for sock in controller.take_sockets():
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass
        self.controllers.clear()
        set_log_handler(None)
        set_status_handler(None)
        self.root.destroy()


def run_gui(args: argparse.Namespace) -> None:
    import tkinter as tk

    root = tk.Tk()
    OnlineLoginGui(root, args)
    root.mainloop()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="SO3D online login + heartbeat client")
    parser.add_argument("--accounts-file", default=CONFIG["accounts_file"], help="JSON account list; missing file falls back to single CONFIG account")
    parser.add_argument("--defaults-file", default=CONFIG["defaults_file"], help="JSON global defaults (fishing/autosell/autobuy/server etc.); shipped with the exe, unlike the accounts file")
    parser.add_argument("--log-dir", default=CONFIG["log_dir"], help="directory for per-account logs")
    parser.add_argument("--log-max-mb", type=float, default=CONFIG["log_max_mb"], help="max MB per account log file")
    parser.add_argument("--log-max-files", type=int, default=CONFIG["log_max_files"], help="max rotated log files per account")
    parser.add_argument("--retry-delay", type=float, default=CONFIG["retry_delay"], help="seconds before retry after a failed/disconnected session")
    parser.add_argument("--max-retries", type=int, default=CONFIG["max_retries"], help="0 = retry forever")
    parser.add_argument("--failure-window-seconds", type=float, default=CONFIG["failure_window_seconds"], help="failure-rate window per account")
    parser.add_argument("--max-failures-per-window", type=int, default=CONFIG["max_failures_per_window"], help="pause account after this many failures in one window; 0 disables")
    parser.add_argument("--start-stagger", type=float, default=CONFIG["start_stagger"], help="seconds between starting account threads")
    parser.add_argument("--single", action="store_true", help="ignore accounts-file and run only --account/CONFIG")
    parser.add_argument("--console-account-logs", action="store_true", help="also print per-account packet logs to console in multi-account mode")
    parser.add_argument("--login-host", default=CONFIG["login_host"], help="login/lobby host")
    parser.add_argument("--login-port", type=int, default=CONFIG["login_port"])
    parser.add_argument("--bill-host", default=CONFIG["bill_host"], help="billing host")
    parser.add_argument("--bill-port", type=int, default=CONFIG["bill_port"])
    parser.add_argument("--chat-host", default=CONFIG["chat_host"], help="chat/friend host")
    parser.add_argument("--chat-port", type=int, default=CONFIG["chat_port"], help="chat/friend port")
    parser.add_argument("--chat-enabled", action=argparse.BooleanOptionalAction, default=CONFIG["chat_enabled"])
    parser.add_argument("--chat-player-id", default=CONFIG["chat_player_id"], help="0 = auto-detect from 1842 packets")
    parser.add_argument("--chat-map-id", default=CONFIG["chat_map_id"], help="0 = auto-detect from 1842 packets")
    parser.add_argument("--chat-status-enabled", action=argparse.BooleanOptionalAction, default=CONFIG["chat_status_enabled"])
    parser.add_argument("--chat-player-id-wait", type=float, default=CONFIG["chat_player_id_wait"])
    parser.add_argument("--chat-watchdog-seconds", type=float, default=CONFIG["chat_watchdog_seconds"], help="reconnect if chat/3000 receives no data for this many seconds; 0 disables")
    parser.add_argument("--account", default=CONFIG["account"], help="account name")
    parser.add_argument("--password", default=CONFIG["password"], help="account password")
    parser.add_argument("--character", default=CONFIG["character"], help="character name")
    parser.add_argument("--server-id", type=int, default=CONFIG["server_id"])
    parser.add_argument("--channel-id", type=int, default=CONFIG["channel_id"])
    parser.add_argument("--server-mode", choices=["test", "prod"], default=CONFIG["server_mode"],
                        help="test=直连公网真服(默认); prod=连本地代理 127.2.57.25, 登录走 123.dll bridge 解密")
    parser.add_argument("--game-host", default=CONFIG["game_host"], help="override returned game-server host")
    parser.add_argument(
        "--game-host-map",
        default=CONFIG["game_host_map"],
        help="virtual-to-public game host map, e.g. 127.1.5.41=128.14.237.136,127.1.5.42=165.154.194.124",
    )
    parser.add_argument("--game-port", type=int, default=CONFIG["game_port"], help="override returned game-server port")
    parser.add_argument("--client-exe", default=CONFIG["client_exe"], help="path to SO3DPlus.exe for CG_ENTER file info")
    parser.add_argument("--encoding", default=CONFIG["encoding"], help="account/character encoding")
    parser.add_argument("--xor-index", default=CONFIG["xor_index"])
    parser.add_argument("--timeout", type=float, default=CONFIG["timeout"])
    parser.add_argument("--run-seconds", type=float, default=CONFIG["run_seconds"], help="0 = run until Ctrl+C")
    parser.add_argument("--secondary-login-delay", type=float, default=CONFIG["secondary_login_delay"])
    parser.add_argument("--cl-live-interval", type=float, default=CONFIG["cl_live_interval"])
    parser.add_argument("--bill-alive-interval", type=float, default=CONFIG["bill_alive_interval"])
    parser.add_argument("--game-alive-interval", type=float, default=CONFIG["game_alive_interval"])
    parser.add_argument("--post-enter-init-delay", type=float, default=CONFIG["post_enter_init_delay"])
    parser.add_argument("--debug-packets", action=argparse.BooleanOptionalAction, default=CONFIG["debug_packets"])
    parser.add_argument("--gui", dest="gui", action="store_true", default=True, help="显示账号配置界面")
    parser.add_argument("--no-gui", dest="gui", action="store_false", help="不显示界面，直接按配置启动")
    parser.add_argument("--self-test", action="store_true")
    return parser


def _close_shared_bridges() -> None:
    """进程退出时统一回收共享 bridge 管道(测试服无 bridge_client 时无操作)。"""
    if bridge_client is not None:
        bridge_client.close_shared_bridges()


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.gui:
        try:
            run_gui(args)
        finally:
            _close_shared_bridges()
        return

    accounts_file = Path(args.accounts_file)
    log_dir = Path(args.log_dir)
    if not args.single and accounts_file.exists():
        defaults, accounts = load_accounts_config(accounts_file, Path(args.defaults_file))
        if not accounts:
            parser.error(f"配置中没有启用的账号：{accounts_file}")

        main_log(f"[主程序] 已从 {accounts_file} 读取 {len(accounts)} 个账号")
        main_log(f"[主程序] 每个账号的日志目录：{log_dir}")
        threads: list[threading.Thread] = []
        try:
            threads = start_account_threads(
                args,
                defaults,
                accounts,
                log_dir,
                console_account_logs=bool(args.console_account_logs),
            )
            wait_account_threads(threads)
        except KeyboardInterrupt:
            stop_threads(threads, "[主程序] 收到 Ctrl+C，正在停止账号连接")
        except Exception as exc:
            parser.error(str(exc))
        finally:
            _close_shared_bridges()
        return

    missing = [name for name in ("account", "password", "character") if not getattr(args, name)]
    if missing:
        parser.error("fill CONFIG in this file or pass: " + ", ".join("--" + name for name in missing))
    try:
        run_forever(
            args,
            log_dir,
            retry_delay=args.retry_delay,
            max_retries=args.max_retries,
            failure_window_seconds=args.failure_window_seconds,
            max_failures_per_window=args.max_failures_per_window,
            console=True,
        )
    except KeyboardInterrupt:
        stop_threads([], "[主程序] 收到 Ctrl+C，正在停止")
    finally:
        _close_shared_bridges()


if __name__ == "__main__":
    main()
