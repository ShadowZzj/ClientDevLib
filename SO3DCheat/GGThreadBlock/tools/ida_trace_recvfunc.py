import idc, idaapi
def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not'): return True
    if m=='push' and o in ('dx','di','cx','bx','ax','si','bp'): return True
    return False

visited=set(); stack=[0x1005097E]; real=[]; calls={}
guard=0
while stack and guard<20000:
    guard+=1
    ea=stack.pop()
    while ea!=idaapi.BADADDR and ea not in visited:
        visited.add(ea); real.append(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
        if m=='jmp':
            ea=idc.get_operand_value(ea,0); continue
        if is_jcc(m):
            t=idc.get_operand_value(ea,0)
            if t!=idaapi.BADADDR: stack.append(t)
            ea=nx; continue
        if m in ('ret','retn','retf','iret'):
            ea=idaapi.BADADDR; continue
        if m=='call':
            calls[ea]=idc.get_operand_value(ea,0); ea=nx; continue
        ea=nx

print('recv-func reachable insns from 0x1005097E:', len(visited))
print('addr range: %08X .. %08X'%(min(visited),max(visited)))
print('\n=== clean listing (junk dropped) ===')
for ea in sorted(visited):
    if is_junk(ea): continue
    tag=''
    if ea in calls:
        t=calls[ea]; tag='   ; -> %08X %s'%(t, idc.get_func_name(t) or idc.get_name(t) or '')
    print('  %08X  %-40s%s'%(ea, idc.GetDisasm(ea), tag))
print('\n=== ret instructions ===')
for ea in sorted(visited):
    if idc.print_insn_mnem(ea) in ('ret','retn','retf','iret'):
        print('  %08X %s'%(ea, idc.GetDisasm(ea)))
