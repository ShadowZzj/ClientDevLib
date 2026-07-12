"""会话编排:登录建号、run() 主循环、线程启停、bridge 清理。"""
from __future__ import annotations

import argparse
import queue
import socket
import struct
import threading
import time
import traceback
from pathlib import Path

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .runtime import AccountController

# 正式服(prod)需要的 123.dll layer-1 解密桥;测试服不依赖,缺失不影响。
try:
    from . import bridge_client
except Exception:  # pragma: no cover - bridge optional for test mode
    bridge_client = None

# 自动选路(proxy_host="auto"):与 port_proxy 代理实例通过共享 registry 文件协调。
from . import auto_route
from . import proxy_pool

from .runtime import (
    LOGIN_GATE,
    PROD_BILL_PORT,
    PROD_CHAT_PORT,
    PROD_LOGIN_PORT,
    PROD_PROXY_HOST,
    STOP_EVENT,
    _CONTROLLER_CONTEXT,
    close_active_sockets,
    close_socket,
    current_controller,
    current_stop_event,
)
from .logio import (
    _LOG_CONTEXT,
    account_log_path,
    biz_log,
    event_log,
    main_log,
    open_account_biz_log,
    open_account_log,
    report_account_status,
)
from .protocol import (
    CG_ENTER,
    CG_HEARTBEAT,
    CG_INIT_411555,
    CG_INIT_411589,
    CG_INIT_411595,
    CG_PLAY_START,
    CG_POST_ENTER_411570,
    CG_POST_ENTER_411625,
    CL_GAMESERVER_CONNECT,
    CL_GET_CHARINFO,
    CL_LIVE,
    ChatStatus,
    LC_CHARCREATE_SUCCESS,
    LC_CHARINFO_SUCCESS,
    LC_GSERV_CONNECT_SUCCESS,
    LC_LOGIN_SECONDARY,
    LC_SERVER_LIST,
    LOGIN_FATAL_PROTOS,
    fixed_bytes,
    parse_int_config,
    short_hex,
    u32,
)
from .crypto import (
    DEFAULT_XOR_INDEX,
    KNOWN_ACCOUNT_XOR_INDEX,
    XOR_KEYS,
    handshake_xor_index,
    resolve_xor_index,
)
from .tables import item_label
from .config import account_namespace, parse_host_map, resolve_game_host
from .packets import (
    bill_account_op,
    bill_op1,
    bill_u32_op,
    cg_enter_body,
    create_character_packet,
    debug_game_recv,
    debug_game_send,
    extract_local_player_id,
    game_packet,
    get_client_file_info,
    login_packet,
    login_secondary_packet,
    merge_chat_status,
    parse_charinfo,
    parse_game_server_info,
    send_game1841,
    take_complete_frames,
)
from .netio import (
    connect_chat,
    drain_socket,
    read_game_packet,
    recv_until_proto,
    send_bill,
    send_chat_status_if_ready,
    socket_connect,
    try_read_bill_packet,
)
from .farm.state import FarmState, bag_count_item, update_farm_state
from .farm.fishing import fishing_start, fishing_stop, fishing_tick, send_revive_to_town, send_warp_to_coord
from .farm.vendor import (
    autobuy_run,
    autobuy_tick,
    automail_tick,
    autosell_run,
    autosell_tick,
    sell_single_slot,
    send_item_mail,
    send_money_mail,
)
from .farm.cash import buff_monitor_tick, cash_use_tick
from .farm.restock import bank_open, bank_pull_to_cash, restock_kick, restock_tick
from .farm.stall import (
    stall_check_sold_out,
    stall_open,
    stall_provision_kick,
    stall_provision_tick,
    stall_terminate,
)

from .logio import log_print as print


class ZombieEntryError(RuntimeError):
    """CG_ENTER 后 game 端口长时间零字节:服务端收了进场却不下发世界数据(顶号/卡服),
    player_id 永远拿不到,只有整套重登能恢复。run() 抛它,run_forever 当普通失败重试。"""


class SecondaryLoginStuck(RuntimeError):
    """CL_LOGIN_SECONDARY 流程异常中断(账号疑似已在线/顶号未清),归为可重试失败。"""


class FishingStallError(RuntimeError):
    """开着钓鱼却长时间无鱼获回包(521102):钓鱼卡死(顶号/服务端不下发),
    只有整套重登能恢复。run() 抛它,run_forever 当普通失败重试(同 ZombieEntryError)。"""


