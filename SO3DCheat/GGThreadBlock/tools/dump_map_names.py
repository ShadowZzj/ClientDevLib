"""
Dump all map id -> map name pairs from a running so3dplus.exe.

How it works (no game files needed, reads from live memory):
  - g_localMapId (slot) @ IDB 0xD67CB4, g_MapIdTable @ 0xD6AE78  -> current map id.
  - The game keeps a MapNameTable as CSealTableManager sub-table type 73.
      CSealTableManager::GetInstance()           @ 0xB6C6C0  (cdecl, singleton @ 0xT18B6560)
      CSealTableManager::GetSubTableByType(73)   @ 0xB6D600  (thiscall this=mgr)
      CSealTableManager::GetTableElem(t,idx,lvl) @ 0xB6D450  (thiscall this=mgr)
      <subtable>.size()                          @ 0x6A8FE0  (thiscall this=subtable)
  - Record layout (size 0x73C): +0x8 mapId(key), +0xC name[260] (Big5),
      +0x11C valid flag, +0x128 teleport dest id.

We inject one remote thread that loops idx=0..count-1 calling GetTableElem and
stores each record pointer into a scratch buffer; then we read the fields here.

Addresses are absolute VAs from the unpackd_so3d dump and are rebased by the
real main-module base (observed 0x400000, delta 0).
"""
import ctypes as C
from ctypes import wintypes as W
import struct, json, sys, os

PROC_NAME = b"so3dplus.exe"
IDB_BASE  = 0x400000

# ---- IDB VAs ----
A_SLOT        = 0xD67CB4
A_MAPTBL      = 0xD6AE78
A_GET_INST    = 0xB6C6C0
A_GET_SUBTBL  = 0xB6D600
A_GET_ELEM    = 0xB6D450
A_COUNT       = 0x6A8FE0

REC_KEY   = 0x08
REC_NAME  = 0x0C
REC_VALID = 0x11C
REC_DEST  = 0x128
REC_NAMELEN = 260

TH32CS_SNAPPROCESS=0x2; TH32CS_SNAPMODULE=0x8; TH32CS_SNAPMODULE32=0x10
PROCESS_ALL_ACCESS=0x1F0FFF
MEM_COMMIT_RESERVE=0x3000; PAGE_EXEC_RW=0x40; MEM_RELEASE=0x8000

k32 = C.WinDLL("kernel32", use_last_error=True)
k32.VirtualAllocEx.restype = C.c_void_p
k32.CreateRemoteThread.restype = W.HANDLE
k32.CreateRemoteThread.argtypes=[W.HANDLE,C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,W.DWORD,C.c_void_p]

class PROCESSENTRY32(C.Structure):
    _fields_=[("dwSize",W.DWORD),("cntUsage",W.DWORD),("th32ProcessID",W.DWORD),
              ("th32DefaultHeapID",C.POINTER(W.ULONG)),("th32ModuleID",W.DWORD),
              ("cntThreads",W.DWORD),("th32ParentProcessID",W.DWORD),
              ("pcPriClassBase",W.LONG),("dwFlags",W.DWORD),("szExeFile",C.c_char*260)]
class MODULEENTRY32(C.Structure):
    _fields_=[("dwSize",W.DWORD),("th32ModuleID",W.DWORD),("th32ProcessID",W.DWORD),
              ("GlblcntUsage",W.DWORD),("ProccntUsage",W.DWORD),("modBaseAddr",C.POINTER(C.c_byte)),
              ("modBaseSize",W.DWORD),("hModule",W.HMODULE),("szModule",C.c_char*256),
              ("szExePath",C.c_char*260)]

def find_pid(name):
    snap=k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)
    pe=PROCESSENTRY32(); pe.dwSize=C.sizeof(pe); pid=None
    if k32.Process32First(snap,C.byref(pe)):
        while True:
            if pe.szExeFile.lower()==name.lower(): pid=pe.th32ProcessID; break
            if not k32.Process32Next(snap,C.byref(pe)): break
    k32.CloseHandle(snap); return pid

def main_base(pid,name):
    snap=k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)
    me=MODULEENTRY32(); me.dwSize=C.sizeof(me)
    if k32.Module32First(snap,C.byref(me)):
        while True:
            if me.szModule.lower()==name.lower():
                b=C.cast(me.modBaseAddr,C.c_void_p).value; k32.CloseHandle(snap); return b
            if not k32.Module32Next(snap,C.byref(me)): break
    k32.CloseHandle(snap); return None

