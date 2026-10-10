"""Read-only inspection of exact CHudWeaponSelection instances in a running game.

Pass PID and relocated client.dll base (decimal or 0xhex). Does not write game
memory, inject calls, change binds, or save process-memory dumps.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import struct

p = argparse.ArgumentParser()
p.add_argument("pid", type=int)
p.add_argument("client", type=lambda s: int(s, 0))
p.add_argument("--engine", type=lambda s: int(s, 0))
a = p.parse_args()
k = c.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.ReadProcessMemory.restype = w.BOOL
k.CloseHandle.argtypes = [w.HANDLE]

class MBI(c.Structure):
    _fields_ = [("BaseAddress", c.c_void_p), ("AllocationBase", c.c_void_p),
                ("AllocationProtect", w.DWORD), ("PartitionId", w.WORD),
                ("RegionSize", c.c_size_t), ("State", w.DWORD),
                ("Protect", w.DWORD), ("Type", w.DWORD)]
k.VirtualQueryEx.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(MBI), c.c_size_t]
k.VirtualQueryEx.restype = c.c_size_t
h = k.OpenProcess(0x10 | 0x400, False, a.pid)
if not h:
    raise c.WinError(c.get_last_error())

def read(address, size):
    b = c.create_string_buffer(size)
    got = c.c_size_t()
    if not k.ReadProcessMemory(h, address, b, size, c.byref(got)):
        return b""
    return b.raw[:got.value]

results = []
binding_manager = {}
engine_client = {}
lastinv_bindings = []
seen = set()
pattern = struct.pack("<I", a.client + 0x5d5a84)
try:
    if a.engine:
        bindings = read(a.engine + 0x698738, 0x2a3 * 8)
        for code in range(len(bindings) // 8):
            pointer = struct.unpack_from("<I", bindings, code * 8)[0]
            if pointer:
                command = read(pointer, 128).split(b"\0", 1)[0]
                if b"lastinv" in command:
                    lastinv_bindings.append({"code": code, "hex": hex(code),
                        "command": command.decode("ascii", errors="replace")})
    engine_pointer = read(a.client + 0x735e3c, 4)
    if len(engine_pointer) == 4:
        engine = struct.unpack("<I", engine_pointer)[0]
        engine_data = read(engine, 4)
        if len(engine_data) == 4:
            table = struct.unpack("<I", engine_data)[0]
            methods = read(table + 0x58, 8)
            engine_client = {"object": hex(engine), "vtable": hex(table),
                "lookup_binding": hex(struct.unpack_from("<I", methods)[0]),
                "binding_for_key": hex(struct.unpack_from("<I", methods, 4)[0])}
    manager_pointer = read(a.client + 0x6c49b4, 4)
    if len(manager_pointer) == 4:
        manager = struct.unpack("<I", manager_pointer)[0]
        manager_data = read(manager, 4)
        if len(manager_data) == 4:
            vtable = struct.unpack("<I", manager_data)[0]
            methods = read(vtable, 24)
            binding_manager = {"object": hex(manager), "vtable": hex(vtable),
                "methods": [hex(x) for x in struct.unpack("<6I", methods)] if len(methods) == 24 else []}
    address = 0
    while address < 0x100000000:
        mbi = MBI()
        if not k.VirtualQueryEx(h, address, c.byref(mbi), c.sizeof(mbi)):
            break
        end = (mbi.BaseAddress or 0) + mbi.RegionSize
        if end <= address:
            break
        if mbi.State == 0x1000 and mbi.Type == 0x20000 and mbi.Protect & 0x0c and not mbi.Protect & 0x101:
            chunk = address
            while chunk < end:
                data = read(chunk, min(0x400000, end - chunk))
                pos = data.find(pattern)
                while pos != -1:
                    root = chunk + pos
                    obj = read(root, 0x5b0)
                    if root not in seen and len(obj) == 0x5b0 and struct.unpack_from("<I", obj, 0x48)[0] == a.client + 0x5d5734:
                        seen.add(root)
                        raw = obj[0x374:0x3f4]
                        text = raw.decode("utf-16-le", errors="replace").split("\0", 1)[0]
                        results.append({"root": hex(root), "panel": hex(root + 0x48),
                            "slot": struct.unpack_from("<i", obj, 0x14)[0],
                            "lastinv_text": text, "lastinv_codepoints": [ord(ch) for ch in text],
                            "bindings_font": struct.unpack_from("<I", obj, 0x538)[0],
                            "binding_y": struct.unpack_from("<f", obj, 0x510)[0],
                            "icon_size": struct.unpack_from("<f", obj, 0x530)[0]})
                    pos = data.find(pattern, pos + 1)
                chunk += 0x400000 - 4
        address = end
finally:
    k.CloseHandle(h)
print(json.dumps({"pid": a.pid, "client_base": hex(a.client),
    "access": "OpenProcess PROCESS_VM_READ|QUERY_INFORMATION; ReadProcessMemory only",
    "binding_manager": binding_manager, "engine_client": engine_client,
    "lastinv_bindings": lastinv_bindings, "instances": results}, indent=2))
