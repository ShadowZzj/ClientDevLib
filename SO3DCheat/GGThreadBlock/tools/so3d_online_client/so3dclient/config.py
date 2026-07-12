"""默认配置、accounts/defaults 文档读写、命名空间构造、host 解析。"""
from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

from .runtime import CONFIG
from .protocol import FARM_FIELD_SPECS, parse_int_config
from .tables import resolve_item_id, resolve_item_id_list

from .logio import log_print as print


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
        "chat_reconnect_delay": CONFIG["chat_reconnect_delay"],
        "failure_window_seconds": CONFIG["failure_window_seconds"],
        "max_failures_per_window": CONFIG["max_failures_per_window"],
        "server_id": CONFIG["server_id"],
        "channel_id": CONFIG["channel_id"],
        "server_mode": CONFIG["server_mode"],
        "proxy_host": CONFIG["proxy_host"],
        "proxy_max_per_node": CONFIG["proxy_max_per_node"],
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
        "zombie_entry_timeout": CONFIG["zombie_entry_timeout"],
        "debug_packets": CONFIG["debug_packets"],
        "log_max_mb": CONFIG["log_max_mb"],
        "log_max_files": CONFIG["log_max_files"],
        "fishing_enabled": CONFIG["fishing_enabled"],
        "fishing_bait_item_id": CONFIG["fishing_bait_item_id"],
        "fishing_special_bait": CONFIG["fishing_special_bait"],
        "fishing_reel_interval": CONFIG["fishing_reel_interval"],
        "fishing_stall_timeout": CONFIG["fishing_stall_timeout"],
        "autosell_enabled": CONFIG["autosell_enabled"],
        "autosell_min_free_slots": CONFIG["autosell_min_free_slots"],
        "autosell_start_slot": CONFIG["autosell_start_slot"],
        "autosell_end_slot": CONFIG["autosell_end_slot"],
        "autosell_keep_item_ids": CONFIG["autosell_keep_item_ids"],
        "autosell_extra_item_ids": CONFIG["autosell_extra_item_ids"],
        "autosell_per_packet_delay": CONFIG["autosell_per_packet_delay"],
        "autobuy_enabled": CONFIG["autobuy_enabled"],
        "autobuy_min_bait": CONFIG["autobuy_min_bait"],
        "autobuy_item_id": CONFIG["autobuy_item_id"],
        "autobuy_count": CONFIG["autobuy_count"],
        "autobuy_open_delay": CONFIG["autobuy_open_delay"],
        "autobuy_buy_delay": CONFIG["autobuy_buy_delay"],
        "cash_use_schedule": CONFIG["cash_use_schedule"],
        "cash_low_stock_threshold": CONFIG["cash_low_stock_threshold"],
        "buff_monitor_schedule": CONFIG["buff_monitor_schedule"],
        "automail_enabled": CONFIG["automail_enabled"],
        "automail_recipient": CONFIG["automail_recipient"],
        "automail_money_threshold": CONFIG["automail_money_threshold"],
        "automail_amount": CONFIG["automail_amount"],
        "automail_poll_interval": CONFIG["automail_poll_interval"],
        "automail_whitelist": CONFIG["automail_whitelist"],
        "restock_enabled": CONFIG["restock_enabled"],
        "restock_schedule": CONFIG["restock_schedule"],
        "stall_open_delay": CONFIG["stall_open_delay"],
        "stall_default_name": CONFIG["stall_default_name"],
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
        "proxy_max_per_node",
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
        "zombie_entry_timeout",
        "chat_player_id_wait",
        "chat_watchdog_seconds",
        "chat_reconnect_delay",
        "failure_window_seconds",
        "log_max_mb",
        "fishing_reel_interval",
        "fishing_stall_timeout",
        "autosell_per_packet_delay",
        "autobuy_open_delay",
        "autobuy_buy_delay",
        "automail_poll_interval",
        "stall_open_delay",
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
    merged["autosell_extra_item_ids"] = resolve_item_id_list(merged.get("autosell_extra_item_ids"))

    return namespace_from_config(merged)


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
        "buff_monitor_schedule",
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


_ACCOUNTS_LOCK_SUFFIX = ".lock"
_ACCOUNTS_LOCK_STALE_SECONDS = 30.0


