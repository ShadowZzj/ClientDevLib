import ctypes
import json
import sys
import os
import tempfile
import time
from ctypes import wintypes

ACCESS_KEY = '{"access_key":"b9f241db50a25b4f4f4e9c877e86641c"}'
DLL_NAME = "protect.dll"

# ---------------------------------------------------------------------------
# 客户端分流补丁（多实例多节点）。
#
# protect.dll 选节点是确定性「最低延迟」(NodeSelector::selectBest)，N 个代理跑在
# 同一台机器上测到的 RTT 几乎一样 -> 全部收敛到同一个节点，裸开多进程不分流。
# 这里在进程内给 protect.dll 打两个补丁，按各自 env 把本实例钉到指定节点 / 监听别名：
#   GGTB_NODE_IP   = 本实例要钉的上游节点 IP（命中候选表里的 NodeInfo 直接返回它）；
#                    同时用作本实例的 dataDir 隔离 id（见下方 Patch③ appdata 重定向）。
#   GGTB_LISTEN_IP = 本实例的监听别名（把 bind() 的 127.2.57.25 改成它，M 实例不撞端口）
#   GGTB_LISTEN_MATCH = 被替换的源监听 IP（默认 127.2.57.25，即 SDK 当前监听别名）
# 全部 env 都没设就完全不打补丁，行为与原版一致。
#
# 多实例为什么裸开不行：protect.dll 有单实例锁——TunnelWrapper_ctor 一开始就对
# dataDir(=%APPDATA%\Protect，由 SHGetFolderPathW(CSIDL_APPDATA) 求得) 下一个按
# accessKey 派生命名的锁文件做 CreateFileW(共享模式=0, OPEN_ALWAYS) 独占打开。实例1
# 独占持有 -> 实例2 SHARING_VIOLATION -> 冲突 -> m_isValid=0 -> protect_start 返回 1009
# （发生在 bind 之前，所以实例2没有任何 [bind]/[Selector] 输出）。SHGetFolderPathW 不认
# APPDATA 环境变量(走注册表已知文件夹)，没法靠 env 直接换 dataDir，故 Patch③ 在进程内
# hook SHGetFolderPathW 的 IAT，命中 CSIDL_APPDATA 时把返回路径追加 \GGTB_<id>，让每个
# 实例的整棵 \Protect\ 树(含锁文件)各自独立，从根上避开单实例锁。
#
# 地址来自 protect.dll 的 IDA 逆向（imagebase 0x312950000）：
#   selectBest        @ 0x31296dbb0  -> RVA 0x1DBB0
#   bind IAT槽        @ 0x312a15ab0  -> RVA 0xC5AB0  (从 WSOCK32 导入)
#   SHGetFolderPathW IAT槽 @ 0x312a15a60 -> RVA 0xC5A60  (从 SHELL32 导入)
# 运行时按 CDLL 加载基址 + RVA 定位，并用原始字节/导入地址自校验，校验不过就放弃打补丁。
# ---------------------------------------------------------------------------

_SELECTBEST_RVA = 0x1DBB0
_BIND_IAT_RVA = 0xC5AB0
_SHGETFOLDER_IAT_RVA = 0xC5A60
_CSIDL_APPDATA = 26
_MAX_PATH = 260
# selectBest 序言前 19 字节：8 个 push + `sub rsp,1A8h`，全部位置无关，可整段搬到
# trampoline。偷 19 字节是为了在指令边界上凑够一条 14 字节 `jmp [rip]` 的空间。
_SELECTBEST_PROLOGUE = bytes(
    [0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x55, 0x57,
     0x56, 0x53, 0x48, 0x81, 0xEC, 0xA8, 0x01, 0x00, 0x00]
)

PAGE_EXECUTE_READWRITE = 0x40
PAGE_READWRITE = 0x04
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000

