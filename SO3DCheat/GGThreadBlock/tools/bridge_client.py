r"""GGThreadBlock LoginBridge client — 123.dll layer-1 decrypt over a named pipe.

The bridge is hosted by the GGThreadBlock DLL injected into a running, logged-in
game process. It exposes `\\.\pipe\GGTB_LOGIN_BRIDGE_<pid>` and a JSON command
protocol. We use it to peel the 123.dll layer-1 transport encryption off the
login/lobby (10002) wire ciphertext, using the live session key inside the game
process — the only place that key exists.

decryptWire(forceDecrypt=True) NOPs the socket-fd gate at 123.dll+0x760C3 so the
VM decrypt runs on our supplied buffer regardless of which fd it came from.

Verified end-to-end by verify_decrypt_chain.py (2/2 wire->[RECV] matches once the
game is logged in).
"""
from __future__ import annotations

import ctypes
import json
import os
import threading
from ctypes import wintypes
from itertools import count

PIPE_PREFIX = os.environ.get("GGTB_BRIDGE_PREFIX", "GGTB_LOGIN_BRIDGE_")

# 代理进程窗口标题以这个前缀开头且不含竖线;正常登录的游戏进程标题是
# "SO3D Plus|<账号>"。优先连代理进程的 bridge,绝不误连正在玩的游戏进程。
PROXY_TITLE_PREFIX = os.environ.get("GGTB_PROXY_TITLE_PREFIX", "SO3D Plus")


