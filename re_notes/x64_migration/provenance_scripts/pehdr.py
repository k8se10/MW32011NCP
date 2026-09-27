# usage: python3 pehdr.py <exe> [<exe>...] -- PE header, sections, debug/PDB, Rich header, load config.
import pefile, sys, datetime
for f in sys.argv[1:]:
    pe = pefile.PE(f, fast_load=False)
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    print("=====", f)
    print(" Machine %#x  TimeDateStamp %#x (%s)  Chars %#x" % (fh.Machine, fh.TimeDateStamp, datetime.datetime.utcfromtimestamp(fh.TimeDateStamp), fh.Characteristics))
    print(" Linker %d.%d  OS %d.%d  Subsys %d.%d  Subsystem %d DllChars %#x" % (oh.MajorLinkerVersion, oh.MinorLinkerVersion, oh.MajorOperatingSystemVersion, oh.MinorOperatingSystemVersion, oh.MajorSubsystemVersion, oh.MinorSubsystemVersion, oh.Subsystem, oh.DllCharacteristics))
    print(" ImageBase %#x Entry %#x SizeOfImage %#x Checksum %#x" % (oh.ImageBase, oh.AddressOfEntryPoint, oh.SizeOfImage, oh.CheckSum))
    for s in pe.sections:
        print("  sec %-8s VA %#09x VS %#09x RS %#09x ent %.2f ch %#x" % (s.Name.rstrip(b'\0').decode(errors='replace'), s.VirtualAddress, s.Misc_VirtualSize, s.SizeOfRawData, s.get_entropy(), s.Characteristics))
    for i,d in enumerate(oh.DATA_DIRECTORY):
        if d.Size: print("  dir %-40s %#x %#x" % (d.name, d.VirtualAddress, d.Size))
    if hasattr(pe,'DIRECTORY_ENTRY_DEBUG'):
        for d in pe.DIRECTORY_ENTRY_DEBUG:
            e = d.entry
            print("  debug type", d.struct.Type, "ts %#x"%d.struct.TimeDateStamp, getattr(e,'PdbFileName',b'') if e else '', getattr(e,'Signature_String','') if e else '', getattr(e,'Age','') if e else '')
    rich = pe.parse_rich_header()
    if rich:
        vals = rich['values']
        print("  RICH:")
        for i in range(0,len(vals),2):
            comp=vals[i]; print("    prodid %3d build %5d count %d" % (comp>>16, comp&0xffff, vals[i+1]))
    if hasattr(pe,'VS_VERSIONINFO') and hasattr(pe,'FileInfo'):
        for fi in pe.FileInfo:
            for e in fi:
                if hasattr(e,'StringTable'):
                    for st in e.StringTable:
                        for k,v in st.entries.items(): print("  VER", k.decode(), "=", v.decode())
    if hasattr(pe,'DIRECTORY_ENTRY_LOAD_CONFIG'):
        lc = pe.DIRECTORY_ENTRY_LOAD_CONFIG.struct
        print("  LoadCfg GuardFlags %#x SecurityCookie %#x" % (getattr(lc,'GuardFlags',0), lc.SecurityCookie))
    if hasattr(pe,'DIRECTORY_ENTRY_TLS'): print("  TLS callbacks at %#x" % pe.DIRECTORY_ENTRY_TLS.struct.AddressOfCallBacks)
