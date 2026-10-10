"""Native-menu contract checks: console presentation on the current PC client."""
from pathlib import Path
import math
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import build_packages
import kv
import console_menus as menus
import console_menu_extras as extras
import xbox_ui


def children(nodes):
    return {name: value for name, value, _ in nodes if isinstance(value, list)}


def value(nodes, name):
    return next((v for n, v, _ in nodes if n.lower() == name.lower()), None)


def stock(relative):
    return (ROOT / "game_pc" / relative).read_text(encoding="utf-8-sig")


def controls(relative, language="ENGLISH", text=None):
    converted = menus.convert(stock(relative) if text is None else text, relative)
    nodes = kv.evaluate(kv.parse(converted), (xbox_ui.WIN32 - {"ENGLISH"}) | {language})
    return children(nodes[0][1])


class NativeMenuUnitTests(unittest.TestCase):
    def test_pc_factory_contract_retained_without_desktop_control_visibility(self):
        source = '''"Resource" {
            "Portrait" { "ControlName" "XBoxGamerPic" "wide" "40" } [$X360]
            "Portrait" { "ControlName" "ImagePanel" "wide" "16" } [$WIN32]
            "Chat" { "ControlName" "RichText" "wide" "400" "visible" "1" } [$WIN32]
            "Caption" { "ControlName" "Label"
                "wide" "200" [$X360 && $ENGLISH]
                "wide" "300" [$X360 && !$ENGLISH]
                "wide" "100" [$WIN32]
            }
        }'''
        for language, width in (("ENGLISH", "200"), ("GERMAN", "300")):
            c = controls("fixture.res", language, source)
            self.assertEqual(value(c["Portrait"], "ControlName"), "ImagePanel")
            self.assertEqual(value(c["Portrait"], "wide"), "40")
            self.assertEqual(value(c["Caption"], "wide"), width)
            for key in ("visible", "enabled", "wide", "tall"):
                self.assertEqual(value(c["Chat"], key), "0")

    def test_base_only_resources_keep_native_inheritance_directive(self):
        original = '#base "dropdownmission.res"\n'
        converted = menus.convert(original, "dropdownmissionversus.res")
        self.assertTrue(converted.startswith("#base\t"))
        self.assertEqual(kv.parse(converted), kv.parse(original))


