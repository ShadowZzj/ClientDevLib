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

# linear walk: follow real flow, skip jmp/junk, DO NOT branch on jcc (take fallthrough),
# record real insns + call targets. stop after limit.
def walk(start, limit=700):
    out=[]; calls=[]; ea=start; visited=set(); steps=0
    while ea!=idaapi.BADADDR and steps<limit:
        steps+=1
        if ea in visited:
            out.append((ea,'; LOOP back to %08X'%ea)); break
        visited.add(ea)
        ln=idc.create_insn(ea) or idc.get_item_size(ea)
        if ln<=0: ln=1
        m=idc.print_insn_mnem(ea); nx=ea+ln
        dis=idc.GetDisasm(ea)
        if m=='jmp':
            t=idc.get_operand_value(ea,0)
            ea=t if t!=idaapi.BADADDR else nx; continue
        if is_junk(ea):
            ea=nx; continue
        if m=='call':
            t=idc.get_operand_value(ea,0)
            nm=idc.get_func_name(t) or ''
            calls.append((ea,t,nm))
            out.append((ea,'CALL %08X %s'%(t,nm)))
            ea=nx; continue
        if m in ('ret','retn','retf','iret'):
            out.append((ea,dis)); break
        if is_jcc(m):
            t=idc.get_operand_value(ea,0)
            out.append((ea,'%-28s [taken->%08X]'%(dis,t)))
            ea=nx; continue   # fallthrough only
        tag=''
        if m in ARITH: tag+=' <A>'
        if '[' in dis: tag+=' <M>'
        out.append((ea,dis+tag)); ea=nx
    return out,calls

out,calls=walk(0x10050A9A, 700)
res=['linear walk from obf_recv_dispatch, %d insns, %d calls'%(len(out),len(calls))]
for ea,d in out:
    res.append('%08X  %s'%(ea,d))
res.append('--- CALL TARGETS ---')
for ea,t,nm in calls:
    res.append('%08X -> %08X %s'%(ea,t,nm))
txt='\n'.join(res)
op=r"e:\VS_REPO\red-pass\redpass-daemon\ClientDevLib\SO3DCheat\GGThreadBlock\tools\linear_walk.txt"
with open(op,'w') as fh: fh.write(txt)
print(txt[:5000])
