import idc, idaapi
import ida_nalt
ws_all={}
for i in range(ida_nalt.get_import_module_qty()):
    mod=ida_nalt.get_import_module_name(i) or ''
    ida_nalt.enum_import_names(i,(lambda ea,name,ordn,mod=mod:(ws_all.__setitem__(ea,(mod,name)) if name else None) or True))
def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not'): return True
    if m=='push' and o in ('dx','di','cx','bx','ax','si','bp'): return True
    return False
_bc={}
def is_bridge(t):
    if t in _bc: return _bc[t]
    v=set(); st=[t]
    while st:
        e=st.pop()
        while e!=idaapi.BADADDR and e not in v and len(v)<60:
            v.add(e); m=idc.print_insn_mnem(e)
            ln=idc.create_insn(e) or idc.get_item_size(e); nx=e+(ln if ln>0 else 1)
            if m=='jmp': e=idc.get_operand_value(e,0); continue
            if m in ('ret','retn','retf','iret'): _bc[t]=False; return False
            if is_jcc(m):
                tt=idc.get_operand_value(e,0)
                if tt!=idaapi.BADADDR: st.append(tt)
                e=nx; continue
            if m=='call': e=nx; continue
            e=nx
    _bc[t]=(len(v)<55); return _bc[t]
emitted=[]
def expand(start,depth,cs):
    if depth>12 or len(emitted)>400: return
    v=set(); st=[start]
    while st:
        ea=st.pop()
        while ea!=idaapi.BADADDR and ea not in v and len(emitted)<400:
            v.add(ea)
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            if m=='jmp': ea=idc.get_operand_value(ea,0); continue
            if is_junk(ea): ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                nm=idc.get_func_name(t) or idc.get_name(t) or ''
                if t in ws_all: emitted.append((depth,ea,'CALL >>> %s'%(ws_all[t][1])))
                elif depth<9 and is_bridge(t) and t not in cs:
                    emitted.append((depth,ea,'; inline %08X'%t)); expand(t,depth+1,cs|{t})
                else: emitted.append((depth,ea,'CALL %08X %s'%(t,nm)))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                emitted.append((depth,ea,idc.GetDisasm(ea))); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                emitted.append((depth,ea,'%-26s -> %08X'%(idc.GetDisasm(ea),t)))
                if t!=idaapi.BADADDR: st.append(t)
                ea=nx; continue
            emitted.append((depth,ea,idc.GetDisasm(ea))); ea=nx
for tgt,lbl in [(0x10756085,'sub_10756085 (data check)'),(0x1060F532,'sub_1060F532 (final process)')]:
    emitted=[]
    expand(tgt,0,frozenset([tgt]))
    print('====',lbl,'====')
    for d,ea,t in emitted:
        print('%08X  %s%s'%(ea,'  '*d,t))
    print()
