"""Build the packages that go with the plugin.

    python tools/packaging/build_packages.py [--stock-only]

build/hud.vpk      HUD layouts and the player-two script.
build/menus.vpk    Console menu overlay. It hides PC-only controls (lobby
                   chat, voice, quick join) and expects the plugin.

The launcher installs both for one game session only: a normal Steam start
loads no plugin and should find the stock game, HUD included. None of the
packages has an addoninfo.txt: the game mounts them all the same, and they do
not appear in its Add-ons list.

The same pair for the PC look (config.ini, ConsoleUI=0): Valve's PC layouts
with only what local co-op needs. The launcher installs one pair or the other.

build/hud_pc.vpk    The script, and the vote panel that can show a controller
                    player their buttons.
build/menus_pc.vpk  The PC menus plus the offline tile and rows, the co-op
                    switch, the split direction page and player two's
                    pause-menu rows.

Everything is generated from Valve's current files plus packages/. The
menus follow the Xbox game: the main menu's own offline tile (SINGLE PLAYER, or
OFFLINE CO-OP while the plugin reports two local players), each mode's own
offline row, and the split direction on the console's Audio/Video page.

By default the build sits on top of mods mounted in the game's gameinfo.txt, so
they keep working on this machine; --stock-only uses Valve's files alone.
"""
from copy import deepcopy
from pathlib import Path
import shutil

import game_files
import kv
import vpk1
import xbox_ui


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "packages"
OUT_DIR = ROOT / "build" / "hud"
OUT_VPK = ROOT / "build" / "hud.vpk"
MENU_DIR = ROOT / "build" / "menus"
MENU_VPK = ROOT / "build" / "menus.vpk"
PC_DIR = ROOT / "build" / "hud_pc"
PC_VPK = ROOT / "build" / "hud_pc.vpk"
PC_MENU_DIR = ROOT / "build" / "menus_pc"
PC_MENU_VPK = ROOT / "build" / "menus_pc.vpk"
# The one HUD file the PC look takes from the console set: its vote panel has
# the controller prompts next to the PC ones, and the plugin shows each player
# the pair for their device.
PC_HUD_FILES = ("resource/ui/hud/votehud.res",)
VERSION = "2"

UI = "resource/ui/l4d360ui/"
# Everything the plugin-scoped menus need; all other files form the HUD package.
MENU_PREFIXES = (UI,)
MAINMENU = UI + "mainmenu.res"
OPTIONS_FLYOUT = UI + "optionsflyout.res"
INGAME_FLYOUT = UI + "optionsflyoutingame.res"
PAUSE_MENU = UI + "ingamemainmenu.res"
AUDIO_VIDEO = UI + "audiovideo.res"
DIRECTION_DROPDOWN = UI + "dropdownsplitscreendirection.res"
# Stock files whose Xbox branch only matters with a controller in hand.
EXTRA_XBOX_FILES = (UI + "controllerbindlistitem.res", "scripts/tips.txt")

PC = {name: name in xbox_ui.WIN32 for name in xbox_ui.PLATFORM_SYMBOLS}

# The tile Valve's file still carries for two local players, and the one-player
# variant from the Xbox disc that the current file only has as a comment.
SPLITSCREEN = "$X360SPLITSCREEN"
SINGLE_PLAYER_TILE = (
    ("id", "BtnPlaySolo"),
    ("name", "#L4D360UI_ModeCaps_offline_SP"),
    ("image", "vgui/menu_mode_singleplayer"),
    ("command", "SoloPlay"),
    ("menutitle", "#L4D360UI_MainMenu_PlaySolo"),
    ("menuhint", "#L4D360UI_MainMenu_PlaySolo_Tip"),
)

# Flyout rows: (name, [(condition, label, tooltip, command), ...]).
# The Xbox Audio/Video page holds the split direction; the plugin's package
# reduces that page to this one setting (the PC has its own video and audio pages).
DIRECTION_ROW = ("BtnSplitScreenDirection", [
    (None, "#L4D360UI_VideoOptions_SplitScreenDirection",
     "#L4D360UI_VideoOptions_Tooltip_SplitScreenDirection", "AudioVideo")])
