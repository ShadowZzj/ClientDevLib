#!/usr/bin/env python3
"""独立 SOCKS5 连通性测试:不依赖 so3dclient,也不引第三方库。

用途:验证本地 SOCKS5 代理(默认 127.0.0.1:17891)能不能把 TCP 转发出去。
对每个目标做一次完整 SOCKS5 CONNECT 握手,握手成功 = 代理已和目标建立 TCP 连接
= 流量确实发出去了。再尝试 recv 一点 banner(有就打印,没有也算 PASS)。

用法:
    python socks5_test.py                      # 测内置游戏服列表
    python socks5_test.py 1.2.3.4:443          # 额外测一个自定义目标
    python socks5_test.py --proxy 127.0.0.1:1080 baidu.com:80
    python socks5_test.py --user U --pass P     # 代理需要账号密码时

退出码:全部目标 PASS 返回 0,有任意 FAIL 返回 1。
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import time

# 取自 so3dclient/runtime.py 的 test 模式真服地址(公网,直连即走代理出口)。
DEFAULT_TARGETS = [
    ("login 1818", "165.154.194.121", 1818),
    ("bill 1838", "123.58.197.240", 1838),
]


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(f"代理过早关闭(已收 {len(buf)}/{n} 字节)")
        buf.extend(chunk)
    return bytes(buf)


def socks5_connect(
    proxy_host: str,
    proxy_port: int,
    dst_host: str,
    dst_port: int,
    *,
    username: str | None,
    password: str | None,
    timeout: float,
) -> socket.socket:
    """对 proxy 做 SOCKS5(RFC1928)CONNECT 握手,成功则返回已穿透的 socket。"""
    sock = socket.create_connection((proxy_host, proxy_port), timeout=timeout)
    sock.settimeout(timeout)
    try:
        # --- 1. 方法协商 ---
        if username is not None and password is not None:
            methods = b"\x00\x02"  # NO_AUTH + USERNAME/PASSWORD
        else:
            methods = b"\x00"      # 仅 NO_AUTH
        sock.sendall(b"\x05" + bytes([len(methods)]) + methods)
        ver, method = _recv_exact(sock, 2)
        if ver != 0x05:
            raise ConnectionError(f"代理握手回包版本异常: 0x{ver:02x}(可能不是 SOCKS5)")
        if method == 0xFF:
            raise ConnectionError("代理拒绝所有认证方法(0xFF)")

        # --- 2. 用户名/密码认证(RFC1929),仅当代理选了 0x02 ---
        if method == 0x02:
            if username is None or password is None:
                raise ConnectionError("代理要求账号密码,但未提供 --user/--pass")
            u = username.encode("utf-8")
            p = password.encode("utf-8")
            sock.sendall(b"\x01" + bytes([len(u)]) + u + bytes([len(p)]) + p)
            _ver, status = _recv_exact(sock, 2)
            if status != 0x00:
                raise ConnectionError(f"代理认证失败 status=0x{status:02x}")
        elif method != 0x00:
            raise ConnectionError(f"代理选了不支持的认证方法 0x{method:02x}")

        # --- 3. CONNECT 请求 ---
        try:
            raw_ip = socket.inet_aton(dst_host)  # 目标是 IPv4 字面量
            req = b"\x05\x01\x00\x01" + raw_ip + struct.pack(">H", dst_port)
        except OSError:
            # 不是 IPv4 字面量,按域名(ATYP=0x03)交给代理解析
            name = dst_host.encode("idna") if dst_host.isascii() else dst_host.encode("utf-8")
            req = b"\x05\x01\x00\x03" + bytes([len(name)]) + name + struct.pack(">H", dst_port)
        sock.sendall(req)

        # --- 4. CONNECT 回复 ---
        ver, rep, _rsv, atyp = _recv_exact(sock, 4)
        if ver != 0x05:
            raise ConnectionError(f"CONNECT 回包版本异常: 0x{ver:02x}")
        if rep != 0x00:
            raise ConnectionError(f"CONNECT 被拒 rep=0x{rep:02x} ({_rep_text(rep)})")
        # 读掉绑定地址(BND.ADDR + BND.PORT),长度随 atyp 变化
        if atyp == 0x01:
            _recv_exact(sock, 4 + 2)
        elif atyp == 0x04:
            _recv_exact(sock, 16 + 2)
        elif atyp == 0x03:
            ln = _recv_exact(sock, 1)[0]
            _recv_exact(sock, ln + 2)
        else:
            raise ConnectionError(f"CONNECT 回包 ATYP 异常: 0x{atyp:02x}")
        return sock
    except Exception:
        try:
            sock.close()
        except OSError:
            pass
        raise


def _rep_text(rep: int) -> str:
    return {
        0x01: "general failure",
        0x02: "connection not allowed",
        0x03: "network unreachable",
        0x04: "host unreachable",
        0x05: "connection refused",
        0x06: "TTL expired",
        0x07: "command not supported",
        0x08: "address type not supported",
    }.get(rep, "unknown")


def run_one(proxy_host, proxy_port, label, host, port, *, username, password, timeout) -> bool:
    print(f"[*] {label}: 经 {proxy_host}:{proxy_port} 连 {host}:{port} ...")
    t0 = time.monotonic()
    try:
        sock = socks5_connect(
            proxy_host, proxy_port, host, port,
            username=username, password=password, timeout=timeout,
        )
    except Exception as exc:
        dt = (time.monotonic() - t0) * 1000
        print(f"    FAIL ({dt:.0f}ms): {exc}")
        return False
    dt = (time.monotonic() - t0) * 1000
    print(f"    握手成功 ({dt:.0f}ms)——代理已与目标建立 TCP 连接,流量已发出去。")
    # 顺手探一下 banner:很多游戏服在客户端先发包前不会主动下发,收不到也正常。
    try:
        sock.settimeout(2.0)
        data = sock.recv(256)
        if data:
            print(f"    目标主动下发 {len(data)} 字节: {data[:64].hex(' ')}")
        else:
            print("    目标未主动下发数据(正常:等客户端先发包)。")
    except socket.timeout:
        print("    2s 内无主动下发(正常:等客户端先发包)。")
    except Exception as exc:
        print(f"    recv 探测异常(不影响连通判定): {exc}")
    finally:
        try:
            sock.close()
        except OSError:
            pass
    print("    PASS")
    return True


def parse_target(s: str) -> tuple[str, str, int]:
    if ":" not in s:
        raise argparse.ArgumentTypeError(f"目标格式应为 host:port,收到 {s!r}")
    host, _, port = s.rpartition(":")
    return (f"custom {host}:{port}", host, int(port))


def main() -> int:
    ap = argparse.ArgumentParser(description="SOCKS5 代理连通性测试")
    ap.add_argument("targets", nargs="*", help="额外目标 host:port(可多个)")
    ap.add_argument("--proxy", default="127.0.0.1:17891", help="SOCKS5 代理 host:port")
    ap.add_argument("--user", default=None, help="代理用户名(需要认证时)")
    ap.add_argument("--pass", dest="password", default=None, help="代理密码")
    ap.add_argument("--timeout", type=float, default=8.0, help="单次连接超时秒数")
    ap.add_argument("--no-builtin", action="store_true", help="不测内置游戏服,只测命令行给的目标")
    args = ap.parse_args()

    phost, _, pport = args.proxy.rpartition(":")
    pport = int(pport)

    targets = [] if args.no_builtin else list(DEFAULT_TARGETS)
    for t in args.targets:
        targets.append(parse_target(t))

    if not targets:
        print("没有可测目标(用了 --no-builtin 又没给目标)。")
        return 2

    print(f"=== SOCKS5 测试,代理 {phost}:{pport} ===")
    results = []
    for label, host, port in targets:
        ok = run_one(
            phost, pport, label, host, port,
            username=args.user, password=args.password, timeout=args.timeout,
        )
        results.append((label, ok))
        print()

    passed = sum(1 for _, ok in results if ok)
    print(f"=== 结果: {passed}/{len(results)} PASS ===")
    for label, ok in results:
        print(f"    {'PASS' if ok else 'FAIL'}  {label}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
