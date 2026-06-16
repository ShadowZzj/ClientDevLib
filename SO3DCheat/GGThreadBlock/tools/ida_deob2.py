import idc, idaapi

# ============================================================
# Generic single-function tracer that follows jmp-chains and
# records real (executed) instructions, calls, and successors.
# ============================================================
def is_jcc(m):
    return m and m[0] == 'j' and m != 'jmp'

def trace_real(start, limit=8000):
    visited = set(); real = []; calls = {}; succ = {}
    stack = [start]
    while stack:
        ea = stack.pop()
        while ea != idaapi.BADADDR and ea not in visited and len(visited) < limit:
            visited.add(ea)
            ln = idc.create_insn(ea)
            if ln == 0:
                ln = idc.get_item_size(ea)
            m = idc.print_insn_mnem(ea)
            nxt = ea + (ln if ln > 0 else 1)
            real.append(ea)
            if m == 'jmp':
                tgt = idc.get_operand_value(ea, 0); succ[ea] = [tgt]
                ea = tgt if tgt != idaapi.BADADDR else idaapi.BADADDR
            elif is_jcc(m):
                tgt = idc.get_operand_value(ea, 0); succ[ea] = [tgt, nxt]
                if tgt != idaapi.BADADDR:
                    stack.append(tgt)
                ea = nxt
            elif m in ('ret', 'retn', 'retf', 'iret'):
                succ[ea] = []; ea = idaapi.BADADDR
            elif m == 'call':
                tgt = idc.get_operand_value(ea, 0); calls[ea] = tgt; succ[ea] = [nxt]; ea = nxt
            else:
                succ[ea] = [nxt]; ea = nxt
    return visited, real, calls, succ

# A "bridge"/inline stub = no ret in its traced body (just transfers control)
def is_bridge(t):
    v, r, c, s = trace_real(t, limit=600)
    has_ret = any(idc.print_insn_mnem(e) in ('ret', 'retn', 'retf', 'iret') for e in v)
    return (not has_ret), v, c, s

# ============================================================
# Junk-instruction classifier.
# These appear as obfuscation noise in the stubs and are stack-
# neutral / dead (their results are immediately discarded).
# ============================================================
JUNK_MNEMS_SOLO = {'pushf', 'pushfd', 'popf', 'popfd', 'pusha', 'pushad',
                   'popa', 'popad', 'stc', 'clc', 'cmc', 'std', 'cld',
                   'nop', 'bswap'}

def is_junk(ea):
    m = idc.print_insn_mnem(ea)
    op0 = idc.print_operand(ea, 0)
    # lea esp,[esp+N]  -> stack pointer fixup that cancels junk push
    if m == 'lea' and op0 == 'esp':
        return True
    # push of a garbage value that gets cancelled by a later lea esp
    if m == 'push':
        return True
    if m in JUNK_MNEMS_SOLO:
        return True
    # not al/ax/eax etc with no consumer (these all sit between jumps as noise)
    if m == 'not':
        return True
    return False

# ============================================================
# Classify helpers reached from the main function.
# ============================================================
helpers = [0x100508BD,0x10050A47,0x1075606A,0x1060F35B,0x10755FF8,
           0x10756085,0x1060F532,0x10756108,0x1075613F,0x1060F4CF]
print("=== helper classification ===")
for h in helpers:
    br, v, c, s = is_bridge(h)
    rets = [e for e in v if idc.print_insn_mnem(e) in ('retn','ret','retf')]
    tgts = sorted(set(c.values()))
    print('%08X %-14s bridge=%-5s insns=%3d rets=%d calls=%s' % (
        h, idc.get_func_name(h), str(br), len(v), len(rets),
        [hex(x) for x in tgts]))

# ============================================================
# FULL DEOBFUSCATION: inline-expand bridge "calls", drop junk,
# emit a clean linear listing of the real semantics.
# ============================================================
print("\n\n=== FULL DEOBFUSCATED TRACE of 0x10050A9A ===")

MAIN = 0x10050A9A
emitted = []           # list of (depth, ea, text)
global_seen = set()    # avoid infinite recursion across the whole expansion

# Cache bridge-ness so we don't re-trace constantly
_bridge_cache = {}
def cached_is_bridge(t):
    if t not in _bridge_cache:
        v, r, c, s = trace_real(t, limit=400)
        has_ret = any(idc.print_insn_mnem(e) in ('ret','retn','retf','iret') for e in v)
        # A real bridge is SMALL and has no ret (pure forwarder). If it hit the
        # trace limit (>=400) it's actually a big real function, not a bridge.
        small = len(v) < 60
        _bridge_cache[t] = ((not has_ret) and small)
    return _bridge_cache[t]

