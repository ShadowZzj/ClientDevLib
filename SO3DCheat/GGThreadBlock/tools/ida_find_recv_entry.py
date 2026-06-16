import idc, idaapi

# Collect ALL branch/jump targets reachable from 0x10050A9A (full obf trace, no junk filter),
# then report any that land inside the recv-loop region [0x10050960, 0x100509B5].
def is_jcc(m): return m and m[0]=='j' and m!='jmp'
visited=set(); stack=[0x10050A9A]; edges=[]
guard=0
while stack and guard<20000:
    guard+=1
    ea=stack.pop()
    while ea!=idaapi.BADADDR and ea not in visited and guard<20000:
        guard+=1
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
        if m=='jmp':
            t=idc.get_operand_value(ea,0); edges.append((ea,'jmp',t))
            ea=t; continue
        if is_jcc(m):
            t=idc.get_operand_value(ea,0); edges.append((ea,m,t))
            if t!=idaapi.BADADDR: stack.append(t)
            ea=nx; continue
        if m in ('ret','retn','retf','iret'): ea=idaapi.BADADDR; continue
        if m=='call':
            t=idc.get_operand_value(ea,0); edges.append((ea,'call',t))
            ea=nx; continue
        ea=nx

print('total reachable insns:', len(visited))
print('total edges:', len(edges))
LO,HI=0x10050960,0x100509E2
print('\n=== edges that TARGET the recv-loop region [%X,%X) ==='%(LO,HI))
for src,kind,tgt in edges:
    if LO<=tgt<HI:
        print('  %08X  %-5s -> %08X'%(src,kind,tgt))
print('\n=== is 0x1005097E in visited? ===', 0x1005097E in visited)
print('=== is 0x10050989 in visited? ===', 0x10050989 in visited)
# also: list ALL distinct call targets reachable (the real recv etc)
calls=sorted(set(t for s,k,t in edges if k=='call'))
print('\n=== all call targets ===')
for t in calls:
    print('  %08X %s'%(t, idc.get_func_name(t) or idc.get_name(t) or ''))
