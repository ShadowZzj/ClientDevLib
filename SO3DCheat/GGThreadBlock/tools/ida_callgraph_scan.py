import idc, idaapi

WS = {0x10107688:'recv',0x101076a0:'recvfrom',0x101076a4:'getpeername'}
# gather import names map (addr-> name) for socket-ish APIs
import_hits = {}

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not'): return True
    if m=='push' and o in ('dx','di','cx','bx','ax','si','bp'): return True
    return False

# BFS over the obfuscated call graph starting at main; follow call targets,
# expand jmp/junk inside each block; record every distinct call target reached.
START=0x10050A9A
visited_blocks=set()
call_targets=set()
queue=[START]
guard=0
while queue and guard<200000:
    guard+=1
    ea=queue.pop()
    if ea in visited_blocks or ea==idaapi.BADADDR: continue
    visited_blocks.add(ea)
    ln=idc.create_insn(ea) or idc.get_item_size(ea)
    m=idc.print_insn_mnem(ea); nx=ea+(ln if ln>0 else 1)
    if m=='jmp':
        queue.append(idc.get_operand_value(ea,0)); continue
    if is_junk(ea):
        queue.append(nx); continue
    if m=='call':
        t=idc.get_operand_value(ea,0)
        call_targets.add(t)
        queue.append(t)       # descend into callee
        queue.append(nx)
        continue
    if m in ('ret','retn','retf','iret'):
        continue
    if is_jcc(m):
        queue.append(idc.get_operand_value(ea,0))
        queue.append(nx); continue
    queue.append(nx)

# Now check: does any reached instruction reference a WS2_32 / crypto import?
# Scan all visited blocks' operands for the import addresses.
socket_refs=[]
crypto_keywords=('crypt','aes','rc4','xor','decrypt','encrypt','blow','des')
named_calls=[]
for t in sorted(call_targets):
    nm = idc.get_func_name(t) or idc.get_name(t) or ''
    named_calls.append((t,nm))

# check operand references to import addrs across visited blocks
import_addrs=set(WS.keys())
# also pull ALL ws2_32 imports dynamically
import ida_nalt, idautils
ws_all={}
nimps=ida_nalt.get_import_module_qty()
def cb(ea,name,ordn):
    if name: ws_all[ea]=name
    return True
for i in range(nimps):
    mod=ida_nalt.get_import_module_name(i) or ''
    ida_nalt.enum_import_names(i, (lambda ea,name,ordn,mod=mod: (ws_all.__setitem__(ea,(mod,name)) if name else None) or True))

ref_hits=[]
for b in visited_blocks:
    for opn in range(3):
        v=idc.get_operand_value(b,opn)
        if v in ws_all:
            ref_hits.append((b,ws_all[v]))
        if v in import_addrs:
            ref_hits.append((b,WS[v]))

print('visited blocks (real insns reached):', len(visited_blocks))
print('distinct call targets:', len(call_targets))
print('\n--- import/API references found in the reachable code ---')
seen=set()
for b,info in ref_hits:
    if info in seen: continue
    seen.add(info)
    print('  %08X -> %s'%(b, info))
if not ref_hits:
    print('  (NONE)')

print('\n--- named (non sub_) call targets ---')
for t,nm in named_calls:
    if nm and not nm.startswith('sub_') and not nm.startswith('loc_') and not nm.startswith('unk_'):
        print('  %08X %s'%(t,nm))