def _failure_cause(exc: BaseException) -> str:
    """把 run() 抛出的异常归类成一句中文原因,供 run_forever 的失败行展示;
    GUI 据这句话上色(僵尸/reset=红,secondary=黄)。"""
    if isinstance(exc, ZombieEntryError):
        return "僵尸进场"
    if isinstance(exc, FishingStallError):
        return "钓鱼无鱼获"
    if isinstance(exc, SecondaryLoginStuck):
        return "secondary登录未完成"
    text = str(exc) or exc.__class__.__name__
    low = text.lower()
    if isinstance(exc, ConnectionResetError) or "10054" in low or "强迫关闭" in text or "reset" in low:
        return "主端口断链(reset)"
    if ("game" in low and "closed" in low) or "broken pipe" in low or "10053" in low:
        return "主端口断链"
    return text.splitlines()[0][:80] if text else exc.__class__.__name__


def run_with_log(args: argparse.Namespace, log_dir: Path, *, console: bool) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    path = account_log_path(log_dir, args)
    with open_account_log(log_dir, args) as stream, open_account_biz_log(log_dir, args) as biz_stream:
        old_stream = getattr(_LOG_CONTEXT, "stream", None)
        old_console = getattr(_LOG_CONTEXT, "console", True)
        old_biz = getattr(_LOG_CONTEXT, "biz_stream", None)
        old_biz_label = getattr(_LOG_CONTEXT, "biz_label", "")
        _LOG_CONTEXT.stream = stream
        _LOG_CONTEXT.console = console
        _LOG_CONTEXT.biz_stream = biz_stream
        _LOG_CONTEXT.biz_label = f"{args.account}/{args.character}"
        try:
            print(f"[log] writing {path}")
            run(args)
        finally:
            _LOG_CONTEXT.stream = old_stream
            _LOG_CONTEXT.console = old_console
            _LOG_CONTEXT.biz_stream = old_biz
            _LOG_CONTEXT.biz_label = old_biz_label


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
    account_key = f"{getattr(args, 'account', '')}/{getattr(args, 'character', '')}"
    auto_mode = (
        str(getattr(args, "server_mode", "test") or "test").strip().lower() == "prod"
        and str(getattr(args, "proxy_host", "") or "").strip().lower() == "auto"
    )
    if auto_mode:
        _auto_reserve_slot(args, account_key)
    try:
        _run_forever_inner(
            args,
            log_dir,
            retry_delay=retry_delay,
            max_retries=max_retries,
            failure_window_seconds=failure_window_seconds,
            max_failures_per_window=max_failures_per_window,
            console=console,
        )
    finally:
        if auto_mode:
            try:
                auto_route.release_slot(account_key)
            except Exception:  # noqa: BLE001 - 释放名额失败不影响退出
                pass


def _run_forever_inner(
    args: argparse.Namespace,
    log_dir: Path,
    *,
    retry_delay: float,
    max_retries: int,
    failure_window_seconds: float,
    max_failures_per_window: int,
    console: bool,
) -> None:
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
        except Exception as exc:
            report_account_status(args.account, args.character, "离线")
            failure_count_in_window += 1
            cause = _failure_cause(exc)
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
            main_log(f"[{args.account}/{args.character}] 第 {attempt} 次失败({cause})，详情见 {path}")

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


