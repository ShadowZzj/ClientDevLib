import idc, idaapi

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
JUNK_M=set(['pushf','pushfd','popf','popfd','pusha','pushad','popa','popad',
            'stc','clc','cmc','std','cld','nop','bswap'])
JUNK_PUSH=set(['dx','di','cx','bx','ax','si','bp'])
ARITH=set(['xor','add','sub','rol','ror','shl','shr','sar','sal','neg',
           'imul','mul','and','or','adc','sbb','not'])

def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in JUNK_M: return True
    if m=='push' and o in JUNK_PUSH: return True
    return False

def classify(t, _c={}):
    if t in _c: return _c[t]
    ea=t; steps=0; forward_to=None
    while steps<8 and ea!=idaapi.BADADDR:
        steps+=1
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
        if m=='lea' and o=='esp':
            ea+=ln; continue
        if m=='jmp':
            forward_to=idc.get_operand_value(ea,0); break
        if m in JUNK_M or (m=='push' and o in JUNK_PUSH):
            ea+=ln; continue
        forward_to=None; break
    _c[t]=forward_to
    return forward_to

emitted=[]
def expand(start, depth, callstack):
    if depth>20 or len(emitted)>2500: return
    visited=set(); stack=[start]
    while stack:
        ea=stack.pop()
        while ea!=idaapi.BADADDR and ea not in visited and len(emitted)<2500:
            visited.add(ea)
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
            dis=idc.GetDisasm(ea)
            if m=='jmp':
                ea=idc.get_operand_value(ea,0); continue
            if is_junk(ea):
                ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                fwd=classify(t)
                if fwd is not None and t not in callstack and depth<18:
                    emitted.append((depth,ea,'; inline fwd %08X -> %08X'%(t,fwd)))
                    expand(fwd, depth+1, callstack|set([t]))
                else:
                    nm=idc.get_func_name(t) or ''
                    emitted.append((depth,ea,'CALL %08X %s'%(t,nm)))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                emitted.append((depth,ea,dis)); ea=idaapi.BADADDR; continue
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                emitted.append((depth,ea,'%-26s -> %08X'%(dis,t)))
                if t!=idaapi.BADADDR: stack.append(t)
                ea=nx; continue
            tag=''
            if m in ARITH: tag+=' <A>'
            if '[' in dis: tag+=' <M>'
            emitted.append((depth,ea,dis+tag)); ea=nx

expand(0x100521E3, 0, frozenset([0x100521E3]))
res=['emitted %d'%len(emitted)]
for d,ea,t in emitted:
    res.append('%08X  %s%s'%(ea,'  '*d,t))
txt='\n'.join(res)
op=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\deob_full.txt"
with open(op,'w') as fh: fh.write(txt)
arith=[ '%08X %s'%(ea,t) for d,ea,t in emitted if '<A>' in t or '<M>' in t]
calls=[ '%08X %s'%(ea,t) for d,ea,t in emitted if t.startswith('CALL')]
print('emitted', len(emitted), 'arith/mem', len(arith), 'calls', len(calls))
print('--- CALLS ---')
print('\n'.join(calls[:60]))
