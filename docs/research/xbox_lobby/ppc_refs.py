"""PPC string references and disassembly of extracted memory-layout XEX images."""
from pathlib import Path
import argparse
import bisect
import re
import struct

import capstone
import pefile


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.pe = pefile.PE(data=self.data, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN)
        self.md.skipdata = True
        self.text = next(s for s in self.pe.sections if s.Name.startswith(b".text"))
        pdata = next(s for s in self.pe.sections if s.Name.startswith(b".pdata"))
        self.functions = sorted(struct.iter_unpack(">II", self.data[pdata.VirtualAddress:pdata.VirtualAddress + pdata.Misc_VirtualSize]))
        self.starts = [start for start, flags in self.functions]

    def function(self, address):
        n = bisect.bisect_right(self.starts, address) - 1
        if n < 0:
            return address
        start, flags = self.functions[n]
        end = start + ((flags >> 8) & 0x3fffff) * 4
        return start if address < end else address

    def instructions(self, address=None, size=None):
        address = address if address is not None else self.base + self.text.VirtualAddress
        size = size if size is not None else self.text.Misc_VirtualSize
        rva = address - self.base
        return self.md.disasm(self.data[rva:rva + size], address)

    def string(self, address):
        rva = address - self.base
        if rva < 0 or rva >= len(self.data):
            return None
        data = self.data[rva:rva + 220].split(b"\0")[0]
        if len(data) > 3 and all(32 <= c <= 126 for c in data):
            return data.decode()

    def decoded(self, address=None, size=None):
        registers = {}
        previous_function = None
        for instruction in self.instructions(address, size):
            current_function = self.function(instruction.address)
            if current_function != previous_function:
                registers.clear()
            previous_function = current_function
            args = instruction.op_str.split(", ")
            result = None
            try:
                if instruction.mnemonic == "lis":
                    result = (int(args[1], 0) << 16) & 0xffffffff
                elif instruction.mnemonic in ("addi", "ori") and args[1] in registers:
                    prior, position = registers[args[1]]
                    if instruction.address - position < 160:
                        imm = int(args[2], 0)
                        result = ((prior + imm) if instruction.mnemonic == "addi" else (prior | imm)) & 0xffffffff
                elif instruction.mnemonic == "mr" and args[1] in registers:
                    result = registers[args[1]][0]
            except (ValueError, IndexError):
                pass
            if args and args[0].startswith("r") and instruction.mnemonic not in ("cmpwi", "cmplwi", "cmpw", "cmplw", "stw", "stb", "std", "stwu"):
                registers.pop(args[0], None)
                if result is not None:
                    registers[args[0]] = result, instruction.address
            yield instruction, result
            # Decode branch/link bits rather than enumerating Capstone aliases
            # such as bl, bctrl, and conditional branch-and-link mnemonics.
            word = int.from_bytes(instruction.bytes, "big")
            opcode = word >> 26
            is_branch = opcode in (16, 18) or (
                opcode == 19 and ((word >> 1) & 0x3ff) in (16, 528)
            )
            if is_branch and word & 1:
                # Xbox PPC ABI: a callee may replace every volatile GPR.
                for register in ("r0", *(f"r{i}" for i in range(3, 13))):
                    registers.pop(register, None)
            elif is_branch:
                # This is a linear convenience pass, not control-flow analysis.
                # Do not propagate constants through returns or branch merges.
                registers.clear()

    def disassemble(self, address, count):
        for instruction, result in self.decoded(address, count * 4):
            extra = ""
            if result is not None:
                text = self.string(result)
                extra = f" ; {result:08x}" + (f" {text!r}" if text else "")
            print(f"{instruction.address:08x}: {instruction.mnemonic:9s} {instruction.op_str}{extra}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("query")
    parser.add_argument("count", type=int, nargs="?", default=36)
    args = parser.parse_args()
    image = Image(args.image)
    if args.query.startswith("@"):
        image.disassemble(int(args.query[1:], 0), args.count)
        return
    targets = {image.base + m.start() for m in re.finditer(re.escape(args.query.encode() + b"\0"), image.data)}
    print("String addresses:", ", ".join(hex(t) for t in targets))
    for instruction, value in image.decoded():
        if value in targets:
            print(f"Reference {instruction.address:08x}; function {image.function(instruction.address):08x}")
            image.disassemble(instruction.address - args.count // 2 * 4, args.count)


if __name__ == "__main__":
    main()
