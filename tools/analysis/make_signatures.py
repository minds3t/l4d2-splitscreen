"""Derive the search pattern for every address the plugin uses.

    python tools/analysis/make_signatures.py            write plugin/include/signatures.inc
    python tools/analysis/make_signatures.py --check    only report

Reads the addresses of the researched build (plugin/include/build_10097.h) and
that build's installed modules, and writes for each address how the plugin
finds it at startup, so a game update that only moves code does not stop it.
Bytes that change when code moves are left open in a pattern: relocated
addresses and the distances of near calls and jumps.

How an address is found (the "kind" of its row):
  code      the pattern sits at the address, or starts a fixed distance
            before it
  operand   the address is an operand of the instruction the pattern starts
            with: a global or a vtable (or lies a fixed distance before that
            operand, for an object the code only touches through its members)
  call      the address is the target of the near call the pattern starts with:
            a function too small or too common to be recognised by itself
  derived   the address is stored in the module's data a fixed distance behind
            another address: a vtable slot, or the vtable of an object
  located   the address lies a fixed distance before the one place in the
            module's data that stores another address: a vtable known by one
            of its functions
A pattern is accepted only if it occurs exactly once in the module's code.
An "anchor" row is such another address that the plugin itself does not use.
"""
from pathlib import Path
import re
import struct
import sys

import capstone
import pefile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
import game_files

ROOT = Path(__file__).resolve().parents[2]
ADDRESSES = ROOT / "plugin" / "include" / "build_10097.h"
MODULES = ROOT / "plugin" / "include" / "supported_build.h"
OUTPUT = ROOT / "plugin" / "include" / "signatures.inc"
MIN_FIXED = 12     # bytes a pattern must pin down
MAX_LENGTH = 200
MAX_SITES = 60     # referencing instructions tried for one address
MAX_MEMBER = 0x40  # how far behind an object's start a member reference may lie
MAX_SLOT = 0x400   # how far behind another address a stored address may lie

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def addresses():
    """[(module, name, rva)] in the order of the header."""
    rows, module = [], None
    for line in ADDRESSES.read_text(encoding="utf-8").splitlines():
        opened = re.match(r"namespace (\w+) \{", line)
        if opened and opened.group(1) != "rva":
            module = opened.group(1)
        entry = re.match(r"SS_RVA\((\w+), (0x[0-9A-Fa-f]+)\)", line)
        if entry:
            rows.append((module, entry.group(1), int(entry.group(2), 16)))
    return rows


def module_paths():
    header = MODULES.read_text(encoding="utf-8")
    return {name[:-4]: path for name, path in re.findall(r'\{"([^"]+)",\s*"([^"]+)",', header)}


