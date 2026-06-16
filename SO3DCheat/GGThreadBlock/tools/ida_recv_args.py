import idc, idaapi
def dump_range(lo,hi):
    ea=lo
    while ea<hi:
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        print('  %08X  %s'%(ea, idc.GetDisasm(ea)))
        ea+=ln if ln>0 else 1
print('=== recv loop setup region 1005097E..100509B5 ===')
dump_range(0x1005097E,0x100509B5)
print()
print('=== thunk chain from 1060F2DD ===')
ea=0x1060F2DD; cnt=0
while cnt<50:
    ln=idc.create_insn(ea) or idc.get_item_size(ea)
    m=idc.print_insn_mnem(ea)
    print('  %08X  %s'%(ea, idc.GetDisasm(ea)))
    cnt+=1
    if m in ('ret','retn','retf','iret'): break
    if m=='jmp':
        ea=idc.get_operand_value(ea,0); continue
    ea+=ln if ln>0 else 1
