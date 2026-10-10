from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import kv
import mode_ui


def block(nodes, name):
    return next(value for key, value, _ in nodes if key == name)


def properties(nodes, mode):
    """Read a control after applying its native VGUI mode override block."""
    values = {key: value for key, value, _ in nodes if not key.startswith("?condition?")}
    for key, value, _ in nodes:
        if key == f"?condition?{mode}":
            values.update(properties(value, mode))
    return values


class ModeUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / "game_pc" / mode_ui.GAME_SETTINGS
        if not path.exists():
            raise unittest.SkipTest("extracted stock UI unavailable")
        cls.stock = path.read_text(encoding="utf-8-sig")
        cls.converted = mode_ui.patch_gamesettings(cls.stock)
        cls.controls = kv.evaluate(kv.parse(cls.converted), {"WIN32", "ENGLISH"})[0][1]

    def test_no_difficulty_modes_have_compact_rows_and_native_map_lists(self):
        for mode in ("survival", "versus", "scavenge"):
            with self.subTest(mode=mode):
                difficulty = properties(block(self.controls, "DrpDifficulty"), mode)
                self.assertEqual((difficulty["visible"], difficulty["enabled"]), ("0", "0"))
                chapter = properties(block(self.controls, "DrpChapter"), mode)
                character = properties(block(self.controls, "DrpCharacter"), mode)
                start = properties(block(self.controls, "BtnStartGame"), mode)
                self.assertEqual((chapter["ypos"], character["ypos"], start["ypos"]), ("135", "155", "175"))
                self.assertEqual(chapter["navDown"], "DrpCharacter")
                self.assertEqual(character["navUp"], "DrpChapter")
                mission = properties(block(block(self.controls, "DrpMission"), "BtnDropButton"), mode)
                flyout = properties(block(self.controls, mission["command"]), mode)
                self.assertEqual(flyout["ResourceFile"],
                                 f"resource/UI/L4D360UI/DropDownMission{mode.capitalize()}.res")

    def test_coop_and_realism_controls_retain_stock_values(self):
        original = kv.evaluate(kv.parse(self.stock), {"WIN32", "ENGLISH"})[0][1]
        for mode in ("coop", "realism"):
            for name, values, _ in original:
                with self.subTest(mode=mode, control=name):
                    before = properties(values, mode)
                    after = properties(block(self.controls, name), mode)
                    # Nested dropdown buttons also have mode-specific branches.
                    if "BtnDropButton" in before:
                        before["BtnDropButton"] = properties(before["BtnDropButton"], mode)
                        after["BtnDropButton"] = properties(after["BtnDropButton"], mode)
                    self.assertEqual(after, before)

    def test_refuses_to_patch_generated_resource_twice(self):
        with self.assertRaisesRegex(ValueError, "already present"):
            mode_ui.patch_gamesettings(self.converted)


if __name__ == "__main__":
    unittest.main()
