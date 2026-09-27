# usage: python3 parentfn.py <exe> <hexVA>... -- resolve chained .pdata fragments to their primary function.
# Resolve chained .pdata fragments to their primary function via UNW_FLAG_CHAININFO.
import pefile, struct, sys
pe = pefile.PE(sys.argv[1]); b = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
ents = [(e.struct.BeginAddress, e.struct.EndAddress, e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
def parent(rva):
    for _ in range(16):
        e = next((x for x in ents if x[0] <= rva < x[1]), None)
        if not e: return None
        ui = e[2]; ver_flags = img[ui]; cnt = img[ui + 2]
        if not (ver_flags >> 3) & 0x4: return e[0]
        off = ui + 4 + ((cnt + 1) & ~1) * 2
        rva = struct.unpack_from('<I', img, off)[0]
    return rva
for a in sys.argv[2:]:
    p = parent(int(a, 16) - b); print(a, '->', hex(b + p) if p is not None else None)
