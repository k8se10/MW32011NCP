# usage: python3 cg32.py <x86.exe> <out.cg> -- x86 direct call/jmp edge list (site, kind, target).
# Linear-sweep x86 .text and record direct call/jmp edges: "site<TAB>target".
import pefile, capstone, sys
pe = pefile.PE(sys.argv[1]); b = pe.OPTIONAL_HEADER.ImageBase
t = next(s for s in pe.sections if s.Name.startswith(b'.text')); d = t.get_data(); va = b + t.VirtualAddress
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.skipdata = True
with open(sys.argv[2], 'w') as out:
    for i in md.disasm(d, va):
        if i.mnemonic in ('call', 'jmp') and i.op_str.startswith('0x'):
            out.write('%x\t%s\t%s\n' % (i.address, i.mnemonic, i.op_str[2:]))