def expand(start, depth, callstack):
    if depth > 12:
        emitted.append((depth, start, '... [max depth]'))
        return
    visited = set()
    stack = [start]
    while stack:
        ea = stack.pop()
        while ea != idaapi.BADADDR and ea not in visited:
            visited.add(ea)
            key = (depth, ea)
            ln = idc.create_insn(ea)
            if ln == 0:
                ln = idc.get_item_size(ea)
            m = idc.print_insn_mnem(ea)
            nxt = ea + (ln if ln > 0 else 1)
            if m == 'jmp':
                tgt = idc.get_operand_value(ea, 0)
                ea = tgt if tgt != idaapi.BADADDR else idaapi.BADADDR
                continue
            if is_junk(ea):
                ea = nxt
                continue
            if m == 'call':
                tgt = idc.get_operand_value(ea, 0)
                if cached_is_bridge(tgt) and tgt not in callstack:
                    # inline-expand the bridge
                    emitted.append((depth, ea, '; --- inline %08X (%s) ---' % (tgt, idc.get_func_name(tgt) or '')))
                    expand(tgt, depth + 1, callstack | {tgt})
                    ea = nxt
                    continue
                else:
                    nm = idc.get_func_name(tgt) or idc.get_name(tgt) or ''
                    emitted.append((depth, ea, 'CALL %08X %s' % (tgt, nm)))
                    ea = nxt
                    continue
            if m in ('ret', 'retn', 'retf', 'iret'):
                emitted.append((depth, ea, idc.GetDisasm(ea)))
                ea = idaapi.BADADDR
                continue
            if is_jcc(m):
                tgt = idc.get_operand_value(ea, 0)
                emitted.append((depth, ea, '%s -> %08X (else fallthrough)' % (m, tgt)))
                if tgt != idaapi.BADADDR:
                    stack.append(tgt)
                ea = nxt
                continue
            # real op
            emitted.append((depth, ea, idc.GetDisasm(ea)))
            ea = nxt

MAX_LINES = 4000
def expand2(start):
    # iterative, single global visited, bounded
    work = [(start, 0, frozenset([start]))]
    gseen = set()
    while work and len(emitted) < MAX_LINES:
        start_ea, depth, cs = work.pop()
        visited = set()
        stack = [start_ea]
        while stack and len(emitted) < MAX_LINES:
            ea = stack.pop()
            while ea != idaapi.BADADDR and ea not in visited and len(emitted) < MAX_LINES:
                visited.add(ea)
                if (depth, ea) in gseen:
                    break
                gseen.add((depth, ea))
                ln = idc.create_insn(ea)
                if ln == 0:
                    ln = idc.get_item_size(ea)
                m = idc.print_insn_mnem(ea)
                nxt = ea + (ln if ln > 0 else 1)
                if m == 'jmp':
                    tgt = idc.get_operand_value(ea, 0)
                    ea = tgt if tgt != idaapi.BADADDR else idaapi.BADADDR
                    continue
                if is_junk(ea):
                    ea = nxt; continue
                if m == 'call':
                    tgt = idc.get_operand_value(ea, 0)
                    if depth < 8 and cached_is_bridge(tgt) and tgt not in cs:
                        emitted.append((depth, ea, '; --- inline %08X ---' % tgt))
                        work.append((tgt, depth + 1, cs | {tgt}))
                    else:
                        nm = idc.get_func_name(tgt) or idc.get_name(tgt) or ''
                        emitted.append((depth, ea, 'CALL %08X %s' % (tgt, nm)))
                    ea = nxt; continue
                if m in ('ret', 'retn', 'retf', 'iret'):
                    emitted.append((depth, ea, idc.GetDisasm(ea)))
                    ea = idaapi.BADADDR; continue
                if is_jcc(m):
                    tgt = idc.get_operand_value(ea, 0)
                    emitted.append((depth, ea, '%s -> %08X' % (m, tgt)))
                    if tgt != idaapi.BADADDR:
                        stack.append(tgt)
                    ea = nxt; continue
                emitted.append((depth, ea, idc.GetDisasm(ea)))
                ea = nxt

expand2(MAIN)
outp = r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\deob_trace.txt"
with open(outp, 'w') as fh:
    for depth, ea, text in emitted:
        fh.write('%08X  %s%s\n' % (ea, '  ' * depth, text))
print('[total emitted lines]', len(emitted), '-> written to deob_trace.txt')
