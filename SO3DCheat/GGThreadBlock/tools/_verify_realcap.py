"""用真实抓包 bag_capture.bin 跑客户端的新解析器(IDA 核对后的 511591/511324 布局),
打印解析出的主背包 / cash 背包内容,核对 itemId 落点与槽位编码正确。"""
import importlib.util
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLIENT = HERE / "so3d_online_login_client.py"
spec = importlib.util.spec_from_file_location("sologin", CLIENT)
m = importlib.util.module_from_spec(spec)
sys.modules["sologin"] = m
spec.loader.exec_module(m)

blob = (HERE / "bag_capture.bin").read_bytes()
buf = bytearray(blob)
framed = m.take_complete_frames(buf)
print(f"capture={len(blob)}B framed={len(framed)}B leftover={len(buf)}B")

# 帧级统计:确认 511591 / 511324 各出现一次,长度符合 IDA 推算
counts = {}
off, n = 0, len(framed)
while off + 8 <= n:
    size = struct.unpack_from("<I", framed, off)[0]
    proto = struct.unpack_from("<I", framed, off + 4)[0]
    if proto in (m.SC_BAG_BULK, m.SC_CASH_BAG_BULK):
        counts.setdefault(proto, []).append(size)
    off += size
print("bulk frames:", {p: v for p, v in counts.items()})

class _Args:
    fishing_bait_item_id = 0
args = _Args()
state = m.FarmState.__new__(m.FarmState)
state.args = args
state.bag = {}
state.cash_bag = {}
state.bag_observed = state.cash_observed = state.bait_observed = False
state.coord_x = state.coord_y = 0
state.coord_seen = False
state.catch_total = 0

m.update_farm_state(state, framed)

print(f"\n主背包 occupied={len(state.bag)} 格 (内部 slot -> wire slot+13)")
for s in sorted(state.bag):
    iid, cnt = state.bag[s]
    print(f"  slot{s:<3} (wire {s + m.WIRE_SLOT_BASE:<3}) itemId={iid:<8} count={cnt}")
print(f"\ncash 背包 occupied={len(state.cash_bag)} 格")
for s in sorted(state.cash_bag):
    iid, cnt = state.cash_bag[s]
    print(f"  slot{s:<3} (wire {s + m.WIRE_SLOT_BASE:<3}) itemId={iid:<8} count={cnt}")
print(f"\n召唤卷(8036)所在 cash 内部 slot = {m.find_summon_slot(state)}")
