import idc, idaapi
def is_jcc(m): return m and m[0]=='j' and m!='jmp'
visited=set(); stack=[0x10050A9A, 0x1005097E]
guard=0
while stack and guard<40000:
    guard+=1
    ea=stack.pop()
    while ea!=idaapi.BADADDR and ea not in visited:
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
        if m=='jmp': ea=idc.get_operand_value(ea,0); continue
        if is_jcc(m):
            t=idc.get_operand_value(ea,0)
            if t!=idaapi.BADADDR: stack.append(t)
            ea=nx; continue
        if m in ('ret','retn','retf','iret'): ea=idaapi.BADADDR; continue
        if m=='call':
            ea=nx; continue
        ea=nx
hits=[]
for ea in sorted(visited):
    for opn in range(3):
        t=idc.get_operand_type(ea,opn)
        if t in (3,4):
            s=idc.print_operand(ea,opn)
            if 'ebp+' in s:
                hits.append((ea, idc.GetDisasm(ea)))
                break
print('=== references to [ebp+arg] inside recv_dispatch flow ===')
for ea,d in hits:
    print('  %08X  %s'%(ea,d))
