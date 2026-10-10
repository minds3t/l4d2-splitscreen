"""Restore retained Xbox menu layouts for the menu package.

The extracted 360 resources establish the presentation. Current PC resources
remain the build input because their newer native handlers require additional
controls and command names.
Only platform layout conditions are fixed; language/device conditions survive.
"""
from copy import deepcopy
from pathlib import Path

import kv
import xbox_ui


UI = "resource/ui/l4d360ui/"
MAINMENU = UI + "mainmenu.res"
EDIT_MODES = ("coop", "realism", "survival", "versus", "scavenge")
FILES = (
    MAINMENU,
    UI + "ingamemainmenu.res",
    UI + "gamelobby.res",
    UI + "survivorlistitem.res",
    UI + "infectedlistitem.res",
    UI + "footerpanel.res",
    *(UI + f"gamesettings_{mode}edit.res" for mode in EDIT_MODES),
    *(UI + name + ".res" for name in (
        "dropdowncharacters", "dropdowncoopcharacters", "dropdownversuscharacters",
        "dropdownlobbygameaccess", "dropdownmission", "dropdownmissionextended",
        "dropdownmissionversus", "dropdownmissionversusextended",
        "dropdownmissionsurvival", "dropdownmissionsurvivalextended",
        "dropdownmissionscavenge", "dropdownmissionscavengeextended",
        "dropdownchapter", "dropdowndifficulty", "dropdownroundlimit",
        "dropdownservertype", "dropdownswitchmode4", "dropdownswitchmode8",
    )),
)


def _get(nodes, key):
    return next((v for n, v, _ in nodes if n.lower() == key.lower()), None)


def _set(nodes, key, value):
    nodes[:] = [(n, v, c) for n, v, c in nodes if n.lower() != key.lower()]
    nodes.append((key, value, None))


def _controls(nodes):
    return {n: v for n, v, _ in nodes
            if isinstance(v, list) and _get(v, "ControlName") is not None}


def _inert(nodes):
    # Native code can retain and update these pointers. Zero extent also keeps
    # a later SetVisible(true) from bringing a desktop-only control back onscreen.
    for key, value in (("visible", "0"), ("enabled", "0"), ("wide", "0"),
                       ("tall", "0"), ("tabPosition", "0"), ("command", "")):
        _set(nodes, key, value)
    for key in ("navUp", "navDown", "navLeft", "navRight"):
        _set(nodes, key, "")
    for child in _controls(nodes).values():
        _inert(child)


def _retain_pc_controls(xbox, desktop):
    """Keep lookup names and exact PC control types without visible PC clutter."""
    xc = _controls(xbox)
    for name, children, condition in desktop:
        if not isinstance(children, list):
            continue
        if _get(children, "ControlName") is not None:
            if name not in xc:
                compatible = deepcopy(children)
                _inert(compatible)
                xbox.append((name, compatible, condition))
                xc[name] = compatible
            else:
                # A console-only factory type cannot replace the object the PC
                # implementation casts. Geometry may still use console values.
                _set(xc[name], "ControlName", _get(children, "ControlName"))
                _retain_pc_controls(xc[name], children)
        else:
            match = next((v for n, v, _ in xbox if n == name and isinstance(v, list)), None)
            if match is not None:
                _retain_pc_controls(match, children)


def _specialize(nodes, profile):
    return kv.specialize(nodes, {n: n in profile for n in xbox_ui.PLATFORM_SYMBOLS})


def _root(nodes):
    roots = [v for _, v, _ in nodes if isinstance(v, list)]
    if len(roots) != 1:
        raise ValueError("Native menu resource must contain exactly one root block")
    return roots[0]


def _require(controls, *names):
    missing = set(names) - controls.keys()
    if missing:
        raise ValueError("Stock native menu controls changed: " + ", ".join(sorted(missing)))


def _copy_properties(target, source, names):
    target[:] = [(n, v, c) for n, v, c in target if n.lower() not in names]
    target.extend(deepcopy([(n, v, c) for n, v, c in source if n.lower() in names]))


