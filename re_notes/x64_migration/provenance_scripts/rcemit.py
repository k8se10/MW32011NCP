import pefile,capstone,struct,bisect,re
pe=pefile.PE('new/iw5sp.exe'); b=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
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
out={}
seen=set()
for m in re.finditer(rb'\x00\xe0\xff\xff',data):
    va=None
    for back in range(1,4):
        ins=next(md.disasm(data[m.start()-back:m.start()-back+15],tva+m.start()-back),None)
        if ins and '0xffffe000' in ins.op_str and ins.address+ins.size==tva+m.start()+4: va=ins.address; break
    if va is None or va in seen: continue
    seen.add(va)
    p=parent(va-b)
    if p is None: continue
    imms={}; found=None
    for ins in md.disasm(data[va-tva:va-tva+0x140],va):
        mm=re.match(r'(e[a-ds][xip]|e[sd]i|r\d+d), (0x[0-9a-f]+|\d+)$',ins.op_str)
        if ins.mnemonic=='mov' and mm: imms[mm.group(1)]=int(mm.group(2),0)
        mw=re.match(r'word ptr \[[^\]]+\], (\w+)$',ins.op_str)
        if ins.mnemonic=='mov' and mw:
            r=mw.group(1); r32={'ax':'eax','bx':'ebx','cx':'ecx','dx':'edx','si':'esi','di':'edi','bp':'ebp'}.get(r, r.replace('w','d') if r.startswith('r') else r)
            v=imms.get(r32) if not r.startswith('0x') else int(r,16)
            if v and 1<=v<=25: found=v; break
        if ins.mnemonic=='ret': break
    out.setdefault(b+p,set()).add(found)
for k in sorted(out): print(hex(k), sorted(x for x in out[k] if x) or out[k])
