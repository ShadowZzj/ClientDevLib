import struct

def hx(s): return bytes.fromhex(s.replace(" ", ""))

# 10002 recv: WIRE (on-wire) vs RECV (logical/plaintext), both len=48
wire = hx("30 00 00 00 2F 6F BA BE 24 64 7A 7E DD 9D 6A 66 EA AA AA AE E3 4C 4C 64 00 87 BC 58 DE 59 68 B8 EE AE FC 6E E7 A7 FA FE EC AC FA F6 D5 D2 90 44")
log  = hx("30 00 00 00 7E A3 25 2A 26 25 26 2A 24 25 26 2A C4 5E 26 2A 57 4D 47 4E 4B 52 42 4B 4A 46 43 2A 24 25 26 2A 26 25 26 2A 24 25 26 2A 24 25 26 2A")

print("offset wire log  xor")
for i in range(len(wire)):
    x = wire[i] ^ log[i]
    print(f"{i:3d}    {wire[i]:02X}   {log[i]:02X}  {x:02X}")

print("\nXOR diff bytes (from offset 4):")
diff = bytes(wire[i] ^ log[i] for i in range(4, len(wire)))
print(diff.hex(' '))
# Check periodicity
for period in (1,2,3,4,5,6,7,8):
    ok = all(diff[i] == diff[i % period] for i in range(len(diff)))
    if ok:
        print(f"  -> XOR diff is periodic with period {period}: {diff[:period].hex(' ')}")
