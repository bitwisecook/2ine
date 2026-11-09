# Public domain. No rights reserved. Wholly AI-authored.
"""Registered Workplace Shell programs, icons and independent manual launchers."""

import base64
import json
import shutil
import struct
import tempfile
import unittest
from pathlib import Path

from tests.integration.images import linear_image
from tests.tools.test_configuration import profile
from tools.ball import engine as api


def icon(white=True):
    image = struct.pack("<2sIhhI", b"IC", 40, 0, 0, 32)
    image += struct.pack("<I4H", 12, 1, 2, 1, 1) + bytes.fromhex("000000ffffff")
    return image + (b"\x80" if white else b"\0") + bytes(7)


class WpsApplicationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-wps-registration-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ball = self.root / "Fixture.2ineball"
        api.init(self.ball)
        self.dlls = self.ball / "drive_c/OS2/DLL"
        self.app = self.ball / "drive_c/APPS"
        self.app.mkdir()
        (self.app / "APP.EXE").write_bytes(
            linear_image(b"\xc3", [], [], "APP", resources=[(1, 1, icon())])
        )
        self.objects = self.ball / "drive_c/OS2/OBJECTS.INI"
        self.records = {
            "00020000": ("WPFolder", "Suite", "<WP_DESKTOP>", "OBJECTID=<SUITE>;"),
            "00020001": (
                "WPProgram",
                "Word^ Processor",
                "<SUITE>",
                'EXENAME=C:\\APPS\\APP.EXE;STARTUPDIR=C:\\APPS;PARAMETERS="first value" second^;third;OBJECTID=<APP>;',
            ),
        }

    def request(self, action="snapshot", **fields):
        return api.native_request({"action": action, "ball": str(self.ball), **fields})

    def save_objects(self):
        self.objects.write_bytes(
            profile(
                [
                    ("objects", handle, b"\0".join(s.encode("latin1") for s in record) + b"\0")
                    for handle, record in self.records.items()
                ]
            )
        )

    def test_setup_keys_follow_the_shell_parser(self):
        # PMWP trims blanks and tabs around setup keys, nothing else; a key
        # after a line break is not EXENAME to the shell, nor to the launcher.
        self.records["00020001"] = (
            "WPProgram",
            "Tabbed",
            "<SUITE>",
            "OBJECTID=<APP>;\t ExeName \t=C:\\APPS\\APP.EXE;\nSTARTUPDIR=C:\\APPS;",
        )
        self.save_objects()
        app = self.request()["manifest"]["applications"]["Tabbed"]
        self.assertEqual(app["program"], r"C:\APPS\APP.EXE")
        self.assertNotIn("cwd", app)

    def test_registration_fields_update_delete_and_folder_provenance(self):
        self.save_objects()
        result = self.request()
        app = result["manifest"]["applications"]["Word Processor"]
        self.assertEqual(app["program"], r"C:\APPS\APP.EXE")
        self.assertEqual(app["cwd"], r"C:\APPS")
        self.assertEqual(app["arguments"], ["first value", "second;third"])
        self.assertEqual(app["wpsRegistration"]["objectID"], "<APP>")
        self.assertEqual(app["wpsRegistration"]["folders"], ["Suite"])
        self.assertEqual(result["applicationRegistration"]["added"], 1)
        old = self.records["00020001"]
        self.records["00020001"] = (*old[:3], old[3] + "TITLE=Renamed;PARAMETERS=changed;")
        self.save_objects()
        result = self.request()
        self.assertEqual(set(result["manifest"]["applications"]), {"Renamed"})
        self.assertEqual(result["manifest"]["applications"]["Renamed"]["arguments"], ["changed"])
        self.assertEqual(result["applicationRegistration"]["updated"], 1)
        del self.records["00020001"]
        self.save_objects()
        result = self.request()
        self.assertEqual(result["manifest"]["applications"], {})
        self.assertEqual(result["applicationRegistration"]["removed"], 1)

    def test_manual_collision_edit_detach_hide_and_restore(self):
        self.request("command", arguments=["add-app", "Word Processor", r"C:\APPS\APP.EXE"])
        self.save_objects()
        apps = self.request()["manifest"]["applications"]
        self.assertNotIn("wpsRegistration", apps["Word Processor"])
        managed = "Word Processor [WPS 00020001]"
        self.assertIn(managed, apps)
        encoded = json.dumps(apps[managed])
        self.request("command", arguments=["remove-app", managed])
        self.assertNotIn(managed, self.request()["manifest"]["applications"])
        self.request("command", arguments=["restore-app", managed, encoded])
        self.assertIn(managed, self.request()["manifest"]["applications"])
        self.request(
            "save-application",
            name="My Launcher",
            originalName=managed,
            program=r"C:\APPS\APP.EXE",
            arguments=["user"],
        )
        del self.records["00020001"]
        self.save_objects()
        apps = self.request()["manifest"]["applications"]
        self.assertEqual(set(apps), {"Word Processor", "My Launcher"})
        self.assertEqual(apps["My Launcher"]["arguments"], ["user"])
        self.assertNotIn("wpsRegistration", apps["My Launcher"])

    def test_removed_managed_entry_stays_hidden_across_refresh_and_duplicate_ids(self):
        self.records["00020002"] = (
            "WPProgram",
            "Other Registration",
            "<SUITE>",
            self.records["00020001"][3],
        )
        self.save_objects()
        before = self.objects.read_bytes()
        first = self.request()
        encoded = first["applicationRecords"]["Word Processor"]
        self.request("command", arguments=["remove-app", "Word Processor"])
        for _ in range(3):
            apps = self.request()["manifest"]["applications"]
            self.assertEqual(set(apps), {"Other Registration"})
        hidden = self.records["00020001"]
        self.records["00020001"] = (
            *hidden[:3],
            hidden[3] + "TITLE=Hidden Renamed;PARAMETERS=updated;",
        )
        self.save_objects()
        self.assertEqual(set(self.request()["manifest"]["applications"]), {"Other Registration"})
        self.records["00020001"] = hidden
        self.save_objects()
        self.assertEqual(self.objects.read_bytes(), before)
        self.assertTrue((self.app / "APP.EXE").is_file())
        self.request("command", arguments=["restore-app", "Word Processor", encoded])
        self.assertEqual(
            set(self.request()["manifest"]["applications"]),
            {"Word Processor", "Other Registration"},
        )
        self.request("command", arguments=["remove-app", "Word Processor"])
        self.request("command", arguments=["add-app", "My Manual Launcher", r"C:\APPS\APP.EXE"])
        self.assertEqual(
            set(self.request()["manifest"]["applications"]),
            {"Other Registration", "My Manual Launcher"},
        )

    def test_reused_handle_for_another_object_is_not_hidden(self):
        self.save_objects()
        self.request()
        self.request("command", arguments=["remove-app", "Word Processor"])
        tombstone = api.load(self.ball)["wpsHidden"]["00020001"]
        self.assertEqual(tombstone["objectID"], "<APP>")
        self.records["00020001"] = (
            "WPProgram",
            "A Different App",
            "<SUITE>",
            r"EXENAME=C:\APPS\APP.EXE;OBJECTID=<OTHER>;",
        )
        self.save_objects()
        self.assertEqual(set(self.request()["manifest"]["applications"]), {"A Different App"})

    def test_legacy_hidden_policy_and_unidentified_object_hash_remain_supported(self):
        original = self.records["00020001"]
        self.records["00020001"] = (*original[:3], original[3].replace("OBJECTID=<APP>;", ""))
        self.save_objects()
        self.request()
        self.request("command", arguments=["remove-app", "Word Processor"])
        self.assertNotIn("Word Processor", self.request()["manifest"]["applications"])
        self.records["00020001"] = (
            "WPProgram",
            "Replacement Without ID",
            "<SUITE>",
            r"EXENAME=C:\APPS\APP.EXE;",
        )
        self.save_objects()
        self.assertIn("Replacement Without ID", self.request()["manifest"]["applications"])
        manifest = api.load(self.ball)
        manifest["wpsHidden"]["00020001"] = True
        (self.ball / "ball.json").write_text(json.dumps(manifest))
        self.assertNotIn("Replacement Without ID", self.request()["manifest"]["applications"])

    def test_manual_remove_restore_and_readd_do_not_modify_object_profile(self):
        self.save_objects()
        self.request("command", arguments=["add-app", "Manual", r"C:\APPS\APP.EXE"])
        before = self.objects.read_bytes()
        encoded = self.request()["applicationRecords"]["Manual"]
        self.request("command", arguments=["remove-app", "Manual"])
        self.assertNotIn("Manual", self.request()["manifest"]["applications"])
        self.request("command", arguments=["restore-app", "Manual", encoded])
        self.assertIn("Manual", self.request()["manifest"]["applications"])
        self.assertEqual(self.objects.read_bytes(), before)
        self.assertFalse(api.load(self.ball).get("wpsHidden"))

    def test_iconfile_iconresource_id_and_executable_fallback(self):
        (self.app / "EXPLICIT.ICO").write_bytes(icon(False))
        dib = struct.pack("<3I2H6I", 40, 1, 2, 1, 4, 0, 4, 0, 0, 16, 0)
        dib += bytes(4) + b"\0\0\xff\0" + bytes(14 * 4) + b"\x10" + bytes(7)
        windows = (
            struct.pack("<3H", 0, 1, 1)
            + struct.pack("<4B2H2I", 1, 1, 16, 0, 1, 4, len(dib), 22)
            + dib
        )
        (self.app / "WINDOWS.ICO").write_bytes(windows)
        (self.dlls / "ICONS.DLL").write_bytes(
            linear_image(b"\xc3", [], [], "ICONS", resources=[(1, 1, icon()), (1, 7, icon(False))])
        )
        for setting, origin, pixel in [
            ("ICONFILE=C:\\APPS\\EXPLICIT.ICO;", "wps-iconfile", b"\0\0\0\xff"),
            ("ICONFILE=C:\\WRONG\\EXPLICIT.ICO;", "wps-iconfile", b"\0\0\0\xff"),
            ("ICONFILE=C:\\WRONG\\WINDOWS.ICO;", "wps-iconfile", b"\xff\0\0\xff"),
            ("ICONRESOURCE=7 ICONS;", "wps-iconresource", b"\0\0\0\xff"),
            ("ICONFILE=C:\\APPS\\ABSENT.ICO;", "embedded", b"\xff\xff\xff\xff"),
        ]:
            with self.subTest(setting=setting):
                record = self.records["00020001"]
                self.records["00020001"] = (*record[:3], record[3] + setting)
                self.save_objects()
                result = self.request("application-icon", name="Word Processor")
                self.assertEqual(result["source"], origin)
                self.assertEqual(result["displayName"], "Word Processor")
                self.assertEqual((result["width"], result["height"]), (1, 1))
                self.assertEqual(base64.b64decode(result["rgba"]), pixel)
                self.assertEqual(result["wpsRegistration"]["objectID"], "<APP>")
                if "WRONG" in setting:
                    file = "WINDOWS.ICO" if "WINDOWS" in setting else "EXPLICIT.ICO"
                    self.assertEqual(result["iconPath"], "C:\\APPS\\" + file)
                    self.assertEqual(result["declaredIconPath"], "C:\\WRONG\\" + file)
                    self.assertIn("same icon filename", result["fallbackReason"])
                self.records["00020001"] = record
        malformed = []
        truncated = bytearray(windows[:-1])
        struct.pack_into("<I", truncated, 14, len(dib) - 1)
        malformed.append(truncated)
        oversized = bytearray(windows)
        struct.pack_into("<I", oversized, 14, 0xFFFFFFFF)
        malformed.append(oversized)
        oversized = bytearray(windows)
        struct.pack_into("<I", oversized, 26, 0x7FFFFFFF)
        malformed.append(oversized)
        record = self.records["00020001"]
        self.records["00020001"] = (*record[:3], record[3] + "ICONFILE=C:\\APPS\\WINDOWS.ICO;")
        self.save_objects()
        for payload in malformed:
            (self.app / "WINDOWS.ICO").write_bytes(payload)
            self.assertEqual(
                self.request("application-icon", name="Word Processor")["source"], "embedded"
            )

    def test_missing_and_invalid_profiles_preserve_manual_and_managed_data(self):
        before = (self.ball / "ball.json").read_bytes()
        self.request()
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)
        self.assertFalse((self.ball / "logs/wps-registration-latest.json").exists())
        self.save_objects()
        apps = self.request()["manifest"]["applications"]
        self.objects.write_bytes(b"broken profile")
        result = self.request()
        self.assertEqual(result["manifest"]["applications"], apps)
        self.assertIn("applicationRegistrationWarning", result)

    def test_skip_only_changes_refresh_report_without_touching_launchers(self):
        self.records["00020001"] = ("WPProgram", "Hidden", "<MISSING>", "EXENAME=E.EXE;")
        self.save_objects()
        first = self.request()
        report = Path(first["applicationRegistration"]["logPath"])
        before = json.loads(report.read_text())
        self.records["00020001"] = ("WPProgram", "Hidden", "<MISSING>", "EXENAME=OTHER.EXE;")
        self.save_objects()
        self.request()
        after = json.loads(report.read_text())
        self.assertNotEqual(before["profileSha256"], after["profileSha256"])
        self.assertEqual(after["skipped"], 1)
        self.assertEqual(self.request()["manifest"]["applications"], {})

    def test_registration_recreates_omitted_logs_before_prepare_launch(self):
        self.save_objects()
        shutil.rmtree(self.ball / "logs")
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"runtime")
        plan = self.request(
            "prepare-launch",
            build=str(build),
            backend="unicorn",
            arguments=["launch", "Word Processor"],
        )
        self.assertIn(r"C:\APPS\APP.EXE", plan["command"])
        report = self.ball / "logs/wps-registration-latest.json"
        self.assertTrue(report.is_file())
        self.assertEqual(json.loads(report.read_text())["added"], 1)
        self.assertTrue(plan["dependencyAudit"]["cached"])
        self.assertTrue(plan["dependencyAudit"]["unknown"])
        self.assertEqual(plan["dependencyAudit"]["scannedBinaries"], 0)
        self.assertFalse((self.ball / "cache/dependency-index.json").exists())
        self.assertFalse((self.ball / "logs/dependencies-latest.json").exists())
        self.assertIn("Word Processor", api.load(self.ball)["applications"])


if __name__ == "__main__":
    unittest.main()
