# Build per-function feature sets (strings, imm constants, imports, size, callee count) for an x64 PE; pickle them.
import pefile,capstone,struct,bisect,pickle,sys,re
path,out=sys.argv[1],sys.argv[2]
pe=pefile.PE(path); b=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
imp={}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports:
        if i.name: imp[i.address]=i.name.decode()
ents=[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
def parent(rva):
    for _ in range(16):
        e=ents[bisect.bisect_right(st,rva)-1]
        ui=e[2]; vf=img[ui]; cnt=img[ui+2]
        if not (vf>>3)&4: return e[0]
        rva=struct.unpack_from('<I',img,ui+4+((cnt+1)&~1)*2)[0]
    return rva
st=[e[0] for e in ents]
def cstr(va):
    o=va-b
    if not (0<o<len(img)-1): return None
    e=img.find(b'\0',o,o+120)
    if e<0: return None
    s=img[o:e]
    if len(s)>=4 and all(32<=c<127 or c in (9,10) for c in s): return s.decode()
    return None
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=False
feats={}
for (s,e,u) in ents:
    p=parent(s)
    f=feats.setdefault(p,{'str':set(),'imm':set(),'imp':set(),'size':0,'calls':0})
    f['size']+=e-s
    code=img[s:e]
    for ins in md.disasm(bytes(code),b+s):
        ops=ins.op_str
        m=re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]',ops)
        if m:
            t=ins.address+ins.size+(int(m.group(2),16)*(1 if m.group(1)=='+' else -1))
            if t in imp: f['imp'].add(imp[t])
            else:
                c=cstr(t)
                if c: f['str'].add(c)
        if ins.mnemonic=='call': f['calls']+=1
        for mm in re.finditer(r'(?<![\w\[+-])(0x[0-9a-f]+)(?!\])',ops):
            v=int(mm.group(1),16)
            if 0x100<=v<0x10000000 and not (0x140000000<=v<0x150000000): f['imm'].add(v)
pickle.dump({'base':b,'feats':feats},open(out,'wb'))
print(len(feats))
