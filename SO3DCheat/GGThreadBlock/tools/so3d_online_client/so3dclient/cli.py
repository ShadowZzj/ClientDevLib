"""命令行入口:参数解析与 main()。"""
from __future__ import annotations

import argparse
import threading
from pathlib import Path

from .runtime import CONFIG, LOGIN_GATE
from .logio import main_log
from .config import append_accounts_to_file, load_accounts_config
from .session import (
    _close_shared_bridges,
    run_forever,
    start_account_threads,
    stop_threads,
    wait_account_threads,
)
from .selftest import self_test
from .gui import run_gui


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
    parser.add_argument("--login-concurrency", type=int, default=CONFIG["login_concurrency"], help="同时处于登录流程的账号数上限;一次启动很多账号时超出的排队,登录完成才让名额;<1 按 1")
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
    parser.add_argument("--chat-reconnect-delay", type=float, default=CONFIG["chat_reconnect_delay"], help="seconds to wait before retrying the chat/3000 socket after an RST/stale; chat failures only reconnect that socket, never the whole session")
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
    parser.add_argument("--zombie-entry-timeout", type=float, default=CONFIG["zombie_entry_timeout"], help="CG_ENTER 后这么多秒仍未从 game socket 收到任何字节即判定僵尸进场, 主动重登; 0 关闭")
    parser.add_argument("--debug-packets", action=argparse.BooleanOptionalAction, default=CONFIG["debug_packets"])
    parser.add_argument("--gui", dest="gui", action="store_true", default=True, help="显示账号配置界面")
    parser.add_argument("--no-gui", dest="gui", action="store_false", help="不显示界面，直接按配置启动")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--add-account",
        action="append",
        default=None,
        metavar="账号:密码[:角色名]",
        help="把账号写入 accounts 文件后退出(默认 enabled=False 不自动登录);可重复。密码不能含冒号",
    )
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    LOGIN_GATE.set_limit(getattr(args, "login_concurrency", CONFIG["login_concurrency"]))
    if args.self_test:
        self_test()
        return
    if args.add_account:
        new_accounts: list[dict[str, object]] = []
        for spec in args.add_account:
            parts = str(spec).split(":", 2)
            account = parts[0].strip() if parts else ""
            password = parts[1].strip() if len(parts) > 1 else ""
            character = parts[2].strip() if len(parts) > 2 else ""
            if not account or not password:
                parser.error(f"--add-account 需要 账号:密码[:角色名]，收到 {spec!r}")
            new_accounts.append({"account": account, "password": password, "character": character})
        added = append_accounts_to_file(Path(args.accounts_file), new_accounts)
        if added:
            main_log(f"[主程序] 已新增 {len(added)} 个账号到 {args.accounts_file}：{', '.join(added)}（默认不登录）")
        else:
            main_log(f"[主程序] 没有新增账号到 {args.accounts_file}（均已存在）")
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
