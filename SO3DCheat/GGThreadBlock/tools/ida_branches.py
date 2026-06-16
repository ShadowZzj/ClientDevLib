import idc, idaapi
def dump(start, n=40, follow=True):
    ea=start; cnt=0
    while cnt<n and ea!=idaapi.BADADDR:
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea)
        dis=idc.GetDisasm(ea)
        t=''
        if m=='call' or m=='jmp' or (m and m[0]=='j'):
            tt=idc.get_operand_value(ea,0)
            nm=idc.get_func_name(tt) or idc.get_name(tt) or ''
            t='   -> %08X %s'%(tt,nm)
        print('  %08X  %-38s%s'%(ea,dis,t))
        cnt+=1
        if m in ('ret','retn','retf','iret'): break
        ea+=ln if ln>0 else 1

for lbl,a in [('<12 branch 0x10050866',0x10050866),
              ('>=12, cmp 0xFA000 @0x100509FD',0x100509FD),
              ('sub_100509E7 (called @0x10050A04)',0x100509E7)]:
    print('====',lbl)
    dump(a, 30)
    print()
