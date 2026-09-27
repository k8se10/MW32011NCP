# usage: python3 sigscan.py <iw5sp.exe> <iw5mp.exe> -- resolve every proxy_d3d9 signature constant in both exes.
# Extract every IDA-style signature string constant from proxy sources and locate it in SP and MP exes.
import re, sys, glob, pefile, bisect
src = {}
for f in glob.glob('/home/user/MW32011NCP/proxy_d3d9/src/*.cpp'):
    t = open(f, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'(k[A-Za-z0-9_]*Signature[A-Za-z0-9_]*)\s*(?:\[\])?\s*=\s*("[0-9A-Fa-f? ]+"(?:\s*"[0-9A-Fa-f? ]+")*)\s*;', t):
        sig = ''.join(re.findall(r'"([^"]*)"', m.group(2))).split()
        src[m.group(1)] = sig
def scan(path):
    pe = pefile.PE(path, fast_load=True); pe.parse_data_directories(directories=[3]); b = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.startswith(b'.text')); d = text.get_data(); tva = b + text.VirtualAddress
    starts = sorted(b + e.struct.BeginAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION)
    out = {}
    for name, sig in src.items():
        rx = re.compile(b''.join(b'.' if x.startswith('?') else re.escape(bytes([int(x, 16)])) for x in sig), re.S)
        hits = [tva + m.start() for m in rx.finditer(d)]
        out[name] = [(h, starts[bisect.bisect_right(starts, h) - 1]) for h in hits[:3]] , len(hits)
    return out
sp, mp = scan(sys.argv[1]), scan(sys.argv[2])
for n in sorted(src):
    fmt = lambda r: ('%d hit' % r[1]) + ('' if not r[0] else ' @%#x (fn %#x)' % r[0][0])
    print('%-44s SP %-34s MP %s' % (n, fmt(sp[n]), fmt(mp[n])))
