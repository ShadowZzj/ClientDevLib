#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_buff_names.py —— dump 出 cash 状态 buff 的【全部】type → 名字 映射(不只是
当前角色身上有的那几个)。

跟 dump_cash_buffs.py 的区别:
  - dump_cash_buffs.py 读 GameHUD 的 cash vector,只能看到当前【已激活】的 buff;
  - 本脚本直接从游戏的本地化字符串表里把每个 type 对应的名字捞出来,理论上能枚举
    GameHUD_AddCashEffect switch 里出现过的每一个 type,无需角色身上真有这个状态。

依据(IDA unpackd_so3d.exe,imagebase 0x400000,在线进程 SO3DPlus.exe 同 binary 无 ASLR):
  名字来源:GameHUD_AddCashEffect @ 0x7C5BB0 对每个 type 调
      sub_5BF8E0(stringId, dest, len)   // 纯文本
    或 sub_5BF610(stringId, dest, value) // 带 %d 的模板,运行时填 value
  两者都通过 sub_5BB560 取字符串:
      entry = *(DWORD*)0xDFB128 + 260 * stringId      // Big5 定长 260 字节
    其中 0xDFB128 是字符串表 std::vector 头,+0 = begin 指针,+4 = end 指针。
  => 只要在线读到 begin,就能 table[stringId] = read_big5(begin + 260*stringId)。

  ★坑:Hex-Rays 把 sub_5BF8E0 的第一个参数类型当成了 char,伪代码里只显示了
    stringId 的【低字节】(例如真实 push 0x72E=1838,伪代码却写成 46=0x2E)。所以
    type→stringId 必须从【反汇编的 push imm32】里读真值,不能信反编译的小数字!
    下面的 stringId 全部是从 0x7C5BB0 各 case 的 `push imm32; call sub_5BF8E0/610`
    指令字节里读出来的真值(已与在线 cash vector 实测吻合:type7=1335=攻擊速度上升、
    type10=1578=角色死亡無懲罰狀態、type0=1838=力量藥水)。
  type → stringId(网络包 511337 的 type):
    0->1838 1->1839 2->1840 3->1842 4->1841 5->1338 7->1335 8->1576 9->1577
    10->1578 11->1744 12->1819 13->1874 14->1891 15->2042 16->2044 17->2046
    18->2048 19->2050 20->2052 21->2056 22->2058 23->2059 26->2191 27->3432
    1000->1885 1001->1886 1002->1971 1003->1972 1004->2401 1005->2400 1006->2409
    1007->2408 65551->2043 65552->2045 65553->2047 65554->2049 65555->2051
    65556->2053 65557->2056
    24->{2131..2136}(按 a4 子型)  65560->{2137..2142}(按 a4 子型)
    8/9 经 sub_7C5040:type8->1576(掉寶率提高%d%%) type9->1577(經驗值提高%d%%)

用法:  python dump_buff_names.py [pid]    不带参数自动扫 SO3DPlus.exe。
      python dump_buff_names.py --all     额外把整张字符串表里疑似状态名的条目都列出来。
