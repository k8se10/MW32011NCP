# usage: python3 steamstub.py <exe> [<exe>...] -- decrypt + parse the SteamStub 3.1 x64 header, print flags.
# Decrypt and parse a SteamStub (Variant 3.x, x64) header, following Steamless' documented layout.
import pefile, struct, sys
def xor_dec(buf):
    out = bytearray(buf); key = struct.unpack_from('<I', buf, 0)[0]
    for i in range(4, len(buf) - len(buf) % 4, 4):
        v = struct.unpack_from('<I', buf, i)[0]; struct.pack_into('<I', out, i, v ^ key); key = v
    return bytes(out)
for path in sys.argv[1:]:
    pe = pefile.PE(path, fast_load=True); ep = pe.OPTIONAL_HEADER.AddressOfEntryPoint
    epoff = pe.get_offset_from_rva(ep); raw = open(path, 'rb').read()
    print('=====', path, 'EP rva %#x' % ep)
    for hsz in (0xF0, 0xD0):
        h = xor_dec(raw[epoff - hsz:epoff])
        sig = struct.unpack_from('<I', h, 4)[0]
        print('  try header size %#x: signature %#x' % (hsz, sig))
        if sig == 0xC0DEC0DF:
            f = struct.unpack_from('<IIQQIIQIIIIIIIIQQ', h, 0)
            names = ['XorKey','Signature','ImageBase','AddressOfEntryPoint','BindSectionOffset','Unk0','OriginalEntryPoint','Unk1','PayloadSize','DRMPDllOffset','DRMPDllSize','SteamAppId','Flags','BindSectionVirtualSize','Unk2','CodeSectionVirtualAddress','CodeSectionRawSize']
            for n, v in zip(names, f): print('    %-26s %#x' % (n, v))
            fl = f[12]
            for bit, n in ((0x1,'UseValidation?'),(0x2,'NoModuleVerification'),(0x4,'NoEncryption'),(0x10,'NoOwnershipCheck'),(0x20,'NoDebuggerCheck'),(0x40,'NoErrorDialog')):
                print('    flag %-22s %s' % (n, 'SET' if fl & bit else 'clear'))
            print('    -> code section encrypted:', not (fl & 0x4), ' debugger check active:', not (fl & 0x20))
            break
