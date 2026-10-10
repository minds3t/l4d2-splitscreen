"""Minimal XDVDFS (Xbox 360 XGD2) reader. usage: xiso.py ISO list | extract OUTDIR [prefix...]"""
import sys, os, struct
iso_path, cmd = sys.argv[1], sys.argv[2]
f = open(iso_path, "rb")
BASE = 0xFD90000
SECT = 2048
f.seek(BASE + 32 * SECT)
magic = f.read(20); assert magic == b"MICROSOFT*XBOX*MEDIA", magic
root_sec, root_size = struct.unpack("<II", f.read(8))

def walk(sector, size, path=""):
    f.seek(BASE + sector * SECT); data = f.read(size)
    seen = set(); stack = [0]
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data): continue
        seen.add(off)
        l, r, sec, sz, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if l == 0xFFFF and r == 0xFFFF and sec == 0xFFFFFFFF: continue
        name = data[off + 14: off + 14 + nlen].decode("latin-1")
        if l: stack.append(l * 4)
        if r: stack.append(r * 4)
        full = path + "/" + name if path else name
        if attr & 0x10:
            if sz: yield from walk(sec, sz, full)
        else:
            yield full, sec, sz

files = list(walk(root_sec, root_size))
if cmd == "list":
    for p, s, z in sorted(files): print(f"{z:12d}  {p}")
elif cmd == "extract":
    out = sys.argv[3]; pref = [x.lower() for x in sys.argv[4:]]
    for p, s, z in files:
        if pref and not any(p.lower().startswith(x) for x in pref): continue
        dst = os.path.join(out, p); os.makedirs(os.path.dirname(dst), exist_ok=True)
        f.seek(BASE + s * SECT)
        with open(dst, "wb") as o:
            left = z
            while left:
                chunk = f.read(min(left, 1 << 22)); o.write(chunk); left -= len(chunk)
        print("x", p)
