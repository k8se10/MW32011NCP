# usage: python3 vacalls.py <exe> <ImportName> -- call sites of an import with recovered rcx/rdx/r8/r9 constants.
# For each call to an imported API, print the function, and the constant values loaded into rcx/rdx/r8/r9 shortly before.
import pefile, capstone, sys, re
path, api = sys.argv[1], sys.argv[2].encode()
pe = pefile.PE(path); b = pe.OPTIONAL_HEADER.ImageBase; img = pe.get_memory_mapped_image()
slot = next(i.address for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name == api)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
regs = {'rcx':'addr','ecx':'addr','rdx':'size','edx':'size','r8d':'type','r8':'type','r9d':'prot','r9':'prot'}
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    s, t = e.struct.BeginAddress, e.struct.EndAddress
    ins = list(md.disasm(img[s:t], b + s))
    for k, i in enumerate(ins):
        if i.mnemonic in ('call', 'jmp') and i.operands and i.operands[0].type == capstone.x86.X86_OP_MEM and i.operands[0].mem.base == capstone.x86.X86_REG_RIP and i.address + i.size + i.operands[0].mem.disp == slot:
            seen = {}
            for j in ins[max(0, k - 14):k][::-1]:
                m = re.match(r'(\w+), (.+)$', j.op_str)
                if j.mnemonic in ('mov', 'lea', 'xor') and m and m.group(1) in regs and regs[m.group(1)] not in seen:
                    v = m.group(2)
                    if j.mnemonic == 'xor' and v == m.group(1): v = '0'
                    seen[regs[m.group(1)]] = v
            print('%#x fn %#x  size=%-28s type=%-10s prot=%-6s addr=%s' % (i.address, b + s, seen.get('size', '?'), seen.get('type', '?'), seen.get('prot', '?'), seen.get('addr', '?')))
