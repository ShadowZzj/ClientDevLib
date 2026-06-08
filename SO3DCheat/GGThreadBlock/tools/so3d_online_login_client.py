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
import re
import select
import socket
import struct
import sys
import threading
import time
import traceback
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    from Crypto.Cipher import Blowfish as PyCryptoBlowfish
except Exception:  # pragma: no cover - optional dependency fallback
    PyCryptoBlowfish = None


_PRINT_LOCK = threading.Lock()
_LOG_CONTEXT = threading.local()
_BUILTIN_PRINT = builtins.print
STOP_EVENT = threading.Event()
_SOCKET_LOCK = threading.RLock()
_ACTIVE_SOCKETS: set[socket.socket] = set()
_LOG_HANDLER = None
_STATUS_HANDLER = None


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


def main_log(message: str) -> None:
    _emit_line(f"{_timestamp()} | {message}", console=True)


def register_socket(sock: socket.socket) -> socket.socket:
    with _SOCKET_LOCK:
        _ACTIVE_SOCKETS.add(sock)
    return sock


def close_socket(sock: socket.socket | None) -> None:
    if sock is None:
        return
    with _SOCKET_LOCK:
        _ACTIVE_SOCKETS.discard(sock)
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

# Edit this block for normal use, then run:
#   python SO3DCheat\GGThreadBlock\tools\so3d_online_login_client.py
CONFIG = {
    "accounts_file": str(APP_DIR / "so3d_online_accounts.json"),
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
    "game_alive_interval": 10.0,
    "post_enter_init_delay": 1.0,
    "debug_packets": True,
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
CG_INIT_411555 = 411555
CG_INIT_411589 = 411589
CG_INIT_411595 = 411595
CG_POST_ENTER_411570 = 411570
CG_POST_ENTER_411625 = 411625
CG_PLAY_START = 412039
SC_LOCAL_MAP_STATE = 511001
SC_MAP_HINT = 521051

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
    CG_INIT_411555: "CG_INIT_411555",
    CG_INIT_411589: "CG_INIT_411589",
    CG_INIT_411595: "CG_INIT_411595",
    CG_POST_ENTER_411570: "CG_POST_ENTER_411570",
    CG_POST_ENTER_411625: "CG_POST_ENTER_411625",
    CG_PLAY_START: "CG_PLAY_START",
    SC_LOCAL_MAP_STATE: "SC_LOCAL_MAP_STATE",
    SC_MAP_HINT: "SC_MAP_HINT",
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
    }


def load_accounts_config(path: Path) -> tuple[dict[str, object], list[dict[str, object]]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(data, list):
        accounts = data
        defaults: dict[str, object] = {}
    elif isinstance(data, dict):
        defaults_raw = data.get("defaults", {})
        accounts = data.get("accounts", [])
        if not isinstance(defaults_raw, dict):
            raise ValueError("defaults must be an object")
        defaults = dict(defaults_raw)
    else:
        raise ValueError("accounts config must be a JSON array or object")

    if not isinstance(accounts, list):
        raise ValueError("accounts must be an array")

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
    ):
        merged[key] = float(merged.get(key, config_defaults()[key]))
    for key in ("chat_enabled", "chat_status_enabled", "debug_packets"):
        value = merged.get(key)
        if isinstance(value, str):
            merged[key] = value.strip().lower() not in ("0", "false", "no", "off")

    return namespace_from_config(merged)


def account_log_path(log_dir: Path, args: argparse.Namespace) -> Path:
    name = sanitize_log_name(f"{args.account}_{args.character}")
    return log_dir / f"{name}.log"


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


def run_with_log(args: argparse.Namespace, log_dir: Path, *, console: bool) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    path = account_log_path(log_dir, args)
    with open_account_log(log_dir, args) as stream:
        old_stream = getattr(_LOG_CONTEXT, "stream", None)
        old_console = getattr(_LOG_CONTEXT, "console", True)
        _LOG_CONTEXT.stream = stream
        _LOG_CONTEXT.console = console
        try:
            print(f"[log] writing {path}")
            run(args)
        finally:
            _LOG_CONTEXT.stream = old_stream
            _LOG_CONTEXT.console = old_console


