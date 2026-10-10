"""Assemble the folder a user starts the game from: for release, or for development.

    python tools/packaging/build_release.py
    python tools/packaging/build_release.py --dev

dist/SplitscreenSupport-v<version>/      ready to run, also zipped beside it
    Start Local Co-op.cmd, Install Shortcuts.cmd, Uninstall.cmd, README.txt
    config.ini                           the user's settings
    files/                               launcher script, plugin and its checker,
                                         four packages, researched game build,
                                         licenses

For the release the packages are rebuilt from Valve's files alone (no mods
mounted on this machine) and the working build is restored afterwards.

--dev writes the same folder to build/dev/ from the working build, so a
development session runs the launcher users get. Its config.ini starts the game
with a console port and a console log for tools/game/, and is kept from one
build to the next.

The plugin is taken as built: run plugin/build.ps1 first.
"""
from pathlib import Path
import json
import re
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_packages as packages

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "release"
PLUGIN_DIR = ROOT / "plugin"
PLUGIN = PLUGIN_DIR / "build" / "Release" / "splitscreen_support.dll"
CHECKER = PLUGIN_DIR / "build" / "Release" / "splitscreen_check.exe"
DIST = ROOT / "dist"
DEV = ROOT / "build" / "dev"
# What tools/game/netcon.py and the console log need.
DEV_ARGUMENTS = "-novid -netconport 27999 -condebug"
GAME_VERSION = "2.2.4.3"
# Release name of each package the builder writes.
PACKAGES = {
    "hud.vpk": packages.OUT_VPK, "menus.vpk": packages.MENU_VPK,
    "hud_pc.vpk": packages.PC_VPK, "menus_pc.vpk": packages.PC_MENU_VPK,
}


def supported_build(header):
    """The module table of supported_build.h as the launcher reads it."""
    rows = re.findall(r'\{"([^"]+)",\s*"([^"]+)",\s*"([0-9a-f]{64})"\}', header)
    if len(rows) < 6:
        raise ValueError("Could not read the supported game build table")
    return {"game": GAME_VERSION,
            "modules": [{"name": name, "path": path.replace("/", "\\"), "sha256": sha} for name, path, sha in rows]}


def stale_plugin():
    """Plugin sources newer than the built plugin."""
    if not PLUGIN.is_file():
        raise ValueError("Build the plugin first: plugin/build.ps1")
    built = PLUGIN.stat().st_mtime
    sources = [path for pattern in ("src/*.cpp", "src/*.h", "include/*.h", "include/*.inc", "CMakeLists.txt", "exports.def")
               for path in PLUGIN_DIR.glob(pattern)]
    return sorted(path.name for path in sources if path.stat().st_mtime > built)


def _windows_text(source, target):
    """Copy a script with CRLF line ends, whatever the checkout made of them."""
    lines = source.read_bytes().replace(b"\r\n", b"\n").split(b"\n")
    target.write_bytes(b"\r\n".join(lines))


def assemble(target, version, game_arguments=""):
    """Copy everything a user needs into target (package files must exist)."""
    files = target / "files"
    (files / "licenses").mkdir(parents=True)
    for path in SOURCE.iterdir():
        if path.is_file() and path.name != "README.txt":
            _windows_text(path, target / path.name)
    readme = (SOURCE / "README.txt").read_text(encoding="utf-8")
    readme = readme.replace("@VERSION@", version)
    if "@" in readme:
        raise ValueError("Unfilled placeholder in README.txt")
    (target / "README.txt").write_text(readme, encoding="utf-8", newline="\r\n")
    settings = (PLUGIN_DIR / "config.ini").read_text(encoding="utf-8")
    # In a release the file sits beside the starter and is copied per session.
    settings = settings.replace(
        "; Read each time local co-op is switched on: at game start, and when it is\n"
        "; switched on under Options. Remove a line (or this file) for the default.\n",
        "; Changes apply the next time the game is started with the launcher.\n"
        "; Remove a line for its default.\n")
    if "switched on under Options" in settings:
        raise ValueError("Review the settings file header")
    settings += ("\n[Launcher]\n"
                 "; Extra game options for starts through this launcher, for example -novid\n"
                 f"GameArguments={game_arguments}\n")
    (target / "config.ini").write_text(settings, encoding="utf-8", newline="\r\n")
    _windows_text(SOURCE / "files" / "SplitscreenSupport.ps1", files / "SplitscreenSupport.ps1")
    shutil.copyfile(PLUGIN, files / "splitscreen_support.dll")
    shutil.copyfile(CHECKER, files / "splitscreen_check.exe")
    for name, built in PACKAGES.items():
        shutil.copyfile(built, files / name)
    table = supported_build((PLUGIN_DIR / "include" / "supported_build.h").read_text(encoding="utf-8"))
    (files / "supported_build.json").write_text(json.dumps(table, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(PLUGIN_DIR / "vendor" / "minhook" / "LICENSE.txt", files / "licenses" / "MinHook.txt")
    _windows_text(ROOT / "LICENSE", files / "licenses" / "SplitscreenSupport.txt")
    return sorted(path.relative_to(target).as_posix() for path in target.rglob("*") if path.is_file())


def assemble_dev(target, version):
    """The same folder for development; settings made in it are kept."""
    kept = target / "config.ini"
    settings = kept.read_bytes() if kept.is_file() else None
    if target.exists():
        shutil.rmtree(target)
    contents = assemble(target, version + " (development build)", DEV_ARGUMENTS)
    if settings is not None:
        kept.write_bytes(settings)
    return contents


def main():
    version = packages.VERSION
    stale = stale_plugin()
    if stale:
        raise SystemExit("The plugin is older than its sources (" + ", ".join(stale) + "); run plugin/build.ps1")
    builder = [sys.executable, str(Path(packages.__file__).resolve())]
    if "--dev" in sys.argv[1:]:
        # This machine's working build, on top of its mounted mods.
        subprocess.run(builder, check=True, stdout=subprocess.DEVNULL)
        assemble_dev(DEV, version)
        print("development folder:", DEV)
        return
    target = DIST / f"SplitscreenSupport-v{version}"
    archive = DIST / f"SplitscreenSupport-v{version}.zip"
    if target.exists():
        shutil.rmtree(target)
    try:
        subprocess.run(builder + ["--stock-only"], check=True, stdout=subprocess.DEVNULL)
        contents = assemble(target, version)
    finally:
        # Back to this machine's working build (on top of its mounted mods).
        subprocess.run(builder, check=True, stdout=subprocess.DEVNULL)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for relative in contents:
            bundle.write(target / relative, f"{target.name}/{relative}")
    print("release folder:", target)
    print("release archive:", archive, f"({archive.stat().st_size // 1024} KiB, {len(contents)} files)")
    for relative in contents:
        print("  ", relative)


if __name__ == "__main__":
    main()
