# Public domain. No rights reserved. Wholly AI-authored.
"""Read-only CMD bootstrap facts and actual-file navigation, without guest execution."""

import unittest
from pathlib import Path

from tests.tools import test_os2_dependencies as binaries
from tests.tools import test_os2_support as support
from tests.tools import test_os2_tree as tree
from tools.ball import engine as ball_api


class LaunchPrerequisiteTests(unittest.TestCase):
    root: Path
    ball: Path
    setUp = support.OS2SupportTests.setUp
    request = support.OS2SupportTests.request
    media = support.OS2SupportTests.media
    source = tree.OS2TreeTests.source

    def script(self, rexx=False):
        path = self.ball / "drive_c/OS2/INSTALL.CMD"
        path.write_text("/* REXX */\nsay 'fixture'\n" if rexx else "echo fixture\n")
        return r"C:\OS2\INSTALL.CMD"

    def status(self, program, **fields):
        return self.request("launch-prerequisites", arguments=["install", program], **fields)

    def support_source(self):
        names = {
            "OS2/CMD.EXE": support.CMD,
            "OS2/SYSLEVEL.OS2": support.LEVEL,
            "OS2/SYSTEM/OSO001.MSG": support.MESSAGE,
        }
        for name in ["REXX", "REXXAPI", "REXXUTIL", "REXXINIT"]:
            names[f"OS2/DLL/{name}.DLL"] = support.CMD
        for name in ["REX", "REXH"]:
            names[f"OS2/SYSTEM/{name}.MSG"] = support.MESSAGE
        for name in ["RXQUEUE", "RXSUBCOM"]:
            names[f"OS2/{name}.EXE"] = support.CMD
        return self.source(names)

    def snapshot_bytes(self):
        return {
            str(path.relative_to(self.ball)): path.read_bytes()
            for path in self.ball.rglob("*")
            if path.is_file()
        }

    def test_selection_is_not_installation_and_only_cmd_group_is_needed(self):
        script = self.script(rexx=True)
        source = self.support_source()
        self.request("remember-os2-media", sources=[source])
        self.request("plan-os2-extraction", sources=[source])
        before = self.snapshot_bytes()
        status = self.status(script)
        self.assertFalse(status["ready"])
        self.assertTrue(status["rexx"])
        self.assertEqual(status["requiredCategories"], ["cmd"])
        self.assertEqual(len(status["requirements"][0]["missingPaths"]), 11)
        self.assertEqual(status["issues"], [])
        self.assertIn("finish installing", status["reason"])
        self.assertEqual(self.snapshot_bytes(), before)
        self.assertFalse((self.ball / "drive_c/OS2/CMD.EXE").exists())
        imported = self.request("import-os2-support", sources=[source], categories=["cmd"])
        self.assertEqual(len(imported["imported"]), 11)
        ready = self.status(script)
        self.assertTrue(ready["ready"])
        self.assertEqual(ready["requiredCategories"], [])
        self.assertEqual(ready["interpreter"], r"C:\OS2\CMD.EXE")
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        plan = self.request("prepare-launch", build=str(build), arguments=["install", script])
        self.assertEqual(plan["command"][1:5], [r"C:\OS2\CMD.EXE", "/C", "CALL", script])

    def test_rexx_script_on_cd_image_is_detected_without_extracting_or_executing(self):
        image = Path(
            self.media(
                "Lotus.iso", tree.hierarchy_iso({"INSTALL.CMD": b"/* REXX */\nsay 'fixture'"})
            )
        )
        manifest = ball_api.load(self.ball)
        manifest["drives"]["D"] = {"device": "cdrom", "image": str(image)}
        ball_api.save(self.ball / "ball.json", manifest)
        before = self.snapshot_bytes()
        status = self.status(r"D:\INSTALL.CMD")
        self.assertTrue(status["commandFile"] and status["rexx"])
        self.assertFalse(status["ready"])
        self.assertEqual(status["requiredCategories"], ["cmd"])
        self.assertEqual(self.snapshot_bytes(), before)

    def test_bad_explicit_comspec_is_configuration_not_an_extraction_loop(self):
        script = self.script()
        (self.ball / "drive_c/OS2/CMD.EXE").write_bytes(support.CMD)
        status = self.status(script, environment={"COMSPEC": r"C:\MISSING\CMD.EXE"})
        self.assertFalse(status["ready"])
        self.assertEqual(status["requiredCategories"], [])
        self.assertEqual(status["issues"][0]["kind"], "configuration")
        self.assertEqual(status["issues"][0]["path"], r"C:\MISSING\CMD.EXE")
        self.assertTrue(self.status(script)["ready"])
        invalid_environment = self.status(script, environment={"BAD": 4})
        self.assertFalse(invalid_environment["ready"])
        self.assertEqual(invalid_environment["issues"][0]["path"], "environment")
        self.assertEqual(invalid_environment["requiredCategories"], [])
        (self.ball / "drive_c/OS2/CMD.EXE").unlink()
        standard = self.status(script, environment={"COMSPEC": "c:/os2/cmd.exe"})
        self.assertEqual(standard["requiredCategories"], ["cmd"])
        self.assertEqual(standard["issues"], [])

    def test_prepare_error_names_current_inline_actions_without_obsolete_cli(self):
        script = self.script()
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        with self.assertRaises(ValueError) as raised:
            self.request("prepare-launch", build=str(build), arguments=["install", script])
        message = str(raised.exception)
        self.assertIn("OS/2 Components", message)
        self.assertIn("CMD and REXX Support", message)
        self.assertIn("finish installing", message)
        self.assertNotIn("install-os2-support", message)

    def test_navigation_ignores_selected_media_profiles_cache_and_missing_apps(self):
        source = self.support_source()
        self.request("remember-os2-media", sources=[source])
        for name in [
            "OS2/OS2.INI",
            "OS2/OS2SYS.INI",
            "OS2/OBJECTS.INI",
            "OS2/REGISTRY.INI",
            "OS2/DLL/2INE/default/cache/libpmwin.so",
        ]:
            path = self.ball / "drive_c" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"local metadata")
        manifest = ball_api.load(self.ball)
        manifest["applications"] = {
            "Missing": {"program": r"C:\APP\MISSING.EXE", "arguments": []},
            "Invalid": {"program": r"C:\OS2\REGISTRY.INI", "arguments": []},
        }
        ball_api.save(self.ball / "ball.json", manifest)
        before = self.snapshot_bytes()
        state = self.request("ball-navigation-state")
        self.assertEqual(state["validApplications"], [])
        self.assertFalse(state["installedComponents"])
        self.assertEqual(state["page"], "system-components")
        self.assertEqual(self.snapshot_bytes(), before)
        payload = self.ball / "drive_c/PSFONTS/FONT.PFB"
        payload.parent.mkdir()
        payload.write_bytes(b"original font")
        state = self.request("ball-navigation-state")
        self.assertTrue(state["installedComponents"])
        self.assertEqual(state["page"], "application-installation")

    def test_navigation_valid_apps_need_a_real_program_and_script_bootstrap(self):
        app = self.ball / "drive_c/OS2/APP.EXE"
        app.write_bytes(binaries.linear(["DOSCALLS"]))
        script = self.script()
        manifest = ball_api.load(self.ball)
        library = bytearray(binaries.linear())
        library[64 + 17] = 0x80
        (self.ball / "drive_c/OS2/LIBRARY.DLL").write_bytes(library)
        manifest["applications"] = {
            "Library": {"program": r"C:\OS2\LIBRARY.DLL", "arguments": []},
            "Existing": {"program": r"c:\os2\app.exe", "arguments": []},
            "Script": {"program": script, "arguments": []},
            "Missing": {"program": r"D:\MISSING.EXE", "arguments": []},
        }
        ball_api.save(self.ball / "ball.json", manifest)
        state = self.request("ball-navigation-state")
        self.assertEqual(state["page"], "applications")
        self.assertEqual(state["validApplications"], ["Existing"])
        app.unlink()
        state = self.request("ball-navigation-state")
        self.assertEqual(state["validApplications"], [])
        (self.ball / "drive_c/OS2/CMD.EXE").write_bytes(support.CMD)
        state = self.request("ball-navigation-state")
        self.assertEqual(state["validApplications"], ["Script"])
        saved = self.request("launch-prerequisites", arguments=["launch", "Script"])
        self.assertTrue(saved["ready"])


if __name__ == "__main__":
    unittest.main()