def run_forever(
    args: argparse.Namespace,
    log_dir: Path,
    *,
    retry_delay: float,
    max_retries: int,
    failure_window_seconds: float,
    max_failures_per_window: int,
    console: bool,
) -> None:
    attempt = 0
    failure_window_seconds = max(1.0, float(failure_window_seconds or 0.0))
    max_failures_per_window = int(max_failures_per_window or 0)
    failure_window_start = time.monotonic()
    failure_count_in_window = 0
    while not STOP_EVENT.is_set():
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
            if STOP_EVENT.wait(wait_seconds):
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
        STOP_EVENT.wait(max(0.1, retry_delay))


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


def read_game_packet(sock: socket.socket, *, xor: bool, key_index: int, timeout: float) -> tuple[bytes, bytes]:
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
    chunks: list[bytes] = []
    for _ in range(max_reads):
        if STOP_EVENT.is_set():
            return chunks
        if sock.fileno() < 0:
            if STOP_EVENT.is_set():
                return chunks
            raise ConnectionError(f"{label} socket closed")
        try:
            readable, _writable, _errored = select.select([sock], [], [], 0)
        except ValueError as exc:
            if STOP_EVENT.is_set():
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
) -> bytes:
    deadline = time.monotonic() + timeout
    seen: list[int] = []
    fatal_protos = set() if fatal_protos is None else fatal_protos
    return_protos = set() if return_protos is None else return_protos
    while True:
        remaining = max(0.2, deadline - time.monotonic())
        try:
            raw, decoded = read_game_packet(sock, xor=True, key_index=key_index, timeout=remaining)
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


def run(args: argparse.Namespace) -> None:
    encoding = args.encoding
    configured_key_index = resolve_xor_index(args.account, args.xor_index)
    key_index = configured_key_index
    debug = bool(args.debug_packets)
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
            raw, decoded = read_game_packet(login, xor=False, key_index=DEFAULT_XOR_INDEX, timeout=2.0)
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
        )
        if len(first_login_reply) >= 8 and u32(first_login_reply, 4) == LC_LOGIN_SECONDARY:
            if args.secondary_login_delay > 0:
                print(f"[10002] wait {args.secondary_login_delay:g}s before CL_LOGIN_SECONDARY")
                time.sleep(args.secondary_login_delay)
            packet = login_secondary_packet(args.account, args.password, encoding, key_index)
            debug_game_send("CL_LOGIN_SECONDARY", packet, xor=True, key_index=key_index, enabled=debug)
            login.sendall(packet)
            print("[10002] send CL_LOGIN_SECONDARY")

            raw, decoded = read_game_packet(login, xor=False, key_index=key_index, timeout=args.timeout)
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
            charinfo = recv_until_proto(login, LC_CHARINFO_SUCCESS, key_index=key_index, timeout=args.timeout, debug=debug)
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

            packet = game_packet(
                CL_GAMESERVER_CONNECT,
                struct.pack("<II16s", args.server_id, args.channel_id, fixed_bytes(character, 16, encoding)),
                xor=True,
                key_index=key_index,
            )
            debug_game_send("CL_GAMESERVER_CONNECT", packet, xor=True, key_index=key_index, enabled=debug)
            login.sendall(packet)
            print(f"[10002] send CL_GAMESERVER_CONNECT character={character!r}")
            gs_packet = recv_until_proto(login, LC_GSERV_CONNECT_SUCCESS, key_index=key_index, timeout=args.timeout, debug=debug)
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
                next_bill_live = time.monotonic() + args.bill_alive_interval
                next_game_alive = time.monotonic() + args.game_alive_interval
                stop_at = time.monotonic() + args.run_seconds if args.run_seconds > 0 else None
                print("[loop] online simulation is running; Ctrl+C to stop")
                report_account_status(args.account, args.character, "在线")
                main_log(f"[{args.account}/{args.character}] 登录成功，已上线")
                while True:
                    now = time.monotonic()
                    if stop_at is not None and now >= stop_at:
                        print(f"[loop] run_seconds reached ({args.run_seconds:g}s); stopping")
                        break
                    drain_socket(login, "lobby", enabled=debug)
                    drain_socket(bill, "bill", enabled=debug)
                    for chunk in drain_socket(game, "game", enabled=debug):
                        if merge_chat_status(chat_status, chunk, label="loop"):
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
                    if now >= next_bill_live:
                        send_bill(bill, bill_account_op(5, args.account, encoding), "alive")
                        next_bill_live = now + args.bill_alive_interval
                    if now >= next_game_alive:
                        alive_body = struct.pack("<I", CG_GAME_ALIVE)
                        send_game1841(game, CG_GAME_ALIVE, alive_body, key_index=key_index, debug=debug)
                        next_game_alive = now + args.game_alive_interval
                    time.sleep(0.25)
            finally:
                if chat is not None:
                    close_socket(chat)
                close_socket(game)
        finally:
            close_socket(bill)
    finally:
        close_socket(login)


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
    print("self-test ok")


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
    )
    return {key: CONFIG[key] for key in keys}


