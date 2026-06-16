import idc, idaapi
def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not'): return True
    if m=='push' and o in ('dx','di','cx','bx','ax','si','bp'): return True
    return False

# Trace from 0x100509B9 (the bridge after cmp [ebp-4],0Ch) following BOTH jcc branches.
visited=set(); stack=[0x100509B9]; calls={}; succ={}
guard=0
while stack and guard<30000:
    guard+=1
    ea=stack.pop()
    while ea!=idaapi.BADADDR and ea not in visited:
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
        if m=='jmp':
            t=idc.get_operand_value(ea,0); succ[ea]=[t]; ea=t; continue
        if is_jcc(m):
            t=idc.get_operand_value(ea,0); succ[ea]=[t,nx]
            if t!=idaapi.BADADDR: stack.append(t)
            ea=nx; continue
        if m in ('ret','retn','retf','iret'):
            succ[ea]=[]; ea=idaapi.BADADDR; continue
        if m=='call':
            calls[ea]=idc.get_operand_value(ea,0); succ[ea]=[nx]; ea=nx; continue
        succ[ea]=[nx]; ea=nx

print('insns:', len(visited), 'range %08X..%08X'%(min(visited),max(visited)))
for ea in sorted(visited):
    if is_junk(ea): continue
    tag=''
    if ea in calls:
        t=calls[ea]; tag='   ; -> %08X %s'%(t, idc.get_func_name(t) or idc.get_name(t) or '')
    s=succ.get(ea,[])
    print('  %08X  %-40s%s  [%s]'%(ea, idc.GetDisasm(ea), tag, ' '.join('%X'%x for x in s)))
