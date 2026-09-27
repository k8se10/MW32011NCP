# Find RIP-relative references to target VAs in an x64 PE and print each referencing
# instruction with its containing function (from .pdata) and optional context.
# usage: python3 xref.py <exe> <hexVA[,hexVA...]> [context_bytes]
import bisect, sys
import capstone, pefile

path = sys.argv[1]
targets = {int(t, 16) for t in sys.argv[2].split(',')}
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 0

pe = pefile.PE(path)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

seen = set()
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    s, t = e.struct.BeginAddress, e.struct.EndAddress
    if s in seen:
        continue
    seen.add(s)
    insns = list(md.disasm(img[s:t], base + s))
    for idx, ins in enumerate(insns):
        for op in ins.operands:
            if op.type != capstone.x86.X86_OP_MEM or op.mem.base != capstone.x86.X86_REG_RIP:
                continue
            tgt = ins.address + ins.size + op.mem.disp
            if tgt not in targets:
                continue
            print('%#x -> %#x  %s %s   [func %#x]' % (ins.address, tgt, ins.mnemonic, ins.op_str, base + s))
            if ctx:
                for c in insns:
                    if ins.address - ctx <= c.address <= ins.address + ctx:
                        print('      %#x  %-6s %s' % (c.address, c.mnemonic, c.op_str))
