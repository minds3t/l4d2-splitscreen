"""ICE (Information Concealment Engine, Matthew Kwan) decryption for Valve .nuc/.ctx files.
usage: ice.py KEY IN OUT     (L4D2 vscripts: key SDhfi878)"""
import sys

SMOD = [[333, 313, 505, 369], [379, 375, 319, 391], [361, 445, 451, 397], [397, 425, 395, 505]]
SXOR = [[0x83, 0x85, 0x9b, 0xcd], [0xcc, 0xa7, 0xad, 0x41], [0x4b, 0x2e, 0xd4, 0x33], [0xea, 0xcb, 0x2e, 0x04]]
PBOX = [0x00000001, 0x00000080, 0x00000400, 0x00002000, 0x00080000, 0x00200000, 0x01000000, 0x40000000,
        0x00000008, 0x00000020, 0x00000100, 0x00004000, 0x00010000, 0x00800000, 0x04000000, 0x20000000,
        0x00000004, 0x00000010, 0x00000200, 0x00008000, 0x00020000, 0x00400000, 0x08000000, 0x10000000,
        0x00000002, 0x00000040, 0x00000800, 0x00001000, 0x00040000, 0x00100000, 0x02000000, 0x80000000]
KEYROT = [0, 1, 2, 3, 2, 1, 3, 0, 1, 3, 2, 0, 3, 1, 0, 2]


def gf_mult(a, b, m):
    res = 0
    while b:
        if b & 1:
            res ^= a
        a <<= 1; b >>= 1
        if a >= 256:
            a ^= m
    return res


def gf_exp7(b, m):
    if b == 0:
        return 0
    x = gf_mult(b, b, m); x = gf_mult(b, x, m); x = gf_mult(x, x, m)
    return gf_mult(b, x, m)


def perm32(x):
    res, i = 0, 0
    while x:
        if x & 1:
            res |= PBOX[i]
        i += 1; x >>= 1
    return res


SBOX = [[0] * 1024 for _ in range(4)]
for i in range(1024):
    col = (i >> 1) & 0xFF
    row = (i & 1) | ((i & 0x200) >> 8)
    for k in range(4):
        x = gf_exp7(col ^ SXOR[k][row], SMOD[k][row]) << (24 - 8 * k)
        SBOX[k][i] = perm32(x)


class Ice:
    def __init__(self, key, n=0):
        self.rounds = 8 if n < 1 else n * 16
        self.size = 1 if n < 1 else n
        self.sched = [[0, 0, 0] for _ in range(self.rounds)]
        if self.rounds == 8:
            kb = [(key[7 - i * 2 - 1] << 8) | key[7 - i * 2] for i in range(4)]
            self._sched_build(kb, 0, KEYROT)
        else:
            raise NotImplementedError

    def _sched_build(self, kb, n, rot):
        for i in range(8):
            kr = rot[i]
            isk = self.sched[n + i]
            isk[0] = isk[1] = isk[2] = 0
            for j in range(15):
                curr = j % 3
                for k in range(4):
                    curr_kb = kb[(kr + k) & 3]
                    bit = curr_kb & 1
                    isk[curr] = (isk[curr] << 1) | bit
                    kb[(kr + k) & 3] = (curr_kb >> 1) | ((bit ^ 1) << 15)

    @staticmethod
    def _f(p, sk):
        tl = ((p >> 16) & 0x3ff) | (((p >> 14) | (p << 18)) & 0xffc00)
        tr = (p & 0x3ff) | ((p << 2) & 0xffc00)
        al = sk[2] & (tl ^ tr)
        ar = al ^ tr
        al ^= tl
        al ^= sk[0]
        ar ^= sk[1]
        return SBOX[0][al >> 10] | SBOX[1][al & 0x3ff] | SBOX[2][ar >> 10] | SBOX[3][ar & 0x3ff]

    def decrypt_block(self, b):
        l = int.from_bytes(b[0:4], "big"); r = int.from_bytes(b[4:8], "big")
        for i in range(self.rounds - 1, 0, -2):
            l ^= self._f(r, self.sched[i])
            r ^= self._f(l, self.sched[i - 1])
        return r.to_bytes(4, "big") + l.to_bytes(4, "big")


def decrypt(data, key):
    ice = Ice(key)
    out = bytearray()
    n = len(data) - len(data) % 8
    for i in range(0, n, 8):
        out += ice.decrypt_block(data[i:i + 8])
    out += data[n:]  # trailing partial block is stored in plain text
    return bytes(out)


if __name__ == "__main__":
    k, src, dst = sys.argv[1].encode(), sys.argv[2], sys.argv[3]
    open(dst, "wb").write(decrypt(open(src, "rb").read(), k))