def _mainmenu(controls, pc):
    _require(controls, "BtnGameModes", "BtnStatsAndAchievements", "BtnOptions",
             "BtnAddons", "BtnExtras", "BtnQuit", "BtnChangeGamers", "FlmOptionsFlyout")
    _require(pc, "FlmOptionsFlyout")
    # The options flyout keeps its PC rows (see menu_ui), so its first focus
    # must name one of them. The Xbox value names a row that is not there,
    # which leaves an opened flyout without keyboard or controller focus.
    _copy_properties(controls["FlmOptionsFlyout"], pc["FlmOptionsFlyout"], {"initialfocus"})
    # The PC carousel clips its caption; Valve explicitly documents the larger
    # text rectangle and negative z-order as necessary for mouse hit testing.
    _copy_properties(controls["BtnGameModes"], pc["BtnGameModes"],
                     {"zpos", "menutitlewide", "wideatopen", "rightarrowoffsetx"})
    _copy_properties(controls["BtnQuit"], pc["BtnQuit"], {"visible", "enabled"})
    for name in ("BtnChangeGamers", "PnlQuickJoin", "PnlQuickJoinGroups",
                 "LblPlayer1GamerTag", "LblPlayer2GamerTag", "LblPlayer2Enable",
                 "LblPlayer2DisableIcon", "LblPlayer2Disable"):
        if name in controls:
            _inert(controls[name])
    # Xbox sign-in and Xbox Live panels have no corresponding local PC action.
    # Keep the stock supported actions and the platform's normal Quit action.
    # BtnBlogPost starts hidden in the current stock resource. Do not send
    # controller focus through it; its stock mouse action remains available if
    # the game later supplies a blog entry. Addons is a shared visible control.
    order = ["BtnGameModes", "BtnStatsAndAchievements", "BtnOptions", "BtnAddons",
             "BtnExtras", "BtnQuit"]
    order = [name for name in order if name in controls
             and _get(controls[name], "wide") != "0"]
    for i, name in enumerate(order):
        _set(controls[name], "navUp", order[i - 1])
        _set(controls[name], "navDown", order[(i + 1) % len(order)])


def _settings(controls):
    _require(controls, "DrpMission", "DrpMissionExtended", "DrpChapter", "DrpServerType",
             "DrpSwitchMode", "BtnCancel")
    # PC updates replaced the old Xbox SwitchToCoop/Realism/etc buttons with
    # DrpSwitchMode, but left dangling Xbox navigation to the deleted names.
    order = ["DrpMission", "DrpMissionExtended", "DrpChapter"]
    order += [n for n in ("DrpDifficulty", "DrpRoundLimit") if n in controls]
    order += ["DrpServerType", "DrpSwitchMode"]
    for i, name in enumerate(order):
        _set(controls[name], "navUp", order[i - 1])
        _set(controls[name], "navDown", order[(i + 1) % len(order)])


def _lobby(controls, pc):
    _require(controls, "GameLobby", "GplSurvivors", "DrpCharacter", "DrpVersusCharacter",
             "BtnStartGame", "LobbyChatHistory", "ChatInputLine", "BtnVoiceButton",
             "BtnInviteFriends", "DrpInviteFriends", "FlmInviteFriends", "DrpGameAccess",
             "BtnChangeGameSettings")
    # Current PC resources widen this label for non-English languages. Retain
    # that text extent so German's full caption fits the PC clipping rectangle.
    _copy_properties(controls["BtnChangeGameSettings"], pc["BtnChangeGameSettings"], {"wide"})
    # Keep the supported Steam invitation action in the Xbox-sized row. The
    # retained console flyout dispatches Xbox Live UI that the PC cannot open.
    _inert(controls["DrpInviteFriends"])
    _inert(controls["FlmInviteFriends"])
    _copy_properties(controls["BtnInviteFriends"], pc["BtnInviteFriends"], {"visible", "enabled"})
    _set(controls["BtnInviteFriends"], "navDown", "DrpGameAccess")
    _set(controls["DrpGameAccess"], "navUp", "BtnInviteFriends")


