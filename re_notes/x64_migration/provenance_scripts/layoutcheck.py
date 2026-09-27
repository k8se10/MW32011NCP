import pickle,re,sys,bisect,collections
d=pickle.load(open('pairs.pkl','rb')); bs,bm,pairs=d['bs'],d['bm'],d['pairs']
FS=pickle.load(open('feat_sp.pkl','rb'))['feats']; FM=pickle.load(open('feat_mp.pkl','rb'))['feats']
mpstarts=set(FM.keys())
# trusted anchors: signature pairs + unique identical string sets (recompute)
trusted={}
for l in open(sys.argv[1]):
    m=re.findall(r'\(fn (0x[0-9a-f]+)\)',l)
    if len(m)==2: trusted[int(m[0],16)-bs]=int(m[1],16)-bm
def key(f): return frozenset(f['str'])
ks=collections.Counter(key(f) for f in FS.values() if len(f['str'])>=2)
km=collections.Counter(key(f) for f in FM.values() if len(f['str'])>=2)
mi={key(f):k for k,f in FM.items() if len(f['str'])>=2 and km[key(f)]==1}
for k,f in FS.items():
    kk=key(f)
    if len(f['str'])>=2 and ks[kk]==1 and kk in mi: trusted.setdefault(k,mi[kk])
ts=sorted(trusted)
def predict(fn):
    i=bisect.bisect_left(ts,fn)
    preds=collections.Counter()
    for j in range(max(0,i-4),min(len(ts),i+4)):
        a=ts[j]; p=trusted[a]+(fn-a)
        if abs(fn-a)<0x8000 and p in mpstarts: preds[p]+=1
    return preds
for l in open(sys.argv[1]):
    if 'MP 0 hit' not in l: continue
    name=l.split()[0]; fn=int(re.search(r'\(fn (0x[0-9a-f]+)\)',l).group(1),16)-bs
    cg=pairs.get(fn); pr=predict(fn)
    best=pr.most_common(2)
    ptxt=", ".join(f"{hex(bm+p)}x{c}" for p,c in best) or '-'
    agree = 'AGREE' if cg is not None and best and best[0][0]==cg else ''
    print(f"{name:44s} SP {hex(bs+fn)}  cg={hex(bm+cg) if cg else '-':12s} layout={ptxt:36s} {agree}")

print('\n--- neighbourhood consistency of call-graph pairs (SP neighbours within +-0x3000 whose MP twin lies within +-0x6000 of the candidate) ---')
ps=sorted(pairs)
def neigh(fn,cand):
    i=bisect.bisect_left(ps,fn); ok=tot=0
    for j in range(max(0,i-8),min(len(ps),i+9)):
        a=ps[j]
        if a==fn or abs(a-fn)>0x3000: continue
        tot+=1
        if abs(pairs[a]-cand)<=0x6000 and ((a<fn)==(pairs[a]<cand)): ok+=1
    return ok,tot
extra={'kCursorDrawDispatcherSignatureX64':0x14030b3b0,'kUiContextAnchorSignature':0x140309270}
for l in open(sys.argv[1]):
    if 'MP 0 hit' not in l: continue
    name=l.split()[0]; fn=int(re.search(r'\(fn (0x[0-9a-f]+)\)',l).group(1),16)-bs
    for label,c in (('cg',pairs.get(fn)),('str',(extra.get(name)-bm) if name in extra else None)):
        if c is None: continue
        ok,tot=neigh(fn,c)
        print(f"{name:44s} {label:3s} {hex(bm+c)}  neighbours {ok}/{tot}")
