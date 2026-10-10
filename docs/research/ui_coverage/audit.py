import sys, os, re, json
sys.dont_write_bytecode = True
P = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(P, "tools", "packaging"))
import kv, xbox_ui
import console_menus, console_menu_extras
allow = set(xbox_ui.FILES) | set(console_menus.FILES) | set(console_menu_extras.FILES)
built = set()
# The two packages of the Xbox look as python tools/packaging/build_packages.py stages them.
for staged in (os.path.join(P, "build", "hud"), os.path.join(P, "build", "menus")):
    for r, d, f in os.walk(staged):
        for n in f:
            built.add(os.path.relpath(os.path.join(r, n), staged).replace("\\", "/").lower())
SYM = re.compile(r"\[[^\]\n]*\$(\w+)[^\]\n]*\]")
def read(p):
    b = open(p, "rb").read()
    if b[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return b.decode("utf-16")
    return b.decode("utf-8", "replace")
def flat(nodes, parent=""):
    from collections import Counter
    res = {}; seen = Counter()
    for n, v, c in nodes:
        k = n.lower(); seen[k] += 1
        path = parent + "/" + k + (f"[{seen[k]}]" if seen[k] > 1 else "")
        if isinstance(v, list): res.update(flat(v, path))
        else: res[path] = v
    return res
def main():
    out = []
    for sub in ("resource", "scripts", "cfg"):
        for r, d, f in os.walk(os.path.join(P, "game_pc", sub)):
            if "vscripts" in r or "talker" in r: continue
            for n in f:
                p = os.path.join(r, n)
                rel = os.path.relpath(p, os.path.join(P, "game_pc")).replace("\\", "/").lower()
                try: t = read(p)
                except Exception as e: print("ERR", rel, e); continue
                syms = set(m.upper() for m in re.findall(r"\[[^\]\n]*?\]", t) for m in re.findall(r"\$(\w+)", m))
                plat = syms & {"X360","X360WIDE","X360HIDEF","X360LODEF","WIN32","WIN32WIDE","WIN32HIDEF","WIN32LODEF","WINDOWS","GAMECONSOLE","OSX","POSIX","LINUX","PS3","DECK","X360NONWIDE"}
                other = syms - plat
                if not plat: 
                    continue
                try:
                    nodes = kv.parse(t.replace("\r\n", "\n"))
                    x = kv.specialize(nodes, {s: s in xbox_ui.X360 for s in xbox_ui.PLATFORM_SYMBOLS})
                    w = kv.specialize(nodes, {s: s in xbox_ui.WIN32 for s in xbox_ui.PLATFORM_SYMBOLS})
                    fx, fw = flat(x), flat(w)
                    nd = sum(1 for k in fx.keys() | fw.keys() if fx.get(k) != fw.get(k))
                    differs = x != w
                except Exception as e:
                    differs = "ERR " + str(e); nd = -1
                out.append(dict(rel=rel, plat=sorted(plat), other=sorted(other), differs=differs, nd=nd, built=rel in built, allow=rel in {a.lower() for a in allow}))
    json.dump(out, open("audit.json", "w"), indent=1)
    print("files with platform conds:", len(out))
    print("\n== differ, NOT built ==")
    for o in sorted(out, key=lambda o: o["rel"]):
        if o["differs"] and not o["built"]:
            print(f'{o["rel"]:70s} nd={o["nd"]:5d} {",".join(o["plat"])} | {",".join(o["other"])} {"ALLOW" if o["allow"] else ""} {o["differs"] if o["differs"] is not True else ""}')
    print("\n== no difference ==")
    print(", ".join(o["rel"] for o in out if o["differs"] is False))
    print("\n== allowlisted not built ==")
    print([a for a in allow if a.lower() not in built])
    print("\n== built not in conds list ==")
    print(sorted(built - {o["rel"] for o in out}))

if __name__=="__main__": main()
