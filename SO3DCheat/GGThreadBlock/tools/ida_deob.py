import idc, idaapi, ida_ua, ida_bytes

START = 0x10050A9A
visited = set()
calls = {}      # ea -> target
real = set()    # real instruction EAs
succ = {}       # ea -> list of successors (for CFG)
stack = [START]

def is_jcc(m):
    return m and m[0] == 'j' and m != 'jmp'

while stack:
    ea = stack.pop()
    while ea != idaapi.BADADDR and ea not in visited:
        visited.add(ea)
        real.add(ea)
        ln = idc.create_insn(ea)
        if ln == 0:
            ln = idc.get_item_size(ea)
        mnem = idc.print_insn_mnem(ea)
        nxt = ea + (ln if ln > 0 else 1)
        if mnem == 'jmp':
            tgt = idc.get_operand_value(ea, 0)
            succ[ea] = [tgt]
            if idc.get_operand_type(ea, 0) in (5,6,7):  # mem/imm => indirect, stop
                # near jmp imm is type 7 (o_near)
                pass
            ea = tgt if tgt != idaapi.BADADDR else idaapi.BADADDR
        elif is_jcc(mnem):
            tgt = idc.get_operand_value(ea, 0)
            succ[ea] = [tgt, nxt]
            if tgt != idaapi.BADADDR:
                stack.append(tgt)
            ea = nxt
        elif mnem in ('ret', 'retn', 'retf', 'iret'):
            succ[ea] = []
            ea = idaapi.BADADDR
        elif mnem == 'call':
            tgt = idc.get_operand_value(ea, 0)
            calls[ea] = tgt
            succ[ea] = [nxt]
            ea = nxt
        else:
            succ[ea] = [nxt]
            ea = nxt

# Print real trace sorted by address
out = []
for ea in sorted(real):
    dis = idc.GetDisasm(ea)
    tag = ''
    if ea in calls:
        t = calls[ea]
        nm = idc.get_func_name(t) or idc.get_name(t) or ''
        tag = '   ; -> 0x%X %s' % (t, nm)
    out.append('%08X  %s%s' % (ea, dis, tag))
print('REAL INSTRUCTION COUNT:', len(real))
print('CALLS:', sorted(set((hex(t), idc.get_func_name(t) or idc.get_name(t) or '') for t in calls.values())))
print('\n'.join(out))
