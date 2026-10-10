"""Extract files from the L4D2 search paths (highest priority wins) into a folder.
Usage: python extract_game.py OUTDIR PREFIX [PREFIX...]"""
import sys, os, vpk
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "packaging"))
import game_files
G = game_files.GAME
# Mods mounted in gameinfo.txt first, as the game itself resolves files.
ORDER = game_files.mod_paths() + game_files.STOCK_PATHS + ["hl2"]
out, prefixes = sys.argv[1], [p.lower() for p in sys.argv[2:]]
seen = {}
for d in ORDER:
    path = os.path.join(G, d, "pak01_dir.vpk")
    loose = os.path.join(G, d)
    if os.path.exists(path):
        p = vpk.open(path, path_enc="latin-1")
        for f in p:
            if any(f.lower().startswith(x) for x in prefixes) and f.lower() not in seen:
                seen[f.lower()] = d
                dst = os.path.join(out, f); os.makedirs(os.path.dirname(dst), exist_ok=True)
                p.get_file(f).save(dst)
    for root, _, files in os.walk(loose):
        for fn in files:
            rel = os.path.relpath(os.path.join(root, fn), loose).replace("\\", "/")
            if rel.endswith(".vpk"): continue
            if any(rel.lower().startswith(x) for x in prefixes) and rel.lower() not in seen:
                seen[rel.lower()] = d + "(loose)"
                dst = os.path.join(out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
                import shutil; shutil.copy(os.path.join(root, fn), dst)
with open(os.path.join(out, "_sources.txt"), "w") as fh:
    for k, v in sorted(seen.items()): fh.write(f"{v}\t{k}\n")
print(len(seen), "files")
