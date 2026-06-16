#!/usr/bin/env python3
"""Replay and inspect a captured SO3D login flow.

This tool is intentionally log-driven: it does not know the account password and
does not try to regenerate encrypted login packets. It can:

* print a readable timeline from GGThreadBlock net send/recv logs
* run a local replay server that answers with captured RECV packets
* replay captured SEND packets to a local test server

The default log paths point at the GGThreadBlock bootstrap net logs.
"""

from __future__ import annotations

import argparse
import re
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


DEFAULT_LOG_DIR = Path(r"G:\GuGuSealKarel1120\GGConfig\_bootstrap\net")
DEFAULT_SENDLOG = DEFAULT_LOG_DIR / "sendlog"
DEFAULT_RECVLOG = DEFAULT_LOG_DIR / "recvlog"

KNOWN_GAME_PROTOCOLS = {
    111003: "CL_GET_CHARINFO",
    111006: "CL_GAMESERVER_CONNECT",
    111015: "CL_SAFE_LOGIN",
    211024: "LC_SAFE_LOGIN_SUCCESS",
    221003: "LC_CHARINFO_SUCCESS",
    221009: "LC_GSERV_CONNECT_SUCCESS",
    411005: "CG_ENTER",
    412039: "pre-enter/unknown 0x64987",
}

LINE_RE = re.compile(
    r"^(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) "
    r"\| \[(?P<direction>SEND|RECV)\] "
    r"(?P<host>[^:\s]+):(?P<port>\d+) "
    r"len=(?P<length>\d+)"
    r"(?: proto=(?P<proto>\d+))? "
    r"hex: (?P<hex>[0-9A-Fa-f ]+)$"
)


@dataclass(frozen=True)
class PacketEvent:
    ts: datetime
    direction: str
    host: str
    port: int
    declared_len: int
    logged_proto: int | None
    data: bytes
    source: Path
    line_no: int

    @property
    def len(self) -> int:
        return len(self.data)

    @property
    def game_len(self) -> int | None:
        if len(self.data) >= 4:
            return u32le(self.data, 0)
        return None

    @property
    def game_proto(self) -> int | None:
        if len(self.data) >= 8:
            return u32le(self.data, 4)
        return None

    @property
    def helper_len(self) -> int | None:
        if len(self.data) >= 2:
            return u16le(self.data, 0)
        return None

    @property
    def helper_opcode(self) -> int | None:
        if len(self.data) >= 4:
            return u16le(self.data, 2)
        return None


