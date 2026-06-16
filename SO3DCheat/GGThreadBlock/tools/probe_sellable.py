import ctypes as C
from ctypes import wintypes as W
import sys

PROC = "SO3DPlus.exe"
G_TABLE_MANAGER = 0x018B6560
ITEM_VEC_OFF = 0x24

ENTRY_ITEMID_OFF = 0x04
ENTRY_NAME_OFF   = 0x0C
ENTRY_TYPE_OFF   = 0x110   # 272
ENTRY_F134_OFF   = 0x134   # 308 (lo16) / 0x138 hi16
ENTRY_PRICE_A    = 0x3F4   # 1012
ENTRY_PRICE_B    = 0x3F8   # 1016
ENTRY_F158_OFF   = 0x158   # 344 (sub_798640 arg in sub_82C660)
ENTRY_F448_OFF   = 0x448   # 1096 (==2 in Item_GetSellQuantity)
NAME_MAX = 0x104

k32 = C.WinDLL("kernel32", use_last_error=True)
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x2


class PROCESSENTRY32(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                ("th32DefaultHeapID", C.c_void_p), ("th32ModuleID", W.DWORD),
                ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                ("pcPriClassBase", C.c_long), ("dwFlags", W.DWORD),
                ("szExeFile", C.c_char * 260)]


