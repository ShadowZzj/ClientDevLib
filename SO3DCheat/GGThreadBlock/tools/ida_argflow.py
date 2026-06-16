import idc, idaapi
def dump(start, n=60, label=''):
    print('====',label or hex(start))
    ea=start; cnt=0
    while cnt<n and ea!=idaapi.BADADDR:
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); dis=idc.GetDisasm(ea)
        print('  %08X  %s'%(ea,dis))
        cnt+=1
        if m in ('ret','retn','retf','iret'): break
        if m=='jmp':
            ea=idc.get_operand_value(ea,0); continue
        ea+=ln if ln>0 else 1
    print()
dump(0x107561B4, 8, 'callsite push+call @107561B4')
dump(0x1060F532, 25, 'sub_1060F532')
dump(0x1060F515, 25, 'sub_1060F515')
dump(0x100521E3, 45, 'sub_100521E3 (real body)')