def load_accounts_document(path: Path) -> tuple[dict[str, object], list[dict[str, object]]]:
    if not path.exists():
        return default_accounts_defaults(), [
            {
                "account": CONFIG["account"],
                "password": CONFIG["password"],
                "character": CONFIG["character"],
            }
        ]

    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(data, list):
        defaults: dict[str, object] = default_accounts_defaults()
        accounts = data
    elif isinstance(data, dict):
        defaults_raw = data.get("defaults", {})
        accounts = data.get("accounts", [])
        defaults = dict(defaults_raw) if isinstance(defaults_raw, dict) else default_accounts_defaults()
    else:
        raise ValueError("accounts config must be a JSON array or object")

    clean_accounts: list[dict[str, object]] = []
    if isinstance(accounts, list):
        for item in accounts:
            if isinstance(item, dict):
                clean_accounts.append(dict(item))
    return defaults, clean_accounts


def save_accounts_document(path: Path, defaults: dict[str, object], accounts: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {"defaults": defaults, "accounts": accounts}
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class OnlineLoginGui:
    def __init__(self, root, args: argparse.Namespace) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = root
        self.args = args
        self.accounts_file = Path(args.accounts_file)
        self.log_dir = Path(args.log_dir)
        self.log_queue: queue.Queue[str] = queue.Queue()
        self.status_queue: queue.Queue[tuple[str, str, str]] = queue.Queue()
        self.threads: list[threading.Thread] = []
        self.runner: threading.Thread | None = None
        self.running = False

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

        columns = ("account", "password", "character", "online")
        self.tree = ttk.Treeview(list_frame, columns=columns, show="headings", height=8)
        self.tree.heading("account", text="账号")
        self.tree.heading("password", text="密码")
        self.tree.heading("character", text="角色名")
        self.tree.heading("online", text="在线状态")
        self.tree.column("account", width=160, anchor=tk.W)
        self.tree.column("password", width=150, anchor=tk.W)
        self.tree.column("character", width=160, anchor=tk.W)
        self.tree.column("online", width=100, anchor=tk.CENTER)
        self.tree.tag_configure("online", foreground="#1a7f37")
        self.tree.tag_configure("offline", foreground="#999999")
        self.tree.tag_configure("connecting", foreground="#b8860b")
        scroll = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.grid(row=0, column=0, sticky="nsew")
        scroll.grid(row=0, column=1, sticky="ns")
        list_frame.columnconfigure(0, weight=1)
        list_frame.rowconfigure(0, weight=1)

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
        self.start_button = ttk.Button(edit, text="确定并启动", command=self.start)
        self.start_button.grid(row=1, column=5, padx=(0, 6))
        self.stop_button = ttk.Button(edit, text="停止", command=self.stop, state=tk.DISABLED)
        self.stop_button.grid(row=1, column=6)
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

    def insert_account(self, account: dict[str, object]) -> None:
        self.tree.insert(
            "",
            self.tk.END,
            values=(
                str(account.get("account") or ""),
                str(account.get("password") or ""),
                str(account.get("character") or ""),
                "",
            ),
            tags=("offline",),
        )

    def clear_accounts(self) -> None:
        for iid in self.tree.get_children():
            self.tree.delete(iid)

    def reload_accounts_from_json(self) -> list[dict[str, object]]:
        self.defaults, accounts = load_accounts_document(self.accounts_file)
        self.clear_accounts()
        for account in accounts:
            self.insert_account(account)
        return accounts

    def save_accounts_from_tree(self) -> None:
        save_accounts_document(self.accounts_file, self.defaults, self.account_rows())
        main_log(f"[界面] 已保存账号配置到 {self.accounts_file}")

    def account_rows(self) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for iid in self.tree.get_children():
            values = self.tree.item(iid, "values")
            account, password, character = values[0], values[1], values[2]
            rows.append({"account": account, "password": password, "character": character})
        return rows

    def add_account(self) -> None:
        account = self.account_var.get().strip()
        password = self.password_var.get().strip()
        character = self.character_var.get().strip()
        if not account or not password or not character:
            main_log("[界面] 账号、密码、角色名不能为空")
            return
        self.insert_account({"account": account, "password": password, "character": character})
        self.account_var.set("")
        self.password_var.set("")
        self.character_var.set("")
        self.save_accounts_from_tree()

    def delete_selected(self) -> None:
        for iid in self.tree.selection():
            self.tree.delete(iid)
        self.save_accounts_from_tree()

    def set_running(self, running: bool) -> None:
        self.running = running
        state = self.tk.DISABLED if running else self.tk.NORMAL
        self.add_button.configure(state=state)
        self.delete_button.configure(state=state)
        self.start_button.configure(state=state)
        self.stop_button.configure(state=self.tk.NORMAL if running else self.tk.DISABLED)

    def start(self) -> None:
        if self.running:
            return
        try:
            accounts = self.reload_accounts_from_json()
            main_log(f"[界面] 已从 {self.accounts_file} 重新读取 {len(accounts)} 个账号")
        except Exception:
            main_log("[界面] 读取账号配置失败")
            for line in traceback.format_exc().rstrip().splitlines():
                main_log(line)
            return
        if not accounts:
            main_log("[界面] 没有可启动的账号")
            return
        self.set_running(True)
        self.runner = threading.Thread(target=self.run_worker, daemon=True)
        self.runner.start()

    def run_worker(self) -> None:
        try:
            defaults, accounts = load_accounts_config(self.accounts_file)
            if not accounts:
                main_log(f"[主程序] 配置中没有启用的账号：{self.accounts_file}")
                return
            main_log(f"[主程序] 已从 {self.accounts_file} 读取 {len(accounts)} 个账号")
            main_log(f"[主程序] 每个账号的日志目录：{self.log_dir}")
            self.threads = start_account_threads(
                self.args,
                defaults,
                accounts,
                self.log_dir,
                console_account_logs=False,
            )
            wait_account_threads(self.threads)
        except Exception:
            main_log("[界面] 登录线程异常")
            for line in traceback.format_exc().rstrip().splitlines():
                main_log(line)
        finally:
            self.root.after(0, lambda: self.set_running(False))

    def stop(self) -> None:
        if not self.running:
            return
        threading.Thread(
            target=stop_threads,
            args=(list(self.threads), "[主程序] 已点击停止，正在关闭账号连接"),
            daemon=True,
        ).start()

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
        self.root.after(100, self.drain_logs)

    def drain_statuses(self) -> None:
        try:
            while True:
                account, character, status = self.status_queue.get_nowait()
                self.apply_status(account, character, status)
        except queue.Empty:
            pass

    def apply_status(self, account: str, character: str, status: str) -> None:
        tag = {"在线": "online", "登录中": "connecting"}.get(status, "offline")
        for iid in self.tree.get_children():
            values = list(self.tree.item(iid, "values"))
            if len(values) >= 3 and values[0] == account and values[2] == character:
                while len(values) < 4:
                    values.append("")
                values[3] = status
                self.tree.item(iid, values=values, tags=(tag,))
                break

    def on_close(self) -> None:
        if self.running:
            stop_threads(list(self.threads), "[主程序] 界面关闭，正在停止账号连接")
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


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.gui:
        run_gui(args)
        return

    accounts_file = Path(args.accounts_file)
    log_dir = Path(args.log_dir)
    if not args.single and accounts_file.exists():
        defaults, accounts = load_accounts_config(accounts_file)
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


if __name__ == "__main__":
    main()
