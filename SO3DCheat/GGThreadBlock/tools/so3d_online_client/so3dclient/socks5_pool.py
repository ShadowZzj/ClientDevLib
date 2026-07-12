"""SOCKS5 出口池 + 每出口独立启动器 + Proxifier 配置生成。

突破"单出口IP总并发上限(实测~77)"的手段:让每个 port_proxy 代理实例从**不同的真实出口
IP**发包。protect.dll 在 port_proxy 进程内自己 connect 边缘节点,我们无法在 Python 里改它
的 connect,所以借助 Proxifier 在系统层把"某个进程的全部出站"整体导向某个 SOCKS5。Proxifier
只能按"程序名/目标host/目标端口"匹配,**无法按 PID**——而所有代理实例都是同一个 python.exe,
于是给每条出口造一个独立文件名的 python 启动器 ggs5exit{lane}.exe(就是 python.exe 的硬链接,
行为与 python 完全一致,只是文件名不同),Proxifier 按这个文件名把它整进程导到对应 SOCKS5。

车道(lane)与监听别名一一对应、可由别名**纯函数**推出(见 lane_for_alias),所以孤儿实例
重连也能稳定落回原出口,无需把 lane 写进 registry:
  lane 0   = 直连(本机真实IP),用普通 python.exe,Proxifier 走默认 Direct;
  lane k≥1 = socks5_pool.txt 第 k-1 条出口,用 ggs5exit{k}.exe 启动 -> Proxifier 导到该 SOCKS5。

socks5_pool.txt(放在交付目录 APP_DIR)每行一个出口,格式:
    host:port:user:pass[:地区备注]
空行与以 # 开头的行忽略。密码里不要含冒号。
"""
from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

from .runtime import APP_DIR
from .logio import main_log

_POOL_NAME = "socks5_pool.txt"
_LAUNCHER_PREFIX = "ggs5exit"
_PROFILE_NAME = "redpass_clash.yaml"

# mihomo 外部控制器(仅本机)。clash_auto 用它热重载配置:出口池加一条后无需重启 mihomo,
# 下次拉新实例时把最新配置 PUT /configs 推给在跑的核心即可让新出口的路由规则即时生效。
# 端口选了个冷门的 29390 避免和常见 clash GUI(9090/9097)或其它服务(实测本机 9090 被
# 0dcloudCore 占用)撞车——撞了的话 mihomo 起不了控制器、热重载就失效。
CLASH_CONTROLLER = "127.0.0.1:29390"
CLASH_SECRET = "redpass-redpass"

_POOL_TEMPLATE = (
    "# SOCKS5 出口池:每行一个出口,格式  host:port:user:pass[:地区备注]\n"
    "# 规则:\n"
    "#  - 第 1 批在线账号走「直连(本机真实IP)」,不占用这里的出口;\n"
    "#  - 装满「每出口上限」后,后续每批依次走下面第 1、第 2… 个 SOCKS5;\n"
    "#  - 改完保存后点界面「生成Clash配置」,bot 会自动用 mihomo 核心(TUN 模式)加载它,\n"
    "#    按进程名把每个代理实例导到对应出口(密码已写进配置,无需手填)。\n"
    "#  - 密码里不要含冒号(:)。空行和以 # 开头的行会被忽略。\n"
    "81.181.174.103:50101:handawei:QURJIEe2Z5:香港\n"
)


def pool_file() -> Path:
    return Path(APP_DIR) / _POOL_NAME


def profile_file() -> Path:
    return Path(APP_DIR) / _PROFILE_NAME


def launcher_name(lane: int) -> str:
    return f"{_LAUNCHER_PREFIX}{lane}.exe"


def ensure_pool_file_template() -> Path:
    """首次打开出口池时若文件不存在,写入带说明的模板(预填用户已购的那条出口)。"""
    path = pool_file()
    if not path.exists():
        try:
            path.write_text(_POOL_TEMPLATE, encoding="utf-8")
        except OSError as exc:  # noqa: BLE001
            main_log(f"[socks5] 创建 {path} 失败:{exc}")
    return path


