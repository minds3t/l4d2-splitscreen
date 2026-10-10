"""callers.py DLL 0xRVA -> RVAs of direct calls/jumps (E8/E9 rel32) to that function, and vtable slots holding it."""
import sys, struct, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
target = int(sys.argv[2], 16)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
start, end = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
for i in range(start, end - 5):
    if img[i] in (0xE8, 0xE9) and i + 5 + struct.unpack_from("<i", img, i + 1)[0] == target:
        print(("call" if img[i] == 0xE8 else "jmp "), hex(i))
needle = struct.pack("<I", base + target)
at = img.find(needle)
while at >= 0:
    print("data", hex(at))
    at = img.find(needle, at + 1)
