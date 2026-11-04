# Public domain. No rights reserved. Wholly AI-authored.
"""Cached dependency reports follow installed files, search paths and media."""

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

    def test_legacy_report_is_stale_and_ensure_retains_full_current_report(self):
        initial = self.request("audit-dependencies", resolve=False)
        latest = self.ball / "logs/dependencies-latest.json"
        legacy = json.loads(latest.read_text())
        legacy.pop("fingerprint")
        latest.write_text(json.dumps(legacy))
        shown = self.request("snapshot")["dependencyAudit"]
        self.assertTrue(shown["stale"])
        self.assertFalse(shown["complete"])
        self.assertIn("stale", shown["summary"])
        fresh = self.request("ensure-dependencies")
        self.assertFalse(fresh["cacheHit"])
        self.assertFalse(fresh["stale"])
        self.assertNotEqual(initial["jsonPath"], fresh["jsonPath"])
        cached = self.request("ensure-dependencies")
        self.assertTrue(cached["cacheHit"])
        self.assertEqual(cached["jsonPath"], fresh["jsonPath"])
        self.assertIn("binaries", cached)
        self.assertIn("imports", cached)

    def test_delayed_installer_files_refresh_and_recover_before_cached_optional_failures(self):
        self.os2_media()
        initial = self.request("audit-dependencies")
        self.assertEqual(initial["scannedBinaries"], 0)
        app = self.installed("Apps/LATE.EXE", fixtures.linear(["SOMD", "OPTIONAL"]))
        original = app.read_bytes()
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        recovered = self.request("ensure-dependencies")
        self.assertFalse(recovered["cacheHit"])
        self.assertEqual(recovered["missing"], ["OPTIONAL"])
        self.assertTrue((self.ball / "drive_c/OS2/DLL/SOMD.DLL").is_file())
        self.assertEqual(app.read_bytes(), original)
        self.assertFalse(self.request("snapshot")["dependencyAudit"]["stale"])
        again = self.request("ensure-dependencies")
        self.assertTrue(again["cacheHit"])
        self.assertEqual(again["jsonPath"], recovered["jsonPath"])

    def test_no_resolve_report_gets_one_automatic_recovery_attempt(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["SOMD"]))
        self.os2_media()
        static = self.request("audit-dependencies", resolve=False)
        self.assertEqual(static["missing"], ["SOMD"])
        recovered = self.request("ensure-dependencies")
        self.assertFalse(recovered["cacheHit"])
        self.assertEqual(recovered["missing"], [])
        self.assertTrue(self.request("ensure-dependencies")["cacheHit"])

    def test_binary_replacement_config_settings_and_media_invalidate(self):
        app = self.installed("Apps/APP.EXE", fixtures.linear(["DOSCALLS"]))
        media = self.os2_media()
        self.request("audit-dependencies")
        app.write_bytes(fixtures.linear(["SOMD"]))
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        self.request("ensure-dependencies")
        config = self.ball / "drive_c/CONFIG.SYS"
        config.write_bytes(config.read_bytes() + b"\r\nLIBPATH=C:\\OS2\\DLL;C:\\CUSTOM\r\n")
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        self.request("ensure-dependencies")
        self.request("command", arguments=["config", "system.dive", "false"])
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        self.request("ensure-dependencies")
        (media / "OS2/NEW.MSG").write_bytes(b"media changed")
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        self.request("ensure-dependencies")
        remembered = self.ball / "os2-media.json"
        remembered.write_text(json.dumps({"sources": [str(media)], "recorded": "changed"}))
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])

    def test_runtime_cache_and_unrelated_data_do_not_invalidate(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["DOSCALLS"]))
        self.request("audit-dependencies")
        self.installed("OS2/DLL/2INE/unicorn/hash/NEW.DLL", b"host cache")
        self.installed("Apps/README.TXT", b"data")
        self.assertFalse(self.request("snapshot")["dependencyAudit"]["stale"])
        self.assertTrue(self.request("ensure-dependencies")["cacheHit"])

    def test_prepare_attaches_audit_and_selected_runtime_changes_refresh(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["OPTIONAL"]))
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        request = dict(build=str(build), arguments=["run", "C:\\Apps\\APP.EXE"])
        first = self.request("prepare-launch", **request)
        audit = first["dependencyAudit"]
        self.assertEqual(audit["missing"], ["OPTIONAL"])
        self.assertFalse(audit["stale"])
        second = self.request("prepare-launch", **request)
        self.assertEqual(second["dependencyAudit"]["jsonPath"], audit["jsonPath"])
        (build / "runtime-capabilities.json").write_text('{"schema":1}')
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        refreshed = self.request("ensure-dependencies", build=str(build))
        self.assertFalse(refreshed["cacheHit"])
        self.assertNotEqual(refreshed["jsonPath"], audit["jsonPath"])
        self.assertIn("media", refreshed)
        self.assertIn("componentGroups", refreshed)


if __name__ == "__main__":
    unittest.main()
