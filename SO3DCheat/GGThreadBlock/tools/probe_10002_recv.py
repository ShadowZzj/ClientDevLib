import importlib.util, struct, sys
from pathlib import Path
TOOLS = Path(r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools")
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("so3dlogin", TOOLS / "so3d_online_login_client.py")
m = importlib.util.module_from_spec(spec); sys.modules["so3dlogin"] = m; spec.loader.exec_module(m)
import protect_blowfish as pb

def hx(s): return bytes.fromhex(s.replace(" ", ""))
def ap(b): return "".join(chr(c) if 32 <= c < 127 else "." for c in b)
def u32(b,o=0): return struct.unpack_from("<I",b,o)[0]

# 17:50 session: a 10002 recv pair that has BOTH layers logged.
WIRE = hx("30 00 00 00 2F 6F BA BE 24 64 7A 7E DD 9D 6A 66 EA AA AA AE E3 4C 4C 64 00 87 BC 58 DE 59 68 B8 EE AE FC 6E E7 A7 FA FE EC AC FA F6 D5 D2 90 44")
RECV = hx("30 00 00 00 7E A3 25 2A 26 25 26 2A 24 25 26 2A C4 5E 26 2A 57 4D 47 4E 4B 52 42 4B 4A 46 43 2A 24 25 26 2A 26 25 26 2A 24 25 26 2A 24 25 26 2A")

print("== (1) Does py xor_payload decode the GAME-BUFFER packet [RECV]? ==")
for ki in range(len(m.XOR_KEYS)):
    dec = m.xor_payload(RECV, ki)
    proto = u32(dec, 4)
    if proto in m.PROTO_NAMES:
        print(f"  RECV xor key={ki}: proto={m.proto_name(proto)} dec={dec.hex(' ')}")
        print(f"                     ascii={ap(dec[8:])}")

print("\n== (2) Does py xor_payload decode the ON-WIRE packet [WIRE_RECV]? ==")
hit = False
for ki in range(len(m.XOR_KEYS)):
    dec = m.xor_payload(WIRE, ki)
    proto = u32(dec, 4)
    if proto in m.PROTO_NAMES:
        print(f"  WIRE xor key={ki}: proto={m.proto_name(proto)} KNOWN")
        hit = True
if not hit:
    print("  no XOR key yields a known proto from WIRE_RECV -> wire has an extra layer")

print("\n== (3) What transform takes WIRE_RECV -> RECV? header (4B len) is plaintext ==")
print(f"  header same? {WIRE[:4]==RECV[:4]}  (len field {u32(WIRE)} == {u32(RECV)})")
# Try raw Blowfish-CBC (IV=0) decrypt of body, game-DES style (full 8B blocks, leftover appended)
def bf_cbc_decrypt_partial(data):
    ecb = pb._ecb(); out=bytearray(); prev=b"\x00"*8
    full=(len(data)//8)*8
    for off in range(0,full,8):
        ct=bytes(data[off:off+8]); ptb=pb._decrypt_block(ecb,ct)
        ptb=bytes(a^b for a,b in zip(ptb,prev)); prev=ct; out+=ptb
    out+=data[full:]
    return bytes(out)
for hdr in (4, 0):
    body = WIRE[hdr:]
    dec = bf_cbc_decrypt_partial(body)
    cand = WIRE[:hdr] + dec
    print(f"  BF-CBC(partial) over WIRE[{hdr}:] -> {cand.hex(' ')}")
    print(f"     matches RECV? {cand==RECV}")
# Try game DES decrypt over body
for hdr in (4, 0):
    body = WIRE[hdr:]
    dec = m.game_des_decrypt(body)
    cand = WIRE[:hdr] + dec
    print(f"  DES(partial) over WIRE[{hdr}:] matches RECV? {cand==RECV}")

print("\n== (4) byte-by-byte WIRE^RECV (look for keystream structure) ==")
x = bytes(a^b for a,b in zip(WIRE,RECV))
print("  xor:", x.hex(' '))
