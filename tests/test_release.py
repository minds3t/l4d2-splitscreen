"""The release folder must be complete and carry no development settings."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
import build_release as release


class ReleaseTests(unittest.TestCase):
    def test_supported_build_table_matches_the_plugins(self):
        header = (ROOT / "plugin/include/supported_build.h").read_text(encoding="utf-8")
        table = release.supported_build(header)
        self.assertEqual(table["game"], "2.2.4.3")
        self.assertEqual([module["name"] for module in table["modules"]],
                         ["matchmaking.dll", "gameui.dll", "engine.dll", "server.dll", "client.dll", "inputsystem.dll"])
        for module in table["modules"]:
            self.assertIn(module["sha256"], header)
            self.assertNotIn("/", module["path"])
        with self.assertRaisesRegex(ValueError, "supported game build"):
            release.supported_build("no table here")

    def test_release_folder_is_complete(self):
        if not release.PLUGIN.is_file() or not all(path.is_file() for path in release.PACKAGES.values()):
            self.skipTest("plugin and packages are not built")
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "SplitscreenSupport"
            contents = release.assemble(target, "99")
            self.assertEqual(contents, [
                "Install Shortcuts.cmd", "README.txt", "Start Local Co-op.cmd", "Uninstall.cmd", "config.ini",
                "files/SplitscreenSupport.ps1", "files/hud.vpk", "files/hud_pc.vpk",
                "files/licenses/MinHook.txt", "files/licenses/SplitscreenSupport.txt", "files/menus.vpk", "files/menus_pc.vpk",
                "files/splitscreen_check.exe", "files/splitscreen_support.dll", "files/supported_build.json"])
            readme = (target / "README.txt").read_text(encoding="utf-8")
            self.assertIn("version 99", readme)
            self.assertNotIn("@", readme)
            settings = (target / "config.ini").read_text(encoding="utf-8")
            # Shipped defaults, and no extra game options.
            for line in ("KeyboardPlayer=2", "XboxSticks=1", "ConsoleUI=1", "SafeArea=1", "GameArguments="):
                self.assertIn(line + "\n", settings)
            # A development folder is the same folder with other game options,
            # and settings made in it survive the next build.
            development = Path(temp) / "dev"
            self.assertEqual(release.assemble_dev(development, "99"), contents)
            settings = (development / "config.ini").read_text(encoding="utf-8")
            self.assertIn("GameArguments=" + release.DEV_ARGUMENTS + "\n", settings)
            (development / "config.ini").write_text(settings.replace("ConsoleUI=1", "ConsoleUI=0"), encoding="utf-8")
            release.assemble_dev(development, "99")
            self.assertIn("ConsoleUI=0\n", (development / "config.ini").read_text(encoding="utf-8"))
            json.loads((target / "files/supported_build.json").read_text(encoding="utf-8"))
            # The starters only run the launcher that ships beside them.
            for name, action in (("Start Local Co-op.cmd", "Start"), ("Install Shortcuts.cmd", "Install"),
                                 ("Uninstall.cmd", "Uninstall")):
                text = (target / name).read_text(encoding="ascii")
                self.assertIn(r'"%~dp0files\SplitscreenSupport.ps1" -Action ' + action + " -NoDialog", text)
            launcher = (target / "files/SplitscreenSupport.ps1").read_text(encoding="utf-8")
            # No development console port, logging switch or fixed paths.
            for forbidden in ("netconport", "condebug", "Hendrik", "Program Files"):
                self.assertNotIn(forbidden, launcher)


if __name__ == "__main__":
    unittest.main()
