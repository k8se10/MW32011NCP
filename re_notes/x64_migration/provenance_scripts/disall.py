# Linear-sweep disassembly of every .pdata function in an x64 PE.
# usage: python3 disall.py <exe> <out.asm>   (lines: addr<TAB>func<TAB>mnemonic<TAB>operands)
import pefile, capstone, sys
pe = pefile.PE(sys.argv[1]); b = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.skipdata = True
img = pe.get_memory_mapped_image()
seen = set()
with open(sys.argv[2], 'w') as out:
    for e in pe.DIRECTORY_ENTRY_EXCEPTION:
        s, t = e.struct.BeginAddress, e.struct.EndAddress
        if s in seen: continue
        seen.add(s)
        for ins in md.disasm(img[s:t], b + s):
            out.write('%x\t%x\t%s\t%s\n' % (ins.address, b + s, ins.mnemonic, ins.op_str))
