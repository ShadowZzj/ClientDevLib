"""自动拉起绿色 mihomo(clash.meta)核心,以 TUN 模式按进程名把 ggs5exit{k}.exe 导到对应出口。

为什么是 TUN 而不是 Proxifier:Proxifier 在进程内热补丁 Winsock(Function Prologue Hotpatch),
与 protect.dll 的反篡改互怼——要么把代理进程打崩,要么 protect.dll 的流量根本绕过被 hook 的
Winsock(实测"完全没流量")。mihomo 的 TUN 在虚拟网卡层路由 IP 包、不往进程注入任何 DLL,
所以和 protect.dll 井水不犯河水;而且它按"操作系统层面这个 socket 属于哪个进程"来归因(PROCESS-NAME
规则),无论 protect.dll 用什么姿势建连都能正确命中 ggs5exit{k}.exe。

绿色/零安装:把 mihomo 核心 + wintun.dll 放进交付目录的 clash 子目录(APP_DIR/clash/),
bot 登录正式服且出口池非空时自动:生成配置 -> 拉起 mihomo(TUN)-> 退出时收掉。用户不装任何东西。

TUN 需要管理员权限(建虚拟网卡 + 改路由)。强烈建议**以管理员身份运行 bot**:这样 mihomo 直接
随 bot 启动、无 UAC 弹窗、句柄归我们(退出能干净收掉)。若 bot 非管理员,会对 mihomo 单独提权
(一次 UAC),但那种情况下子进程是高完整性、本进程无权 terminate 它,bot 退出后它可能残留——
所以请尽量让 bot 自己就是管理员。
"""
from __future__ import annotations

import atexit
import ctypes
import json
import os
import subprocess
import threading
import urllib.request
from ctypes import wintypes
from pathlib import Path

from . import socks5_pool
from .runtime import APP_DIR
from .logio import main_log

_CORE_DIR = Path(APP_DIR) / "clash"
# 兼容各家发行版的核心文件名(都是 mihomo/clash.meta 同一核心,只是改了名)。
_CORE_NAMES = (
    "mihomo.exe",
    "mihomo-windows-amd64.exe",
    "mihomo-windows-amd64-compatible.exe",
    "verge-mihomo.exe",
    "clash-meta.exe",
    "clash.meta.exe",
    "clash.exe",
)
_WINTUN_NAME = "wintun.dll"
_PIDFILE = _CORE_DIR / "mihomo.pid"

_CREATE_NEW_CONSOLE = 0x00000010  # 给 mihomo 一个独立控制台,便于看它的日志(和 port_proxy 一致)

_LOCK = threading.RLock()
_PROC: "subprocess.Popen | None" = None  # bot 自身是管理员时,我们持有的核心进程句柄


def core_dir() -> Path:
    return _CORE_DIR


def find_core() -> "Path | None":
    """在 APP_DIR/clash/ 里找 mihomo 核心 exe(按 _CORE_NAMES 优先级)。找不到返回 None。"""
    if not _CORE_DIR.is_dir():
        return None
    for name in _CORE_NAMES:
        cand = _CORE_DIR / name
        if cand.is_file():
            return cand
    # 兜底:目录里任何看起来像 mihomo/clash 的 exe
    for cand in sorted(_CORE_DIR.glob("*.exe")):
        low = cand.name.lower()
        if "mihomo" in low or "clash" in low:
            return cand
    return None


def find_wintun() -> "Path | None":
    cand = _CORE_DIR / _WINTUN_NAME
    return cand if cand.is_file() else None


def _is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:  # noqa: BLE001
        return False


def _pid_alive(pid: int) -> bool:
    """用 tasklist 按 PID 查存活(对高完整性进程也能查到名字/PID,无需提权)。"""
    try:
        out = subprocess.run(
            ["tasklist", "/FI", f"PID eq {pid}", "/NH"],
            capture_output=True, text=True, timeout=5,
            creationflags=0x08000000,  # CREATE_NO_WINDOW
        )
    except Exception:  # noqa: BLE001
        return False
    return f" {pid} " in f" {out.stdout} " or str(pid) in out.stdout


def _read_pidfile() -> "int | None":
    try:
        return int(_PIDFILE.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def _write_pidfile(pid: int) -> None:
    try:
        _CORE_DIR.mkdir(parents=True, exist_ok=True)
        _PIDFILE.write_text(str(pid), encoding="utf-8")
    except OSError:
        pass


def _clear_pidfile() -> None:
    try:
        _PIDFILE.unlink()
    except OSError:
        pass


def is_running() -> bool:
    """本进程拉起的核心还活着,或 pidfile 记的核心进程还活着(跨 bot 重启复用)。"""
    global _PROC
    if _PROC is not None and _PROC.poll() is None:
        return True
    pid = _read_pidfile()
    return pid is not None and _pid_alive(pid)


def _shell_execute_runas(core: Path, args: str) -> "int | None":
    """非管理员时对 mihomo 单独提权(一次 UAC)。返回子进程 PID(拿不到则 None)。
    注意:本进程非管理员,之后无权 terminate 这个高完整性子进程。"""
    SEE_MASK_NOCLOSEPROCESS = 0x00000040
    SW_HIDE = 0

    class SHELLEXECUTEINFOW(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.DWORD),
            ("fMask", ctypes.c_ulong),
            ("hwnd", wintypes.HWND),
            ("lpVerb", wintypes.LPCWSTR),
            ("lpFile", wintypes.LPCWSTR),
            ("lpParameters", wintypes.LPCWSTR),
            ("lpDirectory", wintypes.LPCWSTR),
            ("nShow", ctypes.c_int),
            ("hInstApp", wintypes.HINSTANCE),
            ("lpIDList", ctypes.c_void_p),
            ("lpClass", wintypes.LPCWSTR),
            ("hkeyClass", wintypes.HKEY),
            ("dwHotKey", wintypes.DWORD),
            ("hIcon", wintypes.HANDLE),
            ("hProcess", wintypes.HANDLE),
        ]

    sei = SHELLEXECUTEINFOW()
    sei.cbSize = ctypes.sizeof(sei)
    sei.fMask = SEE_MASK_NOCLOSEPROCESS
    sei.lpVerb = "runas"
    sei.lpFile = str(core)
    sei.lpParameters = args
    sei.lpDirectory = str(_CORE_DIR)
    sei.nShow = SW_HIDE
    if not ctypes.windll.shell32.ShellExecuteExW(ctypes.byref(sei)):
        return None
    h = sei.hProcess
    if not h:
        return None
    try:
        pid = int(ctypes.windll.kernel32.GetProcessId(wintypes.HANDLE(h)))
    except Exception:  # noqa: BLE001
        pid = None
    try:
        ctypes.windll.kernel32.CloseHandle(wintypes.HANDLE(h))
    except Exception:  # noqa: BLE001
        pass
    return pid or None


