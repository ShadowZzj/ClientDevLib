"""Confirm the non-sellable mechanism by calling the game's own predicate.

sub_996700(itemId) -> bool   (== Shop_ConfirmAndSellItem's So3DSpec block)
We invoke it via CreateRemoteThread (cdecl, single int arg fits in lpParameter)
and read the return value with GetExitCodeThread. No memory is written.
"""
import ctypes as C
from ctypes import wintypes as W
import sys

PROC = "SO3DPlus.exe"
FN_SUB_996700 = 0x00996700   # live addr == IDA linear addr (imagebase 0x400000, no ASLR)

TH32CS_SNAPPROCESS = 0x2
PROCESS_CREATE_THREAD = 0x0002
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_WRITE = 0x0020
RIGHTS = (PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
          PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_VM_WRITE)

k32 = C.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = W.HANDLE
k32.CreateRemoteThread.restype = W.HANDLE
k32.CreateRemoteThread.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, C.c_void_p,
                                   C.c_void_p, W.DWORD, C.c_void_p]


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


def call_predicate(h, item_id):
    th = k32.CreateRemoteThread(h, None, 0, C.c_void_p(FN_SUB_996700),
                                C.c_void_p(item_id), 0, None)
    if not th:
        return None, f"CreateRemoteThread err={C.get_last_error()}"
    k32.WaitForSingleObject(th, 5000)
    code = W.DWORD(0)
    k32.GetExitCodeThread(th, C.byref(code))
    k32.CloseHandle(th)
    return code.value, None


def main():
    pids = find_pids(PROC)
    if not pids:
        print("process not found"); return 1
    targets = {28077: "R代幣 (expect NON-sellable=1)",
               2: "木頭 (expect sellable=0)",
               1: "嚕啦族排泄物 (expect 0)",
               3: "打火石 (expect 0)",
               8036: "攤販呼叫券 (?)",
               4001: "神秘男人的信件 type24 (?)"}
    for pid in pids:
        h = k32.OpenProcess(RIGHTS, False, pid)
        if not h:
            print(f"pid={pid} OpenProcess failed {C.get_last_error()}"); continue
        print(f"\n=== pid={pid} ===")
        for iid, label in targets.items():
            val, err = call_predicate(h, iid)
            print(f"  sub_996700({iid:>6}) = {val if err is None else err}   {label}")
        k32.CloseHandle(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
