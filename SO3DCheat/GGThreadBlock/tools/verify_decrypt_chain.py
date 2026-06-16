#!/usr/bin/env python3
"""End-to-end layer-1 verification of the 123.dll decrypt via GGThreadBlock bridge.

Flow:
  1. Connect the GGTB LoginBridge named pipe (game process hosts it).
  2. For each captured WIRE_RECV ciphertext (port 10002, len>0xC), send a
     `decryptWire` command. The bridge feeds it through 123.dll+0x50A9A
     (obf_recv_dispatch) over a loopback socket and returns the in-place
     decrypted buffer = layer-1 plaintext (== what the game's [RECV] hook saw).
  3. Apply layer-2 xor (xor_payload, key_index 7) on top.
  4. Compare to the captured [RECV] plaintext and to the fully-decoded protocol.

If bridge-layer1 == captured [RECV], the whole 123.dll call chain is proven usable.
"""
from __future__ import annotations
import json, os, sys
from itertools import count

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so3d_online_login_client import XOR_KEYS, xor_payload, proto_name, u32  # noqa

PIPE_PREFIX = os.environ.get("GGTB_BRIDGE_PREFIX", "GGTB_LOGIN_BRIDGE_")
KEY_INDEX = int(os.environ.get("GGTB_XOR_KEY", "7"))

# (label, wire ciphertext hex, expected [RECV] layer-1 plaintext hex)
# Captured from G:\GuGuSealKarel1120\GGConfig\_bootstrap\net\recvlog, port 10002.
CASES = [
    ("len58",
     "3A 00 00 00 DA DE 39 79 CA CE 26 66 AA AE 2F 6F BA BE 24 64 7A 7E DD 9D 6A 6E EA AA A6 67 E3 A3 BA B2 F0 B0 1B 1F E9 A9 0B 0F EE 02 14 D4 4B 48 CC 33 EC AC FA F2 D5 D2 6C 83",
     "3A 00 00 00 68 1F 20 24 20 40 23 24 10 33 46 56 57 25 51 24 01 60 03 04 01 60 03 04 20 40 23 24 13 41 23 24 21 40 23 24 21 40 23 24 21 40 23 24 21 40 23 24 21 40 23 24 21 40"),
    ("len60",
     "3C 00 00 00 20 60 DA D2 39 79 CA CE 22 A8 B8 71 2F 6F DE 73 3C 7C FB FF C5 9D A7 5F 77 79 73 63 7E F1 73 73 E8 A8 9A 9E F1 5E 7C 54 06 81 CC 28 C8 4F 18 C8 EC AC FA F6 D5 D2 6C A3",
     "3C 00 00 00 70 1F 20 24 23 40 23 24 52 28 42 40 4E 37 47 45 4F 23 46 24 21 40 23 24 10 72 14 0A 10 6E 16 0A 15 71 23 04 01 60 03 04 10 47 23 24 8C 3C CA 34 21 40 23 24 20 40 23 24"),
]

_ids = count(1)

def discover_pid() -> int:
    if os.environ.get("GGTB_BRIDGE_PID"):
        return int(os.environ["GGTB_BRIDGE_PID"])
    names = os.listdir(r"\\.\pipe\\")
    pids = sorted({int(n[len(PIPE_PREFIX):]) for n in names
                   if n.startswith(PIPE_PREFIX) and n[len(PIPE_PREFIX):].isdigit()})
    if not pids:
        raise RuntimeError(f"no {PIPE_PREFIX} pipe; inject GGThreadBlock first")
    if len(pids) > 1:
        print(f"[bridge] multiple pids {pids}, using {pids[0]}")
    return pids[0]

class Bridge:
    def __init__(self, pid: int):
        self.path = rf"\\.\pipe\{PIPE_PREFIX}{pid}"
        print(f"[bridge] connecting {self.path}")
        self.pipe = open(self.path, "r+b", buffering=0)
        print(f"[bridge] hello {self._read()}")
    def _read(self):
        line = self.pipe.readline()
        if not line:
            raise ConnectionError("bridge pipe closed")
        return json.loads(line.decode("utf-8"))
    def request(self, cmd: str, **payload):
        rid = str(next(_ids))
        self.pipe.write((json.dumps({"id": rid, "cmd": cmd, **payload}) + "\n").encode())
        while True:
            r = self._read()
            if r.get("type") == "response" and r.get("id") == rid:
                return r
            print(f"[bridge] async {r}")
    def close(self):
        try: self.pipe.close()
        except Exception: pass

def hx(s: str) -> bytes:
    return bytes.fromhex(s.replace("-", " ").replace(":", " ").replace(",", " "))

def main():
    bridge = Bridge(discover_pid())
    try:
        print(f"[bridge] ping {bridge.request('ping').get('recvPatch', {}).get('ready')}")
        passed = total = 0
        for label, wire_hex, expect_hex in CASES:
            total += 1
            wire = hx(wire_hex)
            expect = hx(expect_hex)
            resp = bridge.request("decryptWire", hex=wire_hex, forceDecrypt=True)
            if not resp.get("ok"):
                print(f"[{label}] FAIL bridge error: {resp}")
                continue
            layer1 = hx(resp["plainHex"])
            print(f"[{label}] wireLen={len(wire)} ret={resp['ret']} changed={resp['changed']} "
                  f"force={resp.get('forceDecrypt')} gates={resp.get('gatesApplied')}")
            print(f"[{label}] bridge layer1 : {layer1.hex(' ')}")
            print(f"[{label}] expected RECV : {expect.hex(' ')}")
            match1 = layer1 == expect
            print(f"[{label}] layer1 == [RECV]: {'YES' if match1 else 'NO'}")
            # layer 2
            if len(layer1) >= 8:
                final = xor_payload(layer1, KEY_INDEX)
                proto = u32(final, 4)
                ascii_preview = ''.join(chr(b) if 32 <= b < 127 else '.' for b in final[8:48])
                print(f"[{label}] layer2 proto={proto_name(proto)} (0x{proto:08X}) ascii='{ascii_preview}'")
            if match1:
                passed += 1
        print(f"\n==== {passed}/{total} layer-1 matches; "
              f"{'CHAIN PROVEN' if passed == total else 'see mismatches above'} ====")
    finally:
        bridge.close()

if __name__ == "__main__":
    main()
