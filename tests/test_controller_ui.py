"""Prevent the retained Xbox dialog from reversing PC vertical look.

Stock PC pitch is +1; Xbox's negative slider range clamps that value negative
on dialog creation. Its separate default helper is also negative (-1.5).
These checks exercise the resulting slider behavior, not its patch algorithm.
"""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import kv
import menu_ui


def slider_settings(text):
    nodes = kv.evaluate(kv.parse(text), {"WIN32", "ENGLISH"})
    return kv.to_dict(nodes)["resource/ui/controlleroptions.res"]["sldvertsens"]


def displayed_sensitivity(slider, current):
    return min(float(slider["maxvalue"]), max(float(slider["minvalue"]), current))


class ControllerPitchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = ROOT / "game_pc" / menu_ui.CONTROLLER_OPTIONS
        if not source.exists():
            raise unittest.SkipTest("extracted stock controller resource unavailable")
        cls.stock = slider_settings(source.read_text(encoding="utf-8-sig"))
        cls.converted = menu_ui.patch_controlleroptions(source.read_text(encoding="utf-8-sig"))
        cls.fixed = slider_settings(cls.converted)

    def test_opening_dialog_preserves_pc_pitch_direction_and_sensitivity(self):
        self.assertLess(displayed_sensitivity(self.stock, 1.0), 0,
                        "Fixture must reproduce the native Xbox range regression")
        for current in (0.75, 1.0, 1.5, 3.0):
            with self.subTest(current=current):
                self.assertEqual(displayed_sensitivity(self.fixed, current), current)
        self.assertGreater(float(self.fixed["minvalue"]), 0)
        self.assertGreater(float(self.fixed["maxvalue"]), float(self.fixed["minvalue"]))
        self.assertEqual(self.fixed["concommand"], "joy_pitchsensitivity")
        self.assertEqual(self.fixed["inversefill"], "0")

    def test_slider_default_uses_positive_pc_default_for_either_look_type(self):
        # Native SliderControl selects conCommandDefault when supplied, then
        # falls back to the controlled cvar's own default when it is absent.
        defaults = {"joy_pitchsensitivity": 1.0, "joy_pitchsensitivity_default": -1.5}
        chosen = defaults[self.fixed.get("concommanddefault", self.fixed["concommand"])]
        reset = displayed_sensitivity(self.fixed, chosen)
        self.assertEqual(reset, 1.0)
        for invert in (False, True):
            with self.subTest(invert=invert):
                look_direction = reset * (-1 if invert else 1)
                self.assertEqual(look_direction, -1.0 if invert else 1.0,
                                 "Only joy_inverty should reverse PC vertical look")

    def test_xbox_config_reset_is_not_exposed_or_reachable_by_navigation(self):
        # The dialog's actual Defaults handler bypasses SliderControl defaults
        # and executes config.360.cfg (-0.8 pitch, joystick=1, cl_mouselook=0,
        # console rendering settings). Keep that native control for lookups,
        # but do not let users invoke it through the restored PC screen.
        controls = kv.to_dict(kv.evaluate(kv.parse(self.converted), {"WIN32", "ENGLISH"}))[
            "resource/ui/controlleroptions.res"]
        defaults = controls["btndefaults"]
        self.assertEqual((defaults["visible"], defaults["enabled"], defaults["command"]), ("0", "0", ""))
        for name, values in controls.items():
            if not isinstance(values, dict) or values.get("visible") == "0":
                continue
            with self.subTest(control=name):
                self.assertNotIn(values.get("navup", "").lower(), {"btndefaults", "drpduckmode"})
                self.assertNotIn(values.get("navdown", "").lower(), {"btndefaults", "drpduckmode"})


if __name__ == "__main__":
    unittest.main()
