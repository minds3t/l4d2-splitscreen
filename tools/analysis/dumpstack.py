"""dumpstack.py file.mdmp [words] -> registers and module+offset stack words from the EXCEPTION context."""
import sys, struct, ntpath, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile

md = MinidumpFile.parse(sys.argv[1])
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 60
exc = md.exception.exception_records[0]
mods = [(m.baseaddress, m.size, ntpath.basename(m.name)) for m in md.modules.modules]


def where(v):
    for b, s, n in mods:
        if b <= v < b + s:
            return f"{n}+{v - b:#x}"
    return None


reader = md.get_reader()
loc = exc.ThreadContext
raw = reader.file_handle
raw.seek(loc.Rva)
ctx = raw.read(loc.DataSize)
# x86 CONTEXT: Edi@0x9c Esi Ebx Edx Ecx Eax Ebp Eip SegCs EFlags Esp
names = ["Edi", "Esi", "Ebx", "Edx", "Ecx", "Eax", "Ebp", "Eip", "SegCs", "EFlags", "Esp"]
regs = dict(zip(names, struct.unpack_from("<11I", ctx, 0x9c)))
print("exception at", where(exc.ExceptionRecord.ExceptionAddress))
print({k: hex(v) for k, v in regs.items() if k not in ("SegCs", "EFlags")})
data = reader.read(regs["Esp"], 0x1800)
shown = 0
for i in range(0, len(data) - 3, 4):
    hit = where(struct.unpack_from("<I", data, i)[0])
    if hit:
        print(f"  [esp+{i:#06x}] {hit}")
        shown += 1
        if shown >= limit:
            break