def _pause(controls, pc):
    _require(controls, "BtnReturnToGame", "BtnInviteFriends", "FlmOptionsFlyout",
             "FlmInviteFriends", "PnlBackground")
    # Xbox's pause menu points at a different options flyout containing console
    # AudioVideo/Storage actions. Preserve the existing PC flyout, which already
    # includes the safe Controller dialog beside Keyboard/Mouse settings.
    _copy_properties(controls["FlmOptionsFlyout"], pc["FlmOptionsFlyout"],
                     {"resourcefile", "initialfocus"})
    _copy_properties(controls["BtnInviteFriends"], pc["BtnInviteFriends"],
                     {"command", "labeltext", "tooltiptext"})
    _inert(controls["FlmInviteFriends"])
    # The PC HybridButton renderer clips its text to the button rectangle.
    # At 1600x900 the ISO's width180 clips German achievements/exit captions;
    # retain the current PC text extent while keeping Xbox positions/background.
    for name, body in controls.items():
        if name in pc and _get(body, "ControlName") == "L4D360HybridButton":
            _copy_properties(body, pc[name], {"wide"})


def _character_flyout(controls, pc):
    _require(controls, "HeroPortrait", "PnlBackground")
    _require(pc, "HeroPortrait")
    # PC FlyoutMenu (GameUI RVA 0x2D8D0) compresses the menu height and
    # vgui::Button y positions by 0.7, leaving ImagePanels unchanged. The
    # Xbox portrait offset therefore hangs below these compacted rows. Use
    # Valve's PC offset while retaining the Xbox portrait size and assets.
    _copy_properties(controls["HeroPortrait"], pc["HeroPortrait"], {"ypos"})


def convert(text, relative):
    """Convert one current resource; preserve native command/control contracts."""
    nodes = kv.parse(text)
    xbox = _specialize(nodes, xbox_ui.X360)
    desktop = _specialize(nodes, xbox_ui.WIN32)
    _retain_pc_controls(xbox, desktop)
    # Some stock campaign resources contain only a #base directive.
    if not any(isinstance(v, list) for _, v, _ in nodes):
        return kv.dump(xbox) + "\n"
    controls, pc = _controls(_root(xbox)), _controls(_root(desktop))
    name = Path(relative).name.lower()
    if name == "mainmenu.res":
        _mainmenu(controls, pc)
    elif name == "ingamemainmenu.res":
        _pause(controls, pc)
    elif name == "gamelobby.res":
        _lobby(controls, pc)
    elif name in ("dropdowncharacters.res", "dropdowncoopcharacters.res",
                  "dropdownversuscharacters.res"):
        _character_flyout(controls, pc)
    elif name == "survivorlistitem.res":
        _require(controls, "DrpPlayer", "ImgReady", "PnlGamerPic")
        button = _get(controls["DrpPlayer"], "BtnDropButton")
        if not isinstance(button, list):
            raise ValueError("Native lobby player dropdown button is missing")
        # ISO SurvivorListItem uses MainMenuButton. Keep the newer client ready
        # icon and its offsets, but restore its original name-label typography.
        _set(button, "style", "MainMenuButton")
        # The stock 15-unit parent/button clips descenders in the PC renderer.
        # Native row code recenters DrpPlayer using its measured font height,
        # but never enlarges either clip box. Give both controls 22 units of
        # space inside the unchanged 28-unit player row; keep native fonts.
        _set(controls["DrpPlayer"], "tall", "22")
        _set(button, "tall", "22")
    elif name.startswith("gamesettings_") and name.endswith("edit.res"):
        _settings(controls)
    elif name == "footerpanel.res":
        _require(controls, "LblHelpText")
        # Every window sets this label's visibility itself when it becomes
        # active (FooterPanel::SetButtons). A menu reload re-applies this file
        # without re-activating the window; starting hidden keeps the console
        # main menu free of the PC tooltip strip after such a reload.
        _set(controls["LblHelpText"], "visible", "0")
    return ("// Generated native-lobby overlay: retained Xbox layout with explicit PC compatibility.\n"
            + kv.dump(xbox) + "\n")


def generate(read_stock, out_dir):
    """Run after the native package's main-menu tile has been inserted.

    Read existing mainmenu.res only so its custom tile and anchors survive.
    Other menu resource inputs come from stock. The shared scheme is cached
    before this addon's override becomes active, so native presentation hooks
    handle the console caption insets without replacing that scheme or fonts.
    """
    written = []
    for relative in FILES:
        target = Path(out_dir) / relative
        if relative == MAINMENU and target.exists():
            text = target.read_text(encoding="utf-8-sig")
        else:
            text = read_stock(relative)
        converted = convert(text, relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(converted, encoding="utf-8", newline="\n")
        written.append(relative)
    return written