# One row that switches local co-op off or on. The Xbox adds and removes player
# two by signing in; on PC the plugin is switched instead. The menu is reloaded
# on either change, which re-evaluates the condition.
SWITCH_ROW = ("BtnSplitscreenSwitch", [
    (SPLITSCREEN, "#L4D360UI_MainMenu_DisableSplitscreen",
     "#L4D360UI_MainMenu_DisableSplitscreen_Tip", "#splitscreen_disable;play ui/menu_accept"),
    ("!" + SPLITSCREEN, "#L4D360UI_MainMenu_EnableSplitscreen",
     "#L4D360UI_MainMenu_EnableSplitscreen_Tip", "#splitscreen_enable;play ui/menu_accept")])
# The retained Xbox commands EnableSplitscreen / DisableSplitscreen leave the
# game after a confirmation, and the native pause menu hides their buttons by
# name and by command using its own one-user count. The plugin instead handles
# these two commands of its own with an in-place drop-in / drop-out.
DROP_ROWS = (
    ("BtnPlayerTwoJoin", [(None, "#L4D360UI_MainMenu_EnableSplitscreen",
                           "#L4D360UI_MainMenu_EnableSplitscreen_Tip", "PlayerTwoJoin")]),
    ("BtnPlayerTwoLeave", [(None, "#L4D360UI_MainMenu_DisableSplitscreen",
                            "#L4D360UI_MainMenu_DisableSplitscreen_Tip", "PlayerTwoLeave")]),
)
# "Bildschirmteilung deaktivieren" needs more than the stock 150 units.
OPTIONS_EXTRA_WIDTH = 60
# The PC flyouts of Campaign, Realism and Survival have an offline row (the
# disc has none: its only offline entry is the main-menu tile). The four team
# modes get the same row and command family (Campaign: SoloPlay_coop).
STOCK_OFFLINE_FLYOUTS = (UI + "campaignflyout.res", UI + "realismflyout.res", UI + "survivalflyout.res")
OFFLINE_MODES = {"versus": UI + "versusflyout.res", "scavenge": UI + "scavengeflyout.res",
                 "mutation12": UI + "realismversusflyout.res",    # Realism Versus
                 "mutation15": UI + "versussurvivalflyout.res"}   # Versus Survival
# Valve's two names for the offline lobby, switched like the main-menu tile.
OFFLINE_NAMES = (
    ("!" + SPLITSCREEN, "#L4D360UI_Mode_offline_SP", "#L4D360UI_MainMenu_PlaySolo_Tip"),
    (SPLITSCREEN, "#L4D360UI_Mode_offline_SS", "#L4D360UI_MainMenu_OfflineCoOp_Tip"))


def _on_pc(condition):
    return condition is None or kv._resolve(kv._condition(condition), PC, None) is not False


def _get(body, key):
    return next((value for name, value, condition in body
                 if name.lower() == key.lower() and _on_pc(condition)), None)


def _put(body, key, value):
    """Replace the PC value of an existing key; other platforms keep theirs."""
    found = False
    for index, (name, old, condition) in enumerate(body):
        if name.lower() == key.lower() and _on_pc(condition):
            body[index] = (name, value, condition)
            found = True
    if not found:
        raise ValueError("Flyout control lacks " + key)


def _root(nodes):
    roots = [index for index, (_, value, _) in enumerate(nodes) if isinstance(value, list)]
    if len(roots) != 1:
        raise ValueError("Resource must have exactly one root block")
    return roots[0]


def offline_tile(text):
    """Give the main menu both variants of its offline tile, as on the disc."""
    nodes = kv.parse(text)
    controls = nodes[_root(nodes)][1]
    carousel = next((body for name, body, _ in controls
                     if name == "BtnGameModes" and isinstance(body, list)), None)
    if carousel is None:
        raise ValueError("BtnGameModes not found in mainmenu.res - Valve changed the menu layout")
    tiles = [(index, body, condition) for index, (name, body, condition) in enumerate(carousel)
             if name == "mode" and isinstance(body, list)
             and dict((key, value) for key, value, _ in body).get("id") == "BtnPlaySolo"]
    if len(tiles) != 1 or tiles[0][2] != SPLITSCREEN or _get(tiles[0][1], "command") != "SoloPlay":
        raise ValueError("Review the stock offline tile in mainmenu.res")
    single = [(key, value, None) for key, value in SINGLE_PLAYER_TILE]
    carousel.insert(tiles[0][0], ("mode", single, "!" + SPLITSCREEN))
    return kv.dump(nodes) + "\n"


