"""Read Valve's current files from the installed game (loose files and VPKs)."""
import os
import re

import vpk

DEFAULT_GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2"


def _steam_folders():
    try:
        import winreg
    except ImportError:
        return
    for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                      (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
                      (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam")):
        for value in ("SteamPath", "InstallPath"):
            try:
                with winreg.OpenKey(hive, key) as handle:
                    yield winreg.QueryValueEx(handle, value)[0]
            except OSError:
                pass


def _read(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def find_game():
    """The game folder: L4D2_DIR if set, else the Steam library that holds the
    game (found the way the launcher finds it), else Steam's default folder."""
    override = os.environ.get("L4D2_DIR")
    if override:
        return override
    for steam in _steam_folders():
        libraries = [steam]
        index = os.path.join(steam, "steamapps", "libraryfolders.vdf")
        if os.path.exists(index):
            libraries += [found.replace("\\\\", "\\") for found in re.findall(r'"path"\s+"([^"]+)"', _read(index))]
        for library in libraries:
            manifest = os.path.join(library, "steamapps", "appmanifest_550.acf")
            if not os.path.exists(manifest):
                continue
            folder = re.search(r'"installdir"\s+"([^"]+)"', _read(manifest))
            candidate = os.path.join(library, "steamapps", "common", folder.group(1)) if folder else ""
            if candidate and os.path.exists(os.path.join(candidate, "left4dead2.exe")):
                return os.path.normpath(candidate)
    return DEFAULT_GAME


GAME = find_game()
# Search order of the stock game (without user mods such as 4k_ui). Builds may
# put mounted mods in front of this list in place (see mod_paths).
STOCK_PATHS = ["update", "left4dead2_dlc3", "left4dead2_dlc2", "left4dead2_dlc1", "left4dead2"]


def mod_paths():
    """Search paths a user mounted ahead of the stock game in gameinfo.txt.

    Such a mod (e.g. a 4K HUD) replaces files a build converts. Converting the
    plain stock file instead would silently undo the mod on this machine.
    """
    found = []
    info = os.path.join(GAME, "left4dead2", "gameinfo.txt")
    for line in open(info, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*Game\s+(\S+)\s*(?://.*)?$", line)
        if not m:
            continue
        name = m.group(1).strip('"')
        if name.lower() in ("update", "left4dead2", "hl2", "|gameinfo_path|.") or name.lower().startswith("left4dead2_dlc"):
            continue
        if os.path.isdir(os.path.join(GAME, name)) and name not in found:
            found.append(name)
    return found


def read_stock(path):
    for d in STOCK_PATHS:
        loose = os.path.join(GAME, d, path)
        if os.path.exists(loose):
            return open(loose, "rb").read().decode("utf-8")
        archive = os.path.join(GAME, d, "pak01_dir.vpk")
        if not os.path.exists(archive):
            continue
        pak = vpk.open(archive, path_enc="latin-1")
        if path in pak:
            return pak[path].read().decode("utf-8")
    raise FileNotFoundError(path)
