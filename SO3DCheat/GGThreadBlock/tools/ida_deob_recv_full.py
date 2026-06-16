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

# bridge = small no-ret forwarder (these are inlined CONTROL FLOW, not real calls)
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

def resolve_import(t, maxhops=30):
    seen=set(); cur=t
    while cur!=idaapi.BADADDR and cur not in seen and maxhops>0:
        seen.add(cur); maxhops-=1
        if cur in ws_all: return ws_all[cur]
        ea=cur; jt=None; tail=None; nontrivial=0; steps=0
        while ea!=idaapi.BADADDR and steps<40:
            steps+=1
            if ea in ws_all: return ws_all[ea]
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            if m=='jmp': jt=idc.get_operand_value(ea,0); break
            if is_junk(ea): ea=nx; continue
            if m=='call':
                if tail is None: tail=idc.get_operand_value(ea,0)
                else: nontrivial+=1
                ea=nx; continue
            if m in ('ret','retn','retf','iret'): break
            if is_jcc(m): nontrivial+=1; ea=nx; continue
            nontrivial+=1; ea=nx
        if jt is not None and nontrivial==0 and tail is None: cur=jt; continue
        if tail is not None and nontrivial==0: cur=tail; continue
        return None
    return None

emitted=[]
def expand(start,depth,cs):
    if depth>14 or len(emitted)>600: return
    v=set(); st=[start]
    while st:
        ea=st.pop()
        while ea!=idaapi.BADADDR and ea not in v and len(emitted)<600:
            v.add(ea)
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            if m=='jmp': ea=idc.get_operand_value(ea,0); continue
            if is_junk(ea): ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                imp=resolve_import(t)
                nm=idc.get_func_name(t) or idc.get_name(t) or ''
                if imp:
                    nmimp=imp[1] if isinstance(imp,tuple) else imp
                    emitted.append((depth,ea,'CALL %08X  >>> %s'%(t,nmimp)))
                elif depth<10 and is_bridge(t) and t not in cs:
                    emitted.append((depth,ea,'; inline %08X'%t)); expand(t,depth+1,cs|{t})
                else:
                    emitted.append((depth,ea,'CALL %08X %s'%(t,nm)))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                emitted.append((depth,ea,idc.GetDisasm(ea))); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                emitted.append((depth,ea,'%-26s -> %08X'%(idc.GetDisasm(ea),t)))
                if t!=idaapi.BADADDR: st.append(t)
                ea=nx; continue
            emitted.append((depth,ea,idc.GetDisasm(ea))); ea=nx

expand(0x1005097E,0,frozenset([0x1005097E]))
outp=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\deob_recv_full.txt"
with open(outp,'w') as fh:
    for d,ea,t in emitted:
        fh.write('%08X  %s%s\n'%(ea,'  '*d,t))
print('lines',len(emitted),'-> deob_recv_full.txt')
for d,ea,t in emitted:
    print('%08X  %s%s'%(ea,'  '*d,t))