"""
import ctypes
import ctypes.wintypes as w
import struct
import sys

k = ctypes.windll.kernel32

IMAGEBASE = 0x400000
RVA_STRTAB = 0xDFB128 - IMAGEBASE      # 0x9FB128,字符串表 std::vector 头(+0=begin,+4=end)
STR_STRIDE = 260                       # 每条 Big5 字符串定长 260 字节
TARGET_EXE = "SO3DPlus.exe"

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x2
TH32CS_SNAPMODULE = 0x8
TH32CS_SNAPMODULE32 = 0x10

# type → stringId(真值,从 0x7C5BB0 反汇编 push imm32 读出)。带 %d 模板的标在 TYPE_FMT。
TYPE_STRINGID = {
    0: 1838, 1: 1839, 2: 1840, 3: 1842, 4: 1841, 5: 1338, 7: 1335,
    8: 1576, 9: 1577, 10: 1578, 11: 1744, 12: 1819, 13: 1874, 14: 1891,
    15: 2042, 16: 2044, 17: 2046, 18: 2048, 19: 2050, 20: 2052, 21: 2056,
    22: 2058, 23: 2059, 26: 2191, 27: 3432,
    1000: 1885, 1001: 1886, 1002: 1971, 1003: 1972, 1004: 2401, 1005: 2400,
    1006: 2409, 1007: 2408,
    65551: 2043, 65552: 2045, 65553: 2047, 65554: 2049, 65555: 2051,
    65556: 2053, 65557: 2056,
}
# 这些 type 走 sub_5BF610(模板,运行时用 value 填 %d;下面读出来保留 %d 占位)。
TYPE_FMT = {2, 8, 9}

# 复合 type:同一个 type 按 511337 的 value(a4)字段走不同子串。
# 24 / 65560 都是按 value 选职业/属性名的一组,列出全部子型,方便人工核对。
TYPE_SUBSWITCH = {
    24: {  # a4(value)→ stringId
        0: 2131, 18110: 2131,
        1: 2132, 18111: 2132,
        2: 2133, 8837: 2133,
        3: 2134, 8838: 2134,
        4: 2135,          # else 分支
        5: 2136, 8840: 2136,
    },
    65560: {
        0: 2137,
        1: 2138,
        2: 2139, 8837: 2139,
        3: 2140, 8838: 2140,
        4: 2141, 8839: 2141,
        5: 2142, 8840: 2142,
    },
}


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


def read_cstr(h, addr: int, maxlen: int = STR_STRIDE) -> bytes:
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


class StrTab:
    def __init__(self, h, begin: int, count: int):
        self.h = h
        self.begin = begin
        self.count = count

    def get(self, sid: int) -> str:
        if sid < 0 or (self.count and sid >= self.count):
            return f"<id {sid} 越界(count={self.count})>"
        return decode_name(read_cstr(self.h, self.begin + STR_STRIDE * sid))


def dump_process(pid: int, lines: list[str], scan_all: bool) -> None:
    base = module_base(pid)
    if not base:
        lines.append(f"\n=== pid {pid}: 取模块基址失败(权限?需管理员) ===")
        return
    h = k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        lines.append(f"\n=== pid {pid}: OpenProcess 失败 err={k.GetLastError()}(可能需要管理员) ===")
        return
    try:
        vec = base + RVA_STRTAB
        begin = ru32(h, vec)
        end = ru32(h, vec + 4)
        if not begin or not end or end < begin:
            lines.append(f"\n=== pid {pid}: 字符串表未初始化 begin=0x{begin or 0:08X} end=0x{end or 0:08X}(角色没进游戏?) ===")
            return
        count = (end - begin) // STR_STRIDE
        lines.append(f"\n=== pid {pid}  {TARGET_EXE} base=0x{base:08X}  字符串表 begin=0x{begin:08X} count={count} ===")
        tab = StrTab(h, begin, count)

        lines.append("\n  [ 单值 type → 名字 ]")
        lines.append(f"  {'type':>6} | {'sid':>4} | name")
        lines.append("  " + "-" * 56)
        for t in sorted(TYPE_STRINGID):
            sid = TYPE_STRINGID[t]
            name = tab.get(sid)
            tag = " (模板,%d=value)" if t in TYPE_FMT else ""
            lines.append(f"  {t:>6} | {sid:>4} | {name}{tag}")

        for t in sorted(TYPE_SUBSWITCH):
            lines.append(f"\n  [ 复合 type {t} → 按 value 子型 ]")
            lines.append(f"  {'value':>6} | {'sid':>4} | name")
            lines.append("  " + "-" * 56)
            seen = {}
            for v, sid in sorted(TYPE_SUBSWITCH[t].items()):
                seen.setdefault(sid, tab.get(sid))
                lines.append(f"  {v:>6} | {sid:>4} | {seen[sid]}")

        if scan_all:
            lines.append("\n  [ 全表扫描:疑似状态/药水/提高 的条目 ]")
            kws = ("藥水", "狀態", "提高", "祝福", "上升", "增加", "下降", "輪盤", "速度")
            for sid in range(count):
                s = tab.get(sid)
                if any(kw in s for kw in kws):
                    lines.append(f"  {sid:>4} | {s}")
    finally:
        k.CloseHandle(h)


def main() -> None:
    scan_all = "--all" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    pids = [int(args[0], 0)] if args else find_pids()
    if not pids:
        print(f"没找到 {TARGET_EXE} 进程,游戏没开?")
        return
    lines: list[str] = []
    for pid in pids:
        dump_process(pid, lines, scan_all)
    text = "\n".join(lines)
    # 控制台是 GBK,Big5 繁体直接 print 会乱码;写 UTF-8 文件再看最稳。
    out = "buff_names_dump.txt"
    with open(out, "w", encoding="utf-8") as f:
        f.write(text + "\n")
    try:
        print(text)
    except UnicodeEncodeError:
        print(f"(控制台编码无法显示繁体,完整结果见 {out})")
    print(f"\n[已写入 {out}(UTF-8)]")


if __name__ == "__main__":
    main()
