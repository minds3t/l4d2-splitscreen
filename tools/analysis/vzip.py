"""Valve .360.zip reader (tolerant of Valve's padding extra fields).
usage: vzip.py ZIP list | extract OUTDIR [prefix...]"""
import sys, os, struct, mmap
path, cmd = sys.argv[1], sys.argv[2]
fh = open(path, "rb"); m = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
eocd = m.rfind(b"PK\x05\x06")
_, _, _, _, n_entries, cd_size, cd_off, _ = struct.unpack_from("<IHHHHIIH", m, eocd)
entries = []; p = cd_off
for _ in range(n_entries):
    sig, _, _, _, method, _, _, crc, csz, usz, nl, el, cl, _, _, _, lho = struct.unpack_from("<IHHHHHHIIIHHHHHII", m, p)
    assert sig == 0x02014b50
    name = m[p + 46: p + 46 + nl].decode("latin-1")
    entries.append((name, method, csz, usz, lho)); p += 46 + nl + cl
    if m[p:p+4] != b'PK' and len(entries) < n_entries: p = m.find(b'PK', p)
def data_of(e):
    name, method, csz, usz, lho = e
    _, _, _, _, _, _, _, _, _, nl, el = struct.unpack_from("<IHHHHHIIIHH", m, lho)
    start = lho + 30 + nl + el
    return m[start:start + csz], method
if cmd == "list":
    for e in entries: print(f"{e[3]:10d} m{e[1]} {e[0]}")
elif cmd == "extract":
    out = sys.argv[3]; pref = [x.lower() for x in sys.argv[4:]]
    n = 0
    for e in entries:
        if pref and not any(e[0].replace(chr(92),'/').lower().startswith(x) for x in pref): continue
        d, meth = data_of(e)
        dst = os.path.join(out, e[0].replace("\\", "/")); os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        open(dst, "wb").write(d); n += 1
    print(n, "files")
