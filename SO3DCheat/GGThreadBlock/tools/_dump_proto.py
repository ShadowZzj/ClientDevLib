"""把指定 proto 的帧整段按 16 字节/行 dump,并标出非空条目;同时全局搜索某 itemId。"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
blob = (HERE / "bag_capture.bin").read_bytes()


def frames(data):
    off, n = 0, len(data)
    while off + 8 <= n:
        size = struct.unpack_from("<I", data, off)[0]
        if 8 <= size <= n - off:
            yield off, data[off:off + size]
            off += size
        else:
            off += 1


target = int(sys.argv[1])
stride = int(sys.argv[2]) if len(sys.argv) > 2 else 16
for off, fr in frames(blob):
    proto = struct.unpack_from("<I", fr, 4)[0]
    if proto != target:
        continue
    body = fr[8:]
    print(f"--- proto {proto} len={len(fr)} body={len(body)} stride={stride} entries={len(body)//stride} ---")
    for i in range(0, len(body) - stride + 1, stride):
        words = struct.unpack_from("<" + "I" * (stride // 4), body, i)
        slot = i // stride
        nonzero = any(words)
        mark = "  " if nonzero else ".."
        if nonzero:
            print(f"{mark} slot{slot:<3} @body+{i:<4} {words}")
    print()
    break

# 全局搜索 8036 / 自定义 id
ids = [int(x) for x in sys.argv[3:]] or [8036]
for wid in ids:
    pat = struct.pack("<I", wid)
    hits = []
    start = 0
    while True:
        j = blob.find(pat, start)
        if j < 0:
            break
        hits.append(j)
        start = j + 1
    print(f"value {wid} (0x{wid:X}) found at offsets: {hits}")