def append_pc_rows(text, rows, extra_width=0, root_name=None):
    """Append rows to the PC branch of a flyout, keeping its navigation loop."""
    nodes = kv.parse(text)
    root = _root(nodes)
    controls = nodes[root][1]
    if root_name:
        nodes[root] = (root_name, controls, nodes[root][2])
    buttons = [(name, body, condition) for name, body, condition in controls
               if isinstance(body, list) and _on_pc(condition)
               and _get(body, "ControlName") == "L4D360HybridButton"
               and _get(body, "ypos") is not None]
    background = next((body for name, body, condition in controls
                       if name == "PnlBackground" and isinstance(body, list) and _on_pc(condition)), None)
    if background is None or not buttons or any(name == row[0] for name, _, _ in buttons for row in rows):
        raise ValueError("Review flyout rows")
    buttons.sort(key=lambda item: int(_get(item[1], "ypos")))
    first, last = buttons[0], buttons[-1]
    pitch = int(_get(last[1], "tall"))
    if _get(last[1], "navDown") != first[0] or _get(first[1], "navUp") != last[0]:
        raise ValueError("Review flyout navigation loop")
    replaced = {"fieldname", "labeltext", "tooltiptext", "disabled_tooltiptext", "command",
                "navup", "navdown", "ypos"}
    previous = last
    for index, (name, variants) in enumerate(rows, 1):
        body = [node for node in deepcopy(last[1]) if node[0].lower() not in replaced]
        body += [("fieldName", name, None), ("navUp", previous[0], None), ("navDown", first[0], None),
                 ("ypos", str(int(_get(last[1], "ypos")) + index * pitch), None)]
        for condition, label, tooltip, command in variants:
            body += [("labelText", label, condition), ("tooltiptext", tooltip, condition),
                     ("command", command, condition)]
        _put(previous[1], "navDown", name)
        controls.append((name, body, last[2]))
        previous = (name, body)
    _put(first[1], "navUp", previous[0])
    _put(background, "tall", str(int(_get(background, "tall")) + len(rows) * pitch))
    if extra_width:
        for body in [background] + [body for _, body, _ in buttons] + \
                [body for name, body, _ in controls if any(name == row[0] for row in rows)]:
            for index, (key, value, condition) in enumerate(body):
                if key.lower() == "wide" and _on_pc(condition):
                    body[index] = (key, str(int(value) + extra_width), condition)
    return kv.dump(nodes) + "\n"


def main_options_flyout(text):
    """Options flyout of the main menu: split direction and the co-op switch."""
    return append_pc_rows(text, (DIRECTION_ROW, SWITCH_ROW), OPTIONS_EXTRA_WIDTH)


def ingame_options_flyout(text):
    """Pause-menu copy with the two player-2 rows.

    No split direction here: the native page disables that setting in a game.
    """
    return append_pc_rows(text, DROP_ROWS, OPTIONS_EXTRA_WIDTH,
                          "Resource/UI/OptionsFlyoutIngame.res")


def offline_mode_flyout(text, mode):
    """Add the offline row the other modes already have."""
    return append_pc_rows(text, (("BtnSinglePlayer", [
        (condition, label, tooltip, "SoloPlay_" + mode) for condition, label, tooltip in OFFLINE_NAMES]),))


def offline_row_names(text):
    """Name a stock offline row for one player or for two."""
    nodes = kv.parse(text)
    rows = [body for name, body, condition in nodes[_root(nodes)][1]
            if name == "BtnSinglePlayer" and isinstance(body, list) and _on_pc(condition)]
    if len(rows) != 1:
        raise ValueError("Review the stock offline row")
    body = rows[0]
    for key, column in (("labelText", 1), ("tooltiptext", 2)):
        found = [index for index, (name, _, condition) in enumerate(body) if name.lower() == key.lower()]
        if len(found) != 1 or body[found[0]][1] != OFFLINE_NAMES[0][column] or body[found[0]][2] is not None:
            raise ValueError("Review the stock offline row's " + key)
        body[found[0]:found[0] + 1] = [(key, variant[column], variant[0]) for variant in OFFLINE_NAMES]
    return kv.dump(nodes) + "\n"


