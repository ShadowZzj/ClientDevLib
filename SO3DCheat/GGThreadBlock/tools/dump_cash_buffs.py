#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_cash_buffs.py —— 从在线 SO3DPlus.exe 进程内存里 dump cash 状态 buff 的
type(=网络包 511337 里的 type)→ 名字 映射,用来核实 so3d_online_login_client.py
里硬编码的 CASH_BUFF_TYPE_NAMES。

依据(IDA unpackd_so3d.exe,imagebase 0x400000):
  GameHUD 单例【对象本身】@ 0x00ED40F0(RVA 0x00AD40F0)—— 注意不是指针:
    handler 调用点是 `mov ecx, 0xED40F0; call GameHUD_AddCashEffect`,直接把 0xED40F0 当 this。
  cash vector(std::vector)@ GameHUD + 0x25C0 = 0x00ED66B0:  vec+0=begin, vec+4=end, count=(end-begin)/280
  记录 stride 280:  +0x00 type(u32) / +0x04 expiry(u32,绝对秒) / +0x08 value(u32)
                    / +0x0C iconId(u32) / +0x10 name(Big5 字符串) / +0x114 闪烁flag(u8)

用法:  python dump_cash_buffs.py [pid]   不带参数则自动扫所有 SO3DPlus.exe 实例。
"""
import ctypes
import ctypes.wintypes as w
import struct
import sys

k = ctypes.windll.kernel32

IMAGEBASE = 0x400000
RVA_GAMEHUD = 0x00ED40F0 - IMAGEBASE      # 0xAD40F0,GameHUD 对象本身(非指针)
CASH_VEC_OFF = 0x25C0
REC_STRIDE = 280
TARGET_EXE = "SO3DPlus.exe"

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x2
TH32CS_SNAPMODULE = 0x8
TH32CS_SNAPMODULE32 = 0x10


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ProcessID", w.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)), ("th32ModuleID", w.DWORD),
        ("cntThreads", w.DWORD), ("th32ParentProcessID", w.DWORD),
        ("pcPriClassBase", ctypes.c_long), ("dwFlags", w.DWORD),
        ("szExeFile", ctypes.c_char * 260),
    ]


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", w.DWORD), ("th32ModuleID", w.DWORD), ("th32ProcessID", w.DWORD),
        ("GlblcntUsage", w.DWORD), ("ProccntUsage", w.DWORD),
        ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)), ("modBaseSize", w.DWORD),
        ("hModule", w.HMODULE), ("szModule", ctypes.c_char * 256),
        ("szExePath", ctypes.c_char * 260),
    ]


def find_pids() -> list[int]:
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(PROCESSENTRY32)
    out = []
    ok = k.Process32First(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.decode("mbcs", "ignore").lower() == TARGET_EXE.lower():
            out.append(pe.th32ProcessID)
        ok = k.Process32Next(snap, ctypes.byref(pe))
    k.CloseHandle(snap)
    return out


def module_base(pid: int) -> int | None:
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == -1 or snap == 0xFFFFFFFF:
        return None
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(MODULEENTRY32)
    base = None
    ok = k.Module32First(snap, ctypes.byref(me))
    while ok:
        if me.szModule.decode("mbcs", "ignore").lower() == TARGET_EXE.lower():
            base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
            break
        ok = k.Module32Next(snap, ctypes.byref(me))
    k.CloseHandle(snap)
    return base


def rpm(h, addr: int, size: int) -> bytes | None:
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t(0)
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        return None
    return buf.raw[: n.value]


def ru32(h, addr: int) -> int | None:
    b = rpm(h, addr, 4)
    return struct.unpack("<I", b)[0] if b and len(b) == 4 else None


def read_cstr(h, addr: int, maxlen: int = 256) -> bytes:
    b = rpm(h, addr, maxlen) or b""
    i = b.find(b"\x00")
    return b[:i] if i >= 0 else b


def decode_name(raw: bytes) -> str:
    for enc in ("big5", "gbk", "utf-8"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("latin-1", "replace")


def dump_process(pid: int) -> None:
    base = module_base(pid)
    print(f"\n=== pid {pid}  {TARGET_EXE} base=0x{base:08X} ===" if base else f"\n=== pid {pid}: 取模块基址失败(权限?需管理员) ===")
    if not base:
        return
    h = k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        print(f"  OpenProcess 失败 err={k.GetLastError()}(可能需要管理员权限)")
        return
    try:
        hud = base + RVA_GAMEHUD     # GameHUD 对象本身,不解引用
        print(f"  GameHUD 对象 @ 0x{hud:08X}")
        vec = hud + CASH_VEC_OFF
        begin = ru32(h, vec)
        end = ru32(h, vec + 4)
        if not begin or not end or end < begin:
            print(f"  cash vector 异常 begin=0x{begin or 0:08X} end=0x{end or 0:08X}")
            return
        count = (end - begin) // REC_STRIDE
        print(f"  cash vector @ 0x{vec:08X}  begin=0x{begin:08X} end=0x{end:08X}  count={count}")
        if count <= 0:
            print("  当前没有 cash 状态(该角色身上没 buff)")
            return
        print(f"  {'type':>4} | {'value':>6} | {'expiryUnix':>10} | {'icon':>4} | name")
        print("  " + "-" * 60)
        for i in range(count):
            rec = begin + REC_STRIDE * i
            t = ru32(h, rec + 0)
            exp = ru32(h, rec + 4)
            val = ru32(h, rec + 8)
            icon = ru32(h, rec + 12)
            name = decode_name(read_cstr(h, rec + 16))
            print(f"  {t:>4} | {val:>6} | {exp:>10} | {icon:>4} | {name}")
    finally:
        k.CloseHandle(h)


def main() -> None:
    if len(sys.argv) > 1:
        pids = [int(sys.argv[1], 0)]
    else:
        pids = find_pids()
    if not pids:
        print(f"没找到 {TARGET_EXE} 进程,游戏没开?")
        return
    for pid in pids:
        dump_process(pid)


if __name__ == "__main__":
    main()
