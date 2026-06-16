import idc, idaapi
def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not'): return True
    if m=='push' and o in ('dx','di','cx','bx','ax','si','bp'): return True
    return False

# Unified trace: treat `call loc_*` to a non-ret bridge as JMP (call-as-jmp obf).
# Start at recv loop head; follow everything.
def bridge_rets(t):
    v=set(); st=[t]
    while st:
        e=st.pop()
        while e!=idaapi.BADADDR and e not in v and len(v)<80:
            v.add(e); m=idc.print_insn_mnem(e)
            ln=idc.create_insn(e) or idc.get_item_size(e); nx=e+(ln if ln>0 else 1)
            if m=='jmp': e=idc.get_operand_value(e,0); continue
            if m in ('ret','retn','retf','iret'): return True
            if is_jcc(m):
                tt=idc.get_operand_value(e,0)
                if tt!=idaapi.BADADDR: st.append(tt)
                e=nx; continue
            if m=='call': e=nx; continue
            e=nx
    return False

visited=set(); stack=[0x1005097E]; calls={}; succ={}; kind={}
guard=0
while stack and guard<40000:
    guard+=1
    ea=stack.pop()
    while ea!=idaapi.BADADDR and ea not in visited:
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
        if m=='jmp':
            t=idc.get_operand_value(ea,0); succ[ea]=[t]; kind[ea]='jmp'; ea=t; continue
        if is_jcc(m):
            t=idc.get_operand_value(ea,0); succ[ea]=[t,nx]; kind[ea]='jcc'
            if t!=idaapi.BADADDR: stack.append(t)
            ea=nx; continue
        if m in ('ret','retn','retf','iret'):
            succ[ea]=[]; kind[ea]='ret'; ea=idaapi.BADADDR; continue
        if m=='call':
            t=idc.get_operand_value(ea,0); calls[ea]=t
            # call-as-jmp if target is a non-returning bridge
            if not bridge_rets(t):
                succ[ea]=[t]; kind[ea]='call-jmp'; ea=t; continue
            else:
                succ[ea]=[nx]; kind[ea]='call'; ea=nx; continue
        succ[ea]=[nx]; kind[ea]='insn'; ea=nx

print('unified insns:', len(visited), 'range %08X..%08X'%(min(visited),max(visited)))
print()
for ea in sorted(visited):
    if is_junk(ea): continue
    tag=''
    if ea in calls:
        t=calls[ea]; tag='  ; ->%08X %s [%s]'%(t, idc.get_func_name(t) or idc.get_name(t) or '', kind.get(ea))
    print('  %08X  %-38s%s'%(ea, idc.GetDisasm(ea), tag))