def use_ingame_flyout(text):
    """Point the pause menu's options flyout at the in-game copy."""
    nodes = kv.parse(text)
    controls = nodes[_root(nodes)][1]
    flyouts = [body for name, body, _ in controls if name == "FlmOptionsFlyout" and isinstance(body, list)]
    if len(flyouts) != 1:
        raise ValueError("Review pause menu options flyout")
    resources = [index for index, (key, _, condition) in enumerate(flyouts[0])
                 if key.lower() == "resourcefile" and _on_pc(condition)]
    if len(resources) != 1:
        raise ValueError("Review pause menu options flyout resource")
    key, _, cond = flyouts[0][resources[0]]
    flyouts[0][resources[0]] = (key, "resource/UI/L4D360UI/OptionsFlyoutIngame.res", cond)
    return kv.dump(nodes) + "\n"


def split_direction_page(converted):
    """Reduce the console's Audio/Video page to its split direction setting.

    The native dialog initializes every control it finds, so the others stay
    present but inert; the PC keeps them on its own video and audio pages.
    """
    import console_menus
    nodes = kv.parse(converted)
    controls = console_menus._controls(console_menus._root(nodes))
    console_menus._require(controls, "AudioVideo", "DrpSplitScreenDirection", "FlmSplitScreenDirection",
                            "SldBrightness")
    top = console_menus._get(controls["SldBrightness"], "ypos")
    for name, body in controls.items():
        if name not in ("AudioVideo", "DrpSplitScreenDirection", "FlmSplitScreenDirection"):
            console_menus._inert(body)
    direction = controls["DrpSplitScreenDirection"]
    console_menus._set(direction, "ypos", top)
    console_menus._set(direction, "navUp", "")
    console_menus._set(direction, "navDown", "")
    return kv.dump(nodes) + "\n"


def split_packages(out_dir, menu_dir):
    """Move the plugin-scoped menu files into their own package directory."""
    out_dir, menu_dir = Path(out_dir), Path(menu_dir)
    moved = []
    for source in sorted(out_dir.rglob("*")):
        relative = source.relative_to(out_dir).as_posix()
        if source.is_file() and relative.lower().startswith(MENU_PREFIXES):
            target = menu_dir / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(source), str(target))
            moved.append(relative)
    for folder in sorted((p for p in out_dir.rglob("*") if p.is_dir()), reverse=True):
        if not any(folder.iterdir()):
            folder.rmdir()
    return moved


def _fresh(directory):
    if directory.exists():
        if ROOT.resolve() not in directory.resolve().parents:
            raise ValueError("Build directory is outside the workspace")
        shutil.rmtree(directory)
    directory.mkdir(parents=True)


def _write(relative, text, directory=None):
    target = (directory or OUT_DIR) / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def offline_rows(directory=None):
    """Offline row of every mode flyout that can have one, named like the tile."""
    written = []
    for mode, relative in OFFLINE_MODES.items():
        _write(relative, offline_mode_flyout(_stock(relative), mode), directory)
        written.append(relative)
    for relative in STOCK_OFFLINE_FLYOUTS:
        _write(relative, offline_row_names(_stock(relative)), directory)
        written.append(relative)
    return written


def build_pc_menus(directory):
    """Valve's PC menus with only the additions local co-op needs."""
    import menu_ui
    import mode_ui
    import console_menus

    written = [MAINMENU]
    _write(MAINMENU, offline_tile(_stock(MAINMENU)), directory)
    # Controller options for each player, and offline settings for team modes.
    written += menu_ui.generate(game_files.read_stock, directory)
    written += mode_ui.generate(game_files.read_stock, directory)
    options = (directory / OPTIONS_FLYOUT).read_text(encoding="utf-8-sig")
    _write(INGAME_FLYOUT, ingame_options_flyout(options), directory)
    _write(OPTIONS_FLYOUT, main_options_flyout(options), directory)
    _write(PAUSE_MENU, use_ingame_flyout(_stock(PAUSE_MENU)), directory)
    # The split direction has no PC page: the console's one, as in the console set.
    _write(AUDIO_VIDEO, split_direction_page(console_menus.convert(_stock(AUDIO_VIDEO), AUDIO_VIDEO)), directory)
    _write(DIRECTION_DROPDOWN, console_menus.convert(_stock(DIRECTION_DROPDOWN), DIRECTION_DROPDOWN), directory)
    written += [INGAME_FLYOUT, PAUSE_MENU, AUDIO_VIDEO, DIRECTION_DROPDOWN]
    written += offline_rows(directory)
    return written