def _enum_proxy_pids() -> set[int]:
    """Return PIDs whose visible window title starts with PROXY_TITLE_PREFIX but
    contains no '|' (i.e. the local proxy windows, not "SO3D Plus|<account>")."""
    try:
        user32 = ctypes.windll.user32
    except Exception:
        return set()

    pids: set[int] = set()
    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def _cb(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        title = buf.value
        if title.startswith(PROXY_TITLE_PREFIX) and "|" not in title:
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value:
                pids.add(int(pid.value))
        return True

    try:
        user32.EnumWindows(WNDENUMPROC(_cb), 0)
    except Exception:
        return set()
    return pids


class BridgeError(RuntimeError):
    pass


class BridgeUnavailable(BridgeError):
    """No usable bridge pipe / recvPatch not ready. The caller should abort prod login."""


def discover_pids() -> list[int]:
    """Return all PIDs that currently host a GGTB login bridge pipe, sorted."""
    try:
        names = os.listdir(r"\\.\pipe\\")
    except OSError:
        return []
    pids = sorted(
        {
            int(n[len(PIPE_PREFIX):])
            for n in names
            if n.startswith(PIPE_PREFIX) and n[len(PIPE_PREFIX):].isdigit()
        }
    )
    return pids


class LoginBridge:
    """Thin, thread-safe JSON client for one bridge pipe.

    All requests are serialized through a lock because several account threads may
    share a single bridge (one injected game process serves everyone)."""

    def __init__(self, pid: int):
        self.pid = pid
        self.path = rf"\\.\pipe\{PIPE_PREFIX}{pid}"
        self._ids = count(1)
        self._lock = threading.Lock()
        self.pipe = open(self.path, "r+b", buffering=0)
        hello = self._read_locked()  # first line is the unsolicited hello
        if hello.get("type") != "hello":
            raise BridgeError(f"unexpected bridge greeting: {hello}")
        # 共享池标记:被 connect_ready_bridge 放进 _SHARED 的实例不允许被单个账号
        # close() 真正关掉(同进程多账号线程共用一条 bridge 管道)。
        self._shared = False

    def _read_locked(self) -> dict:
        line = self.pipe.readline()
        if not line:
            raise BridgeError("bridge pipe closed")
        return json.loads(line.decode("utf-8"))

    def request(self, cmd: str, **payload) -> dict:
        rid = str(next(self._ids))
        msg = (json.dumps({"id": rid, "cmd": cmd, **payload}) + "\n").encode()
        with self._lock:
            self.pipe.write(msg)
            while True:
                r = self._read_locked()
                if r.get("type") == "response" and r.get("id") == rid:
                    return r
                # async / out-of-band lines are ignored

    def recv_patch_ready(self) -> bool:
        try:
            rp = self.request("ping").get("recvPatch", {})
        except BridgeError:
            return False
        return bool(rp.get("ready"))

    def decrypt_wire(self, wire: bytes) -> bytes:
        """Feed login-socket ciphertext through 123.dll's obf_recv_dispatch and
        return the layer-1 plaintext (caller still applies layer-2 xor)."""
        resp = self.request("decryptWire", hex=wire.hex(" "), forceDecrypt=True)
        if not resp.get("ok"):
            raise BridgeError(f"decryptWire failed: {resp.get('error', resp)}")
        return bytes.fromhex(resp["plainHex"])

    def close(self) -> None:
        # 共享实例由进程统一回收(close_shared_bridges),单个账号 close() 视作放手,
        # 不真正关管道 —— 否则会把其它仍在用这条 bridge 的账号线程一起搞断。
        if self._shared:
            return
        self._hard_close()

    def _hard_close(self) -> None:
        try:
            self.pipe.close()
        except Exception:
            pass


# 进程内共享 bridge 池:同一个游戏进程(pid)对应的 bridge 管道只开一条,所有账号线程复用。
# DLL 端命名管道实例数有限,逐账号各开一条会触发 [Errno 22] Invalid argument。
_SHARED: dict[int, "LoginBridge"] = {}
_SHARED_LOCK = threading.Lock()


def _shared_alive(bridge: "LoginBridge") -> bool:
    try:
        return bridge.recv_patch_ready()
    except Exception:
        return False


def close_shared_bridges() -> None:
    """进程退出时统一关闭所有共享 bridge 管道。"""
    with _SHARED_LOCK:
        for bridge in _SHARED.values():
            bridge._hard_close()
        _SHARED.clear()


def connect_ready_bridge(preferred_pid: int | None = None) -> LoginBridge:
    """Find a bridge whose 123.dll recv patch is ready, or raise BridgeUnavailable.

    进程内对每个游戏进程(pid)只维护一条共享 bridge 管道:多个账号线程复用同一实例
    (request() 已用 _lock 串行化)。先看共享池里有没有现成且就绪的;没有再按
    `preferred_pid`(或 env GGTB_BRIDGE_PID)、再 discover 出来的 pid 逐个开。
    """
    with _SHARED_LOCK:
        # 1) 优先复用共享池里仍然就绪的实例。
        wanted = preferred_pid
        if wanted is None:
            env_pid = os.environ.get("GGTB_BRIDGE_PID")
            if env_pid and env_pid.isdigit():
                wanted = int(env_pid)
        if wanted is not None and wanted in _SHARED and _shared_alive(_SHARED[wanted]):
            return _SHARED[wanted]
        for pid, bridge in list(_SHARED.items()):
            if _shared_alive(bridge):
                return bridge
            # 池里这条已经死了(宿主进程退出 / 进游戏关了登录连接),丢弃。
            bridge._hard_close()
            del _SHARED[pid]

        # 2) 没有现成可用的,按候选 pid 新开一条并放进共享池。
        #    候选只允许"代理进程"(窗口标题以 PROXY_TITLE_PREFIX 开头且不含 '|'):
        #    正常登录的游戏进程标题是 "SO3D Plus|<账号>",绝不能误连它。
        proxy_pids = _enum_proxy_pids()
        bridge_pids = set(discover_pids())
        # 同时拥有 bridge 管道、又是代理标题的 pid 才合格。
        eligible = bridge_pids & proxy_pids

        candidates: list[int] = []
        # preferred_pid / env 显式指定时作为逃生口,但仍要求它确实有 bridge 管道。
        forced = preferred_pid if preferred_pid is not None else wanted
        if forced is not None and forced in bridge_pids:
            candidates.append(forced)
        for pid in sorted(eligible):
            if pid not in candidates:
                candidates.append(pid)

        if not candidates:
            if bridge_pids and not eligible:
                raise BridgeUnavailable(
                    "检测到 bridge 管道,但没有一个属于代理进程"
                    f"(窗口标题以 \"{PROXY_TITLE_PREFIX}\" 开头且不含 '|')。"
                    "请先启动你自己的代理进程,不要直接连正在登录/游玩的游戏窗口。"
                )
            raise BridgeUnavailable(
                "未检测到 GGThreadBlock 登录 bridge 管道。请先启动代理进程"
                "(注入 GGThreadBlock.dll,窗口标题为纯 \"" + PROXY_TITLE_PREFIX + "\")。"
            )

        last_err: Exception | None = None
        for pid in candidates:
            if pid in _SHARED:
                continue  # 已在池里但上面判过不就绪,跳过
            try:
                bridge = LoginBridge(pid)
            except Exception as exc:  # pipe vanished / busy
                last_err = exc
                continue
            if bridge.recv_patch_ready():
                bridge._shared = True
                _SHARED[pid] = bridge
                return bridge
            bridge._hard_close()
            last_err = BridgeUnavailable(f"pid {pid} 的 123.dll recv patch 未就绪")

        raise BridgeUnavailable(
            "找到了 bridge 管道,但没有一个的 123.dll recv patch 就绪;"
            f"请确认游戏已登录进入。最后错误: {last_err}"
        )
