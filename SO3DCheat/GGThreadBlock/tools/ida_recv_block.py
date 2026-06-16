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

emitted=[]
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

def expand(start,depth,cs):
    if depth>8 or len(emitted)>250: return
    v=set(); st=[start]
    while st:
        ea=st.pop()
        while ea!=idaapi.BADADDR and ea not in v and len(emitted)<250:
            v.add(ea)
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            if m=='jmp': ea=idc.get_operand_value(ea,0); continue
            if is_junk(ea): ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                # recv thunk?
                nm=idc.get_func_name(t) or idc.get_name(t) or ''
                if depth<6 and is_bridge(t) and t not in cs:
                    emitted.append((depth,ea,'; inline %08X'%t)); expand(t,depth+1,cs|{t})
                else:
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

# Start from the function prologue that owns +50984. Find it by scanning back for 'push ebp;mov ebp,esp'
# We know the real entry is 0x10050A9A (push ebp...). But recv call is at 0x1005097F which is BEFORE.
# Likely the true entry differs. Just expand from 0x10050A9A again but ALSO dump around the recv block linearly.
print('=== linear region 0x10050979..0x100509C0 (the recv+len-check block) ===')
ea=0x10050979
while ea<0x100509E2:
    ln=idc.create_insn(ea) or idc.get_item_size(ea)
    print('  %08X  %s'%(ea, idc.GetDisasm(ea)))
    ea+=ln if ln>0 else 1

print('\n=== whoami: which IDA func contains 0x1005097F ? ===')
f=idaapi.get_func(0x1005097F)
print('  func=', hex(f.start_ea) if f else None, '-', hex(f.end_ea) if f else None, idc.get_func_name(0x1005097F))

print('\n=== global 0x105C0004 (pushed as recv fn-ptr arg) ===')
print('  name=', idc.get_name(0x105C0004))
print('  value=', hex(idc.get_wide_dword(0x105C0004)))
print('  seg=', idc.get_segm_name(0x105C0004))
