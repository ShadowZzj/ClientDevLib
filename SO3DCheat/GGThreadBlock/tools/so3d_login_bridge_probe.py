#!/usr/bin/env python3
"""Probe the in-game SO3D login bridge.

This talks to GGThreadBlock.dll's LoginBridge named pipe. The bridge opens the
socket from inside SO3DPlus.exe and calls the game's patched WinSock IAT, so
this is a direct feasibility test for using one injected game process as the
protocol/crypto host for Python-driven login sessions.
"""

from __future__ import annotations

import json
import os
import time
from dataclasses import dataclass
from itertools import count
from typing import Any

from so3d_online_login_client import (
    CONFIG as LOGIN_CONFIG,
    debug_game_recv,
    debug_game_send,
    handshake_xor_index,
    login_packet,
    proto_name,
    short_hex,
    u32,
    xor_payload,
)


CONFIG = {
    # 0 means auto-detect \\.\pipe\GGTB_LOGIN_BRIDGE_<pid>.
    "pid": int(os.environ.get("GGTB_BRIDGE_PID", "0") or "0"),
    "pipe_prefix": os.environ.get("GGTB_BRIDGE_PREFIX", "GGTB_LOGIN_BRIDGE_"),
    "host": "127.2.57.25",
    "port": 10002,
    "account": LOGIN_CONFIG.get("account", "gongyu9011212"),
    "password": LOGIN_CONFIG.get("password", "901121"),
    "encoding": LOGIN_CONFIG.get("encoding", "big5"),
    "read_timeout_ms": 8000,
    "quiet_ms": 300,
    "wait_recv_patch_ms": 30000,
    "debug_packets": True,
    # False = call the game's patched recv IAT directly.
    # True  = drive so3dplus.exe's Net__RawRecv with a minimal fake CGameClient
    #         object, which gives 123.dll the same upper callsite shape as the
    #         real client recv path.
    "game_raw_recv": True,
    "native_connect": bool(int(os.environ.get("GGTB_BRIDGE_NATIVE_CONNECT", "0") or "0")),
}


_ids = count(1)


def pipe_path(pid: int) -> str:
    return rf"\\.\pipe\{CONFIG['pipe_prefix']}{pid}"


def discover_bridge_pid() -> int:
    prefix = str(CONFIG["pipe_prefix"])
    try:
        names = os.listdir(r"\\.\pipe\\")
    except OSError as exc:
        raise RuntimeError(f"cannot list named pipes: {exc}") from exc

    pids: list[int] = []
    for name in names:
        if not name.startswith(prefix):
            continue
        tail = name[len(prefix):]
        if tail.isdigit():
            pids.append(int(tail))

    if not pids:
        raise RuntimeError(f"no {prefix} pipe found; inject/restart GGThreadBlock.dll first")
    pids = sorted(set(pids))
    if len(pids) > 1:
        print(f"[bridge] found multiple bridge pipes: {', '.join(map(str, pids))}; using {pids[0]}")
    return pids[0]


def bytes_from_hex(text: str) -> bytes:
    return bytes.fromhex(text.replace("-", " ").replace(":", " ").replace(",", " "))


def print_chunks(label: str, chunks: list[dict[str, Any]], key_index: int | None = None) -> None:
    if not chunks:
        print(f"[{label}] no chunks")
        return
    for index, chunk in enumerate(chunks, 1):
        raw = bytes_from_hex(str(chunk.get("hex", "")))
        via = chunk.get("via")
        via_text = f" via={via}" if via else ""
        print(f"[{label}] chunk#{index} len={len(raw)}{via_text} raw={short_hex(raw)}")
        if key_index is not None and len(raw) >= 8:
            decoded = xor_payload(raw, key_index)
            proto = u32(decoded, 4)
            print(f"[{label}] chunk#{index} xor-key={key_index} proto={proto_name(proto)} dec={short_hex(decoded)}")
            debug_game_recv(f"{label}#{index}", raw, decoded, key_index=key_index, enabled=bool(CONFIG["debug_packets"]))


