import importlib.util, struct, sys
from pathlib import Path
TOOLS = Path(r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools")
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("so3dlogin", TOOLS / "so3d_online_login_client.py")
m = importlib.util.module_from_spec(spec); sys.modules["so3dlogin"] = m; spec.loader.exec_module(m)

def hx(s): return bytes.fromhex(s.replace(" ", ""))
def ap(b): return "".join(chr(c) if 32 <= c < 127 else "." for c in b)

# 10002 sends from this login capture
pkts = {
 "10002 #1 len=60 (proto=639931835)": hx("3C 00 00 00 BB 95 24 26 23 24 25 26 DC C7 60 AB 10 08 1F 7F EE 7F 9F 3E B9 96 C6 A5 23 62 D5 23 97 3F 10 5F B5 D9 5F BA EA 4D 83 C4 38 11 9B 91 6C A9 01 1F 33 7B 17 A3 01 A3 F7 08"),
 "10002 #2 len=40 (proto=639931776)": hx("28 00 00 00 80 95 24 26 44 4B 4B 41 5A 51 1C 16 12 15 17 17 10 24 25 26 1A 14 14 17 11 15 25 26 23 24 25 26 23 24 25 26"),
 "10002 #3 len=60 (proto=707237052)": hx("3C 00 00 00 BC 94 27 2A 24 25 26 2A DB C6 63 A7 17 09 1C 73 E9 7E 9C 32 BE 97 C5 A9 24 63 D6 2F 90 3E 13 53 B2 D8 5C B6 ED 4C 80 C8 3F 10 98 9D 6B A8 02 13 34 7A 14 AF 06 A2 F4 04"),
 "10002 #4 len=16 (proto=707237055)": hx("10 00 00 00 BF 94 27 2A 26 25 26 2A 25 25 26 2A"),
 "10002 #5 len=32 (proto=707237050)": hx("20 00 00 00 BA 94 27 2A 26 25 26 2A 25 25 26 2A 57 4D 47 4E 4B 52 42 4B 4A 46 43 2A 24 25 26 2A"),
}

for label, raw in pkts.items():
    # find XOR key that makes body[0:4] (the u32(0) marker) zero, else try all
    best = None
    for ki in range(len(m.XOR_KEYS)):
        dec = m.xor_payload(raw, ki)
        proto = struct.unpack_from("<I", dec, 4)[0]
        if proto in m.PROTO_NAMES:
            best = (ki, dec, proto); break
    if best is None:
        # fall back: key index 1 (23 24 25 26) which we hand-verified
        ki = 1; dec = m.xor_payload(raw, ki); proto = struct.unpack_from("<I", dec, 4)[0]
    else:
        ki, dec, proto = best
    print(f"\n### {label}")
    print(f"  xor key={ki} decoded proto={m.proto_name(proto)} (0x{proto:08X})")
    print(f"  dec body = {dec[8:].hex(' ')}")
    print(f"  ascii    = {ap(dec[8:])}")
    # If body looks like u32(0)+DES blocks, DES-decrypt them
    body = dec[8:]
    if len(body) >= 4 and struct.unpack_from("<I", body, 0)[0] == 0 and (len(body) - 4) % 16 == 0 and len(body) > 4:
        enc = body[4:]
        print(f"  -> looks like login_packet: u32(0) + {len(enc)//16} x 16B blocks; DES-decrypting:")
        for i in range(0, len(enc), 16):
            block = enc[i:i+16]
            ddec = m.game_des_decrypt(block)
            print(f"     block{i//16}: {ddec.hex(' ')}  ascii={ap(ddec)}")
