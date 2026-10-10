"""Reject stale, incomplete or wrong-configuration plugin test evidence."""
from pathlib import Path
import os
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plugin"))
import validate_build as validator


class BuildValidationTests(unittest.TestCase):
    def test_ctest_requires_all_fresh_matching_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "Release"
            config.mkdir()
            dll = config / "splitscreen_support.dll"
            dll.write_bytes(b"fixture DLL identity only")
            for name in validator.FIXTURES:
                (config / (name + "_test.exe")).write_bytes(name.encode())
            log = Path(directory) / "LastTest.log"
            source = "Start testing: localized timezone\r\n"
            for index, name in enumerate(validator.FIXTURES, 1):
                source += (f'{index}/{len(validator.FIXTURES)} Testing: {name}\r\n'
                           f'Command: "{config / (name + "_test.exe")}"\r\n'
                           'Test Passed.\r\n')
            source += "End testing: localized timezone\r\n"
            def save(text=source, dll_time=100, fixture_time=100):
                log.write_bytes(text.encode())
                os.utime(log, (200, 200))
                os.utime(dll, (dll_time, dll_time))
                for name in validator.FIXTURES:
                    os.utime(config / (name + "_test.exe"), (fixture_time, fixture_time))
            save()
            evidence = validator.validate_ctest(log, dll, config)
            self.assertEqual(set(evidence["tests"]), set(validator.FIXTURES))
            for bad in (source.replace("Test Passed.", "Test Failed.", 1),
                        source.replace("End testing:", "Still testing:"),
                        source.replace("Testing: input_abi", "Testing: other_test"),
                        source.replace("Testing: safearea_abi", "Omitted: safearea_abi"),
                        source.replace("Release", "Debug")):
                with self.subTest(log=bad), self.assertRaises(ValueError):
                    save(bad)
                    validator.validate_ctest(log, dll, config)
            for dll_time, fixture_time in ((300, 100), (100, 300)):
                with self.subTest(dll_time=dll_time, fixture_time=fixture_time), self.assertRaises(ValueError):
                    save(dll_time=dll_time, fixture_time=fixture_time)
                    validator.validate_ctest(log, dll, config)


if __name__ == "__main__":
    unittest.main()
