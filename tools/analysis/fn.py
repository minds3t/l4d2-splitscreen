"""fn.py DLL 0xRVA [before] [after]: disassemble the function containing RVA (from int3 padding)."""
import sys, pefile, capstone, re
pe = pefile.PE(sys.argv[1], fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
rva = int(sys.argv[2], 16)
before = int(sys.argv[3]) if len(sys.argv) > 3 else 60
after = int(sys.argv[4]) if len(sys.argv) > 4 else 30
start = rva
while start > rva - 0x3000 and not (img[start - 1] == 0xCC and img[start - 2] == 0xCC and img[start] != 0xCC):
    start -= 1
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
ins = list(md.disasm(img[start:rva + after * 8], base + start))
idx = next((i for i, x in enumerate(ins) if x.address >= base + rva), len(ins))
print(f"function start rva {start:#x}")
for x in ins[max(0, idx - before):idx + after]:
    extra = ""
    for m in re.finditer(r"0x([0-9a-f]{6,8})", x.op_str):
        t = int(m.group(1), 16) - base
        if 0 < t < len(img):
            s = img[t:t + 80].split(b"\0")[0]
            if len(s) >= 3 and all(32 <= c < 127 for c in s):
                extra += f'  ; "{s.decode()}"'
    mark = ">>" if x.address == base + rva else "  "
    print(f"{mark}{x.address - base:07x}: {x.mnemonic:7s} {x.op_str}{extra}")
