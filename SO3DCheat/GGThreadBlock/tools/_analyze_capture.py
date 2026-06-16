"""离线分析 bag_capture.bin:把整段 TCP 字节流按帧重组,统计 proto 直方图,
并把疑似背包/物品包(body 里含小整数 itemId/count 模式)打印出来供人工核对。"""
import collections
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BIN = HERE / "bag_capture.bin"
blob = BIN.read_bytes()
print(f"total {len(blob)} bytes")


def frames(data: bytes):
    off = 0
    n = len(data)
    while off + 8 <= n:
        size = struct.unpack_from("<I", data, off)[0]
        if 8 <= size <= n - off:
            yield off, data[off:off + size]
            off += size
        else:
            off += 1


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


hist = collections.Counter()
by_proto = collections.defaultdict(list)
for off, fr in frames(blob):
    proto = u32(fr, 4)
    hist[proto] += 1
    if len(by_proto[proto]) < 4:
        by_proto[proto].append(fr)

print(f"\n{len(hist)} distinct protos:")
for proto, c in sorted(hist.items(), key=lambda kv: -kv[1]):
    print(f"  proto={proto:<8} count={c:<4} len(samples)={[len(x) for x in by_proto[proto]]}")

want = sys.argv[1:]
if want:
    for w in want:
        wp = int(w)
        print(f"\n==== proto {wp} samples ====")
        for fr in by_proto.get(wp, []):
            hexs = " ".join(f"{x:02x}" for x in fr[:80])
            words = [u32(fr, i) for i in range(0, min(len(fr) // 4 * 4, 80), 4)]
            print(f"len={len(fr)}")
            print(f"  hex={hexs}")
            print(f"  u32={words}")
