# Public domain. No rights reserved. Wholly AI-authored.
"""Checked static imports, post-install auditing and original-media recovery."""

import contextlib
import hashlib
import io
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from tests.integration.disk_images import iso_image
from tools import ball as ball_api


def linear(imports=()):
    names = b"".join(bytes([len(name)]) + name.encode("ascii") for name in imports)
    dos = bytearray(64)
    dos[:2] = b"MZ"
    struct.pack_into("<I", dos, 60, 64)
    header = bytearray(196)
    header[:2] = b"LX"
    struct.pack_into("<H", header, 10, 1)
    struct.pack_into("<III", header, 112, 196, len(imports), 196 + len(names))
    return dos + header + names


def segmented(imports=()):
    names = bytearray()
    references = bytearray()
    for name in imports:
        references += struct.pack("<H", len(names))
        names += bytes([len(name)]) + name.encode("ascii")
    dos = bytearray(64)
    dos[:2] = b"MZ"
    struct.pack_into("<I", dos, 60, 64)
    header = bytearray(64)
    header[:2] = b"NE"
    header[54] = 1
    struct.pack_into("<H", header, 30, len(imports))
    struct.pack_into("<HH", header, 40, 64, 64 + len(references))
    return dos + header + references + names


def dsom_companions():
    return {
        **{f"OS2/{name}.EXE": linear(["DOSCALLS"]) for name in ("SOMDD", "SOMDSVR")},
        "OS2/ETC/SOM.IR": b"repository",
        **{
            f"OS2/ETC/DSOM/{name}.{ext}": b"repository index"
            for name in ("SOMDIMPL", "SOMDCLS")
            for ext in ("DAT", "TOC")
        },
        **{f"OS2/SYSTEM/{name}.MSG": b"messages" for name in ("SOMD", "SOMK", "SOME_OS2")},
    }


class DependencyAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-dependencies-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "Test.2ineball"
        ball_api.init(self.ball)

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def installed(self, path, data):
        target = self.ball / "drive_c" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return target

    def os2_media(self):
        media = self.root / "OS2 CD"
        dlls = media / "OS2/DLL"
        dlls.mkdir(parents=True)
        (media / "OS2/CMD.EXE").write_bytes(linear())
        (media / "OS2/SYSLEVEL.OS2").write_bytes(b"\xff\xffSYSLEVEL" + bytes(40))
        (dlls / "SOM.DLL").write_bytes(linear(["SOMD"]))
        (dlls / "SOMD.DLL").write_bytes(segmented(["DOSCALLS"]))
        for relative, content in dsom_companions().items():
            path = media / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        self.request("command", arguments=["map", "D", str(media)])
        return media

    def test_media_chooser_only_when_missing_and_no_verified_source(self):
        self.assertFalse(ball_api.needs_dependency_media({"missing": []}))
        self.assertTrue(
            ball_api.needs_dependency_media(
                {
                    "missing": [],
                    "componentFiles": [{"path": "C:\\OS2\\SOMDD.EXE", "status": "missing"}],
                }
            )
        )
        self.assertTrue(ball_api.needs_dependency_media({"missing": ["SOMD"], "media": []}))
        for status in (None, "unavailable", "unverified", "not-os2-media"):
            self.assertTrue(
                ball_api.needs_dependency_media(
                    {"missing": ["SOMD"], "media": [{"status": status}]}
                )
            )
        for status in ("verified-os2-media", "verified-os2-set"):
            self.assertFalse(
                ball_api.needs_dependency_media(
                    {"missing": ["SOMD"], "media": [{"status": status}]}
                )
            )

    def test_missing_and_unreachable_components_have_verified_group_metadata(self):
        self.installed(
            "Apps/APP.EXE", linear(["SOMD", "LIBUNI", "LIBCS", "UCONV", "ABSENT", "DOSCALLS"])
        )
        self.installed("Other/LIBCS.DLL", linear())
        result = self.request("audit-dependencies", recover=False)
        groups = {g["id"]: g for g in result["componentGroups"]}
        self.assertEqual(set(groups), {"som", "runtime"})
        self.assertEqual(groups["som"]["modules"], ["SOMD"])
        self.assertEqual(groups["som"]["requires"], ["runtime"])
        self.assertEqual(set(groups["runtime"]["modules"]), {"LIBUNI", "LIBCS", "UCONV"})
        self.assertTrue(all(not g["defaultSelected"] for g in groups.values()))
        self.assertNotIn("DOSCALLS", result["missing"])
        saved = json.loads(Path(result["jsonPath"]).read_text())
        self.assertEqual(saved["componentGroups"], result["componentGroups"])
        self.installed("Apps/SOMD.DLL", linear())
        self.installed("Apps/LIBUNI.DLL", linear())
        self.installed("Apps/LIBCS.DLL", linear())
        self.installed("Apps/UCONV.DLL", linear())
        for relative, content in dsom_companions().items():
            self.installed(relative, content)
        refreshed = self.request("audit-dependencies", recover=False)
        self.assertEqual(refreshed["componentGroups"], [])

    def test_som_component_marks_runtime_as_required_without_claiming_it_missing(self):
        self.installed("Apps/APP.EXE", linear(["SOM"]))
        result = self.request("audit-dependencies", recover=False)
        groups = {g["id"]: g for g in result["componentGroups"]}
        self.assertEqual(result["missing"], ["SOM"])
        self.assertEqual(groups["runtime"]["modules"], [])
        self.assertTrue(groups["runtime"]["requiredByGroup"])

    def test_graphics_provider_policy_suppresses_disabled_or_native_recovery(self):
        self.installed("Apps/APP.EXE", linear(["OPENGL", "DIVE"]))
        media = self.os2_media()
        (media / "OS2/DLL/OPENGL.DLL").write_bytes(linear(["DOSCALLS"]))
        (media / "OS2/DLL/DIVE.DLL").write_bytes(linear(["DOSCALLS"]))
        self.request("command", arguments=["config", "system.opengl", "off"])
        self.request("command", arguments=["config", "system.dive", "false"])
        result = self.request("audit-dependencies")
        status = {entry["module"]: entry["status"] for entry in result["imports"]}
        self.assertEqual(status["OPENGL"], "disabled")
        self.assertEqual(status["DIVE"], "disabled")
        self.assertEqual(set(result["disabled"]), {"OPENGL", "DIVE"})
        self.assertFalse(result["complete"])
        self.assertEqual(result["imported"], [])
        self.assertFalse((self.ball / "drive_c/OS2/DLL/OPENGL.DLL").exists())
        # An absent choice always selects Mesa, including old/non-Mesa runtimes.
        build = self.root / "Receiving runtime"
        build.mkdir()
        (build / "runtime-capabilities.json").write_text(
            json.dumps({"schema": 1, "features": {"mesa": True}})
        )
        saved = ball_api.load(self.ball)
        del saved["system"]["opengl"]
        ball_api.save(self.ball / "ball.json", saved)
        for capability in (None, False):
            metadata = build / "runtime-capabilities.json"
            if capability is None:
                metadata.unlink(missing_ok=True)
            else:
                metadata.write_text(json.dumps({"schema": 1, "features": {"mesa": False}}))
            defaulted = self.request("audit-dependencies", build=str(build))
            default_status = {entry["module"]: entry["status"] for entry in defaulted["imports"]}
            self.assertEqual(default_status["OPENGL"], "provided")
            self.assertEqual(defaulted["imported"], [])
            self.assertFalse((self.ball / "drive_c/OS2/DLL/OPENGL.DLL").exists())
            plan = self.request("plan-os2-extraction", sources=[str(media)], build=str(build))
            original_gl = next(
                file for file in plan["files"] if file["path"].upper().endswith("OPENGL.DLL")
            )
            self.assertFalse(original_gl["provided"])
            self.assertEqual(original_gl["category"], "opengl")
        inherited = self.request("audit-dependencies", build=str(build))
        inherited_status = {entry["module"]: entry["status"] for entry in inherited["imports"]}
        self.assertEqual(inherited_status["OPENGL"], "provided")
        recorded = self.request(
            "record-install",
            build=str(build),
            receipt={},
            exitCode=0,
            programHost=str(self.ball / "drive_c/Apps/APP.EXE"),
        )
        self.assertEqual(recorded["dependencyAudit"]["disabled"], ["DIVE"])
        self.assertNotIn("OPENGL", recorded["dependencyAudit"]["missing"])
        self.assertFalse((self.ball / "drive_c/OS2/DLL/OPENGL.DLL").exists())
        self.request("command", arguments=["config", "system.opengl", "mesa"])
        result = self.request("audit-dependencies")
        status = {entry["module"]: entry["status"] for entry in result["imports"]}
        self.assertEqual(status["OPENGL"], "provided")
        self.assertEqual(result["imported"], [])
        self.request("command", arguments=["config", "system.opengl", "original"])
        result = self.request("audit-dependencies")
        self.assertTrue((self.ball / "drive_c/OS2/DLL/OPENGL.DLL").exists())
        self.assertEqual(
            next(entry["status"] for entry in result["imports"] if entry["module"] == "OPENGL"),
            "found",
        )
        self.assertFalse((self.ball / "drive_c/OS2/DLL/DIVE.DLL").exists())

    def test_import_cycles_native_services_and_unreachable_libraries(self):
        self.installed("Apps/APP.EXE", linear(["A", "PMWIN", "OTHER", "ABSENT"]))
        self.installed("Apps/A.DLL", segmented(["B"]))
        self.installed("Apps/B.DLL", linear(["A", "DOSCALL1.DLL"]))
        self.installed("Hidden/OTHER.DLL", linear())
        result = self.request("audit-dependencies", resolve=False)
        self.assertEqual(result["scannedBinaries"], 4)
        self.assertEqual(result["missing"], ["ABSENT"])
        self.assertEqual(result["unreachable"][0]["module"], "OTHER")
        statuses = {entry["module"]: entry["status"] for entry in result["imports"]}
        self.assertEqual(statuses["PMWIN"], "provided")
        self.assertEqual(statuses["DOSCALL1"], "provided")
        self.assertEqual(statuses["A"], "found")
        self.assertEqual(statuses["OTHER"], "unreachable")
        self.assertFalse(result["complete"])
        self.assertTrue(result["staticOnly"])
        text = Path(result["logPath"]).read_text()
        self.assertIn("APP.EXE -> ABSENT: missing", text)
        retained = json.loads(Path(result["jsonPath"]).read_text())
        self.assertEqual(retained["imports"], result["imports"])
        self.assertEqual(
            self.request("snapshot")["dependencyAudit"]["jsonPath"], result["jsonPath"]
        )

    def test_lx_and_ne_counted_tables_reject_truncation_and_skip_pe(self):
        self.installed("Bad/LX.EXE", linear(["DLL"])[:-1])
        self.installed("Bad/NE.DLL", segmented(["DLL"])[:-1])
        bad = bytearray(linear())
        struct.pack_into("<I", bad, 60, 0xFFFFFFF0)
        self.installed("Bad/OFFSET.EXE", bad)
        pe = bytearray(linear())
        pe[64:68] = b"PE\0\0"
        self.installed("Bad/WINDOWS.EXE", pe)
        result = self.request("audit-dependencies", resolve=False)
        self.assertEqual(len(result["errors"]), 3)
        self.assertEqual(result["unsupportedBinaries"], 1)
        self.assertEqual(result["missing"], [])
        self.assertFalse(result["complete"])
        self.assertTrue(any("Windows" in limit for limit in result["limits"]))

    def test_attached_os2_folder_recovers_transitive_original_modules(self):
        self.installed("Apps/APP.EXE", linear(["SOM", "PMGPI"]))
        media = self.os2_media()
        result = self.request("audit-dependencies")
        self.assertTrue(result["automaticRecovery"])
        self.assertEqual(result["missing"], [])
        self.assertTrue(result["complete"])
        imported = {item["path"]: item for item in result["imported"]}
        self.assertEqual(
            set(imported),
            {"C:\\OS2\\DLL\\SOM.DLL", "C:\\OS2\\DLL\\SOMD.DLL"}
            | {"C:\\" + path.replace("/", "\\") for path in dsom_companions()},
        )
        for name in ("SOM.DLL", "SOMD.DLL"):
            original = (media / "OS2/DLL" / name).read_bytes()
            self.assertEqual((self.ball / "drive_c/OS2/DLL" / name).read_bytes(), original)
            self.assertEqual(
                imported["C:\\OS2\\DLL\\" + name]["sha256"], hashlib.sha256(original).hexdigest()
            )
        self.assertFalse((self.ball / "drive_c/OS2/CMD.EXE").exists())
        self.assertEqual(result["media"][0]["status"], "verified-os2-media")
        self.assertEqual(self.request("audit-dependencies")["imported"], [])

    def test_cli_sources_restore_without_changing_cd_mapping(self):
        self.installed("Apps/APP.EXE", linear(["SOM"]))
        media = self.os2_media()
        self.request("command", arguments=["unmap", "D"])
        extra = self.root / "Additional OS2 disk"
        extra.mkdir()
        before = ball_api.load(self.ball)["drives"]
        output = io.StringIO()
        build = self.root / "Receiving runtime"
        build.mkdir()
        with patch.object(ball_api, "configure_library"), contextlib.redirect_stdout(output):
            code = ball_api.main(
                [
                    "--ball",
                    str(self.ball),
                    "--build",
                    str(build),
                    "audit-dependencies",
                    "--source",
                    str(media),
                    "--source",
                    str(extra),
                ]
            )
        self.assertEqual(code, 0)
        report = json.loads(output.getvalue())
        self.assertTrue(report["complete"])
        self.assertEqual({entry["path"] for entry in report["media"]}, {str(media), str(extra)})
        self.assertEqual(ball_api.load(self.ball)["drives"], before)
        self.assertTrue((self.ball / "drive_c/OS2/DLL/SOM.DLL").is_file())
        self.assertTrue((self.ball / "drive_c/OS2/DLL/SOMD.DLL").is_file())

    def test_existing_somd_recovers_package_files_and_owns_service_without_overwriting_user_data(
        self,
    ):
        self.installed("Apps/APP.EXE", linear(["SOMD"]))
        self.installed("OS2/DLL/SOMD.DLL", segmented(["DOSCALLS"]))
        self.installed("OS2/ETC/DSOM/SOMDIMPL.DAT", b"user repository data")
        (self.ball / "drive_c/CONFIG.SYS").write_text(
            "SET SOMIR=C:\\APPS\\APP.IR\nSET SOMBASE=C:\\OS2\n"
        )
        manifest = ball_api.load(self.ball)
        manual = {"program": r"C:\APPS\OTHER.EXE", "arguments": ["keep"], "cwd": r"C:\APPS"}
        manifest["startupPrograms"] = [manual]
        ball_api.save(self.ball / "ball.json", manifest)
        before = self.request("audit-dependencies", resolve=False)
        self.assertEqual(before["missing"], [])
        self.assertFalse(before["complete"])
        self.assertEqual(len(before["componentFiles"]), 10)
        groups = {g["id"]: g for g in before["componentGroups"]}
        self.assertIn(r"C:\OS2\ETC\SOM.IR", groups["som"]["files"])
        self.assertEqual(groups["som"]["modules"], [])
        media = self.os2_media()
        recovered = self.request("audit-dependencies")
        self.assertTrue(recovered["complete"])
        self.assertEqual(len(recovered["imported"]), 9)
        self.assertEqual(
            (self.ball / "drive_c/OS2/ETC/DSOM/SOMDIMPL.DAT").read_bytes(), b"user repository data"
        )
        for path in dsom_companions():
            if not path.endswith("SOMDIMPL.DAT"):
                self.assertEqual(
                    (self.ball / "drive_c" / path).read_bytes(), (media / path).read_bytes()
                )
        services = ball_api.load(self.ball)["startupPrograms"]
        self.assertEqual(services[0], manual)
        self.assertEqual(len(services), 2)
        self.assertEqual(services[1]["owner"], "os2-som")
        self.assertEqual(services[1]["environment"]["SOMIR"], r"C:\OS2\ETC\SOM.IR;%SOMIR%")
        self.assertEqual(services[1]["environment"]["SOMBASE"], r"C:\OS2")
        self.assertEqual(self.request("audit-dependencies")["imported"], [])
        self.assertEqual(ball_api.load(self.ball)["startupPrograms"], services)
        (self.ball / "drive_c/CONFIG.SYS").write_text("SET SOMIR=C:\\APPS\\LATER.IR\n")
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"loader")
        self.installed("APPS/OTHER.EXE", linear())
        plan = self.request(
            "prepare-launch", build=str(build), arguments=["run", r"C:\APPS\APP.EXE"]
        )
        self.assertEqual(
            plan["startup"][1]["environment"]["SOMIR"], r"C:\OS2\ETC\SOM.IR;C:\APPS\LATER.IR"
        )

    def test_manual_som_daemon_service_is_preserved_and_incomplete_media_is_reported(self):
        self.installed("Apps/APP.EXE", linear(["SOMD"]))
        self.installed("OS2/DLL/SOMD.DLL", linear())
        before = self.request("audit-dependencies")
        self.assertFalse(before["complete"])
        self.assertNotIn("startupPrograms", ball_api.load(self.ball))
        self.os2_media()
        manifest = ball_api.load(self.ball)
        manual = {
            "program": r"C:\OS2\SOMDD.EXE",
            "arguments": ["custom"],
            "environment": {"SOMIR": "custom.ir"},
        }
        manifest["startupPrograms"] = [manual]
        ball_api.save(self.ball / "ball.json", manifest)
        recovered = self.request("audit-dependencies")
        self.assertTrue(recovered["complete"])
        self.assertEqual(ball_api.load(self.ball)["startupPrograms"], [manual])

    def test_companion_deletion_and_missing_managed_service_invalidate_recovery_cache(self):
        self.installed("Apps/APP.EXE", linear(["SOMD"]))
        self.os2_media()
        recovered = self.request("ensure-dependencies")
        self.assertTrue(recovered["complete"])
        self.assertFalse(self.request("snapshot")["dependencyAudit"]["stale"])
        (self.ball / "drive_c/OS2/ETC/SOM.IR").unlink()
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        restored = self.request("ensure-dependencies")
        self.assertFalse(restored["cacheHit"])
        self.assertEqual([f["path"] for f in restored["imported"]], [r"C:\OS2\ETC\SOM.IR"])
        self.assertTrue(restored["complete"])
        self.assertFalse(restored["stale"])
        manifest = ball_api.load(self.ball)
        del manifest["startupPrograms"]
        ball_api.save(self.ball / "ball.json", manifest)
        self.assertTrue(self.request("snapshot")["dependencyAudit"]["stale"])
        service = self.request("ensure-dependencies")
        self.assertEqual(service["imported"], [])
        self.assertTrue(service["complete"])
        self.assertFalse(service["stale"])
        self.assertEqual(len(ball_api.load(self.ball)["startupPrograms"]), 1)
        self.assertTrue(self.request("ensure-dependencies")["cacheHit"])

    def test_cli_detects_iso_inside_zip_before_dependency_recovery(self):
        self.installed("Apps/APP.EXE", linear(["SOMD"]))
        payload = io.BytesIO()
        original = segmented(["DOSCALLS"])
        with zipfile.ZipFile(payload, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("OS2/CMD.EXE", linear())
            archive.writestr("OS2/SYSLEVEL.OS2", b"\xff\xffSYSLEVEL" + bytes(40))
            archive.writestr("OS2/DLL/SOMD.DLL", original)
            for relative, content in dsom_companions().items():
                archive.writestr(relative, content)
        source = self.root / "OS2 nested.zip"
        with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("0-unreadable.iso", b"Not a readable ISO filesystem")
            archive.writestr("OS2.iso", iso_image(payload=payload.getvalue(), filename="FILES.ZIP"))
        output = io.StringIO()
        with patch.object(ball_api, "configure_library"), contextlib.redirect_stdout(output):
            self.assertEqual(
                ball_api.main(
                    ["--ball", str(self.ball), "audit-dependencies", "--source", str(source)]
                ),
                0,
            )
        report = json.loads(output.getvalue())
        self.assertTrue(report["complete"])
        self.assertTrue(report["media"][0]["path"].startswith("/@2ine-media/"))
        self.assertEqual(len(report["skippedSources"]), 1)
        self.assertIn("0-unreadable.iso", report["skippedSources"][0]["path"])
        self.assertIn("Cannot open", report["skippedSources"][0]["reason"])
        self.assertEqual((self.ball / "drive_c/OS2/DLL/SOMD.DLL").read_bytes(), original)

    def test_unrelated_attached_application_media_is_not_a_recovery_source(self):
        self.installed("Apps/APP.EXE", linear(["SOM"]))
        media = self.root / "Application CD"
        (media / "OS2/DLL").mkdir(parents=True)
        (media / "OS2/DLL/SOM.DLL").write_bytes(linear())
        self.request("command", arguments=["map", "D", str(media)])
        result = self.request("audit-dependencies")
        self.assertEqual(result["missing"], ["SOM"])
        self.assertEqual(result["imported"], [])
        self.assertFalse((self.ball / "drive_c/OS2/DLL/SOM.DLL").exists())
        self.assertIn("No verified OS/2", " ".join(result["limits"]))

    def test_remembered_floppy_set_and_missing_media_are_reported(self):
        self.installed("Apps/APP.EXE", linear(["SOM"]))
        command_disk = self.root / "command disk"
        version_disk = self.root / "version disk"
        library_disk = self.root / "library disk"
        for disk in (command_disk, version_disk, library_disk):
            (disk / "OS2/DLL").mkdir(parents=True)
        (command_disk / "OS2/CMD.EXE").write_bytes(linear())
        (version_disk / "OS2/SYSLEVEL.OS2").write_bytes(b"\xff\xffSYSLEVEL" + bytes(40))
        (library_disk / "OS2/DLL/SOM.DLL").write_bytes(linear(["DOSCALLS"]))
        (self.ball / "os2-media.json").write_text(
            json.dumps({"sources": [str(command_disk), str(version_disk), str(library_disk)]})
        )
        result = self.request("audit-dependencies")
        self.assertTrue(result["complete"])
        self.assertEqual(len(result["imported"]), 1)
        self.assertTrue(all(item["status"] == "verified-os2-set" for item in result["media"]))
        self.installed("Apps/SECOND.EXE", linear(["ABSENT"]))
        for disk in (command_disk, version_disk, library_disk):
            disk.rename(disk.with_name(disk.name + " moved"))
        result = self.request("audit-dependencies")
        self.assertEqual(result["missing"], ["ABSENT"])
        self.assertFalse(result["complete"])
        self.assertTrue(all(item["status"] == "unavailable" for item in result["media"]))
        self.assertTrue(Path(result["logPath"]).is_file())

    def test_full_disc_secondary_lookup_and_source_index_cache(self):
        self.installed("Apps/APP.EXE", linear(["FIRST", "SECOND"]))
        source = self.root / "OS2 archive.zip"
        with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("OS2IMAGE/OS2/CMD.EXE", linear())
            archive.writestr("OS2IMAGE/OS2/SYSLEVEL.OS2", b"\xff\xffSYSLEVEL" + bytes(40))
            archive.writestr("OS2IMAGE/OS2/DLL/FIRST.DLL", linear(["DOSCALLS"]))
            archive.writestr("CID/OPTIONAL/OS2/DLL/SECOND.DLL", segmented(["PMWIN"]))
        first = self.request("audit-dependencies", sources=[str(source)])
        self.assertTrue(first["complete"])
        self.assertEqual(len(first["imported"]), 2)
        self.assertEqual(first["media"][0]["coreIndexCache"], "miss")
        self.assertEqual(first["media"][0]["fullIndexCache"], "miss")
        self.assertEqual(
            json.loads((self.ball / "os2-media.json").read_text())["sources"], [str(source)]
        )
        for name in ("FIRST.DLL", "SECOND.DLL"):
            (self.ball / "drive_c/OS2/DLL" / name).unlink()
        second = self.request("audit-dependencies")
        self.assertTrue(second["complete"])
        self.assertEqual(second["media"][0]["coreIndexCache"], "hit")
        self.assertEqual(second["media"][0]["fullIndexCache"], "hit")
        core = next((self.ball / "cache").glob("*-core.json"))
        cache = json.loads(core.read_text())
        cache["index"]["FIRST"][0]["source"] = str(self.root / "outside.dll")
        core.write_text(json.dumps(cache))
        (self.ball / "drive_c/OS2/DLL/FIRST.DLL").unlink()
        third = self.request("audit-dependencies")
        self.assertTrue(third["complete"])
        self.assertEqual(third["media"][0]["coreIndexCache"], "miss")
        self.assertEqual(len(third["imported"]), 1)

    def test_wrong_platform_candidate_does_not_block_other_recovery_or_receipt(self):
        app = self.installed("Apps/APP.EXE", linear(["WINDOWS", "SOM"]))
        media = self.os2_media()
        other = bytearray(linear())
        other[64:68] = b"PE\0\0"
        (media / "OS2/DLL/WINDOWS.DLL").write_bytes(other)
        result = self.request("record-install", receipt={}, programHost=str(app), exitCode=0)
        receipt = json.loads((self.ball / "logs/last-install.json").read_text())
        report = json.loads(Path(result["dependencyAudit"]["jsonPath"]).read_text())
        self.assertEqual(receipt["exit_code"], 0)
        self.assertEqual(report["missing"], ["WINDOWS"])
        self.assertTrue((self.ball / "drive_c/OS2/DLL/SOM.DLL").is_file())
        self.assertTrue((self.ball / "drive_c/OS2/DLL/SOMD.DLL").is_file())
        self.assertFalse((self.ball / "drive_c/OS2/DLL/WINDOWS.DLL").exists())
        self.assertTrue(any("another platform" in error["message"] for error in report["errors"]))

    def test_help_manager_recovers_verified_runtime_resources_and_viewers(self):
        self.installed("Apps/APP.EXE", linear(["HELPMGR"]))
        media = self.os2_media()
        (media / "OS2/DLL/HELPMGR.DLL").write_bytes(linear(["PMWIN"]))
        (media / "OS2/DLL/HPMGRMRI.DLL").write_bytes(linear())
        (media / "OS2/VIEW.EXE").write_bytes(linear(["HELPMGR"]))
        (media / "OS2/VIEWDOC.EXE").write_bytes(linear(["HELPMGR"]))
        report = self.request("audit-dependencies")
        self.assertTrue(report["complete"])
        self.assertEqual(len(report["imported"]), 4)
        companion = next(item for item in report["imports"] if item["module"] == "HPMGRMRI")
        self.assertEqual(companion["requirement"], "verified-runtime-companion")
        (self.ball / "drive_c/OS2/DLL/HPMGRMRI.DLL").unlink()
        report = self.request("audit-dependencies", resolve=False)
        self.assertEqual(report["missing"], ["HPMGRMRI"])

    def test_existing_dll_is_never_overwritten(self):
        self.installed("Apps/APP.EXE", linear(["SOM"]))
        original = linear(["DOSCALLS"]) + b"existing application version"
        installed = self.installed("OS2/DLL/SOM.DLL", original)
        self.os2_media()
        result = self.request("audit-dependencies")
        self.assertEqual(result["imported"], [])
        self.assertEqual(installed.read_bytes(), original)
        self.assertTrue(result["complete"])

    def test_every_recorded_install_has_a_report_and_failed_installs_are_read_only(self):
        app = self.installed("Apps/APP.EXE", linear(["SOM"]))
        self.os2_media()
        for exit_code in (5, -15, 0):
            result = self.request(
                "record-install", receipt={}, programHost=str(app), exitCode=exit_code
            )
            receipt = json.loads((self.ball / "logs/last-install.json").read_text())
            self.assertEqual(receipt["exit_code"], exit_code)
            self.assertEqual(receipt["dependencyAudit"], result["dependencyAudit"])
            audit = json.loads(Path(receipt["dependencyAudit"]["jsonPath"]).read_text())
            self.assertEqual(audit["automaticRecovery"], exit_code == 0)
            self.assertTrue(Path(audit["logPath"]).is_file())
            self.assertEqual((self.ball / "drive_c/OS2/DLL/SOM.DLL").exists(), exit_code == 0)
        directories = list((self.ball / "logs").glob("dependency-audit-*"))
        self.assertEqual(len(directories), 3)
        with self.assertRaisesRegex(ValueError, "resolve must be a boolean"):
            self.request("audit-dependencies", resolve="yes")

    def test_wrong_platform_existing_dll_is_reported_and_preserved(self):
        self.installed("Apps/APP.EXE", linear(["BAD"]))
        windows = bytearray(128)
        windows[:2] = b"MZ"
        struct.pack_into("<I", windows, 60, 64)
        windows[64:68] = b"PE\0\0"
        target = self.installed("Apps/BAD.DLL", windows)
        result = self.request("audit-dependencies", resolve=False)
        self.assertFalse(result["complete"])
        self.assertEqual(result["imports"][0]["status"], "incompatible")
        self.assertTrue(any("existing file preserved" in e["message"] for e in result["errors"]))
        self.assertEqual(target.read_bytes(), windows)


if __name__ == "__main__":
    unittest.main()
