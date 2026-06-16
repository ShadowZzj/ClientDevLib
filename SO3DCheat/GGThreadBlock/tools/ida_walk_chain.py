import idc, idaapi

JUNK_M=set(['pushf','pushfd','popf','popfd','pusha','pushad','popa','popad',
            'stc','clc','cmc','std','cld','nop','bswap'])
JUNK_PUSH=set(['dx','di','cx','bx','ax','si','bp'])
ARITH=set(['xor','add','sub','rol','ror','shl','shr','sar','sal','neg',
           'imul','mul','and','or','adc','sbb','not','inc','dec'])

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in JUNK_M: return True
    if m=='push' and o in JUNK_PUSH: return True
    return False

def walk(start, limit=900):
    out=[]; calls=[]; ea=start; visited=set(); steps=0
    while ea!=idaapi.BADADDR and steps<limit:
        steps+=1
        if ea in visited:
            out.append((ea,'; LOOP %08X'%ea)); break
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        if ln<=0: ln=1
        m=idc.print_insn_mnem(ea); nx=ea+ln; dis=idc.GetDisasm(ea)
        if m=='jmp':
            t=idc.get_operand_value(ea,0); ea=t if t!=idaapi.BADADDR else nx; continue
        if is_junk(ea): ea=nx; continue
        if m=='call':
            t=idc.get_operand_value(ea,0)
            calls.append((ea,t)); out.append((ea,'CALL %08X %s'%(t,idc.get_func_name(t) or ''))); ea=nx; continue
        if m in ('ret','retn','retf','iret'): out.append((ea,dis)); break
        if is_jcc(m):
            out.append((ea,'%-26s [T->%08X]'%(dis,idc.get_operand_value(ea,0)))); ea=nx; continue
        tag=''
        if m in ARITH: tag+=' <A>'
        if '[' in dis: tag+=' <M>'
        out.append((ea,dis+tag)); ea=nx
    return out,calls

# walk the decrypt chain entry
res=[]
for start in [0x1060F532, 0x1060F515, 0x100521E3]:
    out,calls=walk(start, 900)
    na=sum(1 for _,d in out if '<A>' in d)
    nm=sum(1 for _,d in out if '<M>' in d)
    res.append('==== %08X  insns=%d arith=%d mem=%d ===='%(start,len(out),na,nm))
    for ea,d in out: res.append('  %08X  %s'%(ea,d))
    res.append('  CALLS: '+', '.join('%08X'%t for _,t in calls))
    res.append('')
txt='\n'.join(res)
op=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\walk_chain.txt"
with open(op,'w') as fh: fh.write(txt)
print(txt[:6500])
