import idc, idaapi

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
JUNK_SOLO={'pushf','pushfd','popf','popfd','pusha','pushad','popa','popad',
           'stc','clc','cmc','std','cld','nop','bswap'}
def is_junk(ea):
    m=idc.print_insn_mnem(ea); op0=idc.print_operand(ea,0)
    if m=='lea' and op0=='esp': return True
    if m=='push': return True
    if m in JUNK_SOLO: return True
    if m=='not': return True
    return False

def clean_trace(start, limit=400):
    visited=set(); out=[]; stack=[start]
    while stack:
        ea=stack.pop()
        while ea!=idaapi.BADADDR and ea not in visited and len(visited)<limit:
            visited.add(ea)
            ln=idc.create_insn(ea)
            if ln==0: ln=idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nxt=ea+(ln if ln>0 else 1)
            if m=='jmp':
                ea=idc.get_operand_value(ea,0); 
                if ea==idaapi.BADADDR: ea=idaapi.BADADDR
                continue
            if is_junk(ea):
                ea=nxt; continue
            if m=='call':
                t=idc.get_operand_value(ea,0); nm=idc.get_func_name(t) or ''
                out.append((ea,'CALL %08X %s'%(t,nm))); ea=nxt; continue
            if m in ('ret','retn','retf','iret'):
                out.append((ea, idc.GetDisasm(ea))); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                out.append((ea,'%s -> %08X'%(m,t)))
                if t!=idaapi.BADADDR: stack.append(t)
                ea=nxt; continue
            out.append((ea, idc.GetDisasm(ea))); ea=nxt
    return out

for h in [0x10050AFA, 0x10755FF8, 0x1060F38D, 0x10756085, 0x1075613F, 0x10756108]:
    print('==== %08X %s ===='%(h, idc.get_func_name(h) or ''))
    for ea,txt in clean_trace(h):
        print('   %08X  %s'%(ea,txt))
    print()
