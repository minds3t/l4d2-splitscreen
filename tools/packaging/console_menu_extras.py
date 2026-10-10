"""Small Xbox presentation corrections outside the shared lobby flow.

Current PC resources remain authoritative for controls, labels and commands.
The vote dialogs retain Valve's own Xbox layout. The separate PC option pages
have no usable Xbox equivalent (the console uses a combined AudioVideo page).
Keep their functions and native fonts, with readable columns and compact
content bounds for CL4DFrame's console-footer-relative positioning.
"""
from pathlib import Path

import kv
import console_menus


UI = "resource/ui/l4d360ui/"
VOTE_DIALOGS = ("ingamechapterselect.res", "ingamedifficultyselect.res")
VOTE_FLYOUTS = tuple("ingamevoteflyout" + suffix + ".res" for suffix in
                    ("", "versus", "survival", "versussurvival", "survivalsolo"))
OPTIONS = ("audio.res", "video.res", "multiplayer.res", "keyboardmouse.res")
FILES = tuple(UI + name for name in (*VOTE_DIALOGS, *VOTE_FLYOUTS, *OPTIONS))


def _vote_flyout(text):
    nodes = kv.parse(text)
    controls = console_menus._controls(console_menus._root(nodes))
    console_menus._require(controls, "PnlBackground", "BtnReturnToLobby")
    rows = sorted((int(console_menus._get(body, "ypos")), body)
                  for body in controls.values()
                  if console_menus._get(body, "ControlName") == "L4D360HybridButton")
    if not rows or [y for y, _ in rows] != list(range(0, len(rows) * 20, 20)):
        raise ValueError("Stock vote flyout row layout changed")
    for index, (_, body) in enumerate(rows):
        if console_menus._get(body, "tall") != "20":
            raise ValueError("Stock vote flyout row height changed")
        # PC FlyoutMenu::PerformLayout (GameUI+2D8D0) multiplies Button Y by
        # 0.7, without scaling their heights. Authored20 pitch becomes14 and
        # overlaps20-high hit bounds.30 becomes21, keeping native fonts and
        # actions with one unit of clearance at the reference resolution.
        console_menus._set(body, "ypos", str(index * 30))
    # The native routine also scales the panel height, but not the final row's
    # height.36 provides ceil((20+5)/0.7), preserving bottom-row padding.
    console_menus._set(controls["PnlBackground"], "tall", str((len(rows) - 1) * 30 + 36))
    return kv.dump(nodes) + "\n"


def _map_y(body, transform):
    # Preserve conditional POSIX placements in Video's advanced section.
    body[:] = [(key, str(transform(int(value))) if key.lower() == "ypos" else value, condition)
               for key, value, condition in body]


