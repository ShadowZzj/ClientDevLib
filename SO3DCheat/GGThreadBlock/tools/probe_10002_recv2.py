import importlib.util, struct, sys
from pathlib import Path
TOOLS = Path(r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools")
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("so3dlogin", TOOLS / "so3d_online_login_client.py")
m = importlib.util.module_from_spec(spec); sys.modules["so3dlogin"] = m; spec.loader.exec_module(m)
import protect_blowfish as pb

def hx(s): return bytes.fromhex(s.replace(" ", ""))

WIRE = hx("30 00 00 00 2F 6F BA BE 24 64 7A 7E DD 9D 6A 66 EA AA AA AE E3 4C 4C 64 00 87 BC 58 DE 59 68 B8 EE AE FC 6E E7 A7 FA FE EC AC FA F6 D5 D2 90 44")
RECV = hx("30 00 00 00 7E A3 25 2A 26 25 26 2A 24 25 26 2A C4 5E 26 2A 57 4D 47 4E 4B 52 42 4B 4A 46 43 2A 24 25 26 2A 26 25 26 2A 24 25 26 2A 24 25 26 2A")

def part(fn, data):
    out = bytearray(); full=(len(data)//8)*8
    for off in range(0, full, 8):
        out += fn(bytes(data[off:off+8]))
    out += data[full:]
    return bytes(out)

ecb = pb._ecb()
print("target RECV[4:] =", RECV[4:].hex(' '))
for hdr in (4, 8):
    body = WIRE[hdr:]
    cands = {
        "BF-ECB.decrypt": part(lambda b: ecb.decrypt(b), body),
        "BF-ECB.encrypt": part(lambda b: ecb.encrypt(b), body),
    }
    for name, dec in cands.items():
        cand = WIRE[:hdr] + dec
        print(f"  [{name}] WIRE[{hdr}:] -> {cand.hex(' ')}  match={cand==RECV}")

# Treat WIRE as a full protect frame and unseal (same Blowfish-CBC as 1842)
print("\n-- unseal_packet(WIRE) as 1842-style protect frame --")
try:
    total, proto, tick, crc, b = pb.unseal_packet(WIRE)
    print(f"  total={total} proto=0x{proto:08X} tick=0x{tick:08X} crc=0x{crc:08X} crc_ok={crc==pb.crc32(b)}")
    print(f"  body={b.hex(' ')}")
except Exception as e:
    print("  unseal failed:", e)

# Is RECV a protect-seal of some plaintext? unseal RECV itself
print("\n-- unseal_packet(RECV) (sanity: RECV is plaintext XOR-layer, expect junk) --")
try:
    total, proto, tick, crc, b = pb.unseal_packet(RECV)
    print(f"  crc_ok={crc==pb.crc32(b)}")
except Exception as e:
    print("  unseal failed:", e)