def main():
    pid=find_pid(PROC_NAME)
    if not pid: raise SystemExit("so3dplus.exe not running")
    base=main_base(pid,PROC_NAME)
    delta=base-IDB_BASE
    def va(a): return a+delta
    h=k32.OpenProcess(PROCESS_ALL_ACCESS,False,pid)
    if not h: raise SystemExit("OpenProcess err=%d"%C.get_last_error())

    def rpm(addr,n):
        buf=C.create_string_buffer(n); rd=C.c_size_t(0)
        ok=k32.ReadProcessMemory(h,C.c_void_p(addr),buf,n,C.byref(rd))
        return buf.raw[:rd.value] if ok else None
    def rdword(addr):
        b=rpm(addr,4); return None if b is None else int.from_bytes(b,"little")

    def run_stub(code, out_size=0):
        out=0
        if out_size:
            out=k32.VirtualAllocEx(h,None,out_size,MEM_COMMIT_RESERVE,PAGE_EXEC_RW)
        # build needs out addr; caller passes a factory if out is needed
        if callable(code):
            code=code(out)
        mem=k32.VirtualAllocEx(h,None,len(code),MEM_COMMIT_RESERVE,PAGE_EXEC_RW)
        wr=C.c_size_t(0); k32.WriteProcessMemory(h,C.c_void_p(mem),code,len(code),C.byref(wr))
        th=k32.CreateRemoteThread(h,None,0,C.c_void_p(mem),None,0,None)
        k32.WaitForSingleObject(th,10000)
        ec=W.DWORD(0); k32.GetExitCodeThread(th,C.byref(ec))
        k32.CloseHandle(th); k32.VirtualFreeEx(h,C.c_void_p(mem),0,MEM_RELEASE)
        return ec.value, out

    GET_INST=va(A_GET_INST); GET_SUBTBL=va(A_GET_SUBTBL); GET_ELEM=va(A_GET_ELEM); COUNT=va(A_COUNT)

    # --- slot / current map ---
    slot=rdword(va(A_SLOT))
    cur_mapid=rdword(va(A_MAPTBL)+slot*4) if (slot is not None and slot<4096) else None

    # --- count stub: GetInstance -> GetSubTableByType(73) -> size() ; returns count in eax ---
    count_stub = (b"\xB8"+struct.pack("<I",GET_INST)+b"\xFF\xD0"   # mov eax,GetInstance; call eax
                  b"\x8B\xC8"                                       # mov ecx,eax
                  b"\x6A\x49"                                       # push 73
                  b"\xB8"+struct.pack("<I",GET_SUBTBL)+b"\xFF\xD0" # mov eax,GetSubTable; call eax (cleans 1)
                  b"\x8B\xC8"                                       # mov ecx,eax (this=subtable)
                  b"\xB8"+struct.pack("<I",COUNT)+b"\xFF\xD0"      # mov eax,size; call eax
                  b"\xC3")
    count,_=run_stub(count_stub)
    print("pid=%d base=0x%X delta=0x%X  slot=%s curMapId=%s  table73.count=%d"
          %(pid,base,delta&0xffffffff,slot,cur_mapid,count))
    if not count or count>4096:
        raise SystemExit("unexpected count=%r"%count)

    # --- loop stub over idx=0..count-1, store record ptr to out[idx] ---
    def make_loop(out):
        pro = b"\x53\x56\x57"                                  # push ebx,esi,edi
        setup = (b"\xBE"+struct.pack("<I",out)                 # mov esi, out
                 +b"\xBB"+struct.pack("<I",count)              # mov ebx, count
                 +b"\x31\xFF")                                 # xor edi,edi
        body = (b"\xB8"+struct.pack("<I",GET_INST)+b"\xFF\xD0" # mov eax,GetInstance; call eax
                +b"\x8B\xC8"                                   # mov ecx,eax
                +b"\x6A\x00"                                   # push 0   (itemLevel)
                +b"\x57"                                       # push edi (index)
                +b"\x6A\x49"                                   # push 73  (type)
                +b"\xB8"+struct.pack("<I",GET_ELEM)+b"\xFF\xD0"# mov eax,GetTableElem; call eax (cleans 3)
                +b"\x89\x04\xBE"                               # mov [esi+edi*4], eax
                +b"\x47")                                      # inc edi
        cmp_ = b"\x39\xDF"                                     # cmp edi,ebx
        jge  = b"\x0F\x8D"                                     # jge rel32 (placeholder)
        jmp  = b"\xE9"                                         # jmp rel32 (placeholder)
        epi  = b"\x5F\x5E\x5B\xC3"                             # pop edi,esi,ebx; ret
        # layout: pro setup [Lstart: cmp jge body jmp] epi
        Lstart = len(pro)+len(setup)
        after_jge = Lstart+len(cmp_)+6
        after_jmp = after_jge+len(body)+5
        done = after_jmp
        jge_rel = done-after_jge
        jmp_rel = Lstart-after_jmp
        code = pro+setup+cmp_+jge+struct.pack("<i",jge_rel)+body+jmp+struct.pack("<i",jmp_rel)+epi
        return code

    _, out = run_stub(make_loop, out_size=count*4)
    raw = rpm(out, count*4)
    k32.VirtualFreeEx(h,C.c_void_p(out),0,MEM_RELEASE)
    recs = struct.unpack("<%dI"%count, raw)

    rows=[]; misses=[]
    for idx,rec in enumerate(recs):
        if not rec:
            misses.append(idx); continue
        key   = rdword(rec+REC_KEY)
        valid = rdword(rec+REC_VALID)
        dest  = rdword(rec+REC_DEST)
        nb    = rpm(rec+REC_NAME, REC_NAMELEN) or b""
        nb    = nb.split(b"\x00",1)[0]
        try: name=nb.decode("big5")
        except Exception: name=nb.decode("big5","replace")
        rows.append({"id":key if key is not None else idx,"probe_idx":idx,
                     "valid":valid,"dest":dest,"name":name})

    rows.sort(key=lambda r:(r["id"] if r["id"] is not None else 1<<30))
    out_json=os.path.join(os.path.dirname(os.path.abspath(__file__)),"map_names.json")
    with open(out_json,"w",encoding="utf-8") as f:
        json.dump({"current_map_id":cur_mapid,"count":count,"maps":rows},f,ensure_ascii=False,indent=2)

    sys.stdout.reconfigure(encoding="utf-8")
    print("  id  valid  dest  name")
    for r in rows:
        mark=" <== current" if r["id"]==cur_mapid else ""
        print("%4d   %3s  %4s  %s%s"%(r["id"],r["valid"],r["dest"],r["name"],mark))
    if misses:
        print("missing/empty indices (%d):"%len(misses), misses[:40], "..." if len(misses)>40 else "")
    print("\nwrote", out_json)
    k32.CloseHandle(h)

if __name__=="__main__":
    main()
