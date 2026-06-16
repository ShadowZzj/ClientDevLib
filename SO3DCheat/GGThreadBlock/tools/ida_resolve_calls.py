import idc, idaapi, ida_funcs

def is_jcc(m): return m and m[0]=='j' and m!='jmp'

# Resolve a call target through forwarder/bridge chains until we hit:
#  - an import thunk (name in WS2_32 etc)
#  - a function that actually does work (has >N real insns and is not a pure forwarder)
def first_meaningful(target, maxhops=40):
    seen=set()
    cur=target
    chain=[]
    while cur!=idaapi.BADADDR and cur not in seen and maxhops>0:
        seen.add(cur); maxhops-=1
        nm = idc.get_func_name(cur) or idc.get_name(cur) or ''
        chain.append((cur,nm))
        # import?
        if nm and any(nm.startswith(p) or ('recv' in nm.lower()) for p in ('recv','send','WSA')):
            return cur, nm, chain, 'IMPORT'
        # walk this block, skipping junk, see if it's a pure single-forwarder
        ea=cur; steps=0; real=[]
        tgt_jmp=None; tgt_call=None
        while ea!=idaapi.BADADDR and steps<60:
            steps+=1
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); o0=idc.print_operand(ea,0)
            nx=ea+(ln if ln>0 else 1)
            if m=='jmp':
                tgt_jmp=idc.get_operand_value(ea,0); break
            # junk
            if (m=='lea' and o0=='esp') or m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not') or (m=='push' and o0 in ('dx','di','cx','bx','ax','si','bp')):
                ea=nx; continue
            if m=='call':
                tgt_call=idc.get_operand_value(ea,0)
                real.append((ea,'call',tgt_call)); ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                real.append((ea,'ret',None)); break
            real.append((ea,m,None)); ea=nx
        # classify
        calls=[r for r in real if r[1]=='call']
        nontrivial=[r for r in real if r[1] not in ('ret',)]
        if tgt_jmp is not None and len(nontrivial)==0:
            cur=tgt_jmp; continue                      # pure jmp forwarder
        if len(calls)==1 and len([r for r in nontrivial if r[1]!='call'])==0:
            cur=calls[0][2]; continue                  # tail-call-only forwarder
        # otherwise this block does real work -> stop here
        return cur, nm, chain, 'REAL(%d insns,%d calls)'%(len(real),len(calls))
    return cur, idc.get_func_name(cur) or '', chain, 'STOP'

# The CALL edges in the deobfuscated main flow:
edges = {
 'sub_100508BD@prologue': 0x100508BD,
 'pre1_sub_1060F35B':     0x1060F35B,
 'compute_sub_10755FF8':  0x10755FF8,
 'check_sub_10756085':    0x10756085,
 'final_sub_1060F532':    0x1060F532,
 'epilog_sub_10756108':   0x10756108,
 'failB_sub_1075613F':    0x1075613F,
 'failA_sub_1060F4CF':    0x1060F4CF,
}
for label,t in edges.items():
    cur,nm,chain,kind = first_meaningful(t)
    print('=== %-24s start=%08X' % (label,t))
    print('    chain:', ' -> '.join('%08X(%s)'%(a,n) for a,n in chain))
    print('    LANDS @ %08X %s  [%s]' % (cur,nm,kind))
    print()