@unittest.skipUnless((ROOT / "game_pc" / menus.MAINMENU).exists(), "stock resource extracts unavailable")
class StockNativeMenuTests(unittest.TestCase):
    def test_vote_rows_remain_separate_and_inside_native_compacted_flyout(self):
        # The PC routine rounds its proportional integer coordinates, then
        # multiplies Y/menu-height (but not row-height) by single-precision0.7.
        f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
        for name in extras.VOTE_FLYOUTS:
            relative = menus.UI + name
            converted = kv.parse(extras.convert(stock(relative), relative))
            for language in ("ENGLISH", "GERMAN"):
                profile = (xbox_ui.WIN32 - {"ENGLISH"}) | {language}
                before = children(kv.evaluate(kv.parse(stock(relative)), profile)[0][1])
                after = children(kv.evaluate(converted, profile)[0][1])
                self.assertEqual(before.keys(), after.keys())
                rows = []
                for control, body in after.items():
                    if value(body, "ControlName") != "L4D360HybridButton":
                        continue
                    for key in ("command", "labeltext", "tooltiptext", "style", "wide",
                                "tall", "visible", "enabled", "navUp", "navDown"):
                        self.assertEqual(value(body, key), value(before[control], key))
                    rows.append((int(value(body, "ypos")), int(value(body, "tall"))))
                rows.sort()
                for screen_height in (480, 720, 900, 1080, 1440, 2160):
                    with self.subTest(file=name, language=language, height=screen_height):
                        scale = screen_height / 480
                        bounds = [(int(f32(int(y * scale) * f32(.7))), math.ceil(h * scale))
                                  for y, h in rows]
                        for (y, height), (next_y, _) in zip(bounds, bounds[1:]):
                            self.assertLessEqual(y + height, next_y)
                        panel_height = int(f32(int(int(value(after["PnlBackground"], "tall"))
                                                   * scale) * f32(.7)))
                        self.assertLessEqual(sum(bounds[-1]), panel_height)

    def test_difficulty_vote_hit_bounds_fit_without_overlap(self):
        relative = menus.UI + "ingamedifficultyselect.res"
        text = extras.convert(stock(relative), relative)
        for language in ("ENGLISH", "GERMAN"):
            profile = (xbox_ui.WIN32 - {"ENGLISH"}) | {language}
            c = children(kv.evaluate(kv.parse(text), profile)[0][1])
            names = ("BtnEasy", "BtnNormal", "BtnHard", "BtnImpossible", "BtnCancel")
            rows = [(int(value(c[name], "ypos")), int(value(c[name], "tall"))) for name in names]
            for (y, height), (next_y, _) in zip(rows, rows[1:]):
                self.assertLessEqual(y + height, next_y)
            frame = c["InGameDifficultySelect"]
            height = int(value(frame, "tall"))
            self.assertEqual(value(frame, "ypos"), f"c-{height // 2}")
            self.assertLess(sum(rows[-1]), height)
            self.assertEqual(value(frame, "wide"), "400" if language == "ENGLISH" else "450")
            self.assertEqual([value(c[name], "command") for name in names],
                             ["Easy", "Normal", "Hard", "Impossible", "Cancel"])

    def test_native_tile_and_mouse_hit_testing_survive_console_carousel_conversion(self):
        text = build_packages.offline_tile(stock(menus.MAINMENU))
        c = controls(menus.MAINMENU, text=text)
        carousel = c["BtnGameModes"]
        # Valve's offline tile in both variants. The conversion fixes only the
        # platform; the game still switches the two by its run-time condition.
        raw = children(kv.parse(menus.convert(text, menus.MAINMENU))[0][1])["BtnGameModes"]
        tiles = [(value(v, "id"), value(v, "command"), cond) for n, v, cond in raw if n == "mode"]
        self.assertEqual(tiles[-2:], [("BtnPlaySolo", "SoloPlay", "!$X360SPLITSCREEN"),
                                      ("BtnPlaySolo", "SoloPlay", "$X360SPLITSCREEN")])
        # With one local player (condition false) exactly the first remains.
        modes = [v for n, v, _ in carousel if n == "mode"]
        self.assertEqual(value(modes[-1], "name"), "#L4D360UI_ModeCaps_offline_SP")
        # The options flyout keeps PC rows, so its first focus must be one.
        self.assertEqual(value(c["FlmOptionsFlyout"], "InitialFocus"), "BtnVideo")
        self.assertEqual(value(carousel, "subpicoffsetx"), "20")
        self.assertEqual(value(carousel, "zpos"), "-10")
        self.assertEqual(value(carousel, "menutitlewide"), "500")
        self.assertEqual(value(carousel, "wideatopen"), "180")
        self.assertEqual(value(c["BtnQuit"], "visible"), "1")
        for name in ("BtnChangeGamers", "PnlQuickJoin", "PnlQuickJoinGroups"):
            self.assertEqual(value(c[name], "enabled"), "0")
            self.assertEqual(value(c[name], "wide"), "0")
        self.assertEqual(value(carousel, "navDown"), "BtnStatsAndAchievements")
        self.assertEqual(value(c["BtnQuit"], "navDown"), "BtnGameModes")
        self.assertEqual(value(c["BtnExtras"], "navDown"), "BtnQuit")
        self.assertEqual(value(c["BtnAddons"], "visible"), "1")
        cursor = "BtnGameModes"
        visited = set()
        while cursor not in visited:
            visited.add(cursor)
            self.assertNotEqual(value(c[cursor], "wide"), "0")
            self.assertNotEqual(value(c[cursor], "visible"), "0")
            cursor = value(c[cursor], "navDown")
        self.assertEqual(cursor, "BtnGameModes")
        self.assertEqual(len(visited), 6)

    def test_lobby_keeps_lookup_objects_and_current_actions_with_xbox_geometry(self):
        c = controls(menus.UI + "gamelobby.res")
        self.assertEqual(value(c["GameLobby"], "tall"), "380")
        self.assertEqual(value(c["BtnChangeGameSettings"], "xpos"), "c-260")
        self.assertEqual(value(c["BtnChangeGameSettings"], "tall"), "20")
        self.assertEqual(value(c["ImgBackground"], "visible"), "0")
        self.assertEqual(value(c["BtnCancelDedicatedSearch"], "command"), "CancelStartGame")
        self.assertIn("BtnCancelRequestStart", c)
        for name in ("LobbyChatHistory", "ChatInputLine", "BtnVoiceButton", "BtnLeaveLobby",
                     "ChatBackground", "IconButtonVoice", "IconBackArrow", "IconBackArrow2"):
            with self.subTest(control=name):
                self.assertIn(name, c)
                self.assertEqual(value(c[name], "visible"), "0")
                self.assertEqual(value(c[name], "enabled"), "0")
                self.assertEqual(value(c[name], "wide"), "0")
        self.assertEqual(value(c["BtnInviteFriends"], "command"), "InviteUI_Steam")
        self.assertEqual(value(c["DrpInviteFriends"], "enabled"), "0")
        self.assertEqual(value(c["BtnInviteFriends"], "navDown"), "DrpGameAccess")
        self.assertEqual(value(children(c["DrpCharacter"])["BtnDropButton"], "ActivationType"), "1")

    def test_edit_navigation_never_targets_deleted_console_buttons_or_hidden_pc_back(self):
        for mode in menus.EDIT_MODES:
            c = controls(menus.UI + f"gamesettings_{mode}edit.res")
            with self.subTest(mode=mode):
                self.assertEqual(value(c["DrpMission"], "tall"), "20")
                self.assertEqual(value(c["BtnCancel"], "enabled"), "0")
                self.assertEqual(value(c["DrpMission"], "navUp"), "DrpSwitchMode")
                self.assertEqual(value(c["DrpSwitchMode"], "navDown"), "DrpMission")
                for name, body in c.items():
                    if not name.startswith("Drp"):
                        continue
                    for direction in ("navUp", "navDown"):
                        target = value(body, direction)
                        self.assertIn(target, c)
                        self.assertNotEqual(target, "BtnCancel")

    def test_pause_uses_xbox_layout_without_console_storage_or_live_actions(self):
        c = controls(menus.UI + "ingamemainmenu.res")
        self.assertEqual(value(c["BtnReturnToGame"], "wide"), "220")
        self.assertEqual(value(c["PnlBackground"], "visible"), "0")
        self.assertEqual(value(c["BtnInviteFriends"], "command"), "InviteUI_Steam")
        self.assertEqual(value(c["FlmInviteFriends"], "enabled"), "0")
        self.assertEqual(value(c["FlmOptionsFlyout"], "ResourceFile").lower(),
                         menus.UI + "optionsflyout.res")
        self.assertEqual(value(c["FlmOptionsFlyout"], "InitialFocus"), "BtnVideo")
        pc = children(kv.evaluate(kv.parse(stock(menus.UI + "ingamemainmenu.res")),
                                  (xbox_ui.WIN32 - {"ENGLISH"}) | {"GERMAN"})[0][1])
        german = controls(menus.UI + "ingamemainmenu.res", language="GERMAN")
        for name in ("BtnExitToMainMenu", "BtnStatsAndAchievements", "BtnReturnToGame"):
            # Console's narrower rectangle clips localized text in PC's native
            # renderer. Preserve the supported PC extent and Xbox placement.
            self.assertEqual(value(german[name], "wide"), value(pc[name], "wide"))
            self.assertEqual(value(german[name], "xpos"), "100")
            self.assertEqual(value(german[name], "ActivationType"), "1")
        # These files contain earlier safety adaptations or have identical
        # Xbox/PC geometry. This overlay must never replace them from stock.
        for name in ("optionsflyout.res", "controlleroptions.res", "controlleroptionsbuttons.res",
                     "controlleroptionssticks.res", "splitscreenmodesflyout.res"):
            self.assertNotIn(menus.UI + name, menus.FILES)

    def test_character_portrait_fits_pc_compacted_flyout_without_changing_xbox_rows(self):
        for name in ("dropdowncharacters", "dropdowncoopcharacters", "dropdownversuscharacters"):
            relative = menus.UI + name + ".res"
            nodes = kv.parse(stock(relative))
            desktop = children(kv.evaluate(nodes, xbox_ui.WIN32)[0][1])
            xbox = children(kv.evaluate(nodes, xbox_ui.X360)[0][1])
            converted = controls(relative)
            with self.subTest(resource=name):
                portrait = converted["HeroPortrait"]
                self.assertEqual(value(portrait, "ypos"), value(desktop["HeroPortrait"], "ypos"))
                for key in ("xpos", "wide", "tall", "image", "scaleimage"):
                    self.assertEqual(value(portrait, key), value(xbox["HeroPortrait"], key))
                # The native PC flyout scales its height and Button positions,
                # but not the ImagePanel. The complete portrait must still fit.
                compact_height = int(int(value(converted["PnlBackground"], "tall")) * 0.7)
                self.assertLessEqual(int(value(portrait, "ypos")) + int(value(portrait, "tall")),
                                     compact_height)
                for control, body in xbox.items():
                    if value(body, "ControlName") == "L4D360HybridButton":
                        self.assertCountEqual(converted[control], body)

    def test_generation_preserves_output_tile_and_all_current_pc_child_names(self):
        with tempfile.TemporaryDirectory() as folder:
            main = Path(folder) / menus.MAINMENU
            main.parent.mkdir(parents=True)
            main.write_text(build_packages.offline_tile(stock(menus.MAINMENU)), encoding="utf-8")
            written = menus.generate(stock, folder)
            self.assertEqual(tuple(written), menus.FILES)
            self.assertIn('"BtnPlaySolo"', main.read_text(encoding="utf-8"))
            for rel in written:
                before = kv.evaluate(kv.parse(stock(rel)), xbox_ui.WIN32)
                after = kv.evaluate(kv.parse((Path(folder) / rel).read_text(encoding="utf-8")), xbox_ui.WIN32)
                if not before or not isinstance(before[0][1], list):
                    continue
                self.assertTrue(children(before[0][1]).keys() <= children(after[0][1]).keys(), rel)
        row = controls(menus.UI + "survivorlistitem.res")
        self.assertIn("ImgReady", row)
        self.assertEqual(value(row["ImgPlayerPortrait"], "xpos"), "60")
        self.assertEqual(value(children(row["DrpPlayer"])["BtnDropButton"], "style"), "MainMenuButton")
        self.assertEqual(value(row["PnlGamerPic"], "wide"), "0")


if __name__ == "__main__":
    unittest.main()