class Module:
    def __init__(self, path):
        self.pe = pefile.PE(str(path), fast_load=True)
        self.pe.parse_data_directories([pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.image = self.pe.get_memory_mapped_image()
        text = next(s for s in self.pe.sections if s.Characteristics & 0x20000000)
        self.text_start = text.VirtualAddress
        self.text_end = text.VirtualAddress + min(text.Misc_VirtualSize, text.SizeOfRawData)
        self.text = self.image[self.text_start:self.text_end]
        self.relocations = set()
        for block in getattr(self.pe, "DIRECTORY_ENTRY_BASERELOC", []):
            for entry in block.entries:
                if entry.type == 3:
                    self.relocations.add(entry.rva)
        self._references = None
        self._holders = None
        self._calls = None

    def in_text(self, rva):
        return self.text_start <= rva < self.text_end

    def constant(self, rva):
        """In a section the running game cannot write to."""
        return any(s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize and not s.Characteristics & 0x80000000
                   for s in self.pe.sections)

    def _stored(self, in_code):
        table = {}
        for site in self.relocations:
            if self.in_text(site) == in_code and site + 4 <= len(self.image):
                value = struct.unpack_from("<I", self.image, site)[0] - self.base
                table.setdefault(value, []).append(site)
        return table

    def references(self, rva):
        """Code addresses of relocated operands that hold this address."""
        if self._references is None:
            self._references = self._stored(True)
        return sorted(self._references.get(rva, []))

    def holders(self, rva):
        """Data addresses that store this address."""
        if self._holders is None:
            self._holders = self._stored(False)
        return sorted(self._holders.get(rva, []))

    def calls(self, rva):
        """Addresses of near calls whose target is this address."""
        if self._calls is None:
            self._calls = {}
            for match in re.finditer(rb"\xE8", self.text):
                site = self.text_start + match.start()
                if site + 5 > self.text_end:
                    continue
                target = site + 5 + struct.unpack_from("<i", self.image, site + 1)[0]
                if self.in_text(target):
                    self._calls.setdefault(target, []).append(site)
        return sorted(self._calls.get(rva, []))

    def instructions(self, start, limit=MAX_LENGTH):
        """(bytes, open) of each instruction from start to the function's end."""
        position = start
        while position - start < limit and position < self.text_end:
            code = self.image[position:position + 16]
            decoded = next(MD.disasm(code, self.base + position, 1), None)
            if decoded is None or decoded.id == capstone.x86.X86_INS_INT3:
                return
            size = decoded.size
            # A relocated address is open wherever its four bytes fall, also when
            # decoding started inside an instruction and split it.
            mask = [any(position + offset - back in self.relocations for back in range(4)) for offset in range(size)]
            first = code[0]
            if first in (0xE8, 0xE9) and size == 5:
                mask[1:5] = [True] * 4
            elif first == 0x0F and 0x80 <= code[1] <= 0x8F and size == 6:
                mask[2:6] = [True] * 4
            yield code[:size], mask
            position += size
            ends = decoded.id in (capstone.x86.X86_INS_RET, capstone.x86.X86_INS_JMP)
            if ends and position < self.text_end and self.image[position] == 0xCC:
                return

    def count(self, data, mask):
        expression = b"".join(b"." if skip else re.escape(bytes([value])) for value, skip in zip(data, mask))
        return sum(1 for _ in re.finditer(b"(?=" + expression + b")", self.text, re.DOTALL))

    def pattern(self, start):
        """Shortest pattern from start that occurs once, or None."""
        data, mask, steps = [], [], []
        for code, opened in self.instructions(start):
            data += code
            mask += opened
            steps.append(len(data))
        # Too little to recognise, like a two-instruction getter: another kind of row.
        if not steps or mask.count(False) < MIN_FIXED or self.count(data, mask) != 1:
            return None
        for length in steps:
            fixed = mask[:length].count(False)
            if fixed >= MIN_FIXED and self.count(data[:length], mask[:length]) == 1:
                return data[:length], mask[:length]
        return None

    def decodes_cleanly(self, start, count=6):
        """Each relocated address in the next instructions is one whole operand."""
        position = start
        for _ in range(count):
            decoded = next(MD.disasm(self.image[position:position + 16], self.base + position, 1), None)
            if decoded is None:
                return False
            encoding = decoded.encoding
            operands = {encoding.disp_offset if encoding.disp_size == 4 else None,
                        encoding.imm_offset if encoding.imm_size == 4 else None}
            for offset in range(-3, decoded.size):
                if position + offset in self.relocations and offset not in operands:
                    return False
            position += decoded.size
            if decoded.id in (capstone.x86.X86_INS_RET, capstone.x86.X86_INS_JMP, capstone.x86.X86_INS_INT3):
                break
        return True

    def operand_site(self, site):
        """Start of the instruction whose relocated operand is at site."""
        # First the start that decoding from the function's beginning arrives at.
        for start in [self.instruction_start(site)] + list(range(site - 1, site - 8, -1)):
            if not site - 8 < start < site:
                continue
            decoded = next(MD.disasm(self.image[start:start + 16], self.base + start, 1), None)
            if decoded is None or start + decoded.size < site + 4:
                continue
            encoding = decoded.encoding
            offsets = {encoding.disp_offset if encoding.disp_size == 4 else None,
                       encoding.imm_offset if encoding.imm_size == 4 else None}
            if site - start in offsets and self.decodes_cleanly(start):
                return start
        return None

    def instruction_start(self, rva):
        """Start of the instruction that contains rva, if rva lies inside one."""
        begin = rva & ~15
        while begin > max(self.text_start, rva - 0x2000):
            if self.image[begin - 1] == 0xCC:
                position = begin
                for code, _ in self.instructions(begin, rva - begin + 16):
                    if position + len(code) > rva:
                        return position
                    position += len(code)
            begin -= 16
        return rva

    def _starts(self, begin, rva):
        """Instruction starts from begin up to rva, if decoding reaches rva exactly."""
        starts, position = [], begin
        for code, _ in self.instructions(begin, rva - begin + 16):
            if position >= rva:
                break
            starts.append(position)
            position += len(code)
        return starts if position == rva else None

    def pattern_around(self, rva):
        """Pattern that starts some instructions before rva: (bytes, mask, distance)."""
        begin = rva & ~15
        while begin > max(self.text_start, rva - 0x2000):
            # A function starts aligned, after padding.
            starts = self._starts(begin, rva) if self.image[begin - 1] == 0xCC else None
            if starts is not None:
                for start in reversed(starts):
                    if rva - start > MAX_LENGTH // 2:
                        break
                    found = self.pattern(start)
                    if found and len(found[0]) > rva - start:
                        return found[0], found[1], rva - start
                return None
            begin -= 16
        return None


def derive(module, rva):
    """(kind, pattern bytes, mask, offset, distance) or None."""
    if module.in_text(rva):
        # Some addresses are an operand inside an instruction.
        start = module.instruction_start(rva)
        found = module.pattern(start)
        if found and len(found[0]) > rva - start:
            return ("code", *found, rva - start, 0)
        best = None
        for site in module.calls(rva)[:MAX_SITES]:
            found = module.pattern(site)
            if found and (best is None or len(found[0]) < len(best[1])):
                best = ("call", *found, 1, 0)
        if best:
            return best
        found = module.pattern_around(rva)
        return ("code", found[0], found[1], found[2], 0) if found else None
    for distance in range(0, MAX_MEMBER + 1, 4):
        best = None
        for site in module.references(rva + distance)[:MAX_SITES]:
            start = module.operand_site(site)
            if start is None:
                continue
            found = module.pattern(start)
            if found and (best is None or len(found[0]) < len(best[1])):
                best = ("operand", *found, site - start, distance)
        if best:
            return best
    return None


def derive_stored(module, rva, known):
    """(row name, distance, anchor) for an address stored in the module's data.

    The row is one of `known` (name -> address); or, with a name of None, a new
    anchor (its address, how it is found) at the storing place itself.
    """
    # Constant data first. A variable is the last resort: the running game may
    # store something else in it, which the plugin's own checks then reject.
    holders = sorted(module.holders(rva), key=lambda holder: not module.constant(holder))
    for holder in holders:
        below = [(address, name) for name, address in known.items() if 0 <= holder - address <= MAX_SLOT]
        if below:
            address, name = max(below)
            return name, holder - address, None
    for holder in holders:
        found = derive(module, holder)
        if found:
            return None, 0, (holder, found)
    return None


def text(data, mask):
    return " ".join("??" if skip else f"{value:02X}" for value, skip in zip(data, mask))


def main():
    game = Path(game_files.find_game())
    paths = module_paths()
    modules, rows, pending, kinds = {}, [], [], {}
    resolved = {}  # module -> {row name: address}

    def signature(macro, name, entry, rva, found):
        kind, data, mask, offset, distance = found
        kinds[kind] = kinds.get(kind, 0) + 1
        rows.append(f'{macro}({name}, {entry}, {rva:#x}, {kind}, {offset}, {distance}, "{text(data, mask)}")')
        resolved.setdefault(name, {})[entry] = rva

    for name, entry, rva in addresses():
        if name not in modules:
            modules[name] = Module(game / paths[name])
        found = derive(modules[name], rva)
        if found:
            signature("SS_SIGNATURE", name, entry, rva, found)
        else:
            pending.append((name, entry, rva))
    progress = True
    while pending and progress:
        progress = False
        for item in list(pending):
            name, entry, rva = item
            module = modules[name]
            data = {key: value for key, value in resolved.get(name, {}).items() if not module.in_text(value)}
            stored = derive_stored(module, rva, data)
            if not stored:
                code = {key: value for key, value in resolved.get(name, {}).items() if module.in_text(value)}
                places = [(key, module.holders(value)) for key, value in code.items()]
                places = [(key, found[0] - rva) for key, found in places
                          if len(found) == 1 and 0 <= found[0] - rva <= MAX_SLOT]
                if places:
                    base, distance = min(places, key=lambda place: place[1])
                    kinds["located"] = kinds.get("located", 0) + 1
                    rows.append(f"SS_LOCATED({name}, {entry}, {rva:#x}, {base}, {distance})")
                    resolved[name][entry] = rva
                    pending.remove(item)
                    progress = True
                continue
            base, distance, anchor = stored
            if anchor:
                base = entry + "Holder"
                signature("SS_ANCHOR", name, base, anchor[0], anchor[1])
            kinds["derived"] = kinds.get("derived", 0) + 1
            rows.append(f"SS_DERIVED({name}, {entry}, {rva:#x}, {base}, {distance})")
            resolved[name][entry] = rva
            pending.remove(item)
            progress = True
    print(len(rows), "rows:", ", ".join(f"{count} {kind}" for kind, count in sorted(kinds.items())))
    lengths = sorted(row.count(" ") - 5 for row in rows if '"' in row)
    print("pattern length: median", lengths[len(lengths) // 2], "longest", lengths[-1])
    for name, entry, rva in pending:
        print(f"no pattern: {name}::{entry} (+{rva:#x})")
    if pending:
        raise SystemExit(1)
    if "--check" not in sys.argv[1:]:
        header = ("// Written by tools/analysis/make_signatures.py from build_10097.h and the\n"
                  "// installed game; that script describes the rows.\n"
                  "// SS_SIGNATURE / SS_ANCHOR: module, name, address in the researched build,\n"
                  "//   kind, operand offset, member distance, pattern (?? = any byte).\n"
                  "// SS_DERIVED: module, name, address in the researched build, the row whose\n"
                  "//   address it is stored behind, distance.\n"
                  "// SS_LOCATED: module, name, address in the researched build, the row whose\n"
                  "//   address is stored once in the data, distance before that place.\n")
        OUTPUT.write_text(header + "\n".join(rows) + "\n", encoding="utf-8", newline="\n")
        print("wrote", OUTPUT)


if __name__ == "__main__":
    main()
