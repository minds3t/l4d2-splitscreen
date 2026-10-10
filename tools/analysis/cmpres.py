"""cmpres.py A B defsA defsB : compare two res files after evaluating conditionals; prints differing paths"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "packaging"))
import kv
def flat(d, p=""):
    for k, v in d.items():
        if isinstance(v, dict): yield from flat(v, p + "/" + k)
        else: yield p + "/" + k, v
def load(path, defs):
    t = open(path, encoding="utf-8", errors="replace").read()
    return dict(flat(kv.to_dict(kv.evaluate(kv.parse(t), getattr(kv, defs)))))
a = load(sys.argv[1], sys.argv[3]); b = load(sys.argv[2], sys.argv[4])
for k in sorted(set(a) | set(b)):
    if a.get(k) != b.get(k): print(f"{k:70s} {a.get(k)!s:25s} | {b.get(k)}")
