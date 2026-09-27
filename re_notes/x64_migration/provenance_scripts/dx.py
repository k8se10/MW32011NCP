import pefile,capstone,sys
pe=pefile.PE(sys.argv[1],fast_load=True); b=pe.OPTIONAL_HEADER.ImageBase; img=pe.get_memory_mapped_image()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
a=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 40
for i,ins in enumerate(md.disasm(bytes(img[a-b:a-b+n*15]),a)):
    if i>=n: break
    print(hex(ins.address),ins.mnemonic,ins.op_str)
