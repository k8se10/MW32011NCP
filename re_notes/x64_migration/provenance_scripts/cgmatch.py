# Call-graph propagation matcher SP->MP. Seeds: signature pairs + unique identical string sets. Then pair callees by call order.
import pefile,capstone,struct,bisect,pickle,re,sys,collections,difflib
def load(path):
    pe=pefile.PE(path); b=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
    ents=[(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData) for e in pe.DIRECTORY_ENTRY_EXCEPTION]; st=[e[0] for e in ents]
    def parent(rva):
        for _ in range(16):
            i=bisect.bisect_right(st,rva)-1
            if i<0 or not(ents[i][0]<=rva<ents[i][1]): return None
            e=ents[i]; ui=e[2]; vf=img[ui]; cnt=img[ui+2]
            if not (vf>>3)&4: return e[0]
            rva=struct.unpack_from('<I',img,ui+4+((cnt+1)&~1)*2)[0]
        return rva
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    calls=collections.defaultdict(list)  # parent rva -> [(site, target rva)]
    for (s,e,u) in ents:
        p=parent(s)
        for ins in md.disasm(bytes(img[s:e]),b+s):
            if ins.mnemonic in ('call','jmp') and ins.op_str.startswith('0x'):
                t=int(ins.op_str,16)-b
                tp=parent(t) if 0<t<len(img) else None
                if tp is not None and tp!=p and (ins.mnemonic=='call' or tp==t):
                    calls[p].append((ins.address,tp))
    for p in calls: calls[p].sort()
    return b,{p:[t for _,t in v] for p,v in calls.items()}
bs,CS=load('new/iw5sp.exe'); bm,CM=load('new/iw5mp.exe')
FS=pickle.load(open('feat_sp.pkl','rb'))['feats']; FM=pickle.load(open('feat_mp.pkl','rb'))['feats']
pairs={}
# seeds: signature table
for l in open(sys.argv[1]):
    m=re.findall(r'\(fn (0x[0-9a-f]+)\)',l)
    if len(m)==2: pairs[int(m[0],16)-bs]=int(m[1],16)-bm
# seeds: unique identical string sets (>=2 strings)
def key(f): return frozenset(f['str'])
ks=collections.Counter(key(f) for f in FS.values() if len(f['str'])>=2)
km=collections.Counter(key(f) for f in FM.values() if len(f['str'])>=2)
mindex={key(f):k for k,f in FM.items() if len(f['str'])>=2 and km[key(f)]==1}
for k,f in FS.items():
    kk=key(f)
    if len(f['str'])>=2 and ks[kk]==1 and kk in mindex: pairs.setdefault(k,mindex[kk])
print('seeds',len(pairs))
used=set(pairs.values())
for rnd in range(30):
    new=0
    for s,m in list(pairs.items()):
        a=CS.get(s,[]); b_=CM.get(m,[])
        if not a or not b_: continue
        if len(a)==len(b_):
            cand=list(zip(a,b_))
        else:
            # align by tokens: mapped callees must line up; unmapped use a coarse size bucket
            inv={v:k for k,v in pairs.items()}
            ta=[('P',x) if x in pairs else ('S',(FS.get(x) or {'size':0})['size']//64) for x in a]
            tb=[('P',inv[y]) if y in inv else ('S',(FM.get(y) or {'size':0})['size']//64) for y in b_]
            sm=difflib.SequenceMatcher(None,ta,tb,autojunk=False)
            cand=[]
            for tag,i1,i2,j1,j2 in sm.get_opcodes():
                if tag=='equal' or (tag=='replace' and i2-i1==j2-j1):
                    cand+=list(zip(a[i1:i2],b_[j1:j2]))
        for x,y in cand:
            if x not in pairs and y not in used:
                fx,fy=FS.get(x),FM.get(y)
                if fx and fy and min(fx['size'],fy['size'])/max(fx['size'],fy['size'],1)>0.6:
                    pairs[x]=y; used.add(y); new+=1
    # callers: pair lone unmatched callers, and align caller lists sorted by address
    RS=collections.defaultdict(list); RM=collections.defaultdict(list)
    for p,v in CS.items():
        for t in set(v): RS[t].append(p)
    for p,v in CM.items():
        for t in set(v): RM[t].append(p)
    for s_,m_ in list(pairs.items()):
        ua=sorted(x for x in RS.get(s_,[]) if x not in pairs); ub=sorted(y for y in RM.get(m_,[]) if y not in used)
        if len(ua)==len(ub) and 0<len(ua)<=3:
            for x,y in zip(ua,ub):
                fx,fy=FS.get(x),FM.get(y)
                if fx and fy and min(fx['size'],fy['size'])/max(fx['size'],fy['size'],1)>0.5:
                    pairs[x]=y; used.add(y); new+=1
    print('round',rnd,'new',new,'total',len(pairs))
    if not new: break
pickle.dump({'bs':bs,'bm':bm,'pairs':pairs},open('pairs.pkl','wb'))
for l in open(sys.argv[1]):
    if 'MP 0 hit' in l:
        name=l.split()[0]; fn=int(re.search(r'\(fn (0x[0-9a-f]+)\)',l).group(1),16)-bs
        print(f"{name:44s} SP {hex(bs+fn)} -> MP {hex(bm+pairs[fn]) if fn in pairs else '-'}")
