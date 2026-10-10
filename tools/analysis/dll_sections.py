"""dll_sections.py DLL [OTHER]: SHA-256 of each PE section, or differing byte counts between two builds."""
import hashlib, sys, pefile
a = pefile.PE(sys.argv[1], fast_load=True)
if len(sys.argv) > 2:
    b = pefile.PE(sys.argv[2], fast_load=True)
    for s, t in zip(a.sections, b.sections):
        x, y = s.get_data(), t.get_data()
        diff = sum(1 for p, q in zip(x, y) if p != q) + abs(len(x) - len(y))
        print(s.Name.rstrip(b"\0").decode(), len(x), len(y), "differing bytes:", diff)
else:
    for s in a.sections:
        print(s.Name.rstrip(b"\0").decode(), s.SizeOfRawData, hashlib.sha256(s.get_data()).hexdigest()[:16])
