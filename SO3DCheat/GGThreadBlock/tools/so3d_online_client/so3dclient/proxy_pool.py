"""bot 端代理自动伸缩:按需拉起 port_proxy.py 实例,无需手动启动代理或配节点。

用法只剩一个旋钮:把账号设成 server_mode=prod + proxy_host=auto,再设每节点上限
proxy_max_per_node(=阈值)。账号上线时本模块负责:
  1. registry 里若已有「未达阈值」的活代理实例 -> 直接占它一个名额;
  2. 所有活实例都占满(或一个都没有) -> 自动 `python port_proxy.py` 起一个新实例
     (GGTB_AUTO=1 + 唯一 listen 别名 127.2.57.N,不指定节点),它会 claim 一个别的
     实例还没占的最低延迟节点;等它登记进 registry 后,把本账号占到它上面。
于是节点占满就自动开下一个,延迟优先,完全不用人工开窗口/选节点。

拉起的代理进程句柄记在 _SPAWNED,bot 正常退出时(atexit)一并 terminate;若 bot 被硬杀,
代理会变孤儿但仍登记在 registry 里,下次 bot 启动会直接复用(它们还活着)。

进程内用 _LOCK 串行化「判断+拉起+等待」:多个账号同时上线时,只有第一个会拉新实例并等它
就绪,其余账号阻塞到锁释放后就能在新实例上找到容量,不会为并发登录拉起一堆多余实例。
"""
from __future__ import annotations

import atexit
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

from . import auto_route
from . import socks5_pool
from . import clash_auto
from .runtime import APP_DIR, PROD_PROXY_HOST
from .logio import main_log

_LOCK = threading.RLock()
_SPAWNED: dict[str, subprocess.Popen] = {}

_ALIAS_PREFIX = "127.2.57."
_ALIAS_MIN = 1
_ALIAS_MAX = 254
# 新实例从拉起到 claim 到节点并登记进 registry 的最长等待秒数。port_proxy 要加载
# protect.dll、跑 selectBest 选路(SDK 自身约 10s 选路超时),18s 给足裕量。
_SPAWN_WAIT_SECONDS = 18.0
_REGISTER_POLL = 0.3
_CREATE_NEW_CONSOLE = 0x00000010  # Windows:每个代理一个独立控制台窗口,便于观察日志


def _python_exe() -> str:
    # 源码运行时 sys.executable 就是 python 解释器;打包成 exe(frozen)时它是 bot 本体,
    # 没法拿来跑 .py,退回 PATH 上的 python。
    if getattr(sys, "frozen", False):
        return "python"
    return sys.executable or "python"


def _port_proxy_script() -> Path:
    # port_proxy.py 与 so3dclient 包同级(都在 so3d_online_client 交付目录 = APP_DIR)。
    return Path(APP_DIR) / "port_proxy.py"


def _proxy_exe() -> "str | None":
    """打包交付时与 bot exe 同目录的 port_proxy.exe。存在则用它跑代理实例(目标机无需装
    python,dist 整个拷走即可用);源码运行时返回 None,回退 `python port_proxy.py`。"""
    cand = Path(APP_DIR) / "port_proxy.exe"
    return str(cand) if cand.is_file() else None


def _used_aliases() -> set[str]:
    used: set[str] = set()
    try:
        used.update(auto_route.list_live_instances().keys())
    except Exception:  # noqa: BLE001 - registry 读不动也别挡住分配
        pass
    for alias, proc in list(_SPAWNED.items()):
        if proc.poll() is None:
            used.add(alias)
    return used


def _reserved_aliases() -> set[str]:
    # 默认单实例代理(PROD_PROXY_HOST=127.2.57.25)固定占这个别名:它是 server_mode=prod 下
    # proxy_host 为空/自愈回退时连的那个代理。自动分流绝不能再把它分出去,否则两个 port_proxy
    # 进程会在同一个 127.2.57.25:port 上撞 bind。所以把它(及其它等于 PROD_PROXY_HOST 的别名)
    # 从可分配池里剔除——自动实例从 .1 起按序分,但跳过 .25。
    return {PROD_PROXY_HOST.strip()}


def _alloc_alias() -> "str | None":
    used = _used_aliases() | _reserved_aliases()
    for n in range(_ALIAS_MIN, _ALIAS_MAX + 1):
        cand = f"{_ALIAS_PREFIX}{n}"
        if cand not in used:
            return cand
    return None


def _reap() -> None:
    for alias, proc in list(_SPAWNED.items()):
        if proc.poll() is not None:
            _SPAWNED.pop(alias, None)


