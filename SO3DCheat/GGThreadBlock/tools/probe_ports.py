#!/usr/bin/env python3
"""Probe captured 9999/10002 packets against every decryption scheme the
so3d_online_login_client module knows about (XOR keys, inner DES, Blowfish-CBC),
to see which port maps to which scheme."""
import importlib.util
import struct
import sys
from pathlib import Path

TOOLS = Path(r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools")
sys.path.insert(0, str(TOOLS))

spec = importlib.util.spec_from_file_location("so3dlogin", TOOLS / "so3d_online_login_client.py")
m = importlib.util.module_from_spec(spec)
sys.modules["so3dlogin"] = m  # dataclass needs the module registered before exec
# Avoid running the GUI/main; the module guards with __main__ so import is safe.
spec.loader.exec_module(m)

import protect_blowfish as pb


def hx(s):
    return bytes.fromhex(s.replace(" ", ""))


def u32(b, off=0):
    return struct.unpack_from("<I", b, off)[0]


def ascii_preview(b):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)


# --- captured packets from net/sendlog + recvlog ---
PKTS = {
    "9999 SEND len=79": hx(
        "E8 03 00 00 47 00 00 00 B9 12 2D 2E CB 68 C1 8E D8 F6 22 B2 DD AE B7 2F "
        "E7 E7 77 FA 4E 30 5E 2E FE 92 AB 4E C3 15 AD 37 2D 3A 9E 62 96 12 03 C9 "
        "4E 1E E6 76 01 40 76 05 EE 9C 7B 9B 7E 4B 90 E7 F5 80 DE 00 8E 05 BE 29 "
        "39 BC 16 BF 2C 32 5B"),
    "9999 RECV len=51": hx(
        "01 02 04 02 05 04 33 00 00 00 54 E6 56 1E 4B A8 A4 A8 A6 BB A0 16 90 AB "
        "72 4E 29 1E 36 CA 0A AD 36 CA C6 8C 23 2F 43 8B FA 76 80 C0 25 55 5C 24 "
        "3F A3 4D"),
    "10002 RECV len=48 (have LOG)": hx(
        "30 00 00 00 2F 6F BA BE 24 64 7A 7E DD 9D 6A 66 EA AA AA AE E3 4C 4C 64 "
        "00 87 BC 58 DE 59 68 B8 EE AE FC 6E E7 A7 FA FE EC AC FA F6 D5 D2 90 44"),
}
# Known LOG (plaintext) for the 10002 recv len=48, to confirm the XOR result.
LOG_10002_48 = hx(
    "30 00 00 00 7E A3 25 2A 26 25 26 2A 24 25 26 2A C4 5E 26 2A 57 4D 47 4E 4B "
    "52 42 4B 4A 46 43 2A 24 25 26 2A 26 25 26 2A 24 25 26 2A 24 25 26 2A")


def try_xor(raw):
    hits = []
    for i in range(len(m.XOR_KEYS)):
        dec = m.xor_payload(raw, i)
        if len(dec) >= 8:
            proto = u32(dec, 4)
            name = m.proto_name(proto)
            known = proto in m.PROTO_NAMES
            hits.append((i, proto, name, known, dec))
    return hits


print("=" * 70)
for label, raw in PKTS.items():
    print(f"\n### {label}  (len={len(raw)})")
    print(f"  raw   = {raw[:32].hex(' ')}{' ...' if len(raw)>32 else ''}")
    total = u32(raw, 0) if len(raw) >= 4 else 0
    print(f"  [first u32] = {total} (0x{total:08X})")

    # 1) XOR layer (all keys)
    print("  -- XOR layer --")
    any_known = False
    for i, proto, name, known, dec in try_xor(raw):
        if known:
            any_known = True
            print(f"     key={i}: proto={name} KNOWN  dec={dec[:24].hex(' ')}  ascii={ascii_preview(dec[8:40])}")
    if not any_known:
        print("     (no XOR key yields a known SO3D proto)")

    # 2) Inner DES on the body after a 4-byte header (login_packet style)
    print("  -- inner DES (game_des_decrypt on body[8:]) --")
    if len(raw) >= 16:
        try:
            ddec = m.game_des_decrypt(raw[8:])
            print(f"     des(body) = {ddec[:32].hex(' ')}  ascii={ascii_preview(ddec[:40])}")
        except Exception as e:
            print(f"     DES failed: {e}")

    # 3) Blowfish-CBC unseal (treat as a protect frame)
    print("  -- Blowfish-CBC unseal --")
    try:
        sealed = raw[8:total] if 8 < total <= len(raw) else raw[8:]
        if len(sealed) % 8 == 0 and sealed:
            tick, crc, body = pb.unseal_body(sealed)
            crc_ok = (crc == pb.crc32(body))
            print(f"     tick=0x{tick:08X} crc=0x{crc:08X} crc_ok={crc_ok}")
            print(f"     body  = {body[:32].hex(' ')}  ascii={ascii_preview(body[:40])}")
        else:
            print(f"     sealed len {len(sealed)} not multiple of 8 -> not Blowfish frame")
    except Exception as e:
        print(f"     Blowfish unseal failed: {e}")

# Confirm the 10002 XOR result actually equals the known LOG.
print("\n" + "=" * 70)
print("### CONFIRM 10002 recv len=48: XOR-decrypt == captured LOG?")
raw48 = PKTS["10002 RECV len=48 (have LOG)"]
for i in range(len(m.XOR_KEYS)):
    dec = m.xor_payload(raw48, i)
    if dec == LOG_10002_48:
        print(f"  MATCH at xor key index={i}  proto={m.proto_name(u32(dec,4))}")
        break
else:
    print("  no XOR key reproduced the LOG exactly; closest:")
    for i in range(len(m.XOR_KEYS)):
        dec = m.xor_payload(raw48, i)
        nmatch = sum(a == b for a, b in zip(dec, LOG_10002_48))
        print(f"    key={i}: {nmatch}/{len(LOG_10002_48)} bytes match, proto={m.proto_name(u32(dec,4))}")
