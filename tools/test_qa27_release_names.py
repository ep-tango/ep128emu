#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest

path=Path(__file__).with_name("qa27_release_names.py")
spec=importlib.util.spec_from_file_location("release_names",path)
n=importlib.util.module_from_spec(spec)
spec.loader.exec_module(n)

class TestNames(unittest.TestCase):
    def test_exact_five(self):
        for platform,basename in {
            "mac_universal":"ep128emu-2.7-mac-universal.zip",
            "mac_arm64":"ep128emu-2.7-mac-arm64.zip",
            "win64":"ep128emu-2.7-win64.exe",
            "win32":"ep128emu-2.7-win32.exe",
            "linux_x86_64":"ep128emu-2.7-linux.tar.gz",
        }.items():
            self.assertEqual(n.expected("2.7",platform),basename)
            self.assertNotIn("R9",basename)
            self.assertNotIn("TEST",basename)
            self.assertNotIn("installer",basename)
    def test_public_universal_release_name(self):
        self.assertEqual(n.expected("2.1.1","mac_universal"),
                         "ep128emu-2.1.1-mac-universal.zip")
        self.assertNotEqual(n.expected("2.1.1","mac_universal"),
                            "ep128emu-2.1.1-mac.zip")
    def test_future_version_increments_real_version(self):
        self.assertEqual(n.expected("2.7.1","win64"),
                         "ep128emu-2.7.1-win64.exe")
    def test_invalid_platform_and_version_rejected(self):
        for version,platform in (
            ("2.7-R9","win64"),("2.7","win64-installer"),
            ("2.6","win64"),("2.7-TEST","linux_x86_64"),
            ("2.7","other")):
            with self.assertRaises(ValueError):
                n.expected(version,platform)
    def test_export_and_refuse_collision(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            src=root/"long-internal-R9-TEST.exe"
            src.write_bytes(b"MZ"+b"\0"*80)
            got=n.export(src,root,"2.7","win64")
            self.assertEqual(got.name,"ep128emu-2.7-win64.exe")
            self.assertEqual(got.read_bytes(),src.read_bytes())
            with self.assertRaises(ValueError):
                n.export(src,root,"2.7","win64")
            self.assertEqual(got.read_bytes(),src.read_bytes())
    def test_wrong_magic_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            src=root/"not-an-installer"
            src.write_bytes(b"PK"+b"\0"*80)
            with self.assertRaises(ValueError):
                n.export(src,root,"2.7","win64")
            self.assertFalse((root/"ep128emu-2.7-win64.exe").exists())

if __name__=="__main__":
    unittest.main()