# CFUNCTYPE 回调和 trampoline 缓冲必须保活，否则被 GC 回收后 SDK 回调会跳进野指针。
_PATCH_KEEPALIVE = []

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32.VirtualProtect.argtypes = [
    wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)
]
_kernel32.VirtualProtect.restype = wintypes.BOOL
_kernel32.VirtualAlloc.argtypes = [
    wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD
]
_kernel32.VirtualAlloc.restype = wintypes.LPVOID
_kernel32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
_kernel32.GetProcAddress.restype = ctypes.c_void_p
_kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
_kernel32.OpenProcess.restype = wintypes.HANDLE
_kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
_kernel32.GetExitCodeProcess.restype = wintypes.BOOL
_kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
_kernel32.CloseHandle.restype = wintypes.BOOL


def _parse_ipv4_octets(text):
    parts = text.strip().split(".")
    if len(parts) != 4:
        raise ValueError(f"bad IPv4 {text!r}")
    octets = [int(p) for p in parts]
    if any(o < 0 or o > 255 for o in octets):
        raise ValueError(f"bad IPv4 {text!r}")
    return octets


def _ipv4_host_u32(text):
    a, b, c, d = _parse_ipv4_octets(text)
    return (a << 24) | (b << 16) | (c << 8) | d


def _format_host_ip(ip_u32):
    """主机序 uint32 -> 点分十进制，用于打印候选节点 IP。"""
    return f"{(ip_u32 >> 24) & 0xFF}.{(ip_u32 >> 16) & 0xFF}.{(ip_u32 >> 8) & 0xFF}.{ip_u32 & 0xFF}"


def _write_code(addr, data):
    old = wintypes.DWORD(0)
    if not _kernel32.VirtualProtect(addr, len(data), PAGE_EXECUTE_READWRITE, ctypes.byref(old)):
        raise OSError(f"VirtualProtect RWX failed @ {addr:#x}: {ctypes.get_last_error()}")
    ctypes.memmove(addr, data, len(data))
    _kernel32.VirtualProtect(addr, len(data), old.value, ctypes.byref(old))
    _kernel32.FlushInstructionCache(ctypes.c_void_p(-1), ctypes.c_void_p(addr), len(data))


def _abs_jmp(target):
    # jmp qword ptr [rip+0]; dq target  (14 字节，不破坏任何寄存器)
    return b"\xff\x25\x00\x00\x00\x00" + target.to_bytes(8, "little")


# ---------------------------------------------------------------------------
# 自动选路(GGTB_AUTO):代理实例在 selectBest 里 claim 一个「别的活实例还没占」的最低延迟
# 节点,钉住它,并把 (listen 别名, node_ip, rtt_ms, pid) 写进共享 registry。bot 端
# (so3dclient/auto_route.py)读这个 registry,把账号按 rtt/每节点容量分摊到各实例。本文件
# 不 import 那个模块(port_proxy 独立运行),只共用同一个 JSON 文件路径与字段格式。
#   registry 默认 %TEMP%\ggtb_auto_route\registry.json,可用 GGTB_ROUTE_FILE 覆盖。
#   rtt 取法:LatencyDetector_getLatencyMs(*(node+24)) —— 即 sub_3129737C0,RVA 0x237C0,
#   返回 int 毫秒,负值=尚未测出。存活以写入方 pid 是否还在为准,无需心跳。
# ---------------------------------------------------------------------------
_LATENCY_RVA = 0x237C0
_ROUTE_LOCK_STALE = 30.0
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_STILL_ACTIVE = 259
_AUTO_CLAIM_GRACE = 6.0  # 等不到有效 rtt 也要在这么多秒内 claim(<10s SDK 选路超时)


def _route_file_path():
    override = os.environ.get("GGTB_ROUTE_FILE", "").strip()
    if override:
        return override
    return os.path.join(tempfile.gettempdir(), "ggtb_auto_route", "registry.json")


