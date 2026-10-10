"""Regression checks for misleading PPC constant annotations across control flow."""
from types import SimpleNamespace
import unittest

import capstone

from ppc_refs import Image


def decoded(words, boundary=None):
    image = Image.__new__(Image)
    image.base = 0x84000000
    image.data = b"".join(word.to_bytes(4, "big") for word in words)
    image.md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN)
    image.text = SimpleNamespace(VirtualAddress=0, Misc_VirtualSize=len(image.data))
    ranges = [len(words)] if boundary is None else [boundary, len(words) - boundary]
    image.functions = []
    start = image.base
    for count in ranges:
        image.functions.append((start, count << 8))
        start += count * 4
    image.starts = [start for start, flags in image.functions]
    return list(image.decoded())


class ConstantLifetimeTests(unittest.TestCase):
    def test_direct_address_is_retained(self):
        # lis r3,0x8400; addi r3,r3,0x60a0
        self.assertEqual(decoded([0x3c608400, 0x386360a0])[-1][1], 0x840060a0)

    def test_calls_invalidate_volatile_address(self):
        for call in (0x48000005, 0x4e800421):  # bl; bctrl
            with self.subTest(call=hex(call)):
                # The returned r3 is unknown even if it held a string before.
                result = decoded([0x3c608400, 0x386360a0, call, 0x7c7f1b78])
                self.assertIsNone(result[-1][1])  # mr r31,r3

    def test_call_preserves_nonvolatile_address(self):
        # lis r31,0x8400; bl; mr r3,r31
        self.assertEqual(decoded([0x3fe08400, 0x48000005, 0x7fe3fb78])[-1][1], 0x84000000)

    def test_return_discards_nonvolatile_address(self):
        self.assertIsNone(decoded([0x3fe08400, 0x4e800020, 0x7fe3fb78])[-1][1])

    def test_function_boundary_discards_nonvolatile_address(self):
        self.assertIsNone(decoded([0x3fe08400, 0x7fe3fb78], boundary=1)[-1][1])


if __name__ == "__main__":
    unittest.main()
