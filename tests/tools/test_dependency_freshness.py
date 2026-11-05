# Public domain. No rights reserved. Wholly AI-authored.
"""Install/add/rescan refresh a portable graph; normal queries never scan."""

import json
import unittest
from pathlib import Path

from tests.tools import test_os2_dependencies as fixtures


class DependencyFreshnessTests(unittest.TestCase):
    root: Path
    ball: Path
    setUp = fixtures.DependencyAuditTests.setUp
    request = fixtures.DependencyAuditTests.request
    installed = fixtures.DependencyAuditTests.installed
    os2_media = fixtures.DependencyAuditTests.os2_media

    @property
    def index(self):
        return self.ball / "cache/dependency-index.json"

    def status(self, program=r"C:\Apps\APP.EXE", **fields):
        return self.request("launch-dependency-status", arguments=["run", program], **fields)

    def test_missing_index_is_unknown_without_scan_recovery_or_any_writes(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["SOMD"]))
        self.os2_media()
        before = {
            p.relative_to(self.ball): p.read_bytes() for p in self.ball.rglob("*") if p.is_file()
        }
        report = self.status()
        self.assertTrue(report["unknown"])
        self.assertTrue(report["cached"])
        self.assertEqual(report["scannedBinaries"], 0)
        self.assertEqual(report["componentGroups"], [])
        self.assertFalse(fixtures.ball_api.needs_dependency_media(report))
        self.assertEqual(
            before,
            {p.relative_to(self.ball): p.read_bytes() for p in self.ball.rglob("*") if p.is_file()},
        )
        self.assertTrue(self.request("ensure-dependencies")["unknown"])
        self.assertFalse(self.index.exists())

    def test_explicit_rescan_updates_graph_and_stored_index_is_portable(self):
        app = self.installed("Apps/APP.EXE", fixtures.linear(["FIRST"]))
        first = self.request("audit-dependencies", resolve=False)
        database = json.loads(self.index.read_text())
        self.assertEqual(database["schema"], 1)
        self.assertNotIn(str(self.root), self.index.read_text())
        self.assertEqual(self.status()["missing"], ["FIRST"])
        original = self.index.read_bytes()
        app.write_bytes(fixtures.linear(["SECOND"]))
        # No executable is read during status/snapshot, even when its bytes change.
        self.assertEqual(self.status()["missing"], ["FIRST"])
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["cached"])
        self.assertEqual(self.index.read_bytes(), original)
        refreshed = self.request("audit-dependencies", resolve=False)
        self.assertNotEqual(first["jsonPath"], refreshed["jsonPath"])
        self.assertEqual(self.status()["missing"], ["SECOND"])

    def test_cached_graph_can_launch_even_if_known_executable_bytes_are_unreadable(self):
        app = self.installed("Apps/APP.EXE", fixtures.linear(["CORE"]))
        dll = self.installed("Apps/CORE.DLL", fixtures.linear())
        self.request("audit-dependencies", resolve=False)
        before = self.index.read_bytes()
        app.unlink()
        dll.unlink()
        report = self.status()
        self.assertTrue(report["complete"])
        self.assertFalse(report["unknown"])
        self.assertEqual(report["scannedBinaries"], 0)
        self.assertEqual(self.index.read_bytes(), before)
        self.assertIn("loader", report["limits"][0])

    def test_libpath_changes_reapply_cached_graph_without_rescan(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["CORE"]))
        self.installed("Private/CORE.DLL", fixtures.linear())
        self.request("audit-dependencies", resolve=False)
        before = self.index.read_bytes()
        self.assertEqual(self.status()["unreachable"][0]["module"], "CORE")
        config = self.ball / "drive_c/CONFIG.SYS"
        config.write_bytes(config.read_bytes() + b"\r\nLIBPATH=C:\\Private\r\n")
        self.assertTrue(self.status()["complete"])
        self.assertEqual(self.index.read_bytes(), before)

    def test_legacy_full_report_is_readonly_fallback_after_ball_move(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["CORE"]))
        self.installed("Apps/CORE.DLL", fixtures.linear())
        saved = self.request("audit-dependencies", resolve=False)
        path = Path(saved["jsonPath"])
        data = json.loads(path.read_text())
        for binary in data["binaries"]:
            binary.pop("modules", None)
            binary.pop("companions", None)
        path.write_text(json.dumps(data))
        self.index.unlink()
        latest = self.ball / "logs/dependencies-latest.json"
        compact = json.loads(latest.read_text())
        compact["jsonPath"] = "/previous/ball/logs/" + path.parent.name + "/audit.json"
        latest.write_text(json.dumps(compact))
        before = {
            p.relative_to(self.ball): p.read_bytes() for p in self.ball.rglob("*") if p.is_file()
        }
        report = self.status()
        self.assertTrue(report["complete"])
        self.assertEqual(report["cachedBinaries"], 2)
        self.assertFalse(self.index.exists())
        self.assertEqual(
            before,
            {p.relative_to(self.ball): p.read_bytes() for p in self.ball.rglob("*") if p.is_file()},
        )

    def test_add_application_and_component_import_removal_refresh_index(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["SOMD"]))
        self.request("command", arguments=["add-app", "Saved", r"C:\Apps\APP.EXE"])
        self.assertTrue(self.index.is_file())
        self.assertEqual(self.status()["missing"], ["SOMD"])
        media = self.os2_media()
        result = self.request("import-os2-support", sources=[str(media)], categories=["som"])
        self.assertTrue(result["imported"])
        self.assertTrue(
            any(
                item["path"].endswith("SOMD.DLL")
                for item in json.loads(self.index.read_text())["binaries"]
            )
        )
        self.request("remove-os2-components", paths=[r"C:\OS2\DLL\SOMD.DLL"])
        self.assertFalse(
            any(
                item["path"].endswith("SOMD.DLL")
                for item in json.loads(self.index.read_text())["binaries"]
            )
        )
        self.assertEqual(self.status()["missing"], ["SOMD"])

    def test_saved_service_environment_is_independent_from_application(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["APPONLY"]))
        self.installed("AppLib/APPONLY.DLL", fixtures.linear())
        self.installed("Services/DAEMON.EXE", fixtures.linear(["SERVICE"]))
        self.installed("ServiceLib/SERVICE.DLL", fixtures.linear())
        manifest = fixtures.ball_api.load(self.ball)
        manifest["applications"]["Saved"] = {
            "program": r"C:\Apps\APP.EXE",
            "environment": {"LIBPATH": r"C:\AppLib"},
        }
        manifest["startupPrograms"] = [
            {"program": r"C:\Services\DAEMON.EXE", "environment": {"LIBPATH": r"C:\ServiceLib"}}
        ]
        fixtures.ball_api.save(self.ball / "ball.json", manifest)
        self.request("audit-dependencies", resolve=False)
        report = self.request("launch-dependency-status", arguments=["launch", "Saved"])
        self.assertTrue(report["complete"], report)
        self.assertEqual(report["missing"], [])


if __name__ == "__main__":
    unittest.main()
