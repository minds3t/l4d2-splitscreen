"""xref.py DLL "string" [ctx]  -> find code references to a string and disassemble around them
   xref.py DLL @0xVA [n]      -> disassemble n instructions at VA"""
import sys, pefile, capstone, struct, re
pe = pefile.PE(sys.argv[1], fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
tstart, tend = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def dis(va, n=40):
    rva = va - base
    for i, ins in enumerate(md.disasm(img[rva:rva + n * 8], va)):
        if i >= n: break
        extra = ""
        for m in re.finditer(r"0x([0-9a-f]{6,8})", ins.op_str):
            t = int(m.group(1), 16) - base
            if 0 < t < len(img):
                s = img[t:t + 80].split(b"\0")[0]
                if len(s) >= 3 and all(32 <= c < 127 for c in s): extra += f'  ; "{s.decode()}"'
        print(f"{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}{extra}")
def func_start(va):
    # walk back to a likely function prologue (int3/nop padding before push ebp / sub esp)
    rva = va - base
    for back in range(0, 0x4000):
        r = rva - back
        if img[r - 1] in (0xCC, 0xC3) and img[r] in (0x55, 0x83, 0x81, 0x56, 0x53, 0x8B, 0x6A, 0x51):
            if img[r-1] == 0xCC or back > 0: return base + r
    return va
arg = sys.argv[2]
if arg.startswith("@"):
    dis(int(arg[1:], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 60)
else:
    ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    needle = arg.encode() + b"\0"
    for m in re.finditer(re.escape(needle), img):
        if img[m.start() - 1] != 0: continue
        sva = base + m.start()
        print(f"== string @ {sva:08x}")
        pat = struct.pack("<I", sva)
        for r in re.finditer(re.escape(pat), img[tstart:tend]):
            ref = base + tstart + r.start()
            print(f"-- ref at {ref:08x} (func ~{func_start(ref):08x})")
            dis(ref - ctx * 3, ctx * 2)
