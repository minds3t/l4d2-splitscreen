"""Read-only, exact-build sampling of native split-screen aim-assist state.

No memory writes, remote calls, injected input, console commands or cvar changes.
Example: python docs/research/aim_assist/probe_runtime.py --seconds 30 --output trace.json
Run while the user sweeps the controller across a visible infected target.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

SUPPORTED_CLIENT = "c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f"
ACCESS = 0x10 | 0x400  # PROCESS_VM_READ | PROCESS_QUERY_INFORMATION only.


class ProcessEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("pid", w.DWORD),
                ("heap", c.c_size_t), ("module", w.DWORD), ("threads", w.DWORD),
                ("parent", w.DWORD), ("priority", w.LONG), ("flags", w.DWORD),
                ("name", w.WCHAR * 260)]


class ModuleEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("id", w.DWORD), ("pid", w.DWORD),
                ("global_usage", w.DWORD), ("process_usage", w.DWORD),
                ("base", c.c_void_p), ("bytes", w.DWORD), ("module", w.HMODULE),
                ("name", w.WCHAR * 256), ("path", w.WCHAR * 260)]


class Gamepad(c.Structure):
    _fields_ = [("buttons", w.WORD), ("left_trigger", c.c_ubyte),
                ("right_trigger", c.c_ubyte), ("left_x", c.c_short),
                ("left_y", c.c_short), ("right_x", c.c_short),
                ("right_y", c.c_short)]


class XState(c.Structure):
    _fields_ = [("packet", w.DWORD), ("gamepad", Gamepad)]


def windows_api():
    k = c.WinDLL("kernel32", use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    k.CreateToolhelp32Snapshot.restype = w.HANDLE
    for name, kind in (("Process32FirstW", ProcessEntry), ("Process32NextW", ProcessEntry),
                       ("Module32FirstW", ModuleEntry), ("Module32NextW", ModuleEntry)):
        fn = getattr(k, name)
        fn.argtypes = [w.HANDLE, c.POINTER(kind)]
        fn.restype = w.BOOL
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    k.OpenProcess.restype = w.HANDLE
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t,
                                    c.POINTER(c.c_size_t)]
    k.ReadProcessMemory.restype = w.BOOL
    k.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
    k.GetExitCodeProcess.restype = w.BOOL
    k.CloseHandle.argtypes = [w.HANDLE]
    k.CloseHandle.restype = w.BOOL
    return k


def snapshot(k, flags, pid, kind, prefix):
    handle = k.CreateToolhelp32Snapshot(flags, pid)
    if handle == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    try:
        item = kind()
        item.size = c.sizeof(item)
        ok = getattr(k, prefix + "FirstW")(handle, c.byref(item))
        while ok:
            yield kind.from_buffer_copy(item)
            ok = getattr(k, prefix + "NextW")(handle, c.byref(item))
    finally:
        k.CloseHandle(handle)


class Reader:
    def __init__(self, api, handle):
        self.api, self.handle = api, handle

    def read(self, address, size):
        if not address or address < 0x10000 or address + size > 0x100000000:
            return None
        buffer = c.create_string_buffer(size)
        got = c.c_size_t()
        if not self.api.ReadProcessMemory(self.handle, address, buffer, size, c.byref(got)):
            return None
        return buffer.raw if got.value == size else None

    def u32(self, address):
        data = self.read(address, 4)
        return struct.unpack("<I", data)[0] if data is not None else None

    def parent_value(self, parent, names):
        if not parent:
            return {"available": False, "reason": "uninitialized_or_unreadable_reference"}
        data = self.read(parent, 0x34)
        if data is None:
            return {"available": False, "reason": "unreadable_parent"}
        name_ptr = struct.unpack_from("<I", data, 0xc)[0]
        name_data = self.read(name_ptr, 48)
        name = name_data.split(b"\0", 1)[0].decode("ascii", "replace") if name_data else None
        if name not in names:
            return {"available": False, "reason": "unexpected_cvar_name", "name": name}
        return {"available": True, "name": name,
                "int": struct.unpack_from("<i", data, 0x30)[0],
                "float": struct.unpack_from("<f", data, 0x2c)[0]}


def read_xinput(api, slot):
    if api is None:
        return {"available": False, "connected": False}
    state = XState()
    error = api.XInputGetState(slot, c.byref(state))
    result = {"available": True, "connected": error == 0, "result": error}
    if error == 0:
        result.update(packet=state.packet,
                      **{key: getattr(state.gamepad, key) for key, _ in Gamepad._fields_})
    return result


def read_slot(reader, base, slot, xinput):
    player = reader.u32(base + 0x726bd8 + slot * 4)
    flag = reader.read(player + 0x128c, 1) if player else None
    suffix = "" if slot == 0 else "2"
    # The two native response helpers keep separate lazy ConVarRef caches.
    # response_look=1 normally initializes the second, leaving the first null.
    autoaim_refs = {name: reader.parent_value(reader.u32(base + rva + slot * 8),
                                              ("cl_autoaim" + suffix,))
                    for name, rva in (("response_12f900", 0x759c64),
                                      ("response_12fb10", 0x759c88))}
    autoaim = next((dict(value, cache=name) for name, value in autoaim_refs.items()
                    if value.get("available")),
                   {"available": False, "reason": "neither_native_response_cache_available"})
    return {"slot": slot, "player": hex(player) if player else None,
            "target": bool(flag[0]) if flag and flag[0] in (0, 1) else None,
            "target_raw": flag[0] if flag else None,
            "cl_autoaim": autoaim, "cl_autoaim_refs": autoaim_refs,
            "cl_inputmode": reader.parent_value(reader.u32(base + 0x759cac + slot * 8),
                                                 ("cl_inputmode" + suffix,)),
            "xinput": read_xinput(xinput, slot)}


def state_key(item):
    return (item["player"], item["target"], item["cl_autoaim"].get("int"),
            item["cl_inputmode"].get("int"), item["xinput"]["connected"])


def summary(samples):
    result = []
    for slot in range(2):
        items = [sample["players"][slot] for sample in samples]
        transitions, previous = [], None
        for sample, item in zip(samples, items):
            key = state_key(item)
            if key != previous:
                transitions.append({"seconds": sample["seconds"], "player": item["player"],
                                    "target": item["target"],
                                    "cl_autoaim": item["cl_autoaim"].get("int"),
                                    "cl_inputmode": item["cl_inputmode"].get("int"),
                                    "controller_connected": item["xinput"]["connected"]})
                previous = key
        moving = [i for i in items if i["xinput"].get("connected") and
                  max(abs(i["xinput"].get("right_x", 0)),
                      abs(i["xinput"].get("right_y", 0))) > 8689]
        result.append({"slot": slot, "samples": len(items),
                       "target_true_samples": sum(i["target"] is True for i in items),
                       "target_unavailable_samples": sum(i["target"] is None for i in items),
                       "right_stick_beyond_xinput_deadzone_samples": len(moving),
                       "target_true_during_right_stick_samples": sum(i["target"] is True for i in moving),
                       "inputmode_values": sorted({i["cl_inputmode"]["int"] for i in items
                                                   if i["cl_inputmode"].get("available")}),
                       "transitions": transitions})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, help="Override automatic left4dead2.exe selection")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--hz", type=float, default=20)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600 or not 1 <= args.hz <= 100:
        parser.error("Require 0 < seconds <= 3600 and 1 <= hz <= 100")
    k = windows_api()
    if args.pid is None:
        matches = [p.pid for p in snapshot(k, 2, 0, ProcessEntry, "Process32")
                   if p.name.lower() == "left4dead2.exe"]
        if len(matches) != 1:
            raise RuntimeError(f"Expected one left4dead2.exe, found {matches}; use --pid")
        args.pid = matches[0]
    modules = [m for m in snapshot(k, 0x8 | 0x10, args.pid, ModuleEntry, "Module32")
               if m.name.lower() == "client.dll"]
    if len(modules) != 1:
        raise RuntimeError("Expected one loaded client.dll")
    module = modules[0]
    digest = hashlib.sha256(Path(module.path).read_bytes()).hexdigest()
    if digest != SUPPORTED_CLIENT:
        raise RuntimeError(f"Unsupported client.dll SHA256: {digest}")
    h = k.OpenProcess(ACCESS, False, args.pid)
    if not h:
        raise c.WinError(c.get_last_error())
    try:
        reader = Reader(k, h)
        # Direct local-player accessor includes this exact relocated array.
        accessor = reader.read(module.base + 0x634e0, 3)
        if accessor != bytes.fromhex("558bec"):
            raise RuntimeError("Loaded client accessor does not match verified build")
        try:
            xinput = c.WinDLL("xinput1_4.dll")
            xinput.XInputGetState.argtypes = [w.DWORD, c.POINTER(XState)]
            xinput.XInputGetState.restype = w.DWORD
        except OSError:
            xinput = None
        record = {"pid": args.pid, "client_path": module.path,
                  "client_base": hex(module.base), "client_sha256": digest,
                  "access": "PROCESS_VM_READ|PROCESS_QUERY_INFORMATION; ReadProcessMemory and XInputGetState only",
                  "started_utc": datetime.now(timezone.utc).isoformat(),
                  "requested_seconds": args.seconds, "sample_hz": args.hz,
                  "global_cvars": {name: reader.parent_value(reader.u32(module.base + rva + 0x1c), (name,))
                                   for name, rva in (("cl_autoaim", 0x759b40),
                                                     ("cl_inputmode", 0x75a460),
                                                     ("joy_autoaimdampen", 0x7595e8),
                                                     ("joy_autoaimdampenrange", 0x7595a0))},
                  "samples": []}
        print(f"Read-only sampling PID {args.pid} for {args.seconds:g}s; client hash verified.", flush=True)
        start = time.monotonic()
        try:
            while time.monotonic() - start < args.seconds:
                exit_code = w.DWORD()
                if not k.GetExitCodeProcess(h, c.byref(exit_code)) or exit_code.value != 259:
                    record["stop_reason"] = "process_exited"
                    break
                elapsed = time.monotonic() - start
                record["samples"].append({"seconds": round(elapsed, 4),
                                          "players": [read_slot(reader, module.base, s, xinput)
                                                      for s in range(2)]})
                next_sample = start + len(record["samples"]) / args.hz
                time.sleep(max(0, min(next_sample - time.monotonic(),
                                      args.seconds - (time.monotonic() - start))))
        except KeyboardInterrupt:
            record["stop_reason"] = "interrupted"
        record.setdefault("stop_reason", "duration_complete")
        record["elapsed_seconds"] = round(time.monotonic() - start, 4)
        record["summary"] = summary(record["samples"])
    finally:
        k.CloseHandle(h)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output.resolve()), "summary": record["summary"]}, indent=2))


if __name__ == "__main__":
    main()