def _resolve_proxy_host(args: argparse.Namespace) -> str:
    """决定本账号要连的代理监听别名:
      - proxy_host 为具体 127.x 别名 -> 直接用(手动分流);
      - proxy_host == "auto":
          * run_forever 已为本账号预订到 _auto_proxy_alias -> 用它(已占名额);
          * 否则(如建号流程)只读挑一个 rtt 最低的在线实例,挑不到回退默认;
      - 空 -> 默认单实例 PROD_PROXY_HOST。"""
    raw = str(getattr(args, "proxy_host", "") or "").strip()
    if raw and raw.lower() != "auto":
        return raw
    if raw.lower() == "auto":
        reserved = str(getattr(args, "_auto_proxy_alias", "") or "").strip()
        if reserved:
            # 预订过:确认该代理实例还活着;若它已死(进程退出被 registry 剔除),自动重选一个
            # ——必要时拉起新实例——并更新预订别名,实现断线后换节点自愈。
            try:
                live = auto_route.list_live_instances()
            except Exception:  # noqa: BLE001
                live = {}
            if reserved in live:
                return reserved
            account_key = f"{getattr(args, 'account', '')}/{getattr(args, 'character', '')}"
            try:
                mpn = int(getattr(args, "proxy_max_per_node", 0) or 0)
            except (TypeError, ValueError):
                mpn = 0
            try:
                res = proxy_pool.acquire(account_key, mpn)
            except Exception:  # noqa: BLE001
                res = None
            if res and res.get("alias"):
                args._auto_proxy_alias = res["alias"]
                print(f"[auto] 原代理实例 {reserved} 已失效,改用 {res['alias']} "
                      f"(node={res.get('node_ip')} rtt={res.get('rtt_ms')}ms)")
                return res["alias"]
            print(f"[auto] 原代理 {reserved} 失效且无法重选,回退默认代理 {PROD_PROXY_HOST}")
            return PROD_PROXY_HOST
        best = auto_route.peek_best()
        if best:
            print(f"[auto] 未预订名额,只读挑选代理实例 {best['alias']} "
                  f"(node={best.get('node_ip')} rtt={best.get('rtt_ms')}ms)")
            return best["alias"]
        print(f"[auto] 共享 registry 无在线代理实例,回退默认代理 {PROD_PROXY_HOST}")
        return PROD_PROXY_HOST
    return PROD_PROXY_HOST


def _auto_reserve_slot(args: argparse.Namespace, account_key: str) -> None:
    """auto 模式下账号上线时预订一个代理名额,把选中的 listen 别名记到 args._auto_proxy_alias,
    供 setup_server_mode 读取。走 proxy_pool:现有实例占满(或没有)会自动拉起新代理实例占下一个
    节点。彻底拉不起来才不写,由 setup_server_mode 回退默认。max_per_node 既是每节点上限,也是
    「到此值就开下一个代理」的阈值。"""
    try:
        max_per_node = int(getattr(args, "proxy_max_per_node", 0) or 0)
    except (TypeError, ValueError):
        max_per_node = 0
    try:
        result = proxy_pool.acquire(account_key, max_per_node)
    except Exception as exc:  # noqa: BLE001 - 选路失败不该挡住账号上线
        main_log(f"[auto] {account_key} 预订代理名额失败({exc}),本次回退默认代理")
        return
    if not result or not result.get("alias"):
        main_log(f"[auto] {account_key} 暂无可用代理实例,本次回退默认代理 {PROD_PROXY_HOST}")
        return
    args._auto_proxy_alias = result["alias"]
    tag = "复用" if result.get("reused") else "分配"
    over = " [超额:所有节点已达上限]" if result.get("overflow") else ""
    main_log(f"[auto] {account_key} {tag}代理 {result['alias']} "
             f"(node={result.get('node_ip')} rtt={result.get('rtt_ms')}ms "
             f"本节点第 {result.get('count')} 个){over}")


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
    # proxy_host: 空=默认单实例;127.x=手动分流;"auto"=自动选路(见 _resolve_proxy_host)。
    proxy_host = _resolve_proxy_host(args)
    args.login_host = proxy_host
    args.login_port = PROD_LOGIN_PORT
    args.bill_host = proxy_host
    args.bill_port = PROD_BILL_PORT
    args.chat_host = proxy_host
    args.chat_port = PROD_CHAT_PORT
    # 游戏服地址由 lobby 下发,prod 下也指向本地代理:固定 host,清空虚拟->公网映射。
    args.game_host = proxy_host
    args.game_host_map = {}

    bridge = bridge_client.connect_ready_bridge()
    print(f"[prod] 已连接 123.dll 解密 bridge (pid={bridge.pid}); 代理 host={proxy_host} "
          f"login={args.login_port} bill={args.bill_port} chat={args.chat_port}")
    return bridge


