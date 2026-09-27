# find instructions that use a given disp32 (image-base-relative addressing), print containing function + insn
import pefile,capstone,sys,struct,bisect
pe=pefile.PE(sys.argv[1]); b=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
text=next(s for s in pe.sections if s.Name.startswith(b'.text')); data=text.get_data(); tva=b+text.VirtualAddress
ents=[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]; st=[e[0] for e in ents]
def parent(rva):
    for _ in range(16):
        i=bisect.bisect_right(st,rva)-1
        if i<0 or not(ents[i][0]<=rva<ents[i][1]): return None
        ui=ents[i][2]; vf=img[ui]; cnt=img[ui+2]
        if not (vf>>3)&4: return ents[i][0]
        rva=struct.unpack_from('<I',img,ui+4+((cnt+1)&~1)*2)[0]
    return rva
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
for t in sys.argv[2:]:
    v=int(t,16); pat=struct.pack('<I',v); off=0
    print('== disp',hex(v))
    while True:
        off=data.find(pat,off)
        if off<0: break
        for back in range(2,9):
            s=off-back
            ins=next(md.disasm(data[s:s+15],tva+s),None)
            if ins and ins.address+ins.size>tva+off+3 and ins.address<=tva+off-1 and hex(v) in ins.op_str:
                p=parent(ins.address-b); print(' ',hex(ins.address),'in',hex(b+p) if p is not None else '?',ins.mnemonic,ins.op_str); break
        off+=1