@dataclass
class BridgeClient:
    pid: int

    def __post_init__(self) -> None:
        self.path = pipe_path(self.pid)
        print(f"[bridge] connecting {self.path}")
        self.pipe = open(self.path, "r+b", buffering=0)
        hello = self._read_json()
        print(f"[bridge] hello {hello}")

    def close(self) -> None:
        try:
            self.pipe.close()
        except Exception:
            pass

    def request(self, cmd: str, **payload: Any) -> dict[str, Any]:
        request_id = str(next(_ids))
        frame = {"id": request_id, "cmd": cmd, **payload}
        raw = (json.dumps(frame, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        self.pipe.write(raw)
        while True:
            response = self._read_json()
            if response.get("type") == "response" and response.get("id") == request_id:
                return response
            print(f"[bridge] async {response}")

    def _read_json(self) -> dict[str, Any]:
        line = self.pipe.readline()
        if not line:
            raise ConnectionError("bridge pipe closed")
        data = json.loads(line.decode("utf-8"))
        if not isinstance(data, dict):
            raise ValueError("bridge response is not a JSON object")
        return data


def require_ok(label: str, response: dict[str, Any]) -> dict[str, Any]:
    if not response.get("ok"):
        raise RuntimeError(f"{label} failed: {json.dumps(response, ensure_ascii=False)}")
    return response


def main() -> None:
    pid = int(CONFIG.get("pid") or 0)
    if pid <= 0:
        pid = discover_bridge_pid()

    bridge = BridgeClient(pid)
    session_id: int | None = None
    try:
        snapshot = require_ok("apiSnapshot", bridge.request("apiSnapshot"))
        print(f"[bridge] api {json.dumps(snapshot.get('api'), ensure_ascii=False)}")
        waited = bridge.request("waitRecvPatch", timeoutMs=int(CONFIG["wait_recv_patch_ms"]))
        print(f"[bridge] recvPatch {json.dumps(waited.get('recvPatch'), ensure_ascii=False)}")
        require_ok("waitRecvPatch", waited)

        opened = require_ok(
            "open",
            bridge.request(
                "open",
                host=CONFIG["host"],
                port=int(CONFIG["port"]),
                readTimeoutMs=int(CONFIG["read_timeout_ms"]),
                quietMs=int(CONFIG["quiet_ms"]),
                gameRawRecv=bool(CONFIG["game_raw_recv"]),
                nativeConnect=bool(CONFIG["native_connect"]),
                waitRecvPatchMs=int(CONFIG["wait_recv_patch_ms"]),
            ),
        )
        session_id = int(opened["sessionId"])
        print(f"[bridge] session {session_id} opened")

        open_chunks = opened.get("read", {}).get("chunks", [])
        print_chunks("open", open_chunks)
        if not open_chunks:
            raise RuntimeError("login server did not send the initial handshake through bridge")

        handshake_raw = bytes_from_hex(open_chunks[0]["hex"])
        key_index, handshake_dec = handshake_xor_index(handshake_raw)
        print(f"[login] handshake key={key_index} dec={short_hex(handshake_dec)}")

        packet = login_packet(
            str(CONFIG["account"]),
            str(CONFIG["password"]),
            str(CONFIG["encoding"]),
            key_index,
        )
        debug_game_send("bridge.CL_LOGIN", packet, xor=True, key_index=key_index, enabled=bool(CONFIG["debug_packets"]))

        login_resp = require_ok(
            "sendRecv CL_LOGIN",
            bridge.request(
                "sendRecv",
                sessionId=session_id,
                hex=packet.hex(" "),
                readTimeoutMs=int(CONFIG["read_timeout_ms"]),
                quietMs=int(CONFIG["quiet_ms"]),
                maxChunks=32,
                maxBytes=262144,
                gameRawRecv=bool(CONFIG["game_raw_recv"]),
            ),
        )
        print(f"[login] sent={login_resp.get('sent')} read={login_resp.get('read')}")
        print_chunks("login", login_resp.get("read", {}).get("chunks", []), key_index=key_index)
    finally:
        if session_id is not None:
            try:
                bridge.request("close", sessionId=session_id)
            except Exception as exc:
                print(f"[bridge] close failed: {exc}")
        bridge.close()
        time.sleep(0.05)


if __name__ == "__main__":
    main()
