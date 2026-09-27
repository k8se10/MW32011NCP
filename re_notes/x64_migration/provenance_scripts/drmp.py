# usage: python3 drmp.py <exe> <out.dll> -- decrypt the SteamDRMP.dll payload for LOCAL analysis only (never commit the output).
# Extract and decrypt the SteamDRMP.dll embedded in a SteamStub 3.1 x64 .bind section (XTEA-CBC-style, per Steamless).
import pefile, struct, sys
M = 0xFFFFFFFF
def xor_dec(buf):
    out = bytearray(buf); key = struct.unpack_from('<I', buf, 0)[0]
    for i in range(4, len(buf) - len(buf) % 4, 4):
        v = struct.unpack_from('<I', buf, i)[0]; struct.pack_into('<I', out, i, v ^ key); key = v
    return bytes(out)
def xtea_dec(r1, r2, k, n=32):
    delta = 0x9E3779B9; s = (delta * n) & M
    for _ in range(n):
        r2 = (r2 - ((((r1 << 4) ^ (r1 >> 5)) + r1) ^ (s + k[(s >> 11) & 3]))) & M
        s = (s - delta) & M
        r1 = (r1 - ((((r2 << 4) ^ (r2 >> 5)) + r2) ^ (s + k[s & 3]))) & M
    return r1, r2
path, outp = sys.argv[1], sys.argv[2]
pe = pefile.PE(path, fast_load=True); raw = open(path, 'rb').read()
ep = pe.OPTIONAL_HEADER.AddressOfEntryPoint; epoff = pe.get_offset_from_rva(ep)
h = xor_dec(raw[epoff - 0xF0:epoff])
drmp_off, drmp_size = struct.unpack_from('<II', h, 0x30)
keys = list(struct.unpack_from('<4I', h, 0x98))
bind = next(s for s in pe.sections if s.Name.startswith(b'.bind'))
for label, base in (('bind raw', bind.PointerToRawData), ('ep-0x310', epoff - 0x310)):
    enc = raw[base + drmp_off: base + drmp_off + drmp_size]
    out = bytearray(len(enc)); v1 = v2 = 0x55555555
    for x in range(0, len(enc) - 7, 8):
        d1, d2 = struct.unpack_from('<II', enc, x)
        n1, n2 = xtea_dec(d1, d2, keys)
        struct.pack_into('<II', out, x, n1 ^ v1, n2 ^ v2); v1, v2 = d1, d2
    print(label, 'first bytes', bytes(out[:4]))
    if out[:2] == b'MZ':
        open(outp, 'wb').write(out); print('wrote', outp, len(out)); break
