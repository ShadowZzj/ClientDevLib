"""共享运行期状态 + 基础网络默认值 + 账号 controller(最底层,无内部依赖)。"""
from __future__ import annotations

import queue
import socket
import sys
import threading
import time
from pathlib import Path

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import argparse
    from .farm.state import FarmState

STOP_EVENT = threading.Event()
_SOCKET_LOCK = threading.RLock()
_ACTIVE_SOCKETS: set[socket.socket] = set()
_CONTROLLER_CONTEXT = threading.local()


def app_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    # 包布局:本文件在 so3dclient/ 下,数据文件在上一级交付目录(与入口脚本同级)
    return Path(__file__).resolve().parent.parent


APP_DIR = app_dir()


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
    # 同时处于「登录流程」(握手->认证->进世界)的账号数上限。一次性启动很多账号时,超出的在
    # 登录入口排队,直到某个账号登录完成(进入在线主循环)让出名额。GUI 可改;<1 视为 1。
    "login_concurrency": 5,
    "account": "gongyu9011212",
    "password": "901121",
    "character": "shadowsing",
    "server_id": 2,
    "channel_id": 1,
    # "test" = 直连公网真服(无 123.dll 保护,现有逻辑,默认);
    # "prod" = 全部连本地代理 PROD_PROXY_HOST,登录 socket recv 走 bridge 解密。
    "server_mode": "test",
    # prod 模式下本实例 bot 要连的代理监听别名(对应某个 port_proxy 实例的 GGTB_LISTEN_IP)。
    # 空=用默认 PROD_PROXY_HOST(127.2.57.25,单实例);多实例分流时按账号/组填 127.2.57.N;
    # 填 "auto" = 自动选路:账号上线时从共享 registry 里按 rtt 升序挑一个未占满的代理实例。
    "proxy_host": "",
    # 自动选路(proxy_host="auto")时,每个上游节点/代理实例最多分配多少个在线账号;
    # 占满就跳到 rtt 次低的实例。<=0 表示不限。auto 模式之外此项忽略。
    "proxy_max_per_node": 0,
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
    # chat(3000)是独立会话:它的 RST/stale 不该拖垮整局,只重连这条 socket。
    "chat_reconnect_delay": 5.0,
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
    # 主端口(game 1841/1842)僵尸进场兜底:CG_ENTER 后超过这么多秒仍未从 game socket 收到
    # 任何字节,判定为僵尸进场(服务端接受了 CG_ENTER 却不下发世界数据,player_id 永远拿不到),
    # 主动断开整套重登。健康进场约 0.7s 就有数据,12s 足够安全、远快于干等 socket 超时。0=关闭。
    "zombie_entry_timeout": 12.0,
    "debug_packets": True,
    # ---------- 自动钓鱼 ----------
    # 开局抛一次竿(411047),之后每隔 reel_interval 秒发一个中鱼包(412048);
    # 抛竿绑定一个鱼饵格,该格被钓空(521102 扣到 0)就自动重抛切到下一个有饵的格,无需配置重抛次数。
    "fishing_enabled": False,        # 总开关
    "fishing_bait_item_id": 0,       # 鱼饵 itemId(数字),0 = 必填才生效;按 itemId 在背包里自动找格
    "fishing_special_bait": False,   # True 用 411174(特殊饵),False 用 411047
    "fishing_reel_interval": 35.0,   # 中鱼间隔(秒),建议 30-45;真实间隔会加 0-3 秒抖动
    # 进业务服务器后开着钓鱼却这么多秒没收到任何鱼获回包(521102),判定钓鱼卡死(顶号/服务端不下发),
    # 主动断开整套重登(与 zombie_entry 同机制)。只在 fishing_enabled 时计时;0=关闭。
    # 注意:本游戏钓鱼是【突发式】的——不是每次中鱼(reel)都出鱼,正常也会连着好几竿空竿,
    # 实测同一健康会话里出现过 ~98s 的自然空窗后又恢复。阈值必须远大于这个自然抖动,否则
    # 一串正常空竿会被误判成卡死,断线重连反而陷入循环。故默认 5 分钟(约 8 竿余量),
    # 只兜真正长时间零鱼获的僵尸钓鱼;别设成 2 分钟(≈3 竿)——会误杀正常但低产的会话。
    "fishing_stall_timeout": 300.0,
    # ---------- 背包将满自动贩卖 ----------
    "autosell_enabled": False,       # 总开关
    "autosell_min_free_slots": 5,    # 剩余空格 <= 此值即触发贩卖
    "autosell_start_slot": 0,        # 从该背包格(0 基)开始向后卖
    "autosell_end_slot": 191,        # 卖到该格为止(含),背包上限 192 格
    "autosell_keep_item_ids": [],    # 这些 itemId 不卖(鱼饵等)
    "autosell_extra_item_ids": [],   # 触发贩卖时,这些 itemId 不分格、全背包一律卖(覆盖靠前格)
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
    # ---------- Buff 监控:某状态名不在就用 cash 物品 ----------
    # 每组 {buff:状态名, item:物品名/ID, interval:探测间隔秒(默认300)}。每 interval 秒探测一次,
    # 角色身上没有该 buff(cash_buffs / skill_buffs 里按名匹配)就发 cash 物品。计时随 FarmState 每次
    # 重连从 0 重排:上线后第一次探测在 interval 之后(即默认 5 分钟后)才发生,不会一上线就用。
    "buff_monitor_schedule": [],     # 例:[{"buff":"進階擺攤","item":"進階擺攤券","interval":300}]
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
    # 每组 {item:背包物品名/id(查游戏表,做cash计数+bank搬运匹配),
    #       threshold:cash该物品总和<此值即触发, buy:每次买几个,
    #       shop:网店物品名/id(可省;查网店目录,做泡点购买)}。
    # shop 省略=与 item 同(网店itemID与游戏itemId一致的物品)。两边不同名(如背包「進階擺攤」/
    # 网店「進階擺攤(7天)」)时分别填 item 与 shop。默认关闭(会花泡点),开启前先用「测试补货」验证。
    "restock_enabled": False,
    "restock_schedule": [],          # 例:[{"item":"進階擺攤","threshold":1,"buy":1,"shop":"進階擺攤(7天)"}]
    # ---------- 个人摆摊(street stall) ----------
    # GUI 右键账号「摆摊…」挑背包/cash 物品+数量+单价上架(411040 开摊->411049 上架),
    # 剩余量由 511110 实时回报,「停止摆摊」发 412050。下列只是默认值,实际上架在界面里配。
    "stall_open_delay": 0.5,         # 开摊(411040)到上架(411049)之间的间隔(秒),太快服务端可能 RST
    "stall_default_name": "小店",     # 摊位默认名(界面未填时用;按账号 encoding 编码,超 32 字节截断)
}


