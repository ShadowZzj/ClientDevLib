"""自动选路:代理实例(port_proxy.py)与 bot(本模块)通过一个共享 JSON 文件协调。

为什么需要文件协调:protect.dll 的选节点是一次性的(NodeSelector::process 只在隧道
启动时选一次最低延迟节点,选定后整个进程生命周期固定),所以「一个代理进程 == 一个上游
节点」。要把成百上千账号分摊到多个节点,只能跑多个代理进程、每个钉不同节点。本文件让:
  - 每个代理进程 claim 到一个(各不相同的)低延迟节点后,把
    (listen 别名, node_ip, rtt_ms, pid) 写进 instances;
  - bot 每个账号上线时读 instances,按 rtt 升序挑一个「未达每节点上限」的实例,
    在 slots 里占一个名额,并把该账号的 proxy_host 指到该实例的 listen 别名;
  - 某节点占满(达到 max_per_node)就跳到下一个 rtt 次低的实例。
存活以写入方进程 pid 是否还在为准:代理进程死 -> 其 instance 失效;launcher 进程死 ->
其名下 slots 全部失效。无需心跳。

文件路径默认 %TEMP%\ggtb_auto_route\registry.json(按用户唯一,且不受 protect.dll 内部
SHGetFolderPathW 重定向影响),可用 env GGTB_ROUTE_FILE 覆盖。proxy 端用相同路径/格式
(逻辑内联在 port_proxy.py,不 import 本模块,因为 port_proxy 独立运行)。
"""

from __future__ import annotations

import ctypes
import json
import os
import tempfile
import time

_ROUTE_FILE_ENV = "GGTB_ROUTE_FILE"
_DEFAULT_DIRNAME = "ggtb_auto_route"
_DEFAULT_FILENAME = "registry.json"
_LOCK_SUFFIX = ".lock"
_LOCK_STALE_SECONDS = 30.0
_LOCK_RETRY_SLEEP = 0.01
_LOCK_TIMEOUT = 5.0
_RTT_INF = 1 << 30

try:
    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
except Exception:  # pragma: no cover - 非 Windows(开发机)导入也不报错
    _k32 = None

_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_STILL_ACTIVE = 259


def route_file_path() -> str:
    override = os.environ.get(_ROUTE_FILE_ENV, "").strip()
    if override:
        return override
    return os.path.join(tempfile.gettempdir(), _DEFAULT_DIRNAME, _DEFAULT_FILENAME)


def _pid_alive(pid) -> bool:
    """pid 进程是否还活着。代理/launcher 崩溃后其 instance/slot 据此被回收。"""
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    if _k32 is None:  # 非 Windows:无法校验,保守当作活着
        return True
    handle = _k32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        if not _k32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == _STILL_ACTIVE
    finally:
        _k32.CloseHandle(handle)


class _FileLock:
    """跨进程文件锁:O_EXCL 抢一个 sidecar .lock 文件,超过 stale 秒视为陈旧锁强抢。
    代理与 bot 共用同一把锁,保证 claim/acquire 串行,避免两边同时改 registry。"""

    def __init__(self, target_path: str):
        self._lock_path = target_path + _LOCK_SUFFIX
        self._fd = None

    def __enter__(self):
        os.makedirs(os.path.dirname(self._lock_path) or ".", exist_ok=True)
        deadline = time.monotonic() + _LOCK_TIMEOUT
        while True:
            try:
                self._fd = os.open(self._lock_path, os.O_CREAT | os.O_EXCL | os.O_RDWR)
                return self
            except FileExistsError:
                try:
                    age = time.time() - os.path.getmtime(self._lock_path)
                except OSError:
                    age = 0.0
                if age > _LOCK_STALE_SECONDS:
                    try:
                        os.unlink(self._lock_path)
                    except OSError:
                        pass
                    continue
                if time.monotonic() > deadline:
                    # 抢不到也别卡死调用方:放弃锁、按无锁继续(极端竞态下宁可偶发误差)
                    self._fd = None
                    return self
                time.sleep(_LOCK_RETRY_SLEEP)

    def __exit__(self, *exc):
        if self._fd is not None:
            try:
                os.close(self._fd)
            finally:
                try:
                    os.unlink(self._lock_path)
                except OSError:
                    pass
        return False


def _read(path: str) -> dict:
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        data = {}
    if not isinstance(data, dict):
        data = {}
    data.setdefault("instances", {})
    data.setdefault("slots", {})
    if not isinstance(data["instances"], dict):
        data["instances"] = {}
    if not isinstance(data["slots"], dict):
        data["slots"] = {}
    return data


