import idc, idaapi

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
JUNK_M=set(['pushf','pushfd','popf','popfd','pusha','pushad','popa','popad',
            'stc','clc','cmc','std','cld','nop','bswap','not'])
JUNK_PUSH=set(['dx','di','cx','bx','ax','si','bp'])
ARITH=set(['xor','add','sub','rol','ror','shl','shr','sar','sal','neg',
           'imul','mul','and','or','inc','dec','adc','sbb'])

def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in JUNK_M: return True
    if m=='push' and o in JUNK_PUSH: return True
    return False

visited=set(); out=[]
stack=[0x10050A9A]
guard=0
while stack and guard<8000:
    guard+=1
    ea=stack.pop()
    while ea!=idaapi.BADADDR and ea not in visited:
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
        dis=idc.GetDisasm(ea)
        if m=='jmp':
            t=idc.get_operand_value(ea,0); ea=t; continue
        if is_junk(ea):
            ea=nx; continue
        if m=='call':
            t=idc.get_operand_value(ea,0)
            out.append((ea,'CALL %08X %s'%(t, idc.get_func_name(t) or idc.print_operand(ea,0))))
            ea=nx; continue
        if m in ('ret','retn','retf','iret'):
            out.append((ea,dis)); ea=idaapi.BADADDR; continue
        if is_jcc(m):
            t=idc.get_operand_value(ea,0)
            out.append((ea,'%-30s -> %08X'%(dis,t)))
            if t!=idaapi.BADADDR: stack.append(t)
            ea=nx; continue
        tag=''
        if m in ARITH: tag+=' <ARITH>'
        if '[' in dis: tag+=' <MEM>'
        out.append((ea,dis+tag)); ea=nx

out.sort()
res=[]
res.append('total real insns: %d'%len(out))
for ea,d in out:
    res.append('%08X  %s'%(ea,d))
txt='\n'.join(res)
op=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\trace_dispatch.txt"
with open(op,'w') as fh: fh.write(txt)
print(txt)
