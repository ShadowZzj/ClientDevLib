import ctypes as C
from ctypes import wintypes as W
import sys

PROC = "so3dplus.exe"
G_PITEMCONTAINER = 0x00E0DDF0   # static addr of g_pItemContainer (imagebase 0x400000)

SLOT_ARRAY_OFF = 0xEB8          # container + 942*4
SLOT_STRIDE    = 0x118          # 70 dwords
SLOT_COUNT_OFF = 0xF6E8         # container + 15802*4
MAX_SLOTS      = 192            # a2 < 0xC0 in sub_7A5120

# within a slot (dword indices)
OFF_ITEMID = 0x04   # slot[1]  - searched field (70*i+943)
OFF_CNT2   = 0x08   # slot[2]
OFF_CNT3   = 0x0C   # slot[3]
OFF_CNT4   = 0x10   # slot[4]
OFF_DBPTR  = 0x20   # slot[8]  - item DB entry ptr
DB_TYPE_OFF = 0x110 # dbentry + 272 -> item type (41/42/...)

k32 = C.WinDLL("kernel32", use_last_error=True)
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x2

class PROCESSENTRY32(C.Structure):
    _fields_ = [("dwSize", W.DWORD),("cntUsage", W.DWORD),("th32ProcessID", W.DWORD),
                ("th32DefaultHeapID", C.c_void_p),("th32ModuleID", W.DWORD),
                ("cntThreads", W.DWORD),("th32ParentProcessID", W.DWORD),
                ("pcPriClassBase", C.c_long),("dwFlags", W.DWORD),("szExeFile", C.c_char*260)]

def find_pid(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    e = PROCESSENTRY32(); e.dwSize = C.sizeof(e)
    pid = None
    if k32.Process32First(snap, C.byref(e)):
        while True:
            if e.szExeFile.decode(errors="ignore").lower() == name.lower():
                pid = e.th32ProcessID; break
            if not k32.Process32Next(snap, C.byref(e)): break
    k32.CloseHandle(snap)
    return pid

def main():
    pid = find_pid(PROC)
    if not pid:
        print(f"process {PROC} not found"); return
    h = k32.OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print("OpenProcess failed", C.get_last_error()); return
    print(f"pid={pid} handle={h}")

    def rd(addr, size):
        buf = (C.c_ubyte*size)(); n = C.c_size_t(0)
        ok = k32.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(n))
        if not ok: return None
        return bytes(buf[:n.value])

    def rdu32(addr):
        b = rd(addr, 4)
        return int.from_bytes(b, "little") if b and len(b)==4 else None

    cont = rdu32(G_PITEMCONTAINER)
    print(f"g_pItemContainer -> 0x{cont:08X}")
    cnt = rdu32(cont + SLOT_COUNT_OFF)
    print(f"slot count (@+0xF6E8) = {cnt}")

    print(f"\n{'slot':>4} {'enc':>5} {'itemId':>8} {'cnt2':>6} {'cnt3':>6} {'dbptr':>10} {'type':>5}")
    print("-"*60)
    limit = min(MAX_SLOTS, cnt if cnt and 0 < cnt <= MAX_SLOTS else MAX_SLOTS)
    for i in range(limit):
        base = cont + SLOT_ARRAY_OFF + i*SLOT_STRIDE
        itemId = rdu32(base + OFF_ITEMID)
        if itemId is None: continue
        if itemId == 0: continue   # empty slot
        cnt2 = rdu32(base + OFF_CNT2)
        cnt3 = rdu32(base + OFF_CNT3)
        dbptr = rdu32(base + OFF_DBPTR)
        itype = rdu32(dbptr + DB_TYPE_OFF) if dbptr and dbptr > 0x10000 else None
        enc = ((i & 0x7FFF) + 13)   # inventory encoding (no 0x8000)
        print(f"{i:>4} {enc:>5} {itemId:>8} {cnt2:>6} {cnt3:>6} 0x{(dbptr or 0):08X} {str(itype):>5}")

    k32.CloseHandle(h)

if __name__ == "__main__":
    main()
