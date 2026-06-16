import idc, idaapi
import ida_nalt

# import addr -> (module,name)
ws_all={}
nimps=ida_nalt.get_import_module_qty()
for i in range(nimps):
    mod=ida_nalt.get_import_module_name(i) or ''
    ida_nalt.enum_import_names(i, (lambda ea,name,ordn,mod=mod: (ws_all.__setitem__(ea,(mod,name)) if name else None) or True))

def is_jcc(m): return m and m[0]=='j' and m!='jmp'
def is_junk(ea):
    m=idc.print_insn_mnem(ea); o=idc.print_operand(ea,0)
    if m=='lea' and o=='esp': return True
    if m in ('pushf','pushfd','popf','popfd','pusha','pushad','popa','popad','stc','clc','cmc','std','cld','nop','bswap','not'): return True
    if m=='push' and o in ('dx','di','cx','bx','ax','si','bp'): return True
    return False

# Trace a call target through forwarder/jmp chains, printing every real insn
# (junk skipped) until we hit a ret, an import jump, or a real multi-insn body.
def resolve(start, maxsteps=400):
    seen=set(); cur=start; out=[]
    while cur!=idaapi.BADADDR and cur not in seen and maxsteps>0:
        seen.add(cur)
        ea=cur; cur=idaapi.BADADDR
        body=[]
        while ea!=idaapi.BADADDR and maxsteps>0:
            maxsteps-=1
            # import jump?
            if ea in ws_all:
                out.append(('IMPORT', ea, ws_all[ea])); return out
            ln=idc.create_insn(ea) or idc.get_item_size(ea)
            m=idc.print_insn_mnem(ea); o0=idc.print_operand(ea,0); nx=ea+(ln if ln>0 else 1)
            if m=='jmp':
                jt=idc.get_operand_value(ea,0)
                # jmp to import thunk?
                if jt in ws_all:
                    out.append(('IMPORT', jt, ws_all[jt])); return out
                # is it indirect jmp [import]?
                opv=idc.get_operand_value(ea,0)
                ot=idc.get_operand_type(ea,0)
                if jt in ws_all:
                    out.append(('IMPORT', jt, ws_all[jt])); return out
                cur=jt; break
            if is_junk(ea): ea=nx; continue
            if m=='call':
                t=idc.get_operand_value(ea,0)
                nm=idc.get_func_name(t) or idc.get_name(t) or ''
                body.append((ea,'call %08X %s'%(t,nm)))
                out.append(('CALL',ea,(t,nm)))
                ea=nx; continue
            if m in ('ret','retn','retf','iret'):
                out.append(('RET',ea,idc.GetDisasm(ea))); return out
            if is_jcc(m):
                t=idc.get_operand_value(ea,0)
                out.append(('JCC',ea,idc.GetDisasm(ea)))
                ea=nx; continue
            out.append(('INSN',ea,idc.GetDisasm(ea)))
            ea=nx
    return out

print('=== resolve call target 0x1060F2DD (from +50984) ===')
for kind,ea,info in resolve(0x1060F2DD):
    print('  [%-6s] %08X  %s'%(kind,ea,info))
