import pickle,sys,re
sp=pickle.load(open('feat_sp.pkl','rb')); mp=pickle.load(open('feat_mp.pkl','rb'))
bs,bm=sp['base'],mp['base']; FS,FM=sp['feats'],mp['feats']
def J(a,b):
    return len(a&b)/len(a|b) if (a or b) else 0.0
def score(f,g):
    s=3*J(f['str'],g['str'])+2*J(f['imm'],g['imm'])+1.5*J(f['imp'],g['imp'])
    s+=1.0*(min(f['size'],g['size'])/max(f['size'],g['size'],1))
    s+=0.5*(min(f['calls'],g['calls'])+1)/(max(f['calls'],g['calls'])+1)
    return s
# inverted index on strings/imps to limit candidates
idx={}
for k,g in FM.items():
    for t in g['str']|{('I',x) for x in g['imp']}|{('C',x) for x in g['imm'] if x>0x1000}: idx.setdefault(t,set()).add(k)
rows=[l for l in open(sys.argv[1]) if 'MP 0 hit' in l]
for l in rows:
    name=l.split()[0]; fn=int(re.search(r'\(fn (0x[0-9a-f]+)\)',l).group(1),16)
    f=FS.get(fn-bs)
    if not f: print(name,'no SP feats'); continue
    cand=set()
    for t in f['str']|{('I',x) for x in f['imp']}|{('C',x) for x in f['imm'] if x>0x1000}: cand|=idx.get(t,set())
    if not cand: cand=set(k for k,g in FM.items() if abs(g['size']-f['size'])<=max(16,f['size']//10) and g['calls']==f['calls'])
    best=sorted(((score(f,FM[k]),k) for k in cand),reverse=True)[:3]
    uniq = f"strs={len(f['str'])} imms={len(f['imm'])} size={f['size']}"
    print(f"{name:44s} SP {hex(fn)} [{uniq}] -> " + ", ".join(f"{hex(bm+k)}({s:.2f})" for s,k in best))
