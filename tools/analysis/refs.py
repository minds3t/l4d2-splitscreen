"""refs.py DLL 0xVA [ctx] - find code that references an absolute address (e.g. convar object)"""
import sys, pefile, capstone, struct, re
pe = pefile.PE(sys.argv[1], fast_load=True); base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
ts, te = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
va = int(sys.argv[2], 16); ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 8
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for off in range(0, 0x80, 4):
    pat = struct.pack("<I", va + off)
    for r in re.finditer(re.escape(pat), img[ts:te]):
        ref = base + ts + r.start()
        print(f"-- ref to {va+off:08x} at {ref:08x}")
        start = ref - ctx * 4
        for i, ins in enumerate(md.disasm(img[start - base:start - base + ctx * 16], start)):
            if i > ctx * 2: break
            print(f"   {ins.address:08x}: {ins.mnemonic} {ins.op_str}")
