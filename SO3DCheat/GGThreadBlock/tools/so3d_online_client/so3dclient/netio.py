"""socket 收发原语:读帧、drain、recv_until_proto、chat 连接。"""
from __future__ import annotations

import argparse
import select
import socket
import struct
import time

from .runtime import current_stop_event, register_socket
from .protocol import ChatStatus, PROTO_NAMES, ascii_preview, proto_name, short_hex, u32
from .crypto import XOR_KEYS, xor_payload
from .packets import chat_login_packet, chat_status_packet, debug_game_recv

from .logio import log_print as print


def read_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("socket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def read_game_packet(sock: socket.socket, *, xor: bool, key_index: int, timeout: float, bridge=None) -> tuple[bytes, bytes]:
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
    # 正式服:从代理收到的 login(10002)wire,只有 body>0xC(总 len>12)的包带 123.dll
    # layer-1 密文;len=12 的 handshake/心跳类没有 layer-1。对带 layer-1 的包先经 bridge
    # 用游戏进程的活 session key 原地解密,得到与测试服等价的明文 frame,再走 layer-2 xor。
    if bridge is not None and len(raw) > 12:
        raw = bridge.decrypt_wire(raw)
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
    stop_event = current_stop_event()
    chunks: list[bytes] = []
    for _ in range(max_reads):
        if stop_event.is_set():
            return chunks
        if sock.fileno() < 0:
            if stop_event.is_set():
                return chunks
            raise ConnectionError(f"{label} socket closed")
        try:
            readable, _writable, _errored = select.select([sock], [], [], 0)
        except ValueError as exc:
            if stop_event.is_set():
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
    bridge=None,
) -> bytes:
    deadline = time.monotonic() + timeout
    seen: list[int] = []
    fatal_protos = set() if fatal_protos is None else fatal_protos
    return_protos = set() if return_protos is None else return_protos
    while True:
        remaining = max(0.2, deadline - time.monotonic())
        try:
            raw, decoded = read_game_packet(sock, xor=True, key_index=key_index, timeout=remaining, bridge=bridge)
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
            # layer-2 XOR key fallback:正式服里某些回包(如 LC_LOGIN_SECONDARY)用的 layer-2
            # key index 与 handshake 协商出来的不同。固定 key 解出的 proto 不是已知 SO3D 协议时,
            # 在已解过 layer-1 的 raw 上逐个试其它 key,命中已知 proto 就采用该包(不改全局 key_index)。
            if got_proto not in PROTO_NAMES and len(raw) > 4:
                for alt in range(len(XOR_KEYS)):
                    if alt == key_index:
                        continue
                    alt_decoded = xor_payload(raw, alt)
                    alt_proto = u32(alt_decoded, 4) if len(alt_decoded) >= 8 else None
                    if alt_proto in PROTO_NAMES:
                        if debug:
                            print(f"[10002] layer-2 key fallback {key_index}->{alt} "
                                  f"proto={proto_name(alt_proto)}")
                        decoded = alt_decoded
                        got_proto = alt_proto
                        break
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


def connect_chat(
    args: argparse.Namespace,
    character: str,
    encoding: str,
    player_id: int,
    chat_status: ChatStatus,
    *,
    debug: bool,
) -> tuple[socket.socket, float, tuple[int, int] | None]:
    """建立 chat(3000) socket:连链 → 0x4100 登录 → drain 首包 → 重放一次 0x4110 状态。
    返回 (sock, last_chat_rx, last_chat_status)。chat 是独立会话,它的 RST/stale 不应
    拖垮整局;主循环据此可只重连这一条 socket,而新链路服务器端无状态,所以每次都重发状态。"""
    sock = socket_connect(args.chat_host, args.chat_port, args.timeout)
    send_chat(
        sock,
        chat_login_packet(args.account, args.password, character, encoding, player_id),
        "login",
    )
    initial_chat_chunks = drain_socket(sock, "chat", enabled=debug)
    last_chat_rx = time.monotonic()
    if initial_chat_chunks:
        print(f"[chat] received {len(initial_chat_chunks)} initial packet chunk(s)")
    last_chat_status: tuple[int, int] | None = None
    if args.chat_status_enabled:
        last_chat_status = send_chat_status_if_ready(
            sock, chat_status, character, encoding, last_chat_status,
        )
    return sock, last_chat_rx, last_chat_status


def socket_connect(host: str, port: int, timeout: float) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    register_socket(sock)
    print(f"[net] connected {host}:{port}")
    return sock
