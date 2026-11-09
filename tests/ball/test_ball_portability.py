# Public domain. No rights reserved. Wholly AI-authored.
"""Host runtime fallback and persistent state after moving a raw ball directory."""

import json
import os
import platform
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

from tools.ball import engine as ball_api

# Host executable header values (system header names) for foreign-CPU loaders.
MH_MAGIC_64 = 0xFEEDFACF
MH_EXECUTE = 2
FAT_MAGIC_64 = 0xCAFEBABF
CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM64 = 0x0100000C
ELF64_LSB_CURRENT = b"\x7fELF\x02\x01\x01"
ET_EXEC = 2
EM_X86_64 = 62
EM_AARCH64 = 183
IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_FILE_MACHINE_ARM64 = 0xAA64
IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002


class BallPortabilityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-portable-ball-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.receiving = self.make_runtime("Receiving runtime")

    def make_runtime(self, name):
        folder = self.root / name
        folder.mkdir()
        loader = folder / ("lx_loader.exe" if os.name == "nt" else "lx_loader")
        shutil.copy2(Path(sys.executable).resolve(), loader)
        (folder / "runtime-capabilities.json").write_text(
            json.dumps({"schema": 1, "backends": ["unicorn"], "defaultBackend": "unicorn"})
        )
        return folder

    def effective(self, override, default=None):
        return ball_api.native_request(
            {
                "action": "effective-runtime",
                "path": str(override),
                "build": str(default or self.receiving),
            }
        )

    def test_matching_override_is_used_and_missing_foreign_or_invalid_ones_fall_back(self):
        matching = self.make_runtime("Matching runtime")
        self.assertEqual(self.effective(matching), str(matching))
        self.assertEqual(self.effective(self.root / "missing"), str(self.receiving))
        foreign = self.root / "Foreign runtime"
        foreign.mkdir()
        loader = foreign / ("lx_loader.exe" if os.name == "nt" else "lx_loader")
        # ELF is foreign on macOS/Windows; Mach-O is foreign on Linux.
        loader.write_bytes(
            (b"\xcf\xfa\xed\xfe" if sys.platform.startswith("linux") else b"\x7fELF") + bytes(124)
        )
        loader.chmod(0o755)
        self.assertEqual(self.effective(foreign), str(self.receiving))
        loader.write_bytes(b"not a host executable")
        self.assertEqual(self.effective(foreign), str(self.receiving))
        if os.name != "nt":
            shutil.copy2(Path(sys.executable).resolve(), loader)
            loader.chmod(0o644)
            self.assertEqual(self.effective(foreign), str(self.receiving))
        with self.assertRaisesRegex(ValueError, "No compatible host runtime"):
            self.effective(foreign, self.root / "missing")

    def test_backend_selection_uses_compiled_capabilities_and_preserves_explicit_requests(self):
        def backend(name):
            return ball_api.native_request(
                {"action": "effective-backend", "build": str(self.receiving), "path": name}
            )

        self.assertEqual(backend("native"), "unicorn")
        self.assertEqual(backend("unicorn"), "unicorn")
        metadata = self.receiving / "runtime-capabilities.json"
        metadata.write_text(
            json.dumps({"schema": 1, "backends": ["native"], "defaultBackend": "native"})
        )
        with self.assertRaisesRegex(ValueError, "does not include Unicorn"):
            backend("unicorn")
        if sys.platform.startswith("linux") and platform.machine().lower() in (
            "i386",
            "i486",
            "i586",
            "i686",
            "x86_64",
            "amd64",
        ):
            self.assertEqual(backend("native"), "native")
        else:
            with self.assertRaisesRegex(ValueError, "Native execution is unavailable"):
                backend("native")
        metadata.write_text('{"schema":1,"backends":["not-a-backend"],"defaultBackend":"native"}')
        with self.assertRaisesRegex(ValueError, "Invalid runtime-capabilities"):
            backend("native")

    def test_foreign_cpu_and_truncated_universal_records_are_not_selected(self):
        folder = self.root / "Wrong CPU"
        folder.mkdir()
        loader = folder / ("lx_loader.exe" if os.name == "nt" else "lx_loader")
        arm = platform.machine().lower() in ("arm64", "aarch64")
        if sys.platform == "darwin":
            cpu = CPU_TYPE_X86_64 if arm else CPU_TYPE_ARM64
            data = struct.pack("<4I", MH_MAGIC_64, cpu, 0, MH_EXECUTE) + bytes(48)
        elif sys.platform.startswith("linux"):
            data = bytearray(64)
            data[:7] = ELF64_LSB_CURRENT
            struct.pack_into("<HH", data, 16, ET_EXEC, EM_X86_64 if arm else EM_AARCH64)
        else:
            data = bytearray(88)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 60, 64)
            data[64:68] = b"PE\0\0"
            struct.pack_into(
                "<H", data, 68, IMAGE_FILE_MACHINE_AMD64 if arm else IMAGE_FILE_MACHINE_ARM64
            )
            struct.pack_into("<H", data, 86, IMAGE_FILE_EXECUTABLE_IMAGE)
        loader.write_bytes(data)
        loader.chmod(0o755)
        self.assertEqual(self.effective(folder), str(self.receiving))
        if sys.platform == "darwin":
            native_cpu = CPU_TYPE_ARM64 if arm else CPU_TYPE_X86_64
            image = struct.pack("<4I", MH_MAGIC_64, native_cpu, 0, MH_EXECUTE) + bytes(48)
            # A little-endian FAT64 slice uses 64-bit little-endian offsets.
            fat = struct.pack("<II", FAT_MAGIC_64, 1)
            fat += struct.pack("<IIQQII", native_cpu, 0, 64, len(image), 0, 0)
            loader.write_bytes(fat + bytes(64 - len(fat)) + image)
            self.assertEqual(self.effective(folder), str(folder))
            truncated = bytearray(loader.read_bytes())
            struct.pack_into("<Q", truncated, 16, 4096)
            loader.write_bytes(truncated)
            self.assertEqual(self.effective(folder), str(self.receiving))

    def test_runtime_undo_restores_missing_or_foreign_choices_as_data(self):
        ball = self.root / "Undo.2ineball"
        ball_api.init(ball)
        for old in (str(self.root / "Other computer/missing"), "Z:\\Other computer\\runtime", ""):
            ball_api.native_request(
                {"action": "runtime-build", "ball": str(ball), "build": str(self.receiving)}
            )
            snapshot = ball_api.native_request(
                {"action": "restore-runtime", "ball": str(ball), "build": old}
            )
            self.assertEqual(snapshot["manifest"]["runtime"]["build"], old)
            self.assertEqual(self.effective(old), str(self.receiving))
        snapshot = ball_api.native_request({"action": "restore-runtime", "ball": str(ball)})
        self.assertNotIn("build", snapshot["manifest"]["runtime"])

    def test_move_keeps_manifest_profiles_and_guest_paths_and_uses_receiving_runtime(self):
        original = self.root / "Original.2ineball"
        ball_api.init(original)
        program = original / "drive_c/APPS/EDITOR.EXE"
        program.parent.mkdir()
        program.write_bytes(b"installed guest application")
        ball_api.native_request(
            {
                "action": "command",
                "ball": str(original),
                "arguments": [
                    "add-app",
                    "--cwd",
                    "C:\\TEMP",
                    "Editor",
                    "C:\\APPS\\EDITOR.EXE",
                    "a b",
                ],
            }
        )
        profile = original / "drive_c/OS2/OS2.INI"
        profile.write_bytes(b"installed profile bytes\0\xff")
        manifest = ball_api.load(original)
        manifest["runtime"]["build"] = str(self.root / "Other computer/old-runtime")
        manifest["runtime"]["future"] = "preserved"
        manifest["runtime"]["backend"] = "native"
        manifest["future"] = {"large": 18446744073709551615}
        ball_api.save(original / "ball.json", manifest)
        before = (original / "ball.json").read_bytes()
        config_before = (original / "drive_c/CONFIG.SYS").read_bytes()
        cached = original / "drive_c/OS2/DLL/2INE/default/foreign-machine/lx_loader"
        cached.parent.mkdir(parents=True)
        cached.write_bytes(b"foreign cached runtime is never selected")
        moved = self.root / "New folder with spaces" / "Relocated café.2ineball"
        moved.parent.mkdir()
        original.rename(moved)
        chosen = self.effective(manifest["runtime"]["build"])
        plan = ball_api.native_request(
            {
                "action": "prepare-launch",
                "ball": str(moved),
                "build": chosen,
                "arguments": ["launch", "Editor"],
            }
        )
        self.assertFalse(original.exists())
        self.assertEqual((moved / "ball.json").read_bytes(), before)
        self.assertEqual((moved / "drive_c/CONFIG.SYS").read_bytes(), config_before)
        self.assertEqual(
            (moved / "drive_c/OS2/OS2.INI").read_bytes(), b"installed profile bytes\0\xff"
        )
        self.assertEqual(plan["command"][1:], ["C:\\APPS\\EDITOR.EXE", "a b"])
        self.assertEqual(Path(plan["cwd"]), moved / "drive_c/TEMP")
        self.assertTrue(Path(plan["command"][0]).is_relative_to(moved))
        self.assertIn(plan["environment"]["TWOINE_RUNTIME_ID"][:16], plan["command"][0])
        self.assertEqual(plan["environment"]["TWOINE_BACKEND"], "unicorn")
        with self.assertRaisesRegex(ValueError, "Native execution is unavailable"):
            ball_api.native_request(
                {
                    "action": "prepare-launch",
                    "ball": str(moved),
                    "build": chosen,
                    "backend": "native",
                    "arguments": ["launch", "Editor"],
                }
            )
        self.assertEqual(
            Path(plan["command"][0]).read_bytes(),
            (self.receiving / Path(plan["command"][0]).name).read_bytes(),
        )
        self.assertNotIn(str(original), json.dumps(plan))
        self.assertEqual(
            (moved / cached.relative_to(original)).read_bytes(),
            b"foreign cached runtime is never selected",
        )


if __name__ == "__main__":
    unittest.main()
