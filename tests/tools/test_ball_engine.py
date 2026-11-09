# Public domain. No rights reserved. Wholly AI-authored.
"""Shared C ABI: lossless metadata, hashlib parity, containment and concurrency."""

import concurrent.futures
import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path

from tools.ball import engine as ball_api


class EngineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-engine-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "Ball with café and spaces.2ineball"
        ball_api.init(self.ball)

    def test_mapping_reports_missing_paths_and_non_directories_without_memory_errors(self):
        missing = self.root / "missing"
        with self.assertRaisesRegex(ValueError, "Path does not exist:") as error:
            ball_api.native_request(
                {
                    "action": "command",
                    "ball": str(self.ball),
                    "arguments": ["map", "D", str(missing)],
                }
            )
        self.assertNotIn("memory", str(error.exception).lower())
        file = self.root / "file.txt"
        file.write_text("file")
        with self.assertRaisesRegex(ValueError, "Not a directory:"):
            ball_api.native_request(
                {"action": "command", "ball": str(self.ball), "arguments": ["map", "D", str(file)]}
            )
        nested_error = "Path does not exist:" if os.name == "nt" else "not a directory:"
        with self.assertRaisesRegex(ValueError, nested_error):
            ball_api.native_request(
                {
                    "action": "command",
                    "ball": str(self.ball),
                    "arguments": ["map", "D", str(file / "nested")],
                }
            )

    @unittest.skipIf(
        os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0),
        "requires POSIX permission checks as a non-root user",
    )
    def test_mapping_reports_permission_failure(self):
        blocked = self.root / "blocked"
        blocked.mkdir()
        child = blocked / "child"
        child.mkdir()
        blocked.chmod(0)
        try:
            with self.assertRaisesRegex(ValueError, "Permission denied"):
                ball_api.native_request(
                    {
                        "action": "command",
                        "ball": str(self.ball),
                        "arguments": ["map", "D", str(child)],
                    }
                )
            with self.assertRaisesRegex(ValueError, "Permission denied"):
                ball_api.native_request(
                    {
                        "action": "command",
                        "ball": str(self.ball),
                        "arguments": ["map", "D", str(blocked)],
                    }
                )
        finally:
            blocked.chmod(0o700)

    def test_large_unknown_numbers_and_nested_metadata_are_lossless(self):
        data = ball_api.load(self.ball)
        data["future"] = {
            "integer": 18446744073709551615,
            "negative": -9007199254740993,
            "nested": [True, None, "café"],
        }
        ball_api.save(self.ball / "ball.json", data)
        ball_api.main(["--ball", str(self.ball), "config", "env.LANG", "de_DE"])
        self.assertEqual(ball_api.load(self.ball)["future"], data["future"])

    def test_runtime_identity_matches_independent_sha256(self):
        build = self.root / "build"
        build.mkdir()
        for name, content in (
            ("lx_loader", b"loader"),
            ("libexample.so", bytes(range(256))),
            ("libexample.dylib", b"dynamic"),
        ):
            (build / name).write_bytes(content)
        digest = hashlib.sha256()
        for path in sorted(build.iterdir()):
            digest.update(path.name.encode())
            digest.update(hashlib.sha256(path.read_bytes()).digest())
        env = ball_api.environment(self.ball, ball_api.load(self.ball), build, "unicorn")
        self.assertEqual(env["TWOINE_RUNTIME_ID"], digest.hexdigest())

    def test_nonexistent_traversal_cannot_escape_mapped_roots(self):
        with self.assertRaisesRegex(ValueError, "outside mapped"):
            ball_api.host_path(r"C:\missing\..\..\escape.ini", self.ball, ball_api.load(self.ball))

    def test_independent_calls_are_reentrant(self):
        # Initialize the ctypes binding before issuing simultaneous C calls.
        ball_api.load(self.ball)

        def create(index):
            path = self.root / f"Parallel {index}"
            ball_api.init(path)
            snapshot = ball_api.native_request({"action": "snapshot", "ball": str(path)})
            return json.dumps(snapshot["manifest"], sort_keys=True)

        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(create, range(24)))
        self.assertEqual(len(set(results)), 1)


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-workspace-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "ball"
        ball_api.init(self.ball)
        self.drive = self.ball / "drive_c"
        self.media = self.root / "Media with spaces"
        self.media.mkdir()
        self.request("command", arguments=["map", "D", str(self.media)])
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "lx_loader").write_bytes(b"fixture")
        (self.drive / "OS2/DOOM.EXE").write_bytes(b"fixture")

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def test_supervision_requires_runtime_support_and_covers_owned_startup_services(self):
        arguments = ["install", r"C:\OS2\DOOM.EXE", "a value"]
        legacy = self.request("prepare-launch", build=str(self.build), arguments=arguments)
        self.assertFalse(legacy["operationSupervised"])
        self.assertEqual(legacy["command"][1:], [r"C:\OS2\DOOM.EXE", "a value"])

        (self.build / "runtime-capabilities.json").write_text(
            json.dumps(
                {
                    "schema": 1,
                    "operationSupervisor": True,
                    "backends": ["unicorn"],
                    "defaultBackend": "unicorn",
                    "features": {
                        "mesa": False,
                        "video": True,
                        "pcm": True,
                        "midi": True,
                        "dive": True,
                    },
                }
            )
        )
        (self.drive / "OS2/SERVICE.EXE").write_bytes(b"service")
        data = ball_api.load(self.ball)
        data["startupPrograms"] = [{"program": r"C:\OS2\SERVICE.EXE"}]
        ball_api.save(self.ball / "ball.json", data)
        plan = self.request("prepare-launch", build=str(self.build), arguments=arguments)
        self.assertTrue(plan["operationSupervised"])
        self.assertEqual(plan["command"][1:], ["--operation", r"C:\OS2\DOOM.EXE", "a value"])
        service = plan["startup"][0]
        self.assertTrue(service["operationSupervised"])
        self.assertEqual(service["command"][1:], ["--operation", r"C:\OS2\SERVICE.EXE"])
        for launch in (plan, service):
            for key in ("TWOINE_OPERATION_LEASE_FD", "TWOINE_OPERATION_OWNER_FD"):
                self.assertNotIn(key, launch["environment"])
                self.assertIn(key, launch["unsetEnvironment"])
        normal = self.request(
            "prepare-launch", build=str(self.build), arguments=["run", r"C:\OS2\DOOM.EXE"]
        )
        self.assertFalse(normal["operationSupervised"])
        self.assertEqual(normal["command"][1:], [r"C:\OS2\DOOM.EXE"])

    def test_startup_programs_have_independent_checked_launch_plans(self):
        (self.drive / "OS2/SERVICE.EXE").write_bytes(b"service")
        data = ball_api.load(self.ball)
        data["startupPrograms"] = [
            {
                "program": r"C:\OS2\SERVICE.EXE",
                "cwd": r"C:\OS2",
                "arguments": ["a b"],
                "environment": {"SERVICE": "yes"},
            }
        ]
        ball_api.save(self.ball / "ball.json", data)
        plan = self.request(
            "prepare-launch", build=str(self.build), arguments=["run", r"C:\OS2\DOOM.EXE"]
        )
        self.assertEqual(len(plan["startup"]), 1)
        service = plan["startup"][0]
        self.assertEqual(service["command"][1:], [r"C:\OS2\SERVICE.EXE", "a b"])
        self.assertEqual(service["cwd"], str(self.drive / "OS2"))
        self.assertEqual(service["environment"]["SERVICE"], "yes")
        self.assertNotIn("SERVICE", plan["environment"])
        self.assertEqual(service["startup"], [])
        direct = self.request(
            "prepare-launch", build=str(self.build), arguments=["run", r"C:\OS2\SERVICE.EXE"]
        )
        self.assertEqual(direct["startup"], [])
        (self.drive / "OS2/SERVICE.EXE").unlink()
        with self.assertRaisesRegex(ValueError, "Program does not exist"):
            self.request(
                "prepare-launch", build=str(self.build), arguments=["run", r"C:\OS2\DOOM.EXE"]
            )

    def test_drive_defaults_relative_paths_and_override_inheritance(self):
        self.request("set-working-directory", path="C:\\OS2")
        (self.media / "INSTALL").mkdir()
        self.request("set-working-directory", path="D:\\INSTALL")
        snapshot = self.request("snapshot")
        self.assertEqual(snapshot["workingDirectories"], {"C": "C:\\OS2", "D": "D:\\INSTALL"})
        self.assertEqual(self.request("browse", path="C:")["path"], "C:\\OS2")
        self.assertEqual(
            self.request("host-path", path="D:SETUP.EXE"), str(self.media / "INSTALL/SETUP.EXE")
        )
        self.request(
            "save-application",
            name="Doom",
            program="C:\\OS2\\DOOM.EXE",
            arguments=[],
            workingDirectories={"C": "C:\\TEMP"},
            environment={"PATH": "%PATH%;C:\\DOOM", "DATA": "%TEMP%"},
        )
        before = (self.ball / "ball.json").read_bytes()
        plan = self.request("prepare-launch", build=str(self.build), arguments=["launch", "Doom"])
        self.assertEqual(plan["cwd"], str(self.drive / "TEMP"))
        self.assertEqual(plan["environment"]["TWOINE_DRIVE_CWD_D"], "INSTALL")
        self.assertEqual(plan["environment"]["TWOINE_DRIVE_CWD_C"], "TEMP")
        self.assertTrue(plan["environment"]["OS2PATH"].endswith(";C:\\DOOM"))
        self.assertEqual(plan["environment"]["DATA"], "C:\\TEMP")
        self.assertNotIn("PATH", plan["environment"])
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)
        self.assertEqual(self.request("snapshot")["workingDirectories"]["C"], "C:\\OS2")

    def test_install_command_file_uses_original_interpreter_and_script_directory(self):
        folder = self.media / "Installer Files"
        folder.mkdir()
        script = folder / "Start Here.cmd"
        script.write_text("echo installer")
        with self.assertRaisesRegex(ValueError, "original OS/2 CMD.EXE"):
            self.request(
                "prepare-launch",
                build=str(self.build),
                arguments=["install", r"D:\Installer Files\Start Here.cmd"],
            )
        interpreter = self.drive / "OS2/CMD.EXE"
        interpreter.write_bytes(b"original interpreter fixture")
        plan = self.request(
            "prepare-launch",
            build=str(self.build),
            arguments=["install", r"D:\Installer Files\Start Here.cmd", "a value", r"C:\TARGET"],
        )
        self.assertEqual(
            plan["command"][1:],
            [
                r"C:\OS2\CMD.EXE",
                "/C",
                "CALL",
                r"D:\Installer Files\Start Here.cmd",
                "a value",
                r"C:\TARGET",
            ],
        )
        self.assertEqual(plan["cwd"], str(folder))
        self.assertEqual(plan["environment"]["TWOINE_CURRENT_DRIVE"], "D")
        self.assertEqual(plan["environment"]["TWOINE_DRIVE_CWD_D"], "Installer Files")
        self.assertEqual(plan["programHost"], str(script))
        self.assertEqual(
            plan["receipt"]["program_sha256"], hashlib.sha256(script.read_bytes()).hexdigest()
        )
        script.unlink()
        self.request(
            "record-install", receipt=plan["receipt"], programHost=plan["programHost"], exitCode=7
        )
        receipt = json.loads((self.ball / "logs/last-install.json").read_text())
        self.assertEqual(receipt["program_sha256"], plan["receipt"]["program_sha256"])
        self.assertEqual(receipt["exit_code"], 7)

    def test_environment_templates_are_order_independent_and_cyclic_edits_are_atomic(self):
        result = self.request(
            "guest-environment",
            environment={"B": "%A%\\more", "A": "%TEMP%", "PATH": "%PATH%;C:\\DOOM"},
        )
        self.assertEqual(result["B"], "C:\\TEMP\\more")
        self.assertTrue(result["OS2PATH"].endswith(";C:\\DOOM"))
        self.request("command", arguments=["config", "env.PATH", "%PATH%;C:\\BALL"])
        self.assertTrue(
            self.request("snapshot")["environmentDefaults"]["PATH"].endswith(";C:\\BALL")
        )
        before = (self.ball / "ball.json").read_bytes()
        for environment in ({"A": "%B%", "B": "%A%"}, {"HOME": "bad"}):
            with self.assertRaises(ValueError):
                self.request(
                    "save-application",
                    name="Bad",
                    program="C:\\OS2\\DOOM.EXE",
                    arguments=[],
                    environment=environment,
                )
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)

    def test_import_folders_and_collision_preflight_do_not_replace_files(self):
        source = self.root / "Doom Files"
        source.mkdir()
        (source / "DOOM.WAD").write_bytes(bytes(range(256)) * 1024)
        (source / "Docs").mkdir()
        (source / "Docs/Read me.txt").write_text("café")
        self.request("import-files", path="C:\\", sources=[str(source)])
        self.assertEqual(
            (self.drive / "Doom Files/DOOM.WAD").read_bytes(), (source / "DOOM.WAD").read_bytes()
        )
        self.assertEqual((self.drive / "Doom Files/Docs/Read me.txt").read_text(), "café")
        new = self.root / "new.txt"
        new.write_text("new")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.request("import-files", path="C:\\", sources=[str(new), str(source)])
        self.assertFalse((self.drive / "new.txt").exists())
        self.assertTrue(source.exists())

    def test_copy_rejects_links_and_drive_escape(self):
        link = self.root / "link"
        try:
            link.symlink_to(self.media, target_is_directory=True)
        except OSError:
            self.skipTest("Host does not permit symlink creation")
        with self.assertRaisesRegex(ValueError, "Symbolic links"):
            self.request("import-files", path="C:\\", sources=[str(link)])
        with self.assertRaisesRegex(ValueError, "outside mapped"):
            self.request("browse", path="C:\\..\\..")

    def test_app_edits_and_undo_records_keep_unknown_metadata_and_icons(self):
        self.request(
            "save-application",
            name="Doom",
            program="C:\\OS2\\DOOM.EXE",
            arguments=["a b", "", "both\"and'quotes"],
            environment={"X": "one"},
            workingDirectories={"D": "D:\\"},
        )
        data = ball_api.load(self.ball)
        data["applications"]["Doom"]["future"] = {"value": 18446744073709551615}
        ball_api.save(self.ball / "ball.json", data)
        self.request(
            "save-application",
            name="Doom",
            originalName="Doom",
            program="C:\\OS2\\DOOM.EXE",
            arguments=["new"],
            environment={"X": "two"},
            workingDirectories={"D": "D:\\"},
        )
        current = self.request("snapshot")
        record = current["applicationRecords"]["Doom"]
        self.request("command", arguments=["remove-app", "Doom"])
        self.request("command", arguments=["restore-app", "Doom", record])
        restored = ball_api.load(self.ball)["applications"]["Doom"]
        self.assertEqual(restored["future"], data["applications"]["Doom"]["future"])
        self.assertEqual(restored["environment"], {"X": "two"})
        arguments = ["a b", "", "both\"and'quotes", "C:\\DATA\\", "%PATH%"]
        formatted = self.request("format-arguments", arguments=arguments)
        self.assertEqual(self.request("parse-arguments", text=formatted), arguments)

    def test_application_name_uses_checked_module_description_and_filename_fallback(self):
        import struct

        for signature in (b"LX", b"NE"):
            image = bytearray(512)
            image[:2] = b"MZ"
            struct.pack_into("<I", image, 60, 64)
            image[64:66] = signature
            description = b"  Example OS/2 Application  "
            table = bytes([len(description)]) + description + bytes(3)
            image[400 : 400 + len(table)] = table
            if signature == b"LX":
                struct.pack_into("<II", image, 64 + 136, 400, len(table))
            else:
                struct.pack_into("<I", image, 64 + 44, 400)
                struct.pack_into("<H", image, 64 + 32, len(table))
            program = self.drive / "OS2/Example.exe"
            program.write_bytes(image)
            result = self.request("application-icon", program="C:\\OS2\\Example.exe")
            self.assertEqual(result["displayName"], "Example OS/2 Application")
            self.assertEqual(result["source"], "none")
            # An exported name is not an ordinal-zero module description.
            image[400 + len(description) + 1] = 1
            program.write_bytes(image)
            self.assertEqual(
                self.request("application-icon", program="C:\\OS2\\Example.exe")["displayName"],
                "Example",
            )
            program.write_bytes(image[:410])
            self.assertEqual(
                self.request("application-icon", program="C:\\OS2\\Example.exe")["displayName"],
                "Example",
            )

    def test_lx_ne_and_array_icons_use_resource_bytes_and_alpha_masks(self):
        import base64
        import struct
        import sys

        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
        from images import linear_image

        # Two bottom-up XOR rows followed by the AND rows; white, black,
        # transparent and inverse pixels exercise mask interpretation.
        icon = struct.pack("<2sIhhI", b"IC", 48, 0, 0, 32)
        icon += struct.pack("<I4H", 12, 2, 4, 1, 1) + bytes.fromhex("000000ffffff")
        icon += bytes.fromhex("800000004000000000000000c0000000")
        for kind in ("LX", "LX-ITERATED", "LX-COMPRESSED", "LX-ARRAY", "NE"):
            if kind.startswith("LX"):
                payload = icon
                if kind == "LX-ARRAY":
                    members = []
                    for offset, width in ((0, 1), (62, 2)):
                        member = bytearray(icon)
                        struct.pack_into("<I", member, 10, offset + 46)
                        struct.pack_into("<H", member, 18, width)
                        members.append(
                            struct.pack("<2sIIHH", b"BA", 62, 62 if not offset else 0, 0, 0)
                            + member
                        )
                    payload = b"".join(members)
                image = linear_image(b"\xc3", [], [], "ICON", resources=[(1, 1, payload)])
                if kind in ("LX-ITERATED", "LX-COMPRESSED"):
                    image = bytearray(image)
                    header = struct.unpack_from("<I", image, 60)[0]
                    pages = struct.unpack_from("<I", image, header + 72)[0]
                    origin = struct.unpack_from("<I", image, header + 128)[0] + 4096
                    packed = (
                        struct.pack("<HH", 1, len(icon)) + icon
                        if kind == "LX-ITERATED"
                        else bytes([len(icon) << 2]) + icon
                    )
                    image[origin:] = packed
                    struct.pack_into(
                        "<HH",
                        image,
                        header + pages + 12,
                        len(packed),
                        1 if kind == "LX-ITERATED" else 5,
                    )
            else:
                image = bytearray(512 + len(icon))
                image[:2] = b"NE"
                struct.pack_into("<H", image, 28, 1)
                struct.pack_into("<HH", image, 34, 64, 72)
                struct.pack_into("<HHB", image, 50, 9, 1, 1)
                struct.pack_into("<4H", image, 64, 1, len(icon), 0, len(icon))
                struct.pack_into("<HH", image, 72, 1, 1)
                image[512:] = icon
            (self.drive / "OS2/ICON.EXE").write_bytes(image)
            result = self.request("application-icon", program="C:\\OS2\\ICON.EXE")
            self.assertEqual(
                (result["source"], result["width"], result["height"]), ("embedded", 2, 2)
            )
            pixels = base64.b64decode(result["rgba"])
            self.assertEqual(len(pixels), 16)
            self.assertEqual(list(pixels[3::4]), [0, 128, 255, 255])
        custom = self.root / "Custom.ico"
        custom.write_bytes(icon)
        self.request(
            "save-application",
            name="Icon",
            program="C:\\OS2\\ICON.EXE",
            arguments=[],
            iconSource=str(custom),
        )
        custom.unlink()
        self.assertEqual(self.request("application-icon", name="Icon")["source"], "custom")
        for malformed in (b"MZ", b"NE" + bytes(100), b"LX" + bytes(200)):
            (self.drive / "OS2/ICON.EXE").write_bytes(malformed)
            self.assertEqual(
                self.request("application-icon", program="C:\\OS2\\ICON.EXE")["source"], "none"
            )