class _AccountsFileLock:
    """跨进程的尽力而为文件锁:用 O_CREAT|O_EXCL 抢一个 sidecar .lock 文件。
    抢不到就轮询等待;超过 stale 秒数的旧锁视为残留(持锁进程已崩溃)直接夺取,避免死锁;
    等到 timeout 仍抢不到则降级为「无锁」继续(尽力而为,不阻塞写入)。
    register.py(外部进程)与 GUI 写同一 accounts.json 时用它串行化读改写,防止互相覆盖。"""

    def __init__(self, target: Path, *, timeout: float = 10.0) -> None:
        self.lock_path = target.with_name(target.name + _ACCOUNTS_LOCK_SUFFIX)
        self.timeout = timeout
        self._fd: "int | None" = None

    def __enter__(self) -> "_AccountsFileLock":
        deadline = time.monotonic() + self.timeout
        while True:
            try:
                self._fd = os.open(self.lock_path, os.O_CREAT | os.O_EXCL | os.O_RDWR)
                return self
            except FileExistsError:
                try:
                    age = time.time() - self.lock_path.stat().st_mtime
                except OSError:
                    age = 0.0
                if age > _ACCOUNTS_LOCK_STALE_SECONDS:
                    try:
                        self.lock_path.unlink()
                    except OSError:
                        pass
                    continue
                if time.monotonic() >= deadline:
                    self._fd = None  # 降级无锁,未持有锁文件
                    return self
                time.sleep(0.05)

    def __exit__(self, exc_type, exc, tb) -> None:
        # 仅在真正持有(_fd 非 None)时清理;降级路径不动别人的锁文件。
        if self._fd is None:
            return
        try:
            os.close(self._fd)
        except OSError:
            pass
        self._fd = None
        try:
            self.lock_path.unlink()
        except OSError:
            pass


def _read_accounts_list(path: Path) -> list[dict[str, object]]:
    """读出 accounts.json 里的账号数组(兼容裸数组与 {"accounts":[...]} 两种)。缺失/损坏返回空。"""
    if not path.exists():
        return []
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except Exception:
        return []
    if isinstance(data, list):
        raw = data
    elif isinstance(data, dict):
        raw = data.get("accounts", [])
    else:
        raw = []
    return [dict(item) for item in raw if isinstance(item, dict)]


def _atomic_write_accounts(path: Path, accounts: list[dict[str, object]]) -> None:
    """先写临时文件再 os.replace 原子替换,避免别的进程读到半截 JSON。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.tmp{os.getpid()}")
    tmp.write_text(json.dumps({"accounts": accounts}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def append_accounts_to_file(
    accounts_file: "str | Path",
    new_accounts: list[dict[str, object]],
    *,
    enabled: bool = False,
) -> list[str]:
    """进程安全地把账号追加进 accounts.json —— 供 register.py / 其他进程调用的对外接口。
    按 account 名去重:已存在的跳过(不覆盖原密码)。新账号默认 enabled=False(不自动登录,
    CLI --no-gui 会过滤掉,GUI 里仍可见、可手动启动)。返回实际新增的账号名列表。"""
    path = Path(accounts_file)
    normalized: list[dict[str, object]] = []
    for item in new_accounts:
        if not isinstance(item, dict):
            continue
        name = str(item.get("account") or "").strip()
        if not name:
            continue
        row: dict[str, object] = {
            "account": name,
            "password": str(item.get("password") or ""),
            "character": str(item.get("character") or ""),
            "enabled": bool(item.get("enabled", enabled)),
        }
        for key, value in item.items():
            if key not in row:
                row[key] = value
        normalized.append(row)
    if not normalized:
        return []

    added: list[str] = []
    with _AccountsFileLock(path):
        existing = _read_accounts_list(path)
        have = {str(item.get("account") or "").strip() for item in existing}
        for row in normalized:
            if row["account"] in have:
                continue
            existing.append(row)
            have.add(str(row["account"]))
            added.append(str(row["account"]))
        if added:
            _atomic_write_accounts(path, existing)
    return added


def save_accounts_document(path: Path, accounts: list[dict[str, object]]) -> None:
    """只写账号列表(含密码),全局默认配置另存于 so3d_online_defaults.json。
    走与 append_accounts_to_file 同一把文件锁+原子替换,防止与外部进程的追加互相覆盖。"""
    path = Path(path)
    with _AccountsFileLock(path):
        _atomic_write_accounts(path, accounts)


def save_defaults_document(path: Path, defaults: dict[str, object]) -> None:
    """写独立的全局默认配置文件(打包时随 exe 分发)。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(defaults, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
