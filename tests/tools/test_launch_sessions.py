# Public domain. No rights reserved. Wholly AI-authored.
"""Launch preparation avoids recovery around peers and retains startup provenance."""

import json
import struct
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

    def test_desktop_identity_prefers_launcher_then_original_description(self):
        image = bytearray(fixtures.linear([]))
        description = b"Original OS/2 Fixture Application"
        table = bytes([len(description)]) + description + bytes(3)
        offset = len(image)
        image.extend(table)
        struct.pack_into("<II", image, 64 + 136, offset, len(table))
        self.installed("Apps/APP.EXE", image)
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        plan = self.request(
            "prepare-launch", build=str(build), arguments=["run", r"C:\Apps\APP.EXE"]
        )
        self.assertEqual(
            plan["desktopIdentity"],
            {"name": description.decode(), "nameSource": "module-description"},
        )
        manifest = self.ball / "ball.json"
        data = json.loads(manifest.read_text())
        data["applications"]["Registered Program Name"] = {
            "program": r"C:\Apps\APP.EXE",
            "icon": "missing-icon.ico",
        }
        manifest.write_text(json.dumps(data))
        named = self.request(
            "prepare-launch", build=str(build), arguments=["launch", "Registered Program Name"]
        )
        self.assertEqual(named["desktopIdentity"]["name"], "Registered Program Name")
        self.assertEqual(named["desktopIdentity"]["nameSource"], "launcher")
        for key in (
            "TWOINE_DESKTOP_SESSION",
            "TWOINE_DESKTOP_AUXILIARY",
            "TWOINE_DESKTOP_NAME",
            "TWOINE_DESKTOP_ICON",
        ):
            self.assertIn(key, named["unsetEnvironment"])
            with self.assertRaisesRegex(ValueError, "[Rr]eserved"):
                self.request(
                    "prepare-launch",
                    build=str(build),
                    arguments=["run", r"C:\Apps\APP.EXE"],
                    environment={key: "guest-override"},
                )


if __name__ == "__main__":
    unittest.main()