def _pid_alive(pid):
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    handle = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return False
    try:
        code = wintypes.DWORD(0)
        if not _kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == _STILL_ACTIVE
    finally:
        _kernel32.CloseHandle(handle)


class _RouteLock:
    """跨进程文件锁(代理实例之间 claim 串行):O_EXCL 抢一个 sidecar .lock,陈旧则强抢。"""

    def __init__(self, path):
        self._lock = path + ".lock"
        self._fd = None

    def __enter__(self):
        os.makedirs(os.path.dirname(self._lock) or ".", exist_ok=True)
        deadline = time.monotonic() + 5.0
        while True:
            try:
                self._fd = os.open(self._lock, os.O_CREAT | os.O_EXCL | os.O_RDWR)
                return self
            except FileExistsError:
                try:
                    age = time.time() - os.path.getmtime(self._lock)
                except OSError:
                    age = 0.0
                if age > _ROUTE_LOCK_STALE:
                    try:
                        os.unlink(self._lock)
                    except OSError:
                        pass
                    continue
                if time.monotonic() > deadline:
                    self._fd = None  # 抢不到也别卡死选路,无锁继续(极端竞态宁可偶发误差)
                    return self
                time.sleep(0.01)

    def __exit__(self, *exc):
        if self._fd is not None:
            try:
                os.close(self._fd)
            finally:
                try:
                    os.unlink(self._lock)
                except OSError:
                    pass
        return False


