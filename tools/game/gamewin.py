"""Send input messages directly to the L4D2 window (works without screen access).
usage: gamewin.py key VK_NAME | click X Y | move X Y   (X/Y in game client pixels)"""
import ctypes, sys, time
from ctypes import wintypes
u32 = ctypes.windll.user32
WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x100, 0x101, 0x102
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x200, 0x201, 0x202
VK = {"RETURN": 0x0D, "ESCAPE": 0x1B, "DOWN": 0x28, "UP": 0x26, "LEFT": 0x25, "RIGHT": 0x27, "TAB": 0x09, "SPACE": 0x20,
      "F6": 0x75, "F7": 0x76, "F8": 0x77, "W": 0x57}

def find():
    pid_target = None
    import subprocess
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq left4dead2.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    pid_target = int(out.split(",")[1].strip('"'))
    found = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, _):
        pid = wintypes.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == pid_target and u32.IsWindowVisible(h):
            n = ctypes.create_unicode_buffer(256); u32.GetClassNameW(h, n, 256); found.append((h, n.value))
        return True
    u32.EnumWindows(cb, 0)
    return found

wins = find()
hwnd = next((h for h, c in wins if c == "Valve001"), wins[0][0] if wins else None)
if hwnd is None: sys.exit("no game window")
cmd = sys.argv[1]
if cmd == "list": print(wins)
elif cmd == "key":
    vk = VK[sys.argv[2].upper()]
    sc = u32.MapVirtualKeyW(vk, 0)
    u32.PostMessageW(hwnd, WM_KEYDOWN, vk, 1 | (sc << 16)); time.sleep(0.05)
    u32.PostMessageW(hwnd, WM_KEYUP, vk, 1 | (sc << 16) | (0xC0 << 24))
elif cmd in ("click", "move"):
    x, y = int(sys.argv[2]), int(sys.argv[3]); lp = (y << 16) | (x & 0xFFFF)
    u32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp); time.sleep(0.15)
    if cmd == "click":
        u32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lp); time.sleep(0.05)
        u32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp)
print("ok", hex(hwnd) if hwnd else None)