def load_pool() -> list[dict]:
    """读 socks5_pool.txt,返回出口列表(顺序即 lane 顺序,第 i 条对应 lane i+1)。"""
    path = pool_file()
    out: list[dict] = []
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return out
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        bits = line.split(":")
        if len(bits) < 2:
            continue
        host = bits[0].strip()
        port = bits[1].strip()
        if not host or not port.isdigit():
            continue
        user = bits[2].strip() if len(bits) >= 3 else ""
        password = bits[3].strip() if len(bits) >= 4 else ""
        region = bits[4].strip() if len(bits) >= 5 else ""
        out.append({"host": host, "port": port, "user": user,
                    "pass": password, "region": region, "raw": line})
    return out


def pool_size() -> int:
    return len(load_pool())


def socks5_for_lane(lane: int) -> "dict | None":
    """lane>=1 时返回出口池第 lane-1 条;lane 0(直连)或越界返回 None。"""
    if lane < 1:
        return None
    pool = load_pool()
    idx = lane - 1
    return pool[idx] if 0 <= idx < len(pool) else None


def lane_for_alias(alias: str, reserved: "set[str] | None" = None) -> int:
    """监听别名 -> 车道号(纯函数,不读盘)。可分配别名按尾号升序排成 0,1,2…,
    跳过 reserved 里的别名(如默认单实例代理 127.2.57.25)。最低尾号 = lane 0 = 直连。"""
    reserved = reserved or set()
    try:
        n = int(str(alias).rsplit(".", 1)[-1])
    except (ValueError, AttributeError):
        return 0
    res_tails: set[int] = set()
    for item in reserved:
        try:
            res_tails.add(int(str(item).rsplit(".", 1)[-1]))
        except ValueError:
            pass
    lane = 0
    for m in range(1, n):
        if m not in res_tails:
            lane += 1
    return lane


def _real_python() -> "str | None":
    """定位真实 python.exe(造启动器硬链接要用)。frozen 打包成 bot 本体时 sys.executable
    不是 python,退回 PATH 上的 python。"""
    exe = sys.executable or ""
    base = os.path.basename(exe).lower()
    if exe and base.startswith("python") and not getattr(sys, "frozen", False):
        return exe
    return shutil.which("python")


def ensure_launcher(lane: int, base_exe: "str | None" = None) -> "str | None":
    """确保存在 ggs5exit{lane}.exe(base_exe 的硬链接,mihomo 按文件名路由它整进程)。
    base_exe 缺省取真实 python.exe(源码运行,启动器与 python 同目录);打包交付时由调用方传入
    port_proxy.exe(启动器与 exe 同目录、目标机无需 python)。优先硬链接(不复制字节、不易触发
    杀软),失败再退回复制。"""
    base = base_exe or _real_python()
    if not base:
        return None
    dst = Path(base).parent / launcher_name(lane)
    if dst.exists():
        return str(dst)
    try:
        os.link(base, dst)  # 同卷硬链接:与 base_exe 共享 inode,仅多一个文件名
        return str(dst)
    except OSError:
        pass
    try:
        shutil.copy2(base, dst)
        return str(dst)
    except OSError as exc:  # noqa: BLE001
        main_log(f"[socks5] 创建启动器 {dst} 失败:{exc}")
        return None


def _yaml_dq(value) -> str:
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def _is_ipv4(host: str) -> bool:
    parts = host.split(".")
    if len(parts) != 4:
        return False
    for p in parts:
        if not p.isdigit() or not 0 <= int(p) <= 255:
            return False
    return True