class LoginGate:
    """限制同时处于登录流程的账号数。一次性启动大量账号时,超出上限的在 acquire() 处排队,
    直到某个账号登录完成(进入在线主循环)调用 release() 让出名额。上限可热改(GUI/CLI)。

    acquire(stop_event) 等待期间会响应全局 STOP_EVENT 或传入的账号 stop_event:收到停止信号
    立即返回 False,调用方据此放弃本次登录,避免停机时线程卡死在队列里。"""

    def __init__(self, limit: int = 5) -> None:
        self._cond = threading.Condition()
        self._limit = max(1, int(limit))
        self._active = 0

    @property
    def limit(self) -> int:
        return self._limit

    def set_limit(self, limit: int) -> None:
        with self._cond:
            self._limit = max(1, int(limit))
            # 上限调大时唤醒所有排队者重新判断;调小不抢占已在登录的账号。
            self._cond.notify_all()

    def acquire_nowait(self) -> bool:
        """有空名额立刻占用并返回 True;满了返回 False(不阻塞,供 GUI 标「排队中」用)。"""
        with self._cond:
            if self._active < self._limit:
                self._active += 1
                return True
            return False

    def acquire(self, stop_event: "threading.Event | None" = None) -> bool:
        with self._cond:
            while self._active >= self._limit:
                if STOP_EVENT.is_set() or (stop_event is not None and stop_event.is_set()):
                    return False
                self._cond.wait(timeout=0.5)
            self._active += 1
            return True

    def release(self) -> None:
        with self._cond:
            if self._active > 0:
                self._active -= 1
            self._cond.notify(1)


# 全局单例:所有账号线程共用,串行化登录流程到配置的并发上限。
LOGIN_GATE = LoginGate(CONFIG["login_concurrency"])
