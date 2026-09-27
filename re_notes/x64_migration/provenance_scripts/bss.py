# usage: python3 bss.py <exe> [N] -- estimate largest static .data arrays from reference-target gaps.
# Estimate the largest static arrays in .data by gaps between distinct RIP-relative reference targets.
import pefile, capstone, sys, bisect
pe = pefile.PE(sys.argv[1]); b = pe.OPTIONAL_HEADER.ImageBase; img = pe.get_memory_mapped_image()
data = next(s for s in pe.sections if s.Name.startswith(b'.data'))
lo, hi = b + data.VirtualAddress, b + data.VirtualAddress + data.Misc_VirtualSize
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
refs = {}
for e in pe.DIRECTORY_ENTRY_EXCEPTION:
    s, t = e.struct.BeginAddress, e.struct.EndAddress
    for ins in md.disasm(img[s:t], b + s):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                tg = ins.address + ins.size + op.mem.disp
                if lo <= tg < hi: refs.setdefault(tg, set()).add(b + s)
addrs = sorted(refs) + [hi]
gaps = sorted(((addrs[i+1] - addrs[i], addrs[i]) for i in range(len(addrs) - 1)), reverse=True)
print('.data VA %#x size %.1f MB (raw %.1f MB), %d distinct targets' % (lo, (hi-lo)/2**20, data.SizeOfRawData/2**20, len(refs)))
tot = 0
for g, a in gaps[:int(sys.argv[2]) if len(sys.argv) > 2 else 25]:
    tot += g
    print('  %#x  %8.2f MB   refs from %s' % (a, g / 2**20, ', '.join(hex(x) for x in sorted(refs[a])[:4])))
print('top-N total %.1f MB' % (tot / 2**20))