def gen_clash_config(out_path: "str | Path | None" = None) -> "Path | None":
    """按出口池生成 mihomo/clash 配置(TUN 模式 + 按进程名路由):
    每个 ggs5exit{k}.exe -> 对应 SOCKS5,其余流量直连。密码直接写进配置(无需手填)。
    用 TUN 在网卡层路由、不往进程注入 DLL,所以不会和 protect.dll 抢 Winsock(Proxifier 那套
    注入式代理就是因此崩溃/抓不到流量)。

    关键:把每条 SOCKS5 服务器自身的 IP 从 TUN 里排除(route-exclude-address)并钉成 DIRECT,
    否则 mihomo 拨向上游 SOCKS5 的那条连接会被自己的 auto-route 再次抓进 TUN -> 路由回环 ->
    连不上 SOCKS5(实测表现:exit 命中正确但 `<socks5ip>:port i/o timeout`,protect.dll 探测
    全部 rtt=-1、选到节点也连不通 -> protect_start 1009)。"""
    pool = load_pool()
    if not pool:
        main_log("[socks5] 出口池为空,无法生成 Clash 配置;请先在 socks5_pool.txt 添加出口。")
        return None
    out = Path(out_path) if out_path else profile_file()

    proxies: list[str] = []
    rules: list[str] = []
    server_ips: list[str] = []  # 上游 SOCKS5 服务器 IP(去重,保序),用于排除 TUN 回环
    for i, ent in enumerate(pool):
        lane = i + 1
        name = f"exit{lane}"
        app = launcher_name(lane)
        host = ent["host"]
        if _is_ipv4(host) and host not in server_ips:
            server_ips.append(host)
        fields = [f"name: {_yaml_dq(name)}", "type: socks5",
                  f"server: {_yaml_dq(host)}", f"port: {ent['port']}"]
        if ent.get("user"):
            fields.append(f"username: {_yaml_dq(ent['user'])}")
            fields.append(f"password: {_yaml_dq(ent['pass'])}")
        fields.append("udp: true")
        proxies.append("  - {" + ", ".join(fields) + "}")
        rules.append(f"  - PROCESS-NAME,{app},{name}")
    # 先把上游 SOCKS5 服务器 IP 钉 DIRECT(放在最前,优先级最高),防止规则层把对它的连接又导回代理。
    direct_rules = [f"  - IP-CIDR,{ip}/32,DIRECT,no-resolve" for ip in server_ips]
    rules = direct_rules + rules
    rules.append("  - MATCH,DIRECT")

    # route-exclude-address:让发往这些 IP 的包不进 TUN(走物理网卡直连),从路由层断开回环。
    exclude_line = ""
    if server_ips:
        exclude_line = ("  route-exclude-address: ["
                        + ", ".join(f"{ip}/32" for ip in server_ips) + "]\n")

    text = (
        "# redpass 出口分流(mihomo/clash,TUN 模式)。bot 会自动用 mihomo 核心加载它。\n"
        "# 按进程名把 ggs5exit{k}.exe 导到第 k 条 SOCKS5,其余直连。请勿手改。\n"
        "mixed-port: 7897\n"
        "mode: rule\n"
        "log-level: warning\n"
        "find-process-mode: always\n"
        f"external-controller: {CLASH_CONTROLLER}\n"
        f"secret: {_yaml_dq(CLASH_SECRET)}\n"
        "tun:\n"
        "  enable: true\n"
        "  stack: gvisor\n"
        "  auto-route: true\n"
        "  auto-detect-interface: true\n"
        "  dns-hijack: [any:53]\n"
        + exclude_line +
        "dns:\n"
        "  enable: true\n"
        "  nameserver: [223.5.5.5, 119.29.29.29]\n"
        "proxies:\n" + "\n".join(proxies) + "\n"
        "rules:\n" + "\n".join(rules) + "\n"
    )
    try:
        out.write_text(text, encoding="utf-8")
    except OSError as exc:  # noqa: BLE001
        main_log(f"[socks5] 写 Clash 配置 {out} 失败:{exc}")
        return None
    main_log(f"[socks5] 已生成 Clash 配置:{out}({len(pool)} 个出口,TUN+按进程名路由,"
             f"已排除 {len(server_ips)} 个上游IP防回环)。")
    return out
