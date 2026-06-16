import idc, idaapi

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
# Only drop clearly-dead stack noise; KEEP push (may be real args)
JUNK_SOLO={'pushf','pushfd','popf','popfd','pusha','pushad','popa','popad',
           'stc','clc','cmc','std','cld','nop','bswap'}
def is_junk(ea):
    m=idc.print_insn_mnem(ea); op0=idc.print_operand(ea,0)
    if m=='lea' and op0=='esp': return True
    if m in JUNK_SOLO: return True
    # 'not al/ax' with throwaway flags
    if m=='not': return True
    # push of a 16-bit reg (push dx / push di) = junk; push dword/mem may be arg
    if m=='push' and op0 in ('dx','di','cx','bx','ax','si','bp'): return True
    return False

# bridge = small, no-ret forwarder
_bc={}
def is_bridge(t):
    if t in _bc: return _bc[t]
    v=set(); st=[t]
    while st:
        e=st.pop()
        while e!=idaapi.BADADDR and e not in v and len(v)<80:
            v.add(e); m=idc.print_insn_mnem(e)
            ln=idc.create_insn(e) or idc.get_item_size(e)
            nx=e+(ln if ln>0 else 1)
            if m=='jmp': e=idc.get_operand_value(e,0); continue
            if m in ('ret','retn','retf','iret'): _bc[t]=False; return False
            if is_jcc(m):
                tt=idc.get_operand_value(e,0)
                if tt!=idaapi.BADADDR: st.append(tt)
                e=nx; continue
            if m=='call': e=nx; continue
            e=nx
    _bc[t]=(len(v)<60); return _bc[t]

emitted=[]
def expand(start,depth,cs):
    if depth>10 or len(emitted)>500: return
    v=set(); st=[start]
    while st:
        ea=st.pop()
        while ea!=idaapi.BADADDR and ea not in v and len(emitted)<500:
            v.add(ea)
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            if m=='jmp': ea=idc.get_operand_value(ea,0); continue
            if is_junk(ea): ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                if depth<8 and is_bridge(t) and t not in cs:
                    emitted.append((depth,ea,'; inline %08X'%t))
                    expand(t,depth+1,cs|{t})
                else:
                    nm=idc.get_func_name(t) or ''
                    emitted.append((depth,ea,'CALL %08X %s'%(t,nm)))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                emitted.append((depth,ea,idc.GetDisasm(ea))); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                emitted.append((depth,ea,'%s -> %08X'%(m,t)))
                if t!=idaapi.BADADDR: st.append(t)
                ea=nx; continue
            emitted.append((depth,ea,idc.GetDisasm(ea))); ea=nx

expand(0x10050A9A,0,frozenset([0x10050A9A]))
for d,ea,t in emitted:
    print('%08X  %s%s'%(ea,'  '*d,t))
print('lines',len(emitted))
