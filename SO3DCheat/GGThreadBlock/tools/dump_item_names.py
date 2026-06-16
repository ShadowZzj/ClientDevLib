import ctypes as C
from ctypes import wintypes as W
import json
import sys

PROC = "SO3DPlus.exe"

# CSealTableManager singleton pointer (linear addr, imagebase 0x400000).
# sub_B6C6C0 stores the lazily-created manager into ds:dword_18B6560.
G_TABLE_MANAGER = 0x018B6560

# Type-20 (item) table vector lives at manager + 0x24 (see sub_B6C880:
#   ecx = manager + 0x24 -> count = (end-begin)/4, accessor begin+4*idx).
ITEM_VEC_OFF = 0x24          # vector::_Myfirst (begin) is at +0x24, _Mylast (end) at +0x28

# ItemTable entry layout (see ItemTable ctor sub_B67750 + parser sub_B6E540):
ENTRY_ITEMID_OFF = 0x04      # itemId (also mirrored at +0x08)
ENTRY_NAME_OFF   = 0x0C      # Big5 char[0x104] name buffer
ENTRY_TYPE_OFF   = 0x110     # item type/category (dword)
ENTRY_STACK_OFF  = 0x114     # 276: stackable flag (Item_IsStackable @0x79D9D0: !=0 -> stackable)
ENTRY_CAPMASK    = 0x45C     # 1116: capability bitmask (ItemSlot__ComputeCapabilities @0x79BBA0)
NAME_MAX         = 0x104


def sellable_from_mask(mask, t):
    # Vendor grey-out = ItemSlot__GetCapability(slot, 2=SELL) = caps[1].
    # caps[1] comes from the +0x45C mask only when bit0 is set, else default sellable.
    #   bit0(0x1)=override present, bit2(0x4)=SELL.
    # types 43/44 are special-cased in ComputeCapabilities to always-sellable.
    if t in (43, 44):
        return True
    if mask & 1:
        return bool(mask & 4)
    return True

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
    e = PROCESSENTRY32()
    e.dwSize = C.sizeof(e)
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
        print(f"process {PROC} not found")
        return 1
    pid = pids[0]
    h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print("OpenProcess failed", C.get_last_error())
        return 1
    print(f"pid={pid} handle={h} (pids found: {pids})")

    def rd(addr, size):
        buf = (C.c_ubyte * size)()
        n = C.c_size_t(0)
        ok = k32.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(n))
        if not ok:
            return None
        return bytes(buf[:n.value])

    def rdu32(addr):
        b = rd(addr, 4)
        return int.from_bytes(b, "little") if b and len(b) == 4 else None

    def rd_big5(addr, maxlen=NAME_MAX):
        b = rd(addr, maxlen)
        if not b:
            return None
        nul = b.find(b"\x00")
        if nul >= 0:
            b = b[:nul]
        if not b:
            return ""
        try:
            return b.decode("cp950")
        except Exception:
            return b.decode("big5", errors="replace")

    mgr = rdu32(G_TABLE_MANAGER)
    print(f"g_pTableManager(@0x{G_TABLE_MANAGER:08X}) -> 0x{(mgr or 0):08X}")
    if not mgr or mgr < 0x10000:
        print("manager pointer invalid; aborting")
        return 1

    begin = rdu32(mgr + ITEM_VEC_OFF)
    end = rdu32(mgr + ITEM_VEC_OFF + 4)
    print(f"item vec begin=0x{(begin or 0):08X} end=0x{(end or 0):08X}")
    if not begin or not end or end < begin:
        print("item vector invalid; aborting")
        return 1
    count = (end - begin) // 4
    print(f"item vector slot count = {count}")
    if count <= 0 or count > 200000:
        print("suspicious count; aborting")
        return 1

    # bulk-read the pointer array in one shot
    ptr_blob = rd(begin, count * 4)
    items = {}
    bad = 0
    for i in range(count):
        entry = int.from_bytes(ptr_blob[i * 4:i * 4 + 4], "little")
        if not entry or entry < 0x10000:
            continue
        name = rd_big5(entry + ENTRY_NAME_OFF)
        if name is None:
            bad += 1
            continue
        item_id = rdu32(entry + ENTRY_ITEMID_OFF)
        itype = rdu32(entry + ENTRY_TYPE_OFF)
        mask = rdu32(entry + ENTRY_CAPMASK) or 0
        stackable = bool(rdu32(entry + ENTRY_STACK_OFF))
        name = name.strip()
        if not name:
            continue
        key = item_id if item_id is not None else i
        items[key] = {"id": key, "slot": i, "type": itype, "name": name,
                      "capmask": mask, "sellable": sellable_from_mask(mask, itype),
                      "stackable": stackable}

    print(f"collected {len(items)} named items ({bad} unreadable entries)")

    out_json = "item_names.json"
    out_csv = "item_names.csv"
    rows = sorted(items.values(), key=lambda r: r["id"])
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump({r["id"]: {"name": r["name"], "type": r["type"],
                             "sellable": r["sellable"], "stackable": r["stackable"]}
                   for r in rows},
                  f, ensure_ascii=False, indent=0)
    with open(out_csv, "w", encoding="utf-8-sig") as f:
        f.write("id,type,sellable,stackable,capmask,name\n")
        for r in rows:
            nm = r["name"].replace('"', '""')
            f.write(f'{r["id"]},{r["type"]},{1 if r["sellable"] else 0},'
                    f'{1 if r["stackable"] else 0},0x{r["capmask"]:04X},"{nm}"\n')

    n_sell = sum(1 for r in rows if r["sellable"])
    n_stack = sum(1 for r in rows if r["stackable"])
    print(f"wrote {out_json} and {out_csv}")
    print(f"sellable: {n_sell}   non-sellable: {len(rows) - n_sell}   total: {len(rows)}")
    print(f"stackable: {n_stack}   non-stackable: {len(rows) - n_stack}")
    for r in rows[:25]:
        mark = "SELL" if r["sellable"] else "GREY"
        stk = "STK" if r["stackable"] else "---"
        print(f'  id={r["id"]:>6} type={r["type"]} [{mark}][{stk}] cap=0x{r["capmask"]:04X} name={r["name"]}')

    # spot-check a known id (8036 = vendor-call ticket)
    if 8036 in items:
        print(f'  [check] id=8036 -> {items[8036]["name"]}')

    k32.CloseHandle(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