def run_create_character(args: argparse.Namespace) -> bool:
    """注册后建号专用最小流程(测试服直连):登入 -> 取角色列表 -> 若无同名角色则发
    CL_CREATE_CHARACTER(111004,复刻抓包默认外观/牧师)-> 等 LC_CHARCREATE_SUCCESS ->
    重新取列表确认。不进世界、不开 farm 循环。返回 True 表示角色已存在或创建成功。"""
    encoding = args.encoding
    key_index = resolve_xor_index(args.account, args.xor_index)
    debug = bool(args.debug_packets)
    login_bridge = setup_server_mode(args)
    character = args.character
    print(f"[create] account={args.account!r} character={character!r}")

    login = socket_connect(args.login_host, args.login_port, args.timeout)
    bill = None
    try:
        # ---- lobby 握手:协商 layer-2 XOR key ----
        # 必须先等到握手再发 CL_LOGIN:key 是逐连接从握手推出来的,fallback key 基本都
        # 是错的(实测某些账号握手 ~2.2s 才到,短的 2.0s 会误判超时→用错 key→被服务器踢)。
        try:
            raw, _decoded = read_game_packet(login, xor=False, key_index=DEFAULT_XOR_INDEX, timeout=args.timeout, bridge=login_bridge)
            handshake_key_index, _ = handshake_xor_index(raw)
            if key_index is None:
                key_index = handshake_key_index
            print(f"[10002] handshake xor key index={handshake_key_index} key={XOR_KEYS[handshake_key_index].hex(' ')}")
        except Exception as exc:
            print(f"[10002] no initial handshake before login: {exc}")
            if key_index is None:
                key_index = KNOWN_ACCOUNT_XOR_INDEX.get(args.account, DEFAULT_XOR_INDEX)
        if key_index is None:
            key_index = DEFAULT_XOR_INDEX
        print(f"[client] active xor key index={key_index} key={XOR_KEYS[key_index].hex(' ')}")

        # ---- CL_LOGIN(+ 可选 secondary,与正常登入一致)----
        login.sendall(login_packet(args.account, args.password, encoding, key_index))
        print("[10002] send CL_LOGIN")
        first_login_reply = recv_until_proto(
            login, LC_SERVER_LIST, key_index=key_index, timeout=args.timeout, debug=debug,
            fatal_protos=LOGIN_FATAL_PROTOS, return_protos={LC_LOGIN_SECONDARY}, bridge=login_bridge,
        )
        if len(first_login_reply) >= 8 and u32(first_login_reply, 4) == LC_LOGIN_SECONDARY:
            if args.secondary_login_delay > 0:
                print(f"[10002] wait {args.secondary_login_delay:g}s before CL_LOGIN_SECONDARY")
                time.sleep(args.secondary_login_delay)
            login.sendall(login_secondary_packet(args.account, args.password, encoding, key_index))
            print("[10002] send CL_LOGIN_SECONDARY")
            raw, _decoded = read_game_packet(login, xor=False, key_index=key_index, timeout=args.timeout, bridge=login_bridge)
            key_index, _ = handshake_xor_index(raw)
            print(f"[10002] secondary handshake xor key index={key_index} key={XOR_KEYS[key_index].hex(' ')}")
            login.sendall(login_packet(args.account, args.password, encoding, key_index))
            print("[10002] send CL_LOGIN after secondary handshake")
            recv_until_proto(
                login, LC_SERVER_LIST, key_index=key_index, timeout=args.timeout, debug=debug,
                fatal_protos=LOGIN_FATAL_PROTOS, bridge=login_bridge,
            )

        # ---- 1838 计费握手:与正常登入同序,确保 lobby 接受后续 charinfo ----
        bill = socket_connect(args.bill_host, args.bill_port, args.timeout)
        send_bill(bill, bill_op1(args.account, encoding), "open")
        try_read_bill_packet(bill, min(args.timeout, 3.0), "open-ack")
        send_bill(bill, bill_account_op(2, args.account, encoding), "account")
        send_bill(bill, struct.pack("<HI", 6, 3), "ready")
        for index in range(2):
            if try_read_bill_packet(bill, min(args.timeout, 3.0), f"post-ready-{index + 1}") is None:
                break

        def fetch_charinfo() -> tuple[int, str, bytes]:
            login.sendall(game_packet(CL_GET_CHARINFO, struct.pack("<II", args.server_id, args.channel_id), xor=True, key_index=key_index))
            print(f"[10002] send CL_GET_CHARINFO server={args.server_id} channel={args.channel_id}")
            info = recv_until_proto(login, LC_CHARINFO_SUCCESS, key_index=key_index, timeout=args.timeout, debug=debug, bridge=login_bridge)
            cnt, name = parse_charinfo(info)
            return cnt, name, info

        name_block = fixed_bytes(character, 16, encoding).rstrip(b"\0")

        # ---- 建号前先取列表:若同名角色已存在则幂等返回 ----
        char_count, _first_name, charinfo = fetch_charinfo()
        if name_block and name_block in charinfo:
            print(f"[create] character {character!r} already exists (count={char_count}); skip create")
            return True

        # 1838 选服握手发完即释放(建号在 lobby 完成,不进世界)
        send_bill(bill, bill_u32_op(4, args.server_id), "select-server")
        close_socket(bill)
        bill = None

        # ---- CL_CREATE_CHARACTER ----
        packet = create_character_packet(character, args.server_id, encoding, key_index)
        debug_game_send("CL_CREATE_CHARACTER", packet, xor=True, key_index=key_index, enabled=debug)
        login.sendall(packet)
        print(f"[10002] send CL_CREATE_CHARACTER character={character!r}")
        recv_until_proto(
            login, LC_CHARCREATE_SUCCESS, key_index=key_index, timeout=args.timeout, debug=debug,
            fatal_protos=LOGIN_FATAL_PROTOS, bridge=login_bridge,
        )
        print(f"[create] LC_CHARCREATE_SUCCESS received for {character!r}")

        # ---- 复核:重新取列表确认角色已落库 ----
        char_count, _first_name, charinfo = fetch_charinfo()
        if name_block in charinfo:
            print(f"[create] verified character {character!r} present (count={char_count})")
            return True
        print(f"[warn] character {character!r} not found in post-create LC_CHARINFO_SUCCESS")
        return False
    finally:
        close_socket(bill)
        close_socket(login)


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

    # 登录并发闸门:同时处于登录流程的账号数受限,超出的在此排队,直到有账号登录完成让出名额。
    # 先非阻塞探一下:没名额就把状态标「排队中」再阻塞等;收到本账号/全局停止信号即放弃本次登录。
    if not LOGIN_GATE.acquire_nowait():
        report_account_status(args.account, args.character, "排队中")
        if not LOGIN_GATE.acquire(current_stop_event()):
            print("[login] 排队等待登录名额时收到停止信号,放弃本次登录")
            if login_bridge is not None:
                login_bridge.close()
            return
        report_account_status(args.account, args.character, "登录中")
    login_gate_held = True
    login = None
    try:
        login = socket_connect(args.login_host, args.login_port, args.timeout)
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
            try:
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
            except Exception as exc:
                # secondary 流程没走完(读不到二次握手 / 收不到服务器列表):多半是账号已在线/顶号
                # 未清,服务端把二次登录晾着。归为 SecondaryLoginStuck,失败行标黄、整套重登。
                raise SecondaryLoginStuck(
                    "CL_LOGIN_SECONDARY 流程未完成(账号疑似已在线/顶号未清)"
                ) from exc

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
                # 僵尸进场兜底:记 CG_ENTER 时刻;game socket 收到第一个字节就置 True。
                # 主循环里若超时仍为 False,判定僵尸进场、抛 ZombieEntryError 触发整套重登。
                enter_sent_at = time.monotonic()
                game_rx_seen = False

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
                            game_rx_seen = True
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
                                game_rx_seen = True
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
                    if player_id:
                        chat, last_chat_rx, last_chat_status = connect_chat(
                            args, character, encoding, player_id, chat_status, debug=debug,
                        )
                    else:
                        # 没拿到本地 player_id 就连 chat 只会用 id=0 登录,被聊天服立刻 RST、陷入无限重连。
                        # 先不连,等主循环从 game 流里解析到 player_id 后,再由下面的重连逻辑连上。
                        print("[chat] 未取得本地 player_id,推迟 chat 登录,待解析到后再连")
                        last_chat_rx = time.monotonic()
                        last_chat_status = None
                else:
                    last_chat_rx = time.monotonic()
                    last_chat_status = None

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
                next_chat_retry = 0.0
                zombie_entry_timeout = float(getattr(args, "zombie_entry_timeout", 0.0) or 0.0)
                fish_stall_timeout = float(getattr(args, "fishing_stall_timeout", 0.0) or 0.0)
                stop_at = time.monotonic() + args.run_seconds if args.run_seconds > 0 else None
                print("[loop] online simulation is running; Ctrl+C to stop")
                report_account_status(args.account, args.character, "在线")
                main_log(f"[{args.account}/{args.character}] 登录成功，已上线")
                # 已进入在线状态,登录流程结束,立即让出登录名额给排队中的账号。
                if login_gate_held:
                    LOGIN_GATE.release()
                    login_gate_held = False
                stop_event = current_stop_event()
                args._key_index = key_index
                farm = FarmState(args)
                farm.real_character = game_server.character   # 服务器回显的真实登录角色
                farm.player_id = player_id                    # 入场前(chat-wait)已解析到的就先带上
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
                # 钓鱼无鱼获看门狗:开着钓鱼时,catch_total 每涨一次就把 deadline 往后推;
                # 超时(默认 2 分钟)没涨即判钓鱼卡死,抛 FishingStallError 整套重登。
                fish_watch_catch = farm.catch_total
                fish_watch_deadline = time.monotonic() + fish_stall_timeout
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
                        game_rx_seen = True
                    if (not game_rx_seen and zombie_entry_timeout > 0
                            and now - enter_sent_at > zombie_entry_timeout):
                        # CG_ENTER 后这么久 game 端口一个字节都没回:僵尸进场。biz 行带「僵尸进场」
                        # 关键字让 GUI 标红;抛异常交给 run_forever 整套重登(比干等 socket 超时快得多)。
                        biz_log(
                            f"僵尸进场:CG_ENTER 后 {zombie_entry_timeout:g}s 内 game 端口无任何数据，触发重登",
                            ui=True,
                        )
                        raise ZombieEntryError(
                            f"僵尸进场:CG_ENTER 后 {zombie_entry_timeout:g}s game 端口零字节"
                        )
                    framed = take_complete_frames(game_buf)
                    if framed:
                        update_farm_state(farm, framed)
                        if merge_chat_status(chat_status, framed, label="loop"):
                            player_id = chat_status.player_id or player_id
                            farm.player_id = player_id
                            if chat is not None:
                                try:
                                    last_chat_status = send_chat_status_if_ready(
                                        chat,
                                        chat_status,
                                        character,
                                        encoding,
                                        last_chat_status,
                                    )
                                except OSError as exc:
                                    event_log(f"[{args.account}/{args.character}] chat(3000)发送失败，{args.chat_reconnect_delay:g}s后单独重连：{exc!r}")
                                    close_socket(chat)
                                    chat = None
                                    next_chat_retry = now + args.chat_reconnect_delay
                    # chat(3000)是独立会话:RST/stale 只重连这一条 socket,绝不拖垮 game/login 整局。
                    if chat is not None:
                        try:
                            chat_chunks = drain_socket(chat, "chat", enabled=debug)
                        except OSError as exc:
                            event_log(f"[{args.account}/{args.character}] chat(3000)断开，{args.chat_reconnect_delay:g}s后单独重连：{exc!r}")
                            close_socket(chat)
                            chat = None
                            next_chat_retry = now + args.chat_reconnect_delay
                        else:
                            if chat_chunks:
                                last_chat_rx = now
                            chat_watchdog_seconds = float(getattr(args, "chat_watchdog_seconds", 0.0) or 0.0)
                            if chat_watchdog_seconds > 0 and now - last_chat_rx > chat_watchdog_seconds:
                                event_log(
                                    f"[{args.account}/{args.character}] chat(3000) {now - last_chat_rx:.1f}s无数据"
                                    f"(watchdog={chat_watchdog_seconds:g}s)，{args.chat_reconnect_delay:g}s后单独重连"
                                )
                                close_socket(chat)
                                chat = None
                                next_chat_retry = now + args.chat_reconnect_delay
                    elif args.chat_enabled and player_id and now >= next_chat_retry:
                        try:
                            chat, last_chat_rx, last_chat_status = connect_chat(
                                args, character, encoding, player_id, chat_status, debug=debug,
                            )
                            event_log(f"[{args.account}/{args.character}] chat(3000)已连接 player_id=0x{player_id:08X}")
                        except OSError as exc:
                            event_log(f"[{args.account}/{args.character}] chat(3000)重连失败，{args.chat_reconnect_delay:g}s后重试：{exc!r}")
                            next_chat_retry = now + args.chat_reconnect_delay
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
                            elif isinstance(action, tuple) and action and action[0] == "revive":
                                send_revive_to_town(farm, game, int(action[1]))
                            elif isinstance(action, tuple) and action and action[0] == "warp":
                                send_warp_to_coord(farm, game, int(action[1]), int(action[2]))
                            elif action == "fishing_start":
                                # 手动开钓:停竿->等1s->重抛->打开 fishing_enabled,之后 fishing_tick 定时发中鱼包
                                fishing_start(farm, game, player_id, reason="手动")
                            elif action == "fishing_stop":
                                # 手动停钓:关 fishing_enabled(主循环不再调 fishing_tick)+ 发停竿包取消当前动作
                                fishing_stop(farm, game, reason="手动")
                            elif isinstance(action, tuple) and action and action[0] == "sell_slot":
                                sell_single_slot(farm, game, int(action[1]), reason="手动单格")
                            elif isinstance(action, tuple) and action and action[0] == "mail":
                                send_money_mail(farm, game, str(action[1]), int(action[2]), reason="手动测试")
                            elif isinstance(action, tuple) and action and action[0] == "mail_item":
                                send_item_mail(farm, game, str(action[1]), int(action[2]),
                                               expect_item_id=int(action[3]), reason="批量邮寄")
                            elif isinstance(action, tuple) and action and action[0] == "restock":
                                # 手动测试:界面解析出的是网店itemID;买到后按同一 id 当背包模板搬运
                                # (测试默认网店id=背包id;两边不同名的物品请走「自动补货」配置)
                                restock_kick(farm, game, int(action[1]), int(action[2]),
                                             bag_id=int(action[1]), reason="手动测试")
                            elif isinstance(action, tuple) and action and action[0] == "stall_open":
                                # ("stall_open", name, listings):listings=[(bag_type, src_slot, item_id, qty, price)]
                                stall_open(farm, game, str(action[1]), action[2], reason="手动")
                            elif action == "stall_close" or (
                                isinstance(action, tuple) and action and action[0] == "stall_close"):
                                stall_terminate(farm, game, reason="手动")
                            elif isinstance(action, tuple) and action and action[0] == "stall_provision":
                                # ("stall_provision", name, rows):rows=[(shop_text, buy_count, price)]
                                # 一键:逐项泡点买 -> 开 bank -> 后续 tick 搬进 cash -> 从 cash 上架
                                stall_provision_kick(farm, game, str(action[1]), action[2], reason="手动")
                            elif action == "bank_open" or (
                                isinstance(action, tuple) and action and action[0] == "bank_open"):
                                # 界面「刷新银行」:发开仓库包(411154/411644),内容随后由 511320 回流刷 farm.bank
                                bank_open(farm, game)
                            elif isinstance(action, tuple) and action and action[0] == "bank_pull":
                                # ("bank_pull", template_id, count):把 bank 里该模板的所有实例搬进 cash,
                                # count<=0 搬全部;不可堆叠物品每件落独立 cash 空格,cash 满即停
                                n = bank_pull_to_cash(farm, game, int(action[1]), int(action[2]), reason="手动")
                                biz_log(f"银行拖出 {item_label(int(action[1]))} 共 {n} 件到 cash", ui=True)
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
                    if args.fishing_enabled and fish_stall_timeout > 0:
                        if farm.catch_total > fish_watch_catch:
                            fish_watch_catch = farm.catch_total
                            fish_watch_deadline = now + fish_stall_timeout
                        elif now >= fish_watch_deadline:
                            biz_log(
                                f"钓鱼卡死:开着钓鱼 {fish_stall_timeout:g}s 内无任何鱼获回包，触发重登",
                                ui=True,
                            )
                            raise FishingStallError(
                                f"钓鱼无鱼获:{fish_stall_timeout:g}s 内 catch_total 未增长"
                            )
                    else:
                        # 钓鱼关着(或看门狗关闭):不计时,窗口只在开钓鱼时累计。
                        fish_watch_catch = farm.catch_total
                        fish_watch_deadline = now + fish_stall_timeout
                    if args.fishing_enabled:
                        fishing_tick(farm, game, player_id, now)
                    cash_use_tick(farm, game, now)
                    buff_monitor_tick(farm, game, now)
                    automail_tick(farm, game, now)
                    restock_tick(farm, game, now)
                    stall_provision_tick(farm, game, now)
                    stall_check_sold_out(farm, game)
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
        # 登录中途失败/断链时也要把名额还回去(成功上线时已在主循环前 release 过,这里不重复)。
        if login_gate_held:
            LOGIN_GATE.release()


def _close_shared_bridges() -> None:
    """进程退出时统一回收共享 bridge 管道(测试服无 bridge_client 时无操作)。"""
    if bridge_client is not None:
        bridge_client.close_shared_bridges()