def _spawn_instance() -> "str | None":
    alias = _alloc_alias()
    if alias is None:
        main_log("[auto] 监听别名已用尽,无法再拉起代理实例")
        return None
    script = _port_proxy_script()
    proxy_exe = _proxy_exe()  # 打包交付:用 port_proxy.exe(无需 python);源码:None
    if proxy_exe is None and not script.exists():
        main_log(f"[auto] 找不到 port_proxy.exe / port_proxy.py({script.parent}),无法自动拉起代理")
        return None
    # 车道(出口)绑定:别名 -> lane(纯函数,见 socks5_pool.lane_for_alias)。
    # lane 0 = 直连(本机真实IP),直接跑 port_proxy(exe 或 python 脚本);lane k≥1 走出口池第
    # k-1 条 SOCKS5,经独立文件名启动器 ggs5exit{k}.exe(port_proxy.exe / python.exe 的硬链接)
    # 拉起,让 mihomo 按进程名 ggs5exit{k}.exe 把它整进程导到该出口。
    # 这样每个实例从不同真实出口IP发包,绕开"单出口IP总并发~77"的天花板。
    lane = socks5_pool.lane_for_alias(alias, reserved=_reserved_aliases())
    base = proxy_exe or _python_exe()  # 跑 port_proxy 逻辑的可执行体(exe 或 python 解释器)
    # 默认命令(lane 0 直连):frozen 直接跑 exe;源码跑 `python port_proxy.py`。
    cmd = [proxy_exe] if proxy_exe else [base, str(script)]
    routed = ""
    if lane >= 1:
        ent = socks5_pool.socks5_for_lane(lane)
        if ent is None:
            main_log(f"[auto] 出口池仅 {socks5_pool.pool_size()} 个 SOCKS5,车道 {lane} 无可用出口——"
                     "请在 socks5_pool.txt 添加更多出口。本次不拉新实例(避免撞 77 上限)。")
            return None
        launcher = socks5_pool.ensure_launcher(lane, base_exe=base)
        if launcher is None:
            main_log(f"[auto] 车道 {lane} 无法创建启动器(找不到基准 exe 或建链接失败),"
                     "回退默认启动方式——该出口分流将失效,可能撞 77 上限")
        else:
            # 启动器是 base 的硬链接:frozen 它本身就是 port_proxy,直接跑;源码它是 python,带脚本跑。
            cmd = [launcher] if proxy_exe else [launcher, str(script)]
            routed = (f" 经 {os.path.basename(launcher)} -> SOCKS5 "
                      f"{ent['host']}:{ent['port']}({ent.get('region') or '?'})")
            # 关键时序:必须先把 mihomo(TUN)拉起来,再启动 ggs5exit{k}.exe,否则它的首个
            # 连接会在路由就绪前逃逸成本机直连。ensure_running 幂等,已在跑则瞬间返回。
            if not clash_auto.ensure_running():
                main_log(f"[auto] 车道 {lane} 的 mihomo 路由未就绪(缺核心/wintun 或拉起失败),"
                         "该实例将从本机真实IP出网,分流可能失效")
    env = dict(os.environ)
    env["GGTB_AUTO"] = "1"
    env["GGTB_LISTEN_IP"] = alias
    env.pop("GGTB_NODE_IP", None)  # 不手动钉节点,交给 auto-claim
    kwargs: dict = {"cwd": str(script.parent), "env": env}
    if os.name == "nt":
        kwargs["creationflags"] = _CREATE_NEW_CONSOLE
    try:
        proc = subprocess.Popen(cmd, **kwargs)
    except Exception as exc:  # noqa: BLE001
        main_log(f"[auto] 拉起代理实例失败({exc})")
        return None
    _SPAWNED[alias] = proc
    main_log(f"[auto] 已拉起新代理实例 {alias}(pid={proc.pid}, lane={lane}){routed},"
             "等待它选路 claim 节点…")
    return alias


def _wait_registered(alias: str, timeout: float) -> "dict | None":
    """轮询 registry,等到 alias 实例登记并 claim 到节点(node_ip 非空)才返回其 info;
    超时或该进程提前退出则返回 None。"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        inst = auto_route.list_live_instances().get(alias)
        if isinstance(inst, dict) and inst.get("node_ip"):
            return inst
        proc = _SPAWNED.get(alias)
        if proc is not None and proc.poll() is not None:
            return None  # 代理进程已退出(claim 失败/单实例锁等),不必再等
        time.sleep(_REGISTER_POLL)
    return None


def acquire(account_key: str, threshold, launcher_pid: "int | None" = None):
    """账号上线时调用,替代直接 auto_route.acquire_slot:现有实例有容量就用,
    没容量(全满/没有)就自动拉一个新代理实例占下一个节点。返回 auto_route.acquire_slot
    的结果 dict(alias/node_ip/rtt_ms/count/overflow/reused)或 None(连默认都没有)。"""
    try:
        cap = int(threshold or 0)
    except (TypeError, ValueError):
        cap = 0
    with _LOCK:
        _reap()
        # 现有实例里找未满名额;allow_overflow=False -> 全满时不落位,返回 needs_spawn 让我们拉新。
        res = auto_route.acquire_slot(account_key, cap, launcher_pid=launcher_pid,
                                      allow_overflow=False)
        if res is not None and not res.get("needs_spawn"):
            return res  # 命中现有实例,或 sticky 复用

        # 需要新实例:res 为 None(一个实例都没有)或 needs_spawn(全部达上限)。
        alias = _spawn_instance()
        if alias is None:
            # 拉不起来 -> 尽力而为,允许超额落到现有实例(没有就 None,上层回退默认代理)。
            return auto_route.acquire_slot(account_key, cap, launcher_pid=launcher_pid)

        inst = _wait_registered(alias, _SPAWN_WAIT_SECONDS)
        if inst is None:
            main_log(f"[auto] 新代理 {alias} 等待 {_SPAWN_WAIT_SECONDS:.0f}s 仍未选到节点,"
                     f"本次回退现有实例(可能超额)")
            return auto_route.acquire_slot(account_key, cap, launcher_pid=launcher_pid)

        main_log(f"[auto] 新代理 {alias} 已选到节点 node={inst.get('node_ip')} "
                 f"rtt={inst.get('rtt_ms')}ms,本账号将占用它")
        # prefer_alias 确保占到刚拉起的这个实例(它此刻是唯一有容量的)。
        return auto_route.acquire_slot(account_key, cap, launcher_pid=launcher_pid,
                                       prefer_alias=alias)


def shutdown_all() -> None:
    """bot 退出时终止本进程拉起的所有代理实例(atexit 注册;也可手动调用)。"""
    with _LOCK:
        for alias, proc in list(_SPAWNED.items()):
            if proc.poll() is None:
                try:
                    proc.terminate()
                except Exception:  # noqa: BLE001
                    pass
            _SPAWNED.pop(alias, None)


atexit.register(shutdown_all)
