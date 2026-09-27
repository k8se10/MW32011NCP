# Find direct E8/E9 rel32 call/jmp sites to target VAs; print containing (.pdata-parent) function.
import pefile, numpy as np, sys, struct
path=sys.argv[1]; targets=[int(t,16) for t in sys.argv[2].split(',')]
pe=pefile.PE(path); base=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
text=next(s for s in pe.sections if s.Name.startswith(b'.text')); data=text.get_data(); tva=base+text.VirtualAddress
a=np.frombuffer(data,dtype=np.uint8); n=len(a)-5
d=(a[1:n+1].astype(np.int64)|(a[2:n+2].astype(np.int64)<<8)|(a[3:n+3].astype(np.int64)<<16)|(a[4:n+4].astype(np.int64)<<24))
d=np.where(d>=2**31,d-2**32,d); dst=tva+np.arange(n)+5+d
ents=[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
import bisect; st=[e[0] for e in ents]
def parent(rva):
    for _ in range(16):
        i=bisect.bisect_right(st,rva)-1
        if i<0 or not(ents[i][0]<=rva<ents[i][1]): return None
        ui=ents[i][2]; vf=img[ui]; cnt=img[ui+2]
        if not (vf>>3)&4: return ents[i][0]
        rva=struct.unpack_from('<I',img,ui+4+((cnt+1)&~1)*2)[0]
    return rva
for t in targets:
    idx=np.nonzero((dst==t)&((a[:n]==0xE8)|(a[:n]==0xE9)))[0]
    print('== callers of',hex(t))
    for i in idx:
        va=tva+int(i); p=parent(va-base)
        print(' ', 'call' if a[i]==0xE8 else 'jmp ', hex(va), 'in', hex(base+p) if p is not None else '?')
