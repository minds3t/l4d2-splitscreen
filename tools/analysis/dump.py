"""Summarize a Windows minidump: exception, faulting module, stack return addresses into game modules."""
import sys, struct, ntpath
from minidump.minidumpfile import MinidumpFile
md = MinidumpFile.parse(sys.argv[1])
exc = md.exception.exception_records[0]
rec = exc.ExceptionRecord
addr = rec.ExceptionAddress
mods = [(m.baseaddress, m.size, ntpath.basename(m.name)) for m in md.modules.modules]
def where(v):
    for b, s, n in mods:
        if b <= v < b + s: return f"{n}+{v-b:#x}"
    return None
print("code", rec.ExceptionCode, "at", where(addr) or hex(addr), "info", [hex(x) for x in rec.ExceptionInformation[:2]])
GAME = {"engine.dll","client.dll","server.dll","gameui.dll","matchmaking.dll","vgui2.dll","inputsystem.dll","vguimatsurface.dll","tier0.dll","vstdlib.dll","materialsystem.dll"}
for t in md.threads.threads:
    if t.ThreadId == exc.ThreadId:
        ctx = t.ContextObject
        print({k: hex(getattr(ctx, k)) for k in ("Eip","Esp","Ebp","Eax","Ebx","Ecx","Edx","Esi","Edi")})
        data = md.get_reader().read(ctx.Esp, 0x800)
        n = 0
        for i in range(0, len(data) - 3, 4):
            w = where(struct.unpack_from("<I", data, i)[0])
            if w and w.split("+")[0].lower() in GAME:
                print(f"  [esp+{i:#x}] {w}"); n += 1
                if n > 30: break
