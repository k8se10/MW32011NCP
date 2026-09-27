# usage: python3 cstr.py <exe> <out.json> -- NUL-terminated ASCII strings from .rdata/.data/_RDATA -> {string: VA}.
# Extract clean NUL-terminated ASCII strings (len>=4) from .rdata/.data, with VA.
import pefile, re, sys, json
pat = re.compile(rb'(?<![\x20-\x7e])([\x20-\x7e\t\r\n]{4,})\x00')
def extract(path):
    pe = pefile.PE(path, fast_load=True); base = pe.OPTIONAL_HEADER.ImageBase; out = {}
    for s in pe.sections:
        n = s.Name.rstrip(b'\0').decode()
        if n not in ('.rdata', '.data', '_RDATA'): continue
        d = s.get_data()
        for m in pat.finditer(d):
            t = m.group(1).decode('ascii')
            out.setdefault(t, base + s.VirtualAddress + m.start(1))
    return out
d = extract(sys.argv[1]); json.dump(d, open(sys.argv[2], 'w'))
print(sys.argv[1], len(d))
