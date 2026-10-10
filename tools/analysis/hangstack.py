"""Print module+offset candidates from the stack of a (hung) 32-bit left4dead2 thread.

usage: hangstack.py [thread id]   (default: the thread with the most CPU time)
"""
import ctypes, struct, subprocess, sys, ntpath
from ctypes import wintypes as w

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class FLOATING(ctypes.Structure):
    _fields_ = [("a", w.DWORD * 7), ("b", ctypes.c_byte * 80), ("c", w.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", w.DWORD), ("Dr", w.DWORD * 6), ("FloatSave", FLOATING),
                ("SegGs", w.DWORD), ("SegFs", w.DWORD), ("SegEs", w.DWORD), ("SegDs", w.DWORD),
                ("Edi", w.DWORD), ("Esi", w.DWORD), ("Ebx", w.DWORD), ("Edx", w.DWORD),
                ("Ecx", w.DWORD), ("Eax", w.DWORD), ("Ebp", w.DWORD), ("Eip", w.DWORD),
                ("SegCs", w.DWORD), ("EFlags", w.DWORD), ("Esp", w.DWORD), ("SegSs", w.DWORD),
                ("Ext", ctypes.c_byte * 512)]


def main():
    out = subprocess.check_output(["powershell", "-NoProfile", "-Command",
        "$p=Get-Process left4dead2; $p.Id; ($p.Threads | Sort-Object TotalProcessorTime -Descending)[0].Id"], text=True).split()
    pid, tid = int(out[0]), int(sys.argv[1]) if len(sys.argv) > 1 else int(out[1])
    k32.OpenProcess.restype = w.HANDLE
    k32.OpenThread.restype = w.HANDLE
    process = k32.OpenProcess(0x0410, False, pid)
    thread = k32.OpenThread(0x000A | 0x0040, False, tid)  # suspend, get context, query
    if not process or not thread:
        raise ctypes.WinError(ctypes.get_last_error())
    modules = (w.HMODULE * 1024)()
    needed = w.DWORD()
    psapi.EnumProcessModulesEx(w.HANDLE(process), modules, ctypes.sizeof(modules), ctypes.byref(needed), 1)
    mods = []
    for handle in modules[:needed.value // ctypes.sizeof(w.HMODULE)]:
        name = ctypes.create_unicode_buffer(260)
        info = (ctypes.c_void_p * 3)()
        psapi.GetModuleFileNameExW(w.HANDLE(process), w.HMODULE(handle), name, 260)
        psapi.GetModuleInformation(w.HANDLE(process), w.HMODULE(handle), info, ctypes.sizeof(info))
        mods.append((handle or 0, (info[1] or 0) & 0xFFFFFFFF, ntpath.basename(name.value)))

    def where(value):
        for base, size, name in mods:
            if base <= value < base + size:
                return f"{name}+{value - base:#x}"
        return None

    k32.Wow64SuspendThread(w.HANDLE(thread))
    try:
        ctx = WOW64_CONTEXT()
        ctx.ContextFlags = 0x10007
        if not k32.Wow64GetThreadContext(w.HANDLE(thread), ctypes.byref(ctx)):
            raise ctypes.WinError(ctypes.get_last_error())
        print(f"thread {tid} eip {where(ctx.Eip) or hex(ctx.Eip)} esp {ctx.Esp:#x}")
        data = ctypes.create_string_buffer(0x3000)
        got = ctypes.c_size_t()
        k32.ReadProcessMemory(w.HANDLE(process), ctypes.c_void_p(ctx.Esp), data, len(data), ctypes.byref(got))
    finally:
        k32.ResumeThread(w.HANDLE(thread))
    for i in range(0, got.value - 3, 4):
        hit = where(struct.unpack_from("<I", data.raw, i)[0])
        if hit:
            print(f"  [esp+{i:#06x}] {hit}")


main()
