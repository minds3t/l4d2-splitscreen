"""Regression checks for the native HUD resource conversion.

Run with: python -m unittest discover -s tests
"""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import kv
import xbox_ui


class KeyValuesTests(unittest.TestCase):
    def test_condition_precedence_and_unknown_symbols(self):
        self.assertTrue(kv.eval_cond("$X360 || $WIN32 && $GERMAN", {"X360"}))
        self.assertFalse(kv.eval_cond("($X360 || $WIN32) && $GERMAN", {"X360"}))
        self.assertTrue(kv.eval_cond("!($X360WIDE && $X360HIDEF)", {"X360WIDE"}))
        self.assertFalse(kv.eval_cond("0 && $X360", {"X360"}))
        for malformed in ("$X360)", "$X360 &&", "__import__('os')"):
            with self.subTest(malformed=malformed), self.assertRaises(ValueError):
                kv.eval_cond(malformed, {"X360"})

    def test_escape_and_directive_roundtrip(self):
        source = '\ufeff' + r'''
            #base "base.res"
            "Panel" {
                "caption" "He said \"ready\"" [$ENGLISH]
                "image" "vgui\hud\icon"
                "font" { "name" "Arial" }
                "font" { "name" "Tahoma" }
            }
        '''
        nodes = kv.parse(source)
        dumped = kv.dump(nodes)
        self.assertTrue(dumped.startswith('#base "base.res"'.replace(" ", "\t", 1)))
        self.assertEqual(kv.parse(dumped), nodes)


class XboxConversionTests(unittest.TestCase):
    def test_preserves_runtime_language_and_device_branches(self):
        source = '''"Panel" {
            "wide" "400" [$X360 && $ENGLISH]
            "wide" "515" [$X360 && !$ENGLISH]
            "wide" "340" [$WIN32]
            "font" "Arial" [!$DECK]
            "font" "Tahoma" [$DECK]
        }'''
        converted = xbox_ui.convert(source)
        self.assertIsNotNone(converted)
        nodes = kv.parse(converted)
        for extra in ({"ENGLISH"}, {"GERMAN"}, {"GERMAN", "DECK"}):
            with self.subTest(extra=extra):
                expected = kv.evaluate(kv.parse(source), (xbox_ui.X360 - {"ENGLISH"}) | extra)
                actual = kv.evaluate(nodes, (xbox_ui.WIN32 - {"ENGLISH"}) | extra)
                self.assertEqual(actual, expected)

    def test_duplicate_children_are_not_collapsed_for_comparison(self):
        source = '''"Fonts" {
            "1" { "name" "Xbox" } [$X360]
            "1" { "name" "Desktop" } [$WIN32]
            "1" { "name" "Fallback" }
        }'''
        converted = xbox_ui.convert(source)
        self.assertIsNotNone(converted)
        self.assertEqual(
            kv.evaluate(kv.parse(converted), xbox_ui.WIN32),
            kv.evaluate(kv.parse(source), xbox_ui.X360),
        )

    def test_language_only_resource_does_not_need_override(self):
        self.assertIsNone(xbox_ui.convert('"width" "1" [$ENGLISH]\n"width" "2" [!$ENGLISH]'))

    @unittest.skipUnless((ROOT / "game_pc/scripts/hudlayout.res").exists(), "extracted stock game resources unavailable")
    def test_selected_stock_resources_match_native_xbox_branches(self):
        languages = {"ENGLISH", "GERMAN", "FRENCH", "RUSSIAN", "JAPANESE"}
        for relative in xbox_ui.FILES:
            source = (ROOT / "game_pc" / relative).read_text(encoding="utf-8-sig")
            original = kv.parse(source)
            converted = xbox_ui.convert(source)
            if converted is None:
                continue
            parsed = kv.parse(converted)
            for language in languages:
                for device in (set(), {"DECK"}, {"OSX"}):
                    with self.subTest(resource=relative, language=language, device=device):
                        extra = {language} | device
                        self.assertEqual(
                            kv.evaluate(parsed, (xbox_ui.WIN32 - {"ENGLISH"}) | extra),
                            kv.evaluate(original, (xbox_ui.X360 - {"ENGLISH"}) | extra),
                        )


if __name__ == "__main__":
    unittest.main()