def _option_layout(root, name):
    controls = console_menus._controls(root)
    frame_name = {"audio.res": "Audio", "video.res": "Video",
                  "multiplayer.res": "Multiplayer", "keyboardmouse.res": "KeyboardMouse"}[name]
    console_menus._require(controls, frame_name, "BtnCancel")
    # 368 is the stock KeyboardMouse content height, whose title is visible
    # with the console footer. The stock 418/448 extents let CL4DFrame::F360
    # position the other pages' native-painted headings above the screen.
    console_menus._set(controls[frame_name], "tall", "368")
    for control, body in controls.items():
        kind = console_menus._get(body, "ControlName")
        if kind in ("DropDownMenu", "SliderControl"):
            console_menus._set(body, "xpos", "c-260")
            console_menus._set(body, "wide", "520")
            children = console_menus._controls(body)
            console_menus._require(children, "BtnDropButton")
            console_menus._set(children["BtnDropButton"], "wide", "520")
        elif kind == "L4D360HybridButton" and control != "Btn3rdPartyCredits":
            console_menus._set(body, "xpos", "c-260")
            console_menus._set(body, "wide", "520")
        else:
            continue
        if name in ("audio.res", "video.res"):
            _map_y(body, lambda y: 85 + (y - 110) * 18 // 20)
        elif name == "multiplayer.res":
            # Keep the 64-unit spray image and its six-unit bottom clearance.
            _map_y(body, lambda y: 85 + (y - 110) * 18 // 20 if y < 210
                   else 245 + (y - 280) * 18 // 20)

    if name == "audio.res":
        console_menus._require(controls, "MicMeter", "MicMeter2", "MicMeterIndicator",
                                "BtnTestMicrophone", "Btn3rdPartyCredits")
        meter_y = int(console_menus._get(controls["BtnTestMicrophone"], "ypos")) - 3
        for control in ("MicMeter", "MicMeter2", "MicMeterIndicator"):
            console_menus._set(controls[control], "xpos", "c187")
            console_menus._set(controls[control], "ypos", str(meter_y))
        console_menus._set(controls["Btn3rdPartyCredits"], "ypos", "355")
    elif name == "video.res":
        console_menus._require(controls, "BtnAdvanced", "DrpAntialias", "BtnUseRecommended",
                                "BtnDone", "Btn3rdPartyCredits")
        # Keep the four trailing actions at17-unit pitch (15-high controls).
        # This separates the credit from the native footer's help line without
        # changing the settings rows or letting adjacent hit bounds overlap.
        for control in ("BtnUseRecommended", "BtnCancel", "BtnDone"):
            _map_y(controls[control], lambda y: 317 + (y - 319) * 17 // 18)
        _map_y(controls["Btn3rdPartyCredits"], lambda y: 317 + (y - 370) * 17 // 20)
        # ApplySchemeSettings retains its own 20-unit collapsed-button pitch;
        # these resource positions remain the native expanded-state targets.
    elif name == "multiplayer.res":
        console_menus._require(controls, "LogoImage", "SldFOV")
        console_menus._set(controls["LogoImage"], "xpos", "c-260")
        console_menus._set(controls["LogoImage"], "ypos", "175")
        fov_children = console_menus._controls(controls["SldFOV"])
        console_menus._require(fov_children, "BtnFOVCurrentValue")
        console_menus._set(fov_children["BtnFOVCurrentValue"], "xpos", "418")


def convert(text, relative):
    name = Path(relative).name.lower()
    if name in VOTE_FLYOUTS:
        return _vote_flyout(text)
    if name in VOTE_DIALOGS:
        # Same platform specialization/PC child retention as the existing
        # native menu overlay. No replacement actions or manual margins.
        converted = console_menus.convert(text, relative)
        if name == "ingamedifficultyselect.res":
            nodes = kv.parse(converted)
            controls = console_menus._controls(console_menus._root(nodes))
            names = ("BtnEasy", "BtnNormal", "BtnHard", "BtnImpossible", "BtnCancel")
            console_menus._require(controls, *names)
            console_menus._require(controls, "InGameDifficultySelect")
            for control, y in zip(names, (55, 75, 95, 115, 145)):
                # The actual disc uses this style for all five choices. Newer
                # PC resources retained Xbox height20 but changed the style to
                # MainMenuSmallButton. Space the retained20-high hit bounds
                # apart; the disc's15-unit pitch overlaps in the PC renderer.
                console_menus._set(controls[control], "style", "MainMenuButton")
                console_menus._set(controls[control], "ypos", str(y))
            console_menus._set(controls["InGameDifficultySelect"], "tall", "180")
            console_menus._set(controls["InGameDifficultySelect"], "ypos", "c-90")
            return kv.dump(nodes) + "\n"
        return converted
    if name not in OPTIONS:
        raise ValueError("Unsupported native menu extra: " + relative)

    nodes = kv.parse(text)
    root = console_menus._root(nodes)
    backgrounds = [body for key, body, _ in root
                   if key.lower() == "imgbackground" and isinstance(body, list)]
    if len(backgrounds) != 1:
        raise ValueError("Stock option background changed: " + relative)
    body = backgrounds[0]
    if console_menus._get(body, "ControlName") != "L4DMenuBackground":
        raise ValueError("Unexpected option background type: " + relative)
    # Keep the named native child available for lookups. The parent platform
    # condition and all PC-specific controls remain exactly as stock supplied.
    for key in ("visible", "enabled", "wide", "tall"):
        console_menus._set(body, key, "0")
    _option_layout(root, name)
    return ("// Native offline experiment: readable console presentation; stock PC settings retained.\n"
            + kv.dump(nodes) + "\n")


def generate(read_stock, out_dir):
    written = []
    for relative in FILES:
        target = Path(out_dir) / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(convert(read_stock(relative), relative), encoding="utf-8", newline="\n")
        written.append(relative)
    return written
