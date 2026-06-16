import idc, idaapi

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
JUNK_M=set(['pushf','pushfd','popf','popfd','pusha','pushad','popa','popad',
            'stc','clc','cmc','std','cld','nop','bswap','not'])
JUNK_PUSH=set(['dx','di','cx','bx','ax','si','bp'])
ARITH=set(['xor','add','sub','rol','ror','shl','shr','sar','sal','neg',
           'imul','mul','and','or','adc','sbb'])

def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in JUNK_M: return True
    if m=='push' and o in JUNK_PUSH: return True
    return False

# 展开一个函数(跟 jmp 链, 跳 junk), 返回真实指令序列; 记录所有 call 目标
def trace_func(start, limit=4000):
    visited=set(); out=[]; calls=[]
    stack=[start]; guard=0
    while stack and guard<limit:
        guard+=1
        ea=stack.pop()
        while ea!=idaapi.BADADDR and ea not in visited:
            visited.add(ea)
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            dis=idc.GetDisasm(ea)
            if m=='jmp':
                t=idc.get_operand_value(ea,0)
                # jmp 到别的函数 = tail call, 记录但不跨
                ea=t; continue
            if is_junk(ea):
                ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                calls.append(t)
                out.append((ea,'CALL %08X %s'%(t, idc.get_func_name(t) or idc.print_operand(ea,0))))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                out.append((ea,dis)); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                out.append((ea,'%-28s -> %08X'%(dis,t)))
                if t!=idaapi.BADADDR: stack.append(t)
                ea=nx; continue
            tag=''
            if m in ARITH: tag+=' <A>'
            if '[' in dis: tag+=' <M>'
            out.append((ea,dis+tag)); ea=nx
    out.sort()
    return out, calls

res=[]
for fn in [0x1060F532, 0x1060F515, 0x100521E3]:
    out,calls=trace_func(fn)
    res.append('==== %08X (%s)  insns=%d ===='%(fn, idc.get_func_name(fn) or '', len(out)))
    for ea,d in out:
        res.append('  %08X  %s'%(ea,d))
    res.append('   calls-> '+', '.join('%08X'%c for c in calls))
    res.append('')

txt='\n'.join(res)
op=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\trace_chain.txt"
with open(op,'w') as fh: fh.write(txt)
print(txt[:6000])