def find_pids(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    e = PROCESSENTRY32(); e.dwSize = C.sizeof(e)
    pids = []
    if k32.Process32First(snap, C.byref(e)):
        while True:
            if e.szExeFile.decode(errors="ignore").lower() == name.lower():
                pids.append(e.th32ProcessID)
            if not k32.Process32Next(snap, C.byref(e)):
                break
    k32.CloseHandle(snap)
    return pids


def main():
    pids = find_pids(PROC)
    if not pids:
        print(f"process {PROC} not found"); return 1
    pid = pids[0]
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print("OpenProcess failed", C.get_last_error()); return 1
    print(f"pid={pid} (all pids: {pids})")

    def rd(addr, size):
        buf = (C.c_ubyte * size)(); n = C.c_size_t(0)
        ok = k32.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(n))
        return bytes(buf[:n.value]) if ok else None

    def rdu32(addr):
        b = rd(addr, 4)
        return int.from_bytes(b, "little") if b and len(b) == 4 else None

    def rdi32(addr):
        v = rdu32(addr)
        return v - 0x100000000 if v is not None and v >= 0x80000000 else v

    def rd_big5(addr, maxlen=NAME_MAX):
        b = rd(addr, maxlen)
        if not b: return None
        nul = b.find(b"\x00")
        if nul >= 0: b = b[:nul]
        try: return b.decode("cp950")
        except Exception: return b.decode("big5", errors="replace")

    mgr = rdu32(G_TABLE_MANAGER)
    begin = rdu32(mgr + ITEM_VEC_OFF)
    end = rdu32(mgr + ITEM_VEC_OFF + 4)
    count = (end - begin) // 4
    print(f"mgr=0x{mgr:08X} vec=[0x{begin:08X},0x{end:08X}] count={count}")

    ptr_blob = rd(begin, count * 4)
    id2entry = {}
    type_hist = {}
    for i in range(count):
        entry = int.from_bytes(ptr_blob[i*4:i*4+4], "little")
        if not entry or entry < 0x10000:
            continue
        iid = rdu32(entry + ENTRY_ITEMID_OFF)
        if iid is None:
            continue
        id2entry[iid] = entry
        t = rdu32(entry + ENTRY_TYPE_OFF)
        type_hist[t] = type_hist.get(t, 0) + 1

    ENTRY_CAPMASK = 0x45C  # 1116: item capability bitmask (sub_79BBA0)

    def sellable_from_mask(mask, t):
        # sub_79BBA0: types 43/44 special-case set a2[1]=1 (sell) -> sellable.
        if t in (43, 44):
            return True
        if mask & 1:           # bit0 set -> mask authoritative
            return bool(mask & 4)   # bit2 = sell
        return True            # default capability = sellable

    def dump(iid, label):
        e = id2entry.get(iid)
        if not e:
            print(f"  id={iid} ({label}): NOT FOUND")
            return
        nm = rd_big5(e + ENTRY_NAME_OFF)
        t  = rdu32(e + ENTRY_TYPE_OFF)
        pa = rdi32(e + ENTRY_PRICE_A)
        mask = rdu32(e + ENTRY_CAPMASK)
        sell = sellable_from_mask(mask, t)
        print(f"  id={iid:>6} type={t:>3} priceA={pa:>8} cap(+0x45C)=0x{mask:04X} "
              f"bit0={mask&1} bit2(sell)={1 if mask&4 else 0} => SELLABLE={sell}  name={nm}  [{label}]")

    print("\n=== target items (rule: cap+0x45C bit0 set => sellable=bit2) ===")
    dump(28077, "R代幣 NON-SELLABLE(grey, ground truth)")
    dump(8036,  "攤販呼叫券")
    for cand in (1, 2, 3, 100, 4001):
        if cand in id2entry:
            dump(cand, "ref")

    # whole-DB stats for the capability mask
    n_override = n_nonsell = 0
    capmask_hist = {}
    for iid, e in id2entry.items():
        mask = rdu32(e + ENTRY_CAPMASK) or 0
        capmask_hist[mask] = capmask_hist.get(mask, 0) + 1
        if mask & 1:
            n_override += 1
            if not (mask & 4):
                n_nonsell += 1
    print(f"\ncap mask bit0(override) set: {n_override} / {len(id2entry)}")
    print(f"  of those, NON-sellable (bit2 clear): {n_nonsell}")
    print("top cap-mask values:")
    for m, c in sorted(capmask_hist.items(), key=lambda kv: -kv[1])[:12]:
        print(f"  0x{m:04X}: {c}")

    print("\n=== type histogram (top 30 by count) ===")
    for t, c in sorted(type_hist.items(), key=lambda kv: -kv[1])[:30]:
        print(f"  type={t}: {c}")

    # full-entry diff between a known non-sellable (R代幣) and known sellable (木頭)
    ENTRY_SIZE = 0x4BC
    def full(iid):
        e = id2entry.get(iid)
        return rd(e, ENTRY_SIZE) if e else None
    a = full(28077)   # R代幣 non-sellable
    b = full(2)       # 木頭 sellable
    c = full(8036)    # 攤販呼叫券
    if a and b:
        print("\n=== dword diff: R代幣(28077, NONSELL) vs 木頭(2, SELL) ===")
        print(f"{'off':>5} {'R代幣':>12} {'木頭':>12} {'攤販呼叫券':>12}")
        for off in range(0, ENTRY_SIZE, 4):
            va = int.from_bytes(a[off:off+4], "little")
            vb = int.from_bytes(b[off:off+4], "little")
            vc = int.from_bytes(c[off:off+4], "little") if c else 0
            if va != vb:
                print(f"+0x{off:03X} {va:>12} {vb:>12} {vc:>12}")

    # validate priceA(+0x3F4) as a sellable proxy: cross-tab vs type in {5,24}
    print("\n=== price/type cross-tab over all items ===")
    n_price0 = n_pricePos = 0
    n_t524 = n_t524_price0 = 0
    sample_price0 = []
    for iid, e in id2entry.items():
        pa = rdu32(e + ENTRY_PRICE_A) or 0
        t = rdu32(e + ENTRY_TYPE_OFF)
        if pa == 0:
            n_price0 += 1
            if len(sample_price0) < 0:
                sample_price0.append(iid)
        else:
            n_pricePos += 1
        if t in (5, 24):
            n_t524 += 1
            if pa == 0:
                n_t524_price0 += 1
    print(f"priceA==0: {n_price0}   priceA>0: {n_pricePos}")
    print(f"type in (5,24): {n_t524}   of which priceA==0: {n_t524_price0}")

    k32.CloseHandle(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
