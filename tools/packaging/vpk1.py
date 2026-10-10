"""Pack a directory into a single-file VPK version 1 (the format L4D2 expects).
usage: vpk1.py SRC_DIR OUT.vpk"""
import os, struct, sys, zlib
from collections import defaultdict


def pack(src, out):
    tree = defaultdict(lambda: defaultdict(list))  # ext -> path -> [(name, fullpath)]
    for root, _, files in os.walk(src):
        for fn in sorted(files):
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, src).replace("\\", "/").lower()
            d, base = os.path.split(rel)
            name, ext = os.path.splitext(base)
            tree[ext.lstrip(".") or " "][d or " "].append((name, full))

    tree_bytes = bytearray(); data = bytearray()
    for ext in sorted(tree):
        tree_bytes += ext.encode() + b"\0"
        for path in sorted(tree[ext]):
            tree_bytes += path.encode() + b"\0"
            for name, full in tree[ext][path]:
                blob = open(full, "rb").read()
                tree_bytes += name.encode() + b"\0"
                # crc, preload bytes, archive index 0x7fff = data follows the tree in this file
                tree_bytes += struct.pack("<IHHIIH", zlib.crc32(blob) & 0xFFFFFFFF, 0, 0x7FFF,
                                          len(data), len(blob), 0xFFFF)
                data += blob
            tree_bytes += b"\0"
        tree_bytes += b"\0"
    tree_bytes += b"\0"
    with open(out, "wb") as f:
        f.write(struct.pack("<III", 0x55AA1234, 1, len(tree_bytes)))
        f.write(tree_bytes); f.write(data)


if __name__ == "__main__":
    pack(sys.argv[1], sys.argv[2])
    print("wrote", sys.argv[2])
