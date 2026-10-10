from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

import pefile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import game_files

INCLUDE = ROOT / "plugin" / "include"
CHECKER = ROOT / "plugin" / "build" / "Release" / "splitscreen_check.exe"
GAME = Path(game_files.find_game() or "missing")
MODULES = re.findall(r'\{"[^"]+",\s*"([^"]+)",', (INCLUDE / "supported_build.h").read_text(encoding="utf-8"))
ROWS = (INCLUDE / "signatures.inc").read_text(encoding="utf-8")


def check(game, *options):
    result = subprocess.run([str(CHECKER), str(game), *options], capture_output=True, text=True)
    return result.returncode, result.stdout


class SignatureTableTest(unittest.TestCase):
    def test_every_address_has_a_row(self):
        wanted, module = set(), None
        for line in (INCLUDE / "build_10097.h").read_text(encoding="utf-8").splitlines():
            opened = re.match(r"namespace (\w+) \{", line)
            if opened and opened.group(1) != "rva":
                module = opened.group(1)
            entry = re.match(r"SS_RVA\((\w+), (0x[0-9A-Fa-f]+)\)", line)
            if entry:
                wanted.add((module, entry.group(1), int(entry.group(2), 16)))
        rows = {(module, name, int(known, 16))
                for module, name, known in re.findall(r"^SS_(?:SIGNATURE|DERIVED|LOCATED)\((\w+), (\w+), (0x[0-9a-f]+),", ROWS, re.M)}
        self.assertEqual(rows, wanted)

    def test_patterns_pin_enough_and_leave_addresses_open(self):
        for kind, pattern in re.findall(r'^SS_(?:SIGNATURE|ANCHOR)\(\w+, \w+, 0x[0-9a-f]+, (\w+), \d+, \d+, "([^"]+)"\)', ROWS, re.M):
            tokens = pattern.split(" ")
            self.assertGreaterEqual(sum(token != "??" for token in tokens), 12, pattern)
            if kind == "call":
                self.assertEqual(tokens[:5], ["E8", "??", "??", "??", "??"])


@unittest.skipUnless(CHECKER.is_file() and (GAME / "left4dead2.exe").is_file(), "needs the built checker and the game")
class CheckerTest(unittest.TestCase):
    def copy(self, folder):
        for relative in MODULES:
            (folder / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(GAME / relative, folder / relative)

    def test_researched_game_has_every_address_in_place(self):
        code, output = check(GAME, "--researched")
        self.assertEqual(code, 0, output)
        self.assertRegex(output, r"\d+ addresses, 0 not found, 0 moved")

    def test_a_file_that_differs_outside_the_patterns_is_accepted(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            self.copy(folder)
            target = folder / MODULES[2]
            data = bytearray(target.read_bytes())
            stamp = pefile.PE(data=bytes(data), fast_load=True).FILE_HEADER.get_file_offset() + 4
            data[stamp] ^= 0xFF  # link time: another file, same code
            target.write_bytes(data)
            code, output = check(folder)
            self.assertEqual(code, 0, output)

    def test_a_changed_function_is_refused_by_name(self):
        module, name, known, pattern = re.search(
            r'^SS_SIGNATURE\((engine), (\w+), (0x[0-9a-f]+), code, 0, 0, "([^"]+)"\)', ROWS, re.M).groups()
        relative = next(path for path in MODULES if path.endswith(module + ".dll"))
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            self.copy(folder)
            target = folder / relative
            data = bytearray(target.read_bytes())
            fixed = next(index for index, token in enumerate(pattern.split(" ")) if token != "??")
            offset = pefile.PE(data=bytes(data), fast_load=True).get_offset_from_rva(int(known, 16) + fixed)
            data[offset] ^= 0xFF
            target.write_bytes(data)
            code, output = check(folder)
            self.assertEqual(code, 1, output)
            self.assertIn(f"{module}: {name} not found", output)

    def test_a_missing_module_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            code, output = check(temp)
            self.assertEqual(code, 1, output)


if __name__ == "__main__":
    unittest.main()
