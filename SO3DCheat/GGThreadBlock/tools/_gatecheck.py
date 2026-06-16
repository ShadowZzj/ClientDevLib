import json, os, sys
from itertools import count

PID = os.environ.get("GGTB_BRIDGE_PID", "55172")
path = rf"\\.\pipe\GGTB_LOGIN_BRIDGE_{PID}"
_ids = count(1)
pipe = open(path, "r+b", buffering=0)
print("hello", json.loads(pipe.readline()))

def req(cmd, **kw):
    rid = str(next(_ids))
    pipe.write((json.dumps({"id": rid, "cmd": cmd, **kw}) + "\n").encode())
    while True:
        r = json.loads(pipe.readline())
        if r.get("id") == rid:
            return r

ping = req("ping")
rp = ping.get("recvPatch", {})
print("recvPatch:", json.dumps(rp, indent=1))
# moduleBase + gate RVAs
mb = int(rp.get("moduleBase", "0x0"), 16)
target = int(rp.get("target", "0x0"), 16)
print(f"moduleBase=0x{mb:08X} target=0x{target:08X} target-base=0x{target-mb:X}")
print(f"gateA live=0x{mb+0x756049:08X}  gateB live=0x{mb+0x7560C3:08X}")
