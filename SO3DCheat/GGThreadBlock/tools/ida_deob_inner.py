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

# A bridge is a SMALL no-ret forwarder used as control flow. But many "process"
# functions here are real. We inline only tiny (<55 insn) no-ret stubs.
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

# Resolve a call target to a final REAL function (skip pure forwarders), so we
# can report the address the user should breakpoint.
def resolve_real(t, maxhops=40):
    seen=set(); cur=t; chain=[t]
    while cur!=idaapi.BADADDR and cur not in seen and maxhops>0:
        seen.add(cur); maxhops-=1
        if cur in ws_all: return cur, ws_all[cur], chain
        ea=cur; jt=None; tail=None; nontrivial=0; steps=0
        while ea!=idaapi.BADADDR and steps<60:
            steps+=1
            if ea in ws_all: return ea, ws_all[ea], chain
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
        if jt is not None and nontrivial==0 and tail is None:
            cur=jt; chain.append(cur); continue
        if tail is not None and nontrivial==0:
            cur=tail; chain.append(cur); continue
        return cur, None, chain  # real body
    return cur, None, chain

emitted=[]
def expand(start,depth,cs):
    if depth>16 or len(emitted)>500: return
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
                real, imp, chain = resolve_real(t)
                if imp:
                    emitted.append((depth,ea,'CALL >>> IMPORT %s  (target chain %s)'%(imp[1], '->'.join('%X'%c for c in chain))))
                elif depth<12 and is_bridge(t) and t not in cs:
                    emitted.append((depth,ea,'; inline %08X'%t)); expand(t,depth+1,cs|{t})
                else:
                    nm=idc.get_func_name(real) or idc.get_name(real) or ''
                    emitted.append((depth,ea,'CALL real=%08X %s   (via %X)'%(real,nm,t)))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                emitted.append((depth,ea,idc.GetDisasm(ea))); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                emitted.append((depth,ea,'%-24s -> %08X'%(idc.GetDisasm(ea),t)))
                if t!=idaapi.BADADDR: st.append(t)
                ea=nx; continue
            emitted.append((depth,ea,idc.GetDisasm(ea))); ea=nx

expand(0x100521E3,0,frozenset([0x100521E3]))
out=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\deob_inner.txt"
with open(out,'w') as fh:
    for d,ea,t in emitted:
        fh.write('%08X  %s%s\n'%(ea,'  '*d,t))
print('lines',len(emitted),'-> deob_inner.txt')
for d,ea,t in emitted:
    print('%08X  %s%s'%(ea,'  '*d,t))
