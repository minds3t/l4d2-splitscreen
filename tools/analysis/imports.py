"""imports.py DLL [substring] -> imported functions (filtered) and the code RVAs that reference each IAT slot."""
import sys, struct, pefile
pe = pefile.PE(sys.argv[1])
want = sys.argv[2].lower() if len(sys.argv) > 2 else ""
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
start, end = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
code = img[start:end]
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode()
    for imp in entry.imports:
        name = imp.name.decode() if imp.name else f"#{imp.ordinal}"
        if want and want not in name.lower() and want not in dll.lower():
            continue
        needle = struct.pack("<I", imp.address)
        refs, at = [], code.find(needle)
        while at >= 0:
            refs.append(hex(start + at))
            at = code.find(needle, at + 1)
        print(f"{dll}!{name} iat={imp.address - base:#x} refs={refs}")
