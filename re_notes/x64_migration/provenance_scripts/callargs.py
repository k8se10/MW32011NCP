# usage: python3 callargs.py <exe> <hexTarget[,..]> -- direct callers of a function with recovered argument constants.
# For each direct call to target function(s), print the constant args loaded into rcx/rdx/r8/r9 just before.
import pefile, capstone, sys, re
path = sys.argv[1]; targets = {int(x, 16) for x in sys.argv[2].split(',')}
pe = pefile.PE(path); b = pe.OPTIONAL_HEADER.ImageBase; img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
names = {'rcx':'a1','ecx':'a1','cl':'a1','rdx':'a2','edx':'a2','dl':'a2','r8':'a3','r8d':'a3','r9':'a4','r9d':'a4'}
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    s, t = e.struct.BeginAddress, e.struct.EndAddress
    ins = list(md.disasm(img[s:t], b + s))
    for k, i in enumerate(ins):
        if i.mnemonic in ('call', 'jmp') and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM and i.operands[0].imm in targets:
            seen = {}
            for j in ins[max(0, k - 12):k][::-1]:
                if j.mnemonic == 'call': break
                m = re.match(r'(\w+), (.+)$', j.op_str)
                if j.mnemonic in ('mov', 'lea', 'xor', 'movzx') and m and m.group(1) in names and names[m.group(1)] not in seen:
                    v = m.group(2)
                    if j.mnemonic == 'xor' and v == m.group(1): v = '0'
                    seen[names[m.group(1)]] = v
            print('%#x fn %#x -> %#x  %s' % (i.address, b + s, i.operands[0].imm, '  '.join('%s=%s' % (a, seen[a]) for a in ('a1','a2','a3','a4') if a in seen)))