def _stock(relative):
    return game_files.read_stock(relative).replace("\r\n", "\n")


def build():
    import menu_ui
    import mode_ui
    import console_hud
    import console_menu_extras
    import console_menus

    _fresh(OUT_DIR)
    shutil.copytree(SOURCE, OUT_DIR, dirs_exist_ok=True)

    # Xbox HUD layouts from the current stock files.
    for rel in xbox_ui.generate(game_files.read_stock, OUT_DIR):
        print("xbox ui:", rel)
    for rel in console_hud.generate(OUT_DIR):
        print("console inventory:", rel)

    # Menus. console_menus reads this main menu instead of the stock one.
    _write(MAINMENU, offline_tile(_stock(MAINMENU)))
    for rel in menu_ui.generate(game_files.read_stock, OUT_DIR):
        print("menu ui:", rel)
    for rel in mode_ui.generate(game_files.read_stock, OUT_DIR):
        print("mode ui:", rel)
    for rel in console_menus.generate(game_files.read_stock, OUT_DIR):
        print("console menu:", rel)
    for rel in console_menu_extras.generate(game_files.read_stock, OUT_DIR):
        print("console options:", rel)

    options = (OUT_DIR / OPTIONS_FLYOUT).read_text(encoding="utf-8-sig")
    _write(INGAME_FLYOUT, ingame_options_flyout(options))
    _write(OPTIONS_FLYOUT, main_options_flyout(options))
    pause = OUT_DIR / PAUSE_MENU
    _write(PAUSE_MENU, use_ingame_flyout(pause.read_text(encoding="utf-8-sig")))
    print("console options rows:", OPTIONS_FLYOUT, INGAME_FLYOUT)
    _write(AUDIO_VIDEO, split_direction_page(console_menus.convert(_stock(AUDIO_VIDEO), AUDIO_VIDEO)))
    _write(DIRECTION_DROPDOWN, console_menus.convert(_stock(DIRECTION_DROPDOWN), DIRECTION_DROPDOWN))
    print("console split direction:", AUDIO_VIDEO)
    for relative in offline_rows():
        print("offline row:", relative)
    for relative in EXTRA_XBOX_FILES:
        converted = xbox_ui.convert(_stock(relative))
        if converted is None:
            raise ValueError("Stock file no longer has an Xbox variant: " + relative)
        _write(relative, converted)
        print("console extra:", relative)

    _fresh(MENU_DIR)
    moved = split_packages(OUT_DIR, MENU_DIR)
    vpk1.pack(str(OUT_DIR), str(OUT_VPK))
    vpk1.pack(str(MENU_DIR), str(MENU_VPK))
    print("HUD package:", OUT_VPK)
    print("menu package:", MENU_VPK, f"({len(moved)} files)")

    # The PC look: the script and the vote panel; PC menus with the additions.
    _fresh(PC_DIR)
    shutil.copytree(SOURCE, PC_DIR, dirs_exist_ok=True)
    for relative in PC_HUD_FILES:
        (PC_DIR / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(OUT_DIR / relative, PC_DIR / relative)
    _fresh(PC_MENU_DIR)
    pc_menus = build_pc_menus(PC_MENU_DIR)
    vpk1.pack(str(PC_DIR), str(PC_VPK))
    vpk1.pack(str(PC_MENU_DIR), str(PC_MENU_VPK))
    print("HUD package, PC look:", PC_VPK)
    print("menu package, PC look:", PC_MENU_VPK, f"({len(pc_menus)} files)")


if __name__ == "__main__":
    import sys
    # Default: build for this machine, on top of mods mounted in gameinfo.txt.
    # --stock-only builds the redistributable package from Valve's files alone.
    mods = [] if "--stock-only" in sys.argv else game_files.mod_paths()
    if mods:
        print("building on top of mounted mods:", ", ".join(mods))
        game_files.STOCK_PATHS[:0] = mods
    build()
