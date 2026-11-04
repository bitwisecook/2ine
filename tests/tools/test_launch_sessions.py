# Public domain. No rights reserved. Wholly AI-authored.
"""Launch preparation avoids recovery around peers and retains startup provenance."""

import unittest
from pathlib import Path

from tests.tools import test_os2_dependencies as fixtures


class LaunchPreparationTests(unittest.TestCase):
    root: Path
    ball: Path
    setUp = fixtures.DependencyAuditTests.setUp
    request = fixtures.DependencyAuditTests.request
    installed = fixtures.DependencyAuditTests.installed
    os2_media = fixtures.DependencyAuditTests.os2_media

    def test_read_only_prepare_never_recovers_available_modules_or_service(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["SOMD"]))
        self.os2_media()
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        manifest = (self.ball / "ball.json").read_bytes()
        plan = self.request(
            "prepare-launch", build=str(build), arguments=["run", r"C:\Apps\APP.EXE"], resolve=False
        )
        self.assertFalse(plan["dependencyAudit"]["automaticRecovery"])
        self.assertEqual(plan["dependencyAudit"]["missing"], ["SOMD"])
        self.assertEqual(plan["dependencyAudit"]["imported"], [])
        self.assertFalse((self.ball / "drive_c/OS2/DLL/SOMD.DLL").exists())
        self.assertEqual((self.ball / "ball.json").read_bytes(), manifest)
        self.assertEqual(plan["startup"], [])
        self.assertEqual(plan["environment"]["TWOINE_BALL"], str(self.ball))
        self.assertEqual(plan["environment"]["TWOINE_IPC_DIR"], str(self.ball / "ipc"))
        with self.assertRaisesRegex(ValueError, "resolve must be a boolean"):
            self.request(
                "prepare-launch",
                build=str(build),
                arguments=["run", r"C:\Apps\APP.EXE"],
                resolve="no",
            )

    def test_prepared_service_has_owner_program_and_existing_operation_supervision(self):
        self.installed("Apps/APP.EXE", fixtures.linear(["SOMD"]))
        self.os2_media()
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        (build / "runtime-capabilities.json").write_text(
            '{"schema":1,"operationSupervisor":true,"backends":["unicorn"],"defaultBackend":"unicorn"}'
        )
        plan = self.request(
            "prepare-launch", build=str(build), arguments=["run", r"C:\Apps\APP.EXE"]
        )
        self.assertFalse(plan["operationSupervised"])
        self.assertEqual(len(plan["startup"]), 1)
        service = plan["startup"][0]
        self.assertEqual(service["owner"], "os2-som")
        self.assertEqual(service["program"], r"C:\OS2\SOMDD.EXE")
        self.assertTrue(service["operationSupervised"])
        self.assertIn("--operation", service["command"])
        self.assertEqual(service["environment"]["TWOINE_BALL"], plan["environment"]["TWOINE_BALL"])
        self.assertEqual(
            service["environment"]["TWOINE_IPC_DIR"], plan["environment"]["TWOINE_IPC_DIR"]
        )
        self.assertEqual(service["startup"], [])


if __name__ == "__main__":
    unittest.main()
