#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_buffdesc.py —— 从在线 SO3DPlus.exe 的 BuffDesc 表里按 buffId 读 name/category。

用来核实「技能型 buff」(网络包 511337 里 disc!=0 的那支,走 BuffHelper)的名字 ——
跟 cash 状态(disc==0,走 GameHUD_AddCashEffect 的 0xED66B0 vector)完全是两套存储。

依据(unpackd_so3d.exe IDB,imagebase 0x400000;在线 SO3DPlus.exe 同 binary 无 ASLR):
  PatternResolver 里登记的是 RVA(相对 0x400000):
    BuffDescTable RVA 0x9E355C → 绝对 0xDE355C,*(BuffDesc**) 数组基址指针(运行时填)。
    BuffDescCount RVA 0x9E3560 → 绝对 0xDE3560,int 元素上限(id 须 < count)。
  BuffDesc 行 stride 0xAF(175):+0x02 category(u8) / +0x05 name(Big5) / +0x79 flags。
    row(id) = *BuffDescTable + 0xAF*id
  category→渲染:RenderCashBuffBar 收 {4,0},RenderNormalBuffBar 收 {1,2,5}。

用法:  python dump_buffdesc.py [id ...]   不带参数默认查 159(进阶摆摊候选)+扫含「擺攤/攤」的行。
"""
import ctypes
import ctypes.wintypes as w
import struct
import sys

k = ctypes.windll.kernel32

IMAGEBASE = 0x400000
RVA_DESC_TABLE = 0xDE355C - IMAGEBASE   # 0x9E355C
RVA_DESC_COUNT = 0xDE3560 - IMAGEBASE   # 0x9E3560
DESC_STRIDE = 0xAF                      # 175
OFF_CATEGORY = 0x02
OFF_NAME = 0x05
OFF_FLAGS = 0x79
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


def read_cstr(h, addr: int, maxlen: int = 64) -> bytes:
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


def dump(pid: int, ids: list[int], lines: list[str]) -> None:
    base = module_base(pid)
    if not base:
        lines.append(f"\n=== pid {pid}: 取模块基址失败(需管理员?) ===")
        return
    h = k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        lines.append(f"\n=== pid {pid}: OpenProcess 失败 err={k.GetLastError()} ===")
        return
    try:
        table = ru32(h, base + RVA_DESC_TABLE)
        count = ru32(h, base + RVA_DESC_COUNT)
        lines.append(f"\n=== pid {pid}  {TARGET_EXE} base=0x{base:08X}  BuffDescTable=0x{table or 0:08X} count={count} ===")
        if not table:
            lines.append("  BuffDesc 表未初始化(没进游戏?)")
            return

        def row_info(i: int) -> tuple[int, str]:
            row = table + DESC_STRIDE * i
            cat = rpm(h, row + OFF_CATEGORY, 1)
            cat = cat[0] if cat else -1
            name = decode_name(read_cstr(h, row + OFF_NAME))
            return cat, name

        lines.append("\n  [ 指定 buffId ]")
        lines.append(f"  {'id':>5} | {'cat':>3} | name")
        lines.append("  " + "-" * 50)
        for i in ids:
            if count and i >= count:
                lines.append(f"  {i:>5} |   - | <越界 count={count}>")
                continue
            cat, name = row_info(i)
            kind = "Cash" if cat in (0, 4) else ("Normal" if cat in (1, 2, 5) else f"?{cat}")
            lines.append(f"  {i:>5} | {cat:>3} | {name}   [{kind}]")

        # 全表扫描:列出所有 category∈{0,4} 的行 —— 这些就是「会进 cash 状态栏」的 buff
        # (含进阶摆摊这类经 disc!=0 BuffHelper 进来、但渲染在 cash 栏的)。
        lines.append("\n  [ 全表扫描:category∈{0,4}(cash 栏)的所有行 ]")
        limit = count if count and count < 4000 else 2000
        for i in range(limit):
            cat, name = row_info(i)
            if cat in (0, 4) and name:
                lines.append(f"  {i:>5} | {cat:>3} | {name}")
    finally:
        k.CloseHandle(h)


def main() -> None:
    args = [int(a, 0) for a in sys.argv[1:] if not a.startswith("--")]
    ids = args or [159]
    pids = find_pids()
    if not pids:
        print(f"没找到 {TARGET_EXE} 进程,游戏没开?")
        return
    lines: list[str] = []
    for pid in pids:
        dump(pid, ids, lines)
    text = "\n".join(lines)
    out = "buffdesc_dump.txt"
    with open(out, "w", encoding="utf-8") as f:
        f.write(text + "\n")
    try:
        print(text)
    except UnicodeEncodeError:
        print(f"(控制台编码无法显示繁体,完整结果见 {out})")
    print(f"\n[已写入 {out}(UTF-8)]")


if __name__ == "__main__":
    main()
