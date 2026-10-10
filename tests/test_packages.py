from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import build_packages as packages
import kv
import console_hud
import xbox_ui


def controls(text):
    root = next(value for _, value, _ in kv.parse(text) if isinstance(value, list))
    return {name: dict((key, value) for key, value, _ in body)
            for name, body, _ in root if isinstance(body, list)}


class NativePackageTests(unittest.TestCase):
    def test_main_menu_carries_both_offline_tiles(self):
        path = ROOT / "game_pc" / packages.MAINMENU
        if not path.exists():
            self.skipTest("extracted stock UI unavailable")
        source = path.read_text(encoding="utf-8-sig")
        text = packages.offline_tile(source)
        root = next(value for _, value, _ in kv.parse(text) if isinstance(value, list))
        carousel = next(body for name, body, _ in root if name == "BtnGameModes")
        tiles = [(dict((key, value) for key, value, _ in body), condition)
                 for name, body, condition in carousel if name == "mode"]
        # Valve's own tile for two local players stays last; the one-player
        # variant from the disc sits in front of it under the opposite condition.
        (single, single_if), (coop, coop_if) = tiles[-2:]
        self.assertEqual((single_if, coop_if), ("!$X360SPLITSCREEN", "$X360SPLITSCREEN"))
        self.assertEqual(single["id"], coop["id"])
        self.assertEqual(single["command"], "SoloPlay")
        self.assertEqual(coop["command"], "SoloPlay")
        self.assertEqual(single["name"], "#L4D360UI_ModeCaps_offline_SP")
        self.assertEqual(coop["name"], "#L4D360UI_ModeCaps_offline_SS")
        # No tile or flyout of our own any more.
        self.assertNotIn("FlmSplitscreenFlyout", text)
        self.assertNotIn("menu_mode_splitscreen", text)
        self.assertEqual(len(tiles), source.count('"mode"') - source.count('//"mode"') + 1)
        with self.assertRaises(ValueError):
            packages.offline_tile(text)

    def test_main_options_flyout_gets_direction_and_one_switch_row(self):
        path = ROOT / "game_pc" / packages.OPTIONS_FLYOUT
        if not path.exists():
            self.skipTest("extracted stock UI unavailable")
        source = path.read_text(encoding="utf-8-sig")
        root = next(value for _, value, _ in kv.parse(packages.main_options_flyout(source))
                    if isinstance(value, list))
        rows = {name: body for name, body, cond in root if isinstance(body, list) and cond == "$WIN32"}
        flat = {name: dict((key, value) for key, value, _ in body) for name, body in rows.items()}
        order = sorted(rows, key=lambda name: int(flat[name]["ypos"]))
        self.assertEqual(order[-2:], ["BtnSplitScreenDirection", "BtnSplitscreenSwitch"])
        for index, name in enumerate(order):
            self.assertEqual(flat[name]["navDown"], order[(index + 1) % len(order)])
            self.assertEqual(flat[name]["navUp"], order[index - 1])
        # The console page that owns the setting, not console commands of ours.
        self.assertEqual(flat["BtnSplitScreenDirection"]["command"], "AudioVideo")
        # One row: what it says and does follows the game's own condition.
        switch = [(key, value, cond) for key, value, cond in rows["BtnSplitscreenSwitch"]
                  if key in ("labelText", "command")]
        self.assertCountEqual(switch, [
            ("labelText", "#L4D360UI_MainMenu_DisableSplitscreen", "$X360SPLITSCREEN"),
            ("command", "#splitscreen_disable;play ui/menu_accept", "$X360SPLITSCREEN"),
            ("labelText", "#L4D360UI_MainMenu_EnableSplitscreen", "!$X360SPLITSCREEN"),
            ("command", "#splitscreen_enable;play ui/menu_accept", "!$X360SPLITSCREEN")])
        # The Xbox branch of the file is untouched.
        before = [node for node in kv.parse(source)[0][1] if node[2] == "$X360"]
        after = [node for node in root if node[2] == "$X360"]
        self.assertEqual(before, after)

    def test_team_modes_get_the_other_modes_offline_row(self):
        campaign = ROOT / "game_pc" / packages.UI / "campaignflyout.res"
        if not campaign.exists():
            self.skipTest("extracted stock UI unavailable")
        stock = controls(campaign.read_text(encoding="utf-8-sig"))["BtnSinglePlayer"]
        self.assertEqual(sorted(packages.OFFLINE_MODES), ["mutation12", "mutation15", "scavenge", "versus"])
        for mode, relative in packages.OFFLINE_MODES.items():
            source = (ROOT / "game_pc" / relative).read_text(encoding="utf-8-sig")
            before = controls(source)
            text = packages.offline_mode_flyout(source, mode)
            after = controls(text)
            self.assertEqual(sorted(after), sorted([*before, "BtnSinglePlayer"]))
            self.assertEqual(self.offline_row(text), [
                ("labelText", stock["labelText"], "!$X360SPLITSCREEN"),
                ("tooltiptext", stock["tooltiptext"], "!$X360SPLITSCREEN"),
                ("command", "SoloPlay_" + mode, "!$X360SPLITSCREEN"),
                ("labelText", "#L4D360UI_Mode_offline_SS", "$X360SPLITSCREEN"),
                ("tooltiptext", "#L4D360UI_MainMenu_OfflineCoOp_Tip", "$X360SPLITSCREEN"),
                ("command", "SoloPlay_" + mode, "$X360SPLITSCREEN")])
            rows = sorted((name for name in after if name != "PnlBackground"),
                          key=lambda name: int(after[name]["ypos"]))
            self.assertEqual(rows[-1], "BtnSinglePlayer")
            for index, name in enumerate(rows):  # control names are not case-sensitive
                self.assertEqual(after[name]["navDown"].lower(), rows[(index + 1) % len(rows)].lower())
                self.assertEqual(after[name]["navUp"].lower(), rows[index - 1].lower())
            for name, body in before.items():
                if name != "PnlBackground":
                    self.assertEqual(after[name]["command"], body["command"])

    @staticmethod
    def offline_row(text):
        root = next(value for _, value, _ in kv.parse(text) if isinstance(value, list))
        body = next(body for name, body, _ in root if name == "BtnSinglePlayer")
        return [node for node in body if node[0] in ("labelText", "tooltiptext", "command")]

    def test_stock_offline_rows_are_named_for_one_player_or_two(self):
        for relative in packages.STOCK_OFFLINE_FLYOUTS:
            path = ROOT / "game_pc" / relative
            if not path.exists():
                self.skipTest("extracted stock UI unavailable")
            source = path.read_text(encoding="utf-8-sig")
            text = packages.offline_row_names(source)
            before, after = self.offline_row(source), self.offline_row(text)
            self.assertEqual(after, [
                ("labelText", "#L4D360UI_Mode_offline_SP", "!$X360SPLITSCREEN"),
                ("labelText", "#L4D360UI_Mode_offline_SS", "$X360SPLITSCREEN"),
                ("tooltiptext", "#L4D360UI_MainMenu_PlaySolo_Tip", "!$X360SPLITSCREEN"),
                ("tooltiptext", "#L4D360UI_MainMenu_OfflineCoOp_Tip", "$X360SPLITSCREEN"),
                before[-1]])
            # Nothing else in the file changes.
            strip = lambda t: [(n, {k: v for k, v in b.items() if k not in ("labelText", "tooltiptext")})
                               for n, b in controls(t).items()]
            self.assertEqual(strip(source), strip(text))
            with self.assertRaisesRegex(ValueError, "stock offline row"):
                packages.offline_row_names(text)

    def test_pc_look_menus_are_stock_layouts_with_the_local_coop_additions(self):
        import game_files
        if not Path(game_files.GAME).exists():
            self.skipTest("game installation unavailable")
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            written = packages.build_pc_menus(directory)
            ui = directory / packages.UI
            self.assertEqual(sorted(path.name for path in ui.iterdir()), [
                "audiovideo.res", "campaignflyout.res", "controlleroptions.res",
                "dropdownsplitscreendirection.res", "gamesettings.res", "ingamemainmenu.res",
                "mainmenu.res", "optionsflyout.res", "optionsflyoutingame.res", "realismflyout.res",
                "realismversusflyout.res", "scavengeflyout.res", "survivalflyout.res",
                "versusflyout.res", "versussurvivalflyout.res"])
            self.assertEqual(len(written), len(set(written)))
            # Stock layout: the platform conditions are still Valve's, and the
            # pause menu's PC branch alone points at the in-game flyout.
            menu = (directory / packages.MAINMENU).read_text(encoding="utf-8")
            self.assertIn("$WIN32", menu)
            self.assertNotIn("Generated native-lobby overlay", menu)
            pause = kv.parse((directory / packages.PAUSE_MENU).read_text(encoding="utf-8"))
            root = next(value for _, value, _ in pause if isinstance(value, list))
            flyout = next(body for name, body, _ in root if name == "FlmOptionsFlyout")
            resources = {condition: value for key, value, condition in flyout if key.lower() == "resourcefile"}
            self.assertEqual(resources["$WIN32"], "resource/UI/L4D360UI/OptionsFlyoutIngame.res")
            self.assertEqual(len(resources), 2)
            rows = controls((directory / packages.OPTIONS_FLYOUT).read_text(encoding="utf-8"))
            for name in ("BtnSplitScreenDirection", "BtnSplitscreenSwitch", "BtnController"):
                self.assertIn(name, rows)

    def test_split_direction_page_keeps_only_that_setting_usable(self):
        import console_menus
        path = ROOT / "game_pc" / packages.AUDIO_VIDEO
        if not path.exists():
            self.skipTest("extracted stock UI unavailable")
        source = path.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
        page = controls(packages.split_direction_page(console_menus.convert(source, packages.AUDIO_VIDEO)))
        stock = controls(source)
        # Every control the native dialog looks up is still present.
        self.assertTrue(stock.keys() <= page.keys())
        for name, body in page.items():
            if name in ("AudioVideo", "DrpSplitScreenDirection", "FlmSplitScreenDirection"):
                continue
            self.assertEqual((body["visible"], body["enabled"], body["wide"], body["tall"]),
                             ("0", "0", "0", "0"), name)
        direction = page["DrpSplitScreenDirection"]
        self.assertEqual((direction["visible"], direction["enabled"]), ("1", "1"))
        self.assertEqual(direction["ypos"], stock["SldBrightness"]["ypos"])

    def test_menu_files_move_to_their_own_package(self):
        with tempfile.TemporaryDirectory() as folder:
            out, menus = Path(folder) / "hud", Path(folder) / "menus"
            files = ("scripts/hudlayout.res", "resource/l4d360uischeme.res",
                     "resource/ui/scoreboard.res", "resource/ui/l4d360ui/mainmenu.res",
                     "resource/ui/l4d360ui/gamelobby.res", "scripts/vscripts/splitscreen_support.nut")
            for relative in files:
                (out / relative).parent.mkdir(parents=True, exist_ok=True)
                (out / relative).write_text(relative, encoding="utf-8")
            moved = packages.split_packages(out, menus)
            self.assertEqual(sorted(moved), ["resource/ui/l4d360ui/gamelobby.res",
                                             "resource/ui/l4d360ui/mainmenu.res"])
            for relative in files:
                home = menus if relative in moved else out
                other = out if relative in moved else menus
                self.assertTrue((home / relative).is_file(), relative)
                self.assertFalse((other / relative).exists(), relative)
            # No empty menu folders are left behind in the HUD package.
            self.assertFalse((out / "resource/ui/l4d360ui").exists())

    def test_native_package_sources_are_self_contained(self):
        # The plugin joins the players and owns the controllers; the script
        # that ships with it must not do either itself.
        script = (packages.SOURCE / "scripts" / "vscripts" / "splitscreen_support.nut").read_text(encoding="utf-8")
        for fallback in ("host_xbox_e3_restart", "connect_splitscreen", "in_forceuser", "joystick",
                         "joy_preset", "SplitscreenCharacters", "SplitscreenTeams"):
            self.assertNotIn(fallback, script)
        self.assertEqual(sorted(path.name for path in (packages.SOURCE / "scripts" / "vscripts").iterdir()),
                         ["director_base_addon.nut", "splitscreen_support.nut"])
    def test_pause_menu_gets_player_two_join_and_leave_rows(self):
        options = ROOT / "game_pc" / "resource" / "ui" / "l4d360ui" / "optionsflyout.res"
        pause = ROOT / "game_pc" / packages.PAUSE_MENU
        if not options.exists() or not pause.exists():
            self.skipTest("extracted stock UI unavailable")
        source = options.read_text(encoding="utf-8-sig")
        text = packages.ingame_options_flyout(source)
        root = next(value for _, value, _ in kv.parse(text) if isinstance(value, list))
        rows = {name: dict((key, value) for key, value, _ in body)
                for name, body, cond in root if isinstance(body, list) and cond == "$WIN32"}
        order = sorted(rows, key=lambda name: int(rows[name]["ypos"]))
        self.assertEqual(order[-2:], ["BtnPlayerTwoJoin", "BtnPlayerTwoLeave"])
        # Not the retained Xbox commands: those leave the game, and the native
        # menu hides buttons that carry them.
        self.assertEqual(rows["BtnPlayerTwoJoin"]["command"], "PlayerTwoJoin")
        self.assertEqual(rows["BtnPlayerTwoLeave"]["command"], "PlayerTwoLeave")
        for index, name in enumerate(order):
            self.assertEqual(rows[name]["navDown"], order[(index + 1) % len(order)])
            self.assertEqual(rows[name]["navUp"], order[index - 1])
        pitch = int(rows[order[-1]]["tall"])
        self.assertEqual(int(rows[order[-1]]["ypos"]) - int(rows[order[-2]]["ypos"]), pitch)
        # The main menu keeps the stock flyout; only the pause menu changes.
        self.assertNotIn("PlayerTwoJoin", source)
        with self.assertRaises(ValueError):
            packages.ingame_options_flyout(text)
        # The build points the console (Xbox-evaluated) pause menu at the copy.
        menu = packages.use_ingame_flyout(
            xbox_ui.convert(pause.read_text(encoding="utf-8-sig").replace("\r\n", "\n")))
        flyout = controls(menu)["FlmOptionsFlyout"]
        resource = next(value for key, value in flyout.items() if key.lower() == "resourcefile")
        self.assertEqual(resource.lower(), packages.INGAME_FLYOUT)

    def test_gamercard_prompt_is_hidden_and_other_prompts_stay(self):
        for relative in console_hud.GAMERCARD_RESOURCES:
            path = ROOT / "game_pc" / relative
            if not path.exists():
                self.skipTest("extracted stock UI unavailable")
            xbox = xbox_ui.convert(path.read_text(encoding="utf-8-sig").replace("\r\n", "\n"))
            text = console_hud.hide_gamercard(xbox)
            flat = {}

            def walk(children):
                for name, value, _ in children:
                    if isinstance(value, list):
                        flat[name] = dict((key, item) for key, item, _ in value
                                          if not isinstance(item, list))
                        walk(value)

            walk(kv.parse(text))
            for name in console_hud.GAMERCARD_CONTROLS:
                self.assertEqual(flat[name]["visible"], "0", relative)
            if "VoteKickLabel" in flat:
                self.assertEqual(flat["VoteKickLabel"]["visible"], "1")
            # Idempotent: a rebuild over existing output changes nothing.
            self.assertEqual(console_hud.hide_gamercard(text), text)


if __name__ == "__main__":
    unittest.main()
