"""Regenerate the selected traces cited by findings.txt."""
from contextlib import redirect_stdout
from pathlib import Path

from ppc_refs import Image


DIRECTORY = Path(__file__).resolve().parent
TRACES = {
    "gameui": ("GameUI_360.dll.pe", [
        (0x8404fb88, 24), (0x84075880, 70), (0x8407c914, 67),
        (0x84052dec, 28), (0x84090d40, 32), (0x84090658, 54),
    ]),
    "matchmaking": ("matchmaking_360.dll.pe", [
        (0x8b0b0c90, 185), (0x8b097390, 100), (0x8b09d768, 110), (0x8b0955b0, 20),
    ]),
    "tier0": ("tier0_360.dll.pe", [
        (0x82c21b48, 19), (0x82c21d20, 12), (0x82c21e40, 12),
    ]),
}


def main():
    for name, (filename, ranges) in TRACES.items():
        image = Image(DIRECTORY / filename)
        with (DIRECTORY / f"xbox_{name}_trace.txt").open("w", encoding="utf-8") as stream:
            with redirect_stdout(stream):
                print(f"Source: {filename} (decrypted XEX memory image, PPC64 big-endian).")
                print("Constant/string annotations are heuristic; branch targets and instruction operands are decoded directly.")
                for address, count in ranges:
                    print(f"\nTrace {address:#x}")
                    image.disassemble(address, count)


if __name__ == "__main__":
    main()