def _reload_config(cfg: Path) -> bool:
    """让在跑的 mihomo 热加载新配置(不重启、不中断已建连接、TUN 不掉)。用于出口池加了新出口后
    把新的 PROCESS-NAME 路由规则即时推给核心。走 mihomo 外部控制器 PUT /configs。"""
    url = f"http://{socks5_pool.CLASH_CONTROLLER}/configs?force=true"
    body = json.dumps({"path": str(cfg)}).encode("utf-8")
    req = urllib.request.Request(
        url, data=body, method="PUT",
        headers={"Content-Type": "application/json",
                 "Authorization": f"Bearer {socks5_pool.CLASH_SECRET}"},
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            resp.read()
        main_log("[clash] 已热重载配置:新出口的分流规则即时生效(无需重启 mihomo)。")
        return True
    except Exception as exc:  # noqa: BLE001
        main_log(f"[clash] 热重载失败({exc});新出口可能要等下次重启 bot 才生效。")
        return False


def ensure_running() -> bool:
    """幂等:确保 mihomo(TUN)在跑,且配置始终反映最新出口池。仅在 Windows + 出口池非空时有意义。
    已在跑 -> 重新生成配置并热重载(出口池加了出口也能即时生效);未跑 -> 拉起。
    成功返回 True;缺核心/缺 wintun/拉起失败返回 False。"""
    if os.name != "nt":
        return False
    global _PROC
    with _LOCK:
        if socks5_pool.pool_size() <= 0:
            return False  # 没有出口,不需要 clash

        # 每次都按最新出口池重生成配置(便宜)。已在跑就热重载,让新加的出口即时生效。
        cfg = socks5_pool.gen_clash_config()
        if cfg is None:
            return False

        if is_running():
            _reload_config(cfg)
            return True

        core = find_core()
        if core is None:
            main_log(
                f"[clash] 未找到 mihomo 核心。请把绿色 mihomo.exe 放到 {_CORE_DIR}\\,"
                "并放一份 wintun.dll 在同目录(下载:github.com/MetaCubeX/mihomo/releases "
                "+ wintun.net)。本次不分流,代理仍会从本机真实IP出网。"
            )
            return False
        if find_wintun() is None:
            main_log(
                f"[clash] 缺少 {_WINTUN_NAME}(TUN 网卡驱动)。请把 wintun.dll 放到 "
                f"{_CORE_DIR}\\(下载:wintun.net)。本次不分流。"
            )
            return False

        # -d 核心工作目录(放缓存等),-f 指定我们生成的配置。
        args = f'-d "{_CORE_DIR}" -f "{cfg}"'
        admin = _is_admin()

        if admin:
            try:
                proc = subprocess.Popen(
                    [str(core), "-d", str(_CORE_DIR), "-f", str(cfg)],
                    cwd=str(_CORE_DIR),
                    creationflags=_CREATE_NEW_CONSOLE,
                )
            except Exception as exc:  # noqa: BLE001
                main_log(f"[clash] 拉起 mihomo 失败:{exc}")
                return False
            _PROC = proc
            _write_pidfile(proc.pid)
            main_log(f"[clash] 已拉起 mihomo TUN(pid={proc.pid},管理员),"
                     f"按进程名把 ggs5exit{{k}}.exe 导到对应 SOCKS5。")
            return True

        # 非管理员:对 mihomo 单独提权(一次 UAC)。提权后我们无权收它,记 pidfile 兜底。
        main_log("[clash] bot 非管理员:正在对 mihomo 提权(会弹一次 UAC)。"
                 "建议以管理员身份运行 bot,免弹窗且退出能自动收掉 mihomo。")
        pid = _shell_execute_runas(core, args)
        if pid is None:
            main_log("[clash] mihomo 提权启动失败(UAC 被拒?)。本次不分流。")
            return False
        _PROC = None
        _write_pidfile(pid)
        main_log(f"[clash] 已提权拉起 mihomo TUN(pid={pid})。"
                 "注意:bot 退出不会自动收它,如需停止请在任务管理器结束 mihomo。")
        return True


def shutdown() -> None:
    """收掉本进程拉起的 mihomo(atexit 注册)。提权启动的子进程无权 terminate,只清 pidfile。"""
    global _PROC
    with _LOCK:
        if _PROC is not None and _PROC.poll() is None:
            try:
                _PROC.terminate()
            except Exception:  # noqa: BLE001
                pass
        _PROC = None
        _clear_pidfile()


atexit.register(shutdown)
