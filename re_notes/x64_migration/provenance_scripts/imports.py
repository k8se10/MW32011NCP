# usage: python3 imports.py <old.exe> <new.exe> -- per-DLL import diff.
import pefile, sys
def imps(f):
    pe = pefile.PE(f); out = {}
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        out[d.dll.decode().lower()] = sorted((i.name.decode() if i.name else "ord%d"%i.ordinal) for i in d.imports)
    return out
a, b = imps(sys.argv[1]), imps(sys.argv[2])
print("DLLs old:", sorted(a)); print("DLLs new:", sorted(b))
for dll in sorted(set(a)|set(b)):
    sa, sb = set(a.get(dll,[])), set(b.get(dll,[]))
    if sa-sb: print(f"[{dll}] OLD-only:", sorted(sa-sb))
    if sb-sa: print(f"[{dll}] NEW-only:", sorted(sb-sa))