def _route_read(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        data = {}
    if not isinstance(data, dict):
        data = {}
    if not isinstance(data.get("instances"), dict):
        data["instances"] = {}
    if not isinstance(data.get("slots"), dict):
        data["slots"] = {}
    return data


def _route_write(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def _route_claim_node(alias, candidates):
    """candidates: list[(ip_str, rtt_int)](rtt 负=未测出)。在共享 registry 里挑一个「别的活
    实例还没占」的 rtt 最低节点,登记 instances[alias]={node_ip,rtt_ms,pid,ts},返回选中 ip_str。
    候选全被占就和 rtt 最低的共用(返回它);没有候选返回 None。"""
    path = _route_file_path()
    mypid = os.getpid()

    def _key(item):
        rtt = item[1]
        return rtt if (isinstance(rtt, int) and rtt >= 0) else (1 << 30)

    with _RouteLock(path):
        data = _route_read(path)
        live = {a: v for a, v in data["instances"].items()
                if isinstance(v, dict) and _pid_alive(v.get("pid"))}
        claimed = {v.get("node_ip") for v in live.values() if v.get("pid") != mypid}
        ordered = sorted(candidates, key=_key)
        chosen = None
        for ip, rtt in ordered:
            if ip not in claimed:
                chosen = (ip, rtt)
                break
        if chosen is None and ordered:
            chosen = ordered[0]
        if chosen is None:
            return None
        rtt_val = chosen[1] if (isinstance(chosen[1], int) and chosen[1] >= 0) else -1
        live[alias] = {"node_ip": chosen[0], "rtt_ms": rtt_val, "pid": mypid, "ts": time.time()}
        data["instances"] = live
        _route_write(path, data)
        return chosen[0]


def _install_node_pin(base, node_ip, auto=False, listen_alias=""):
    """Patch ①：把 selectBest detour 到「命中目标节点就返回它」，否则回原逻辑。
    三种模式:
      - 手动钉(node_ip 非空)：命中 GGTB_NODE_IP 的候选就返回;
      - 自动选路(auto=True,node_ip 空)：首次拿到有效 rtt(或宽限到点)时 claim 一个最低延迟
        且别的实例没占的节点,之后等同手动钉;claim 前返回 0 让 SDK 继续轮询;
      - 仅 dump(都不满足、只 GGTB_NODE_DEBUG)：打印候选(含 rtt)不钉。"""
    node_debug = os.environ.get("GGTB_NODE_DEBUG", "").strip() not in ("", "0")
    force_ip = _ipv4_host_u32(node_ip) if node_ip else None
    fn_addr = base + _SELECTBEST_RVA

    cur = bytes((ctypes.c_ubyte * len(_SELECTBEST_PROLOGUE)).from_address(fn_addr))
    if cur != _SELECTBEST_PROLOGUE:
        print(f"[patch] node-pin SKIP: prologue mismatch @ {fn_addr:#x} got {cur.hex(' ')}",
              file=sys.stderr)
        return False

    steal = len(_SELECTBEST_PROLOGUE)  # 19
    tramp = _kernel32.VirtualAlloc(None, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)
    if not tramp:
        print("[patch] node-pin SKIP: VirtualAlloc failed", file=sys.stderr)
        return False
    # trampoline = 原始序言 + 跳回 selectBest+steal（即原函数体续点）
    ctypes.memmove(tramp, _SELECTBEST_PROLOGUE, steal)
    ctypes.memmove(tramp + steal, _abs_jmp(fn_addr + steal), 14)
    _kernel32.FlushInstructionCache(ctypes.c_void_p(-1), ctypes.c_void_p(tramp), 64)

    hook_type = ctypes.WINFUNCTYPE(ctypes.c_uint64, ctypes.c_uint64)
    call_original = hook_type(tramp)
    # rtt 取法:LatencyDetector_getLatencyMs(detector)。detector=*(node+24),返回 int 毫秒,负=未测出。
    get_latency = ctypes.WINFUNCTYPE(ctypes.c_int32, ctypes.c_uint64)(base + _LATENCY_RVA)
    pin_ip = [force_ip]   # 命中目标(主机序 u32):手动钉=固定;auto 未 claim 前 None,claim 后填入
    first_ts = [0.0]      # auto:首次进 hook 时刻,用于宽限到点强行 claim
    _seen = []            # 候选集去重:同一组 IP 只在变化时打印一次,避免每 500ms 刷屏
    _pin_state = [None]   # 命中/未命中去重:只在翻转时打印一次(候选表会动态变)
    self_alias = listen_alias or f"pid{os.getpid()}"

    def _read_candidates(a1):
        arr = ctypes.c_uint64.from_address(a1 + 80).value          # 候选数组(Node*[])
        count = ctypes.c_int32.from_address(a1 + 92).value         # 元素个数
        out = []
        for i in range(count):
            node = ctypes.c_uint64.from_address(arr + 8 * i).value
            if not node:
                continue
            info = ctypes.c_uint64.from_address(node + 8).value    # NodeInfo*
            if not info:
                continue
            ip = ctypes.c_uint32.from_address(info + 8).value      # 主机序 ip
            det = ctypes.c_uint64.from_address(node + 24).value    # LatencyDetector*
            rtt = int(get_latency(det)) if det else -1
            out.append((ip, info, rtt))
        return out

    def _hook(a1):
        try:
            cands = _read_candidates(a1)
            ips = [c[0] for c in cands]
            if node_debug and ips not in _seen:
                _seen.append(ips)
                pretty = ", ".join(f"{_format_host_ip(ip)}({rtt}ms)" for ip, _info, rtt in cands)
                print(f"[node] candidates({len(cands)}): {pretty}", flush=True)

            # auto:还没 claim 就先 claim(有有效 rtt 或宽限到点)。claim 前返回 0(null),
            # SDK 把它当作「本轮没选出」继续每 ~500ms 轮询,等我们拿到 rtt 再定。
            if auto and pin_ip[0] is None:
                now = time.monotonic()
                if first_ts[0] == 0.0:
                    first_ts[0] = now
                has_rtt = any(r >= 0 for _ip, _info, r in cands)
                if cands and (has_rtt or (now - first_ts[0]) > _AUTO_CLAIM_GRACE):
                    try:
                        chosen = _route_claim_node(
                            self_alias,
                            [(_format_host_ip(ip), rtt) for ip, _info, rtt in cands],
                        )
                    except Exception as exc:  # noqa: BLE001
                        print(f"[patch] auto-claim error: {exc}", file=sys.stderr)
                        chosen = None
                    if chosen:
                        pin_ip[0] = _ipv4_host_u32(chosen)
                        crtt = next((r for ip, _i, r in cands if ip == pin_ip[0]), -1)
                        print(f"[node] AUTO-CLAIMED -> {chosen} (rtt={crtt}ms, alias={self_alias})",
                              flush=True)
                if pin_ip[0] is None:
                    return 0  # 还没选定:返回 null,SDK 继续轮询(原函数本就可能返回 0)

            target = pin_ip[0]
            if target is not None:
                # 候选表会动态变:目标不在当次候选里就退回选最低延迟(等于本轮没分流)。
                matched = 0
                for ip, info, _rtt in cands:
                    if ip == target:
                        matched = info
                        break
                hit = bool(matched)
                if _pin_state[0] != hit:
                    _pin_state[0] = hit
                    label = node_ip if force_ip is not None else _format_host_ip(target)
                    if hit:
                        print(f"[node] PINNED -> {label} (matched in candidates)", flush=True)
                    else:
                        print(f"[node] PIN MISS: {label} not in candidates({len(cands)}), "
                              f"fall back to lowest-RTT", flush=True)
                if matched:
                    return matched
        except Exception as exc:  # noqa: BLE001 - 出错就退回原逻辑,绝不让回调抛进 SDK
            print(f"[patch] node-pin hook error: {exc}", file=sys.stderr)
        return call_original(a1)

    hook_cb = hook_type(_hook)
    hook_addr = ctypes.cast(hook_cb, ctypes.c_void_p).value
    _PATCH_KEEPALIVE.extend([call_original, hook_cb, tramp])

    patch = _abs_jmp(hook_addr) + b"\x90" * (steal - 14)
    _write_code(fn_addr, patch)
    if auto:
        print(f"[patch] node-pin AUTO: selectBest @ {fn_addr:#x} 自动 claim 最低延迟节点 "
              f"(alias={self_alias}, registry={_route_file_path()})")
    elif force_ip is not None:
        print(f"[patch] node-pin ON: selectBest @ {fn_addr:#x} -> {node_ip} "
              f"(host u32 {force_ip:#010x})")
    else:
        print(f"[patch] node-pin DUMP-ONLY: selectBest @ {fn_addr:#x} "
              f"(GGTB_NODE_DEBUG, 不钉节点只打印候选)")
    return True


def _install_listen_rebind(base, listen_ip, match_ip):
    """Patch ②：IAT 钩 bind，把 SDK 的环回监听别名改写成本实例专属地址，M 实例不撞端口。
    SDK 会绑两组监听别名(默认 127.2.57.25 与 127.4.57.25)。按 GGTB_LISTEN_IP 的末段 N(实例号)把
    每组别名的末段都改成 N：127.2.57.25 -> 127.2.57.N、127.4.57.25 -> 127.4.57.N（两组共用同一个 N，
    一个 env 钉住整台实例）。源别名表来自 match_ip(GGTB_LISTEN_MATCH，逗号分隔，默认含上面两组)。
    GGTB_BIND_DEBUG=1 时不改写、只打印每次 bind 的 family/ip/port/返回值/WSAError，用于诊断。
    listen_ip 为空(仅 debug)则只记录、不改写。"""
    debug = os.environ.get("GGTB_BIND_DEBUG", "").strip() not in ("", "0")
    rewrites = {}  # bytes(源 sin_addr 4 字节) -> bytes(目标 4 字节)；只把末段改成实例号 N
    if listen_ip:
        inst_octet = _parse_ipv4_octets(listen_ip)[3]   # 末段=实例号，两组别名共用
        for src in match_ip.split(","):
            src = src.strip()
            if not src:
                continue
            a, b, c, d = _parse_ipv4_octets(src)
            rewrites[bytes([a, b, c, d])] = bytes([a, b, c, inst_octet])

    slot = base + _BIND_IAT_RVA
    orig_addr = ctypes.c_uint64.from_address(slot).value

    wsock32 = ctypes.WinDLL("wsock32", use_last_error=True)
    real_bind = _kernel32.GetProcAddress(wsock32._handle, b"bind")
    if real_bind and orig_addr != real_bind:
        print(f"[patch] listen-rebind SKIP: IAT slot @ {slot:#x} = {orig_addr:#x} "
              f"!= wsock32!bind {real_bind:#x}", file=sys.stderr)
        return False

    wsa_err_addr = _kernel32.GetProcAddress(wsock32._handle, b"WSAGetLastError")
    wsa_get_last_error = ctypes.WINFUNCTYPE(ctypes.c_int)(wsa_err_addr) if wsa_err_addr else None

    bind_type = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int)
    call_original = bind_type(orig_addr)

    def _hook(s, name, namelen):
        rewritten = None
        info = "?"
        try:
            if name:
                fam = ctypes.c_uint16.from_address(name).value
                if fam == 2:  # AF_INET
                    raw_port = ctypes.c_uint16.from_address(name + 2).value
                    port = ((raw_port & 0xFF) << 8) | (raw_port >> 8)   # ntohs
                    cur = bytes((ctypes.c_ubyte * 4).from_address(name + 4))
                    info = f"AF_INET {cur[0]}.{cur[1]}.{cur[2]}.{cur[3]}:{port}"
                    target = rewrites.get(cur)
                    if target is not None:
                        ctypes.memmove(name + 4, target, 4)
                        rewritten = target
                else:
                    info = f"family={fam}"
        except Exception as exc:  # noqa: BLE001
            print(f"[patch] listen-rebind hook error: {exc}", file=sys.stderr)
        ret = call_original(s, name, namelen)
        if debug:
            tag = f" -> {rewritten[0]}.{rewritten[1]}.{rewritten[2]}.{rewritten[3]}" if rewritten else ""
            extra = ""
            if ret != 0 and wsa_get_last_error is not None:
                extra = f" WSAErr={wsa_get_last_error()}"
            print(f"[bind] s={s:#x} {info}{tag} ret={ret}{extra}", flush=True)
        return ret

    hook_cb = bind_type(_hook)
    _PATCH_KEEPALIVE.extend([call_original, hook_cb])

    old = wintypes.DWORD(0)
    if not _kernel32.VirtualProtect(slot, 8, PAGE_READWRITE, ctypes.byref(old)):
        print(f"[patch] listen-rebind SKIP: VirtualProtect IAT failed @ {slot:#x}", file=sys.stderr)
        return False
    ctypes.c_uint64.from_address(slot).value = ctypes.cast(hook_cb, ctypes.c_void_p).value
    _kernel32.VirtualProtect(slot, 8, old.value, ctypes.byref(old))
    if listen_ip:
        pairs = ", ".join(
            f"{s[0]}.{s[1]}.{s[2]}.{s[3]}->{t[0]}.{t[1]}.{t[2]}.{t[3]}"
            for s, t in rewrites.items()
        )
        print(f"[patch] listen-rebind ON: {pairs}" + (" (+debug)" if debug else ""))
    else:
        print("[patch] bind-debug ON: logging every bind, no rewrite")
    return True


def _install_appdata_redirect(base, instance_id):
    """Patch ③：IAT 钩 SHGetFolderPathW，命中 CSIDL_APPDATA 时把返回路径追加 \\GGTB_<id>，
    使本实例的 dataDir 变成 %APPDATA%\\GGTB_<id>\\Protect，独占锁文件与别的实例不再争抢。
    instance_id 一般用 GGTB_NODE_IP（每实例钉的节点 IP 天然唯一）。"""
    token = "".join(c if (c.isalnum() or c in "._-") else "_" for c in instance_id)
    if not token:
        print("[patch] appdata-redirect SKIP: empty instance id", file=sys.stderr)
        return False
    suffix = "\\GGTB_" + token

    slot = base + _SHGETFOLDER_IAT_RVA
    orig_addr = ctypes.c_uint64.from_address(slot).value
    shell32 = ctypes.WinDLL("shell32", use_last_error=True)
    real_fn = _kernel32.GetProcAddress(shell32._handle, b"SHGetFolderPathW")
    if real_fn and orig_addr != real_fn:
        print(f"[patch] appdata-redirect SKIP: IAT slot @ {slot:#x} = {orig_addr:#x} "
              f"!= shell32!SHGetFolderPathW {real_fn:#x}", file=sys.stderr)
        return False

    # HRESULT SHGetFolderPathW(HWND, int csidl, HANDLE, DWORD flags, LPWSTR pszPath)
    fn_type = ctypes.WINFUNCTYPE(ctypes.c_int32, ctypes.c_void_p, ctypes.c_int,
                                 ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p)
    call_original = fn_type(orig_addr)

    def _hook(hwnd, csidl, htoken, flags, pszpath):
        hr = call_original(hwnd, csidl, htoken, flags, pszpath)
        try:
            # csidl 可能 OR 了 CSIDL_FLAG_CREATE 等高位，只看低字节匹配 APPDATA
            if hr == 0 and pszpath and (csidl & 0xFF) == _CSIDL_APPDATA:
                cur = ctypes.wstring_at(pszpath)
                new = cur + suffix
                if len(new) + 1 <= _MAX_PATH:
                    buf = ctypes.create_unicode_buffer(new)
                    ctypes.memmove(pszpath, buf, (len(new) + 1) * ctypes.sizeof(ctypes.c_wchar))
                    # OPEN_ALWAYS 不建父目录，提前把本实例 \Protect 目录建好，锁文件才能落地
                    os.makedirs(os.path.join(new, "Protect"), exist_ok=True)
                else:
                    print(f"[patch] appdata-redirect SKIP rewrite: path too long ({len(new)})",
                          file=sys.stderr)
        except Exception as exc:  # noqa: BLE001 - 出错就退回原 APPDATA，绝不让回调抛进 SDK
            print(f"[patch] appdata-redirect hook error: {exc}", file=sys.stderr)
        return hr

    hook_cb = fn_type(_hook)
    _PATCH_KEEPALIVE.extend([call_original, hook_cb])

    old = wintypes.DWORD(0)
    if not _kernel32.VirtualProtect(slot, 8, PAGE_READWRITE, ctypes.byref(old)):
        print(f"[patch] appdata-redirect SKIP: VirtualProtect IAT failed @ {slot:#x}", file=sys.stderr)
        return False
    ctypes.c_uint64.from_address(slot).value = ctypes.cast(hook_cb, ctypes.c_void_p).value
    _kernel32.VirtualProtect(slot, 8, old.value, ctypes.byref(old))
    print(f"[patch] appdata-redirect ON: %APPDATA%{suffix}\\Protect (per-instance dataDir)")
    return True


def apply_load_balancing_patches(dll):
    """加载 protect.dll 之后、protect_start 之前调用，按 env 把本实例钉到指定节点/监听别名，
    并把本实例的 dataDir 隔离开（避开 protect.dll 的单实例锁，否则第二个实例返回 1009）。"""
    node_ip = os.environ.get("GGTB_NODE_IP", "").strip()
    listen_ip = os.environ.get("GGTB_LISTEN_IP", "").strip()
    match_ip = os.environ.get("GGTB_LISTEN_MATCH", "127.2.57.25,127.4.57.25").strip()
    bind_debug = os.environ.get("GGTB_BIND_DEBUG", "").strip() not in ("", "0")
    node_debug = os.environ.get("GGTB_NODE_DEBUG", "").strip() not in ("", "0")
    # GGTB_AUTO=1:自动选路。本实例不手动钉节点,而是在 selectBest 里 claim 一个别的活实例
    # 还没占的最低延迟节点并登记到共享 registry。手动 GGTB_NODE_IP 优先(显式),设了就不 auto。
    auto = os.environ.get("GGTB_AUTO", "").strip() not in ("", "0")
    do_auto = auto and not node_ip
    if not node_ip and not listen_ip and not bind_debug and not node_debug and not do_auto:
        return

    base = dll._handle  # Windows 上 CDLL._handle == 模块基址(HMODULE)
    print(f"[patch] protect.dll base = {base:#x}")
    if node_ip or do_auto:
        # dataDir 隔离必须在 protect_start 之前装好（SHGetFolderPathW 在 protect_start 里调）。
        # auto 模式下 claim 的节点 IP 此刻还不知道,改用 listen 别名(每窗口唯一)做隔离 id。
        try:
            _install_appdata_redirect(base, node_ip or listen_ip or f"auto{os.getpid()}")
        except Exception as exc:  # noqa: BLE001
            print(f"[patch] appdata-redirect FAILED: {exc}", file=sys.stderr)
    if node_ip or node_debug or do_auto:
        # node_ip 为空且仅 node_debug 时只 dump 候选;do_auto 时自动 claim。
        try:
            _install_node_pin(base, node_ip, auto=do_auto, listen_alias=listen_ip)
        except Exception as exc:  # noqa: BLE001
            print(f"[patch] node-pin FAILED: {exc}", file=sys.stderr)
    if listen_ip or bind_debug:
        try:
            _install_listen_rebind(base, listen_ip, match_ip)
        except Exception as exc:  # noqa: BLE001
            print(f"[patch] listen-rebind FAILED: {exc}", file=sys.stderr)


def main():
    daemon = "-d" in sys.argv

    if daemon:
        hwnd = ctypes.windll.kernel32.GetConsoleWindow()
        if hwnd:
            ctypes.windll.user32.ShowWindow(hwnd, 0)

    # 找 protect.dll:打包成 exe(frozen)时 __file__ 指向 PyInstaller 的 _MEIPASS 临时解包目录,
    # protect.dll 并不在那里,所以 frozen 下按 exe 自身所在目录(交付目录)找;源码运行按本脚本目录找。
    if getattr(sys, "frozen", False):
        base_dir = os.path.dirname(os.path.abspath(sys.executable))
    else:
        base_dir = os.path.dirname(os.path.abspath(__file__))
    dll_path = os.path.join(base_dir, DLL_NAME)
    if not os.path.exists(dll_path):
        dll_path = DLL_NAME

    print(f"[*] Loading {dll_path}")
    try:
        dll = ctypes.CDLL(dll_path)
    except OSError as e:
        print(f"[-] Failed to load {DLL_NAME}: {e}", file=sys.stderr)
        if not daemon:
            os.system("pause")
        return -1

    apply_load_balancing_patches(dll)

    dll.protect_start.restype = ctypes.c_uint32
    dll.protect_start.argtypes = [ctypes.c_char_p]
    dll.protect_stop.restype = ctypes.c_uint64
    dll.protect_stop.argtypes = []

    print("[*] Calling protect_start...")
    ret = dll.protect_start(ACCESS_KEY.encode("utf-8"))

    if ret != 0:
        print(f"[-] protect_start failed with code {ret}", file=sys.stderr)
        dll.protect_stop()
        if not daemon:
            os.system("pause")
        return -1

    print("[+] protect_start OK")

    if daemon:
        while True:
            time.sleep(1)
    else:
        print("[*] Type 'exit' to stop")
        while True:
            try:
                cmd = input("> ").strip()
                if cmd == "exit":
                    break
            except (EOFError, KeyboardInterrupt):
                break

    print("[*] Stopping...")
    dll.protect_stop()
    print("[*] Done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