def u16le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def c_string(data: bytes) -> str:
    raw = data.split(b"\x00", 1)[0]
    for encoding in ("ascii", "big5", "gbk", "utf-8"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    return raw.hex(" ")


def parse_log_file(path: Path) -> list[PacketEvent]:
    events: list[PacketEvent] = []
    if not path.exists():
        return events
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        for line_no, line in enumerate(fh, 1):
            m = LINE_RE.match(line.rstrip("\n"))
            if not m:
                continue
            data = bytes.fromhex(m.group("hex"))
            events.append(
                PacketEvent(
                    ts=datetime.strptime(m.group("ts"), "%Y-%m-%d %H:%M:%S.%f"),
                    direction=m.group("direction"),
                    host=m.group("host"),
                    port=int(m.group("port")),
                    declared_len=int(m.group("length")),
                    logged_proto=int(m.group("proto")) if m.group("proto") else None,
                    data=data,
                    source=path,
                    line_no=line_no,
                )
            )
    return events


def load_events(sendlog: Path, recvlog: Path) -> list[PacketEvent]:
    events = parse_log_file(sendlog) + parse_log_file(recvlog)
    events.sort(key=event_sort_key)
    return events


def event_sort_key(event: PacketEvent) -> tuple[datetime, int, int, str, int]:
    # SEND and RECV logs are separate files, so events that share the same
    # millisecond need a small protocol-aware tie breaker for replay.
    if event.port == 1838:
        op = event.helper_opcode or 0
        helper_order = {
            ("SEND", 1): 10,
            ("RECV", 1): 20,
            ("SEND", 2): 30,
            ("SEND", 3): 40,
            ("RECV", 2): 50,
            ("RECV", 3): 60,
            ("SEND", 4): 70,
        }
        order = helper_order.get((event.direction, op), 100)
    elif event.port == 10002:
        order = 0 if event.direction == "RECV" else 1
    else:
        order = 0 if event.direction == "SEND" else 1
    return (event.ts, event.port, order, event.direction, event.line_no)


def select_events(
    events: list[PacketEvent],
    *,
    since: datetime | None,
    all_events: bool,
    last_gap_seconds: float,
) -> list[PacketEvent]:
    if since is not None:
        return [e for e in events if e.ts >= since]
    if all_events or not events:
        return events

    start = 0
    for i in range(1, len(events)):
        gap = (events[i].ts - events[i - 1].ts).total_seconds()
        if gap >= last_gap_seconds:
            start = i
    return events[start:]


def helper_account_from_event(event: PacketEvent) -> str | None:
    if event.port != 1838 or len(event.data) < 6:
        return None
    op = event.helper_opcode
    if event.direction == "RECV" and op in (2, 3) and len(event.data) >= 10:
        return c_string(event.data[10:])
    if op in (1, 2, 3):
        return c_string(event.data[6:])
    return None


def describe_event(event: PacketEvent, port_index: int) -> str:
    if event.port == 1838:
        op = event.helper_opcode
        account = helper_account_from_event(event)
        if event.direction == "SEND":
            if op == 1:
                return f"helper login/open account={account!r}"
            if op == 2:
                return f"helper account query account={account!r}"
            if op == 3:
                return "helper ready/continue"
            if op == 4 and len(event.data) >= 10:
                return f"helper select/result value={u32le(event.data, 6)}"
        else:
            if op == 1 and len(event.data) >= 6:
                return f"helper login result={u16le(event.data, 4)}"
            if op in (2, 3):
                return f"helper account response account={account!r}"
        return f"helper opcode={op}"

    if event.port == 1841:
        proto = event.game_proto
        name = KNOWN_GAME_PROTOCOLS.get(proto, str(proto))
        if proto == 411005 and len(event.data) >= 52:
            return f"{name} character={c_string(event.data[-16:])!r}"
        return name

    if event.port == 10002:
        if event.direction == "RECV" and event.len == 12:
            return "login server crypto seed/challenge"
        if event.direction == "SEND" and event.len == 60:
            return "encrypted login submit, likely CL_SAFE_LOGIN path"
        if event.direction == "RECV" and event.len == 58:
            return "encrypted login success/server list response"
        if event.direction == "SEND" and event.len == 16:
            return "encrypted CL_GET_CHARINFO"
        if event.direction == "RECV" and event.len > 100:
            return "encrypted LC_CHARINFO_SUCCESS character list"
        if event.direction == "SEND" and event.len == 32:
            return "encrypted CL_GAMESERVER_CONNECT"
        if event.direction == "RECV" and event.len == 60:
            return "encrypted LC_GSERV_CONNECT_SUCCESS game server info"

    proto = event.game_proto
    if proto in KNOWN_GAME_PROTOCOLS:
        return KNOWN_GAME_PROTOCOLS[proto]
    if event.logged_proto is not None:
        return f"logged proto={event.logged_proto}"
    return f"packet #{port_index}"


def event_header(event: PacketEvent) -> str:
    side = "C->S" if event.direction == "SEND" else "S->C"
    return f"{event.ts.strftime('%H:%M:%S.%f')[:-3]} {side} :{event.port} len={event.len}"


def print_trace(events: list[PacketEvent]) -> None:
    per_port_seen: dict[int, int] = {}
    for event in events:
        per_port_seen[event.port] = per_port_seen.get(event.port, 0) + 1
        desc = describe_event(event, per_port_seen[event.port])
        print(f"{event_header(event):36} {desc}")
        if event.port not in (10002, 1838):
            proto = event.game_proto
            if proto is not None:
                print(f"{'':36} game_header len={event.game_len} proto={proto} ({KNOWN_GAME_PROTOCOLS.get(proto, 'unknown')})")
        elif event.port == 1838:
            print(f"{'':36} helper_header len={event.helper_len} opcode={event.helper_opcode}")


def hexdump(data: bytes, limit: int = 32) -> str:
    shown = data[:limit].hex(" ").upper()
    if len(data) > limit:
        shown += " ..."
    return shown


def recvall(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def group_by_port(events: Iterable[PacketEvent]) -> dict[int, list[PacketEvent]]:
    grouped: dict[int, list[PacketEvent]] = {}
    for event in events:
        grouped.setdefault(event.port, []).append(event)
    return grouped


def run_replay_server(events: list[PacketEvent], bind_host: str, strict: bool) -> None:
    grouped = group_by_port(events)
    threads = []
    stop = threading.Event()

    def serve_port(port: int, seq: list[PacketEvent]) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((bind_host, port))
            server.listen(1)
            print(f"[server] listening {bind_host}:{port}, {len(seq)} scripted events")
            conn, addr = server.accept()
            with conn:
                print(f"[server:{port}] client {addr[0]}:{addr[1]}")
                for idx, event in enumerate(seq, 1):
                    desc = describe_event(event, idx)
                    if event.direction == "RECV":
                        conn.sendall(event.data)
                        print(f"[server:{port}] send {event.len:4d} {desc}")
                    else:
                        got = recvall(conn, event.len)
                        ok = got == event.data
                        print(f"[server:{port}] recv {len(got):4d} {desc} match={ok}")
                        if strict and not ok:
                            print(f"[server:{port}] expected {hexdump(event.data)}")
                            print(f"[server:{port}]      got {hexdump(got)}")
                            break
                print(f"[server:{port}] done")
        stop.set()

    for port, seq in grouped.items():
        t = threading.Thread(target=serve_port, args=(port, seq), daemon=True)
        t.start()
        threads.append(t)

    try:
        while any(t.is_alive() for t in threads):
            time.sleep(0.2)
    except KeyboardInterrupt:
        print("\n[server] interrupted")
        stop.set()


def run_replay_client(events: list[PacketEvent], host: str, strict: bool, timed: bool) -> None:
    sockets: dict[int, socket.socket] = {}
    first_ts = events[0].ts if events else None
    start_time = time.monotonic()

    def get_socket(port: int) -> socket.socket:
        sock = sockets.get(port)
        if sock is None:
            sock = socket.create_connection((host, port), timeout=10)
            sockets[port] = sock
            print(f"[client] connected {host}:{port}")
        return sock

    try:
        for idx, event in enumerate(events, 1):
            if timed and first_ts is not None:
                target_delay = (event.ts - first_ts).total_seconds()
                current_delay = time.monotonic() - start_time
                if target_delay > current_delay:
                    time.sleep(target_delay - current_delay)
            sock = get_socket(event.port)
            desc = describe_event(event, idx)
            if event.direction == "SEND":
                sock.sendall(event.data)
                print(f"[client:{event.port}] send {event.len:4d} {desc}")
            else:
                got = recvall(sock, event.len)
                ok = got == event.data
                print(f"[client:{event.port}] recv {len(got):4d} {desc} match={ok}")
                if strict and not ok:
                    print(f"[client:{event.port}] expected {hexdump(event.data)}")
                    print(f"[client:{event.port}]      got {hexdump(got)}")
                    break
    finally:
        for sock in sockets.values():
            sock.close()


def parse_since(text: str | None) -> datetime | None:
    if not text:
        return None
    for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S", "%H:%M:%S.%f", "%H:%M:%S"):
        try:
            parsed = datetime.strptime(text, fmt)
            if fmt.startswith("%H"):
                today = datetime.now()
                return parsed.replace(year=today.year, month=today.month, day=today.day)
            return parsed
        except ValueError:
            pass
    raise SystemExit(f"bad --since value: {text!r}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Inspect or replay a captured SO3D login flow")
    parser.add_argument("--sendlog", type=Path, default=DEFAULT_SENDLOG)
    parser.add_argument("--recvlog", type=Path, default=DEFAULT_RECVLOG)
    parser.add_argument("--since", help="keep events at/after this time, e.g. '2026-06-04 18:08:56'")
    parser.add_argument("--all", action="store_true", help="do not split at the last large timestamp gap")
    parser.add_argument("--last-gap-seconds", type=float, default=20.0)
    sub = parser.add_subparsers(dest="cmd")

    sub.add_parser("trace", help="print decoded timeline")

    server = sub.add_parser("server", help="serve captured RECV packets locally")
    server.add_argument("--bind", default="127.0.0.1")
    server.add_argument("--no-strict", action="store_true", help="do not require client bytes to match SEND log")

    client = sub.add_parser("client", help="replay captured SEND packets to a test server")
    client.add_argument("--host", default="127.0.0.1")
    client.add_argument("--no-strict", action="store_true", help="do not require server bytes to match RECV log")
    client.add_argument("--timed", action="store_true", help="preserve timing gaps from the log")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    cmd = args.cmd or "trace"

    events = load_events(args.sendlog, args.recvlog)
    events = select_events(
        events,
        since=parse_since(args.since),
        all_events=args.all,
        last_gap_seconds=args.last_gap_seconds,
    )
    if not events:
        print("no events selected", file=sys.stderr)
        return 1

    print(
        f"selected {len(events)} events from {events[0].ts} to {events[-1].ts} "
        f"(ports: {', '.join(str(p) for p in sorted(group_by_port(events)))})"
    )

    if cmd == "trace":
        print_trace(events)
    elif cmd == "server":
        run_replay_server(events, args.bind, strict=not args.no_strict)
    elif cmd == "client":
        run_replay_client(events, args.host, strict=not args.no_strict, timed=args.timed)
    else:
        parser.error(f"unknown command {cmd}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