def _write(path: str, data: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def _live_instances(data: dict) -> dict:
    """剔除写入方 pid 已死的代理实例。"""
    out = {}
    for alias, info in data.get("instances", {}).items():
        if isinstance(info, dict) and _pid_alive(info.get("pid")):
            out[alias] = info
    return out


def _live_slots(data: dict, instances: dict) -> dict:
    """剔除 launcher pid 已死、或指向已不存在实例的名额。"""
    out = {}
    for key, slot in data.get("slots", {}).items():
        if not isinstance(slot, dict):
            continue
        if slot.get("alias") not in instances:
            continue
        if not _pid_alive(slot.get("pid")):
            continue
        out[key] = slot
    return out


def acquire_slot(account_key: str, max_per_node: int, launcher_pid: int | None = None,
                 *, prefer_alias: str | None = None, allow_overflow: bool = True):
    """账号上线时调用:按 rtt 升序挑一个未达每节点上限的代理实例,占一个名额。
    返回 dict(alias, node_ip, rtt_ms, count, overflow) 或 None(当前没有任何在线代理实例)。
    max_per_node<=0 表示不限。同 account_key 已有有效名额则原样复用(sticky,断线重连不换节点)。

    prefer_alias:优先把名额落到这个实例(若它存在且未满)——proxy_pool 拉起新实例后用它
        确保账号占到刚拉起的那个,而不是被其他实例抢走。
    allow_overflow:False 时,若所有实例都已达上限则「不落位」,返回 {"needs_spawn": True}
        让上层(proxy_pool)去拉一个新代理实例;True(默认)沿用旧行为:超额落到 rtt 最低实例。"""
    path = route_file_path()
    launcher_pid = int(launcher_pid if launcher_pid is not None else os.getpid())
    cap = int(max_per_node or 0)
    with _FileLock(path):
        data = _read(path)
        instances = _live_instances(data)
        slots = _live_slots(data, instances)

        cur = slots.get(account_key)
        if cur and cur.get("alias") in instances:
            alias = cur["alias"]
            cur["pid"] = launcher_pid
            cur["ts"] = time.time()
            info = instances[alias]
            data["instances"] = instances
            data["slots"] = slots
            _write(path, data)
            count = sum(1 for s in slots.values() if s.get("alias") == alias)
            return {"alias": alias, "node_ip": info.get("node_ip"),
                    "rtt_ms": info.get("rtt_ms"), "count": count, "overflow": False,
                    "reused": True}

        if not instances:
            return None

        ranked = sorted(instances.items(),
                        key=lambda kv: _safe_rtt(kv[1].get("rtt_ms")))
        counts: dict[str, int] = {}
        for slot in slots.values():
            counts[slot["alias"]] = counts.get(slot["alias"], 0) + 1

        chosen = None
        overflow = False
        if prefer_alias and prefer_alias in instances:
            if cap <= 0 or counts.get(prefer_alias, 0) < cap:
                chosen = (prefer_alias, instances[prefer_alias])
        if chosen is None:
            for alias, info in ranked:
                if cap > 0 and counts.get(alias, 0) >= cap:
                    continue
                chosen = (alias, info)
                break
        if chosen is None:
            if not allow_overflow:
                # 所有实例都已达上限:不落位,交给上层(proxy_pool)决定是否拉新实例
                return {"alias": None, "node_ip": None, "rtt_ms": None,
                        "count": 0, "overflow": True, "reused": False,
                        "needs_spawn": True}
            chosen = ranked[0]  # 全部占满 -> 仍给 rtt 最低的那个(超额)
            overflow = True

        alias, info = chosen
        slots[account_key] = {"alias": alias, "pid": launcher_pid, "ts": time.time()}
        data["instances"] = instances
        data["slots"] = slots
        _write(path, data)
        count = counts.get(alias, 0) + 1
        return {"alias": alias, "node_ip": info.get("node_ip"),
                "rtt_ms": info.get("rtt_ms"), "count": count, "overflow": overflow,
                "reused": False}


def release_slot(account_key: str, launcher_pid: int | None = None) -> None:
    """账号永久停止时调用:释放它占的名额,让出节点容量。"""
    path = route_file_path()
    launcher_pid = int(launcher_pid if launcher_pid is not None else os.getpid())
    with _FileLock(path):
        data = _read(path)
        slots = data.get("slots", {})
        cur = slots.get(account_key)
        if cur and cur.get("pid") in (None, launcher_pid):
            slots.pop(account_key, None)
            data["slots"] = slots
            _write(path, data)


def peek_best():
    """只读挑一个 rtt 最低的在线实例(不占名额),用于建号等不需要长期占位的流程。
    返回 dict(alias, node_ip, rtt_ms) 或 None。"""
    path = route_file_path()
    data = _read(path)
    instances = _live_instances(data)
    if not instances:
        return None
    alias, info = min(instances.items(), key=lambda kv: _safe_rtt(kv[1].get("rtt_ms")))
    return {"alias": alias, "node_ip": info.get("node_ip"), "rtt_ms": info.get("rtt_ms")}


def list_live_instances() -> dict:
    """只读快照:当前所有写入方进程仍存活的代理实例(alias -> info)。
    proxy_pool 用它统计已占别名、等待新拉起实例 claim 到节点后登记。"""
    return _live_instances(_read(route_file_path()))


def _safe_rtt(value) -> int:
    try:
        rtt = int(value)
    except (TypeError, ValueError):
        return _RTT_INF
    return rtt if rtt >= 0 else _RTT_INF
