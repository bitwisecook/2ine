# Public domain. No rights reserved. Wholly AI-authored.
"""Static LX/LE/NE references, export evidence and conservative Lotus closures."""

import json
import struct
import tempfile
import unittest
from pathlib import Path

from tests.integration.images import linear_image
from tools.binary import lotus_imports as inventory


def segmented(named=False, size=8, iterated=False):
    image = bytearray(512)
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 60, 64)
    h = 64
    image[h : h + 2] = b"NE"
    image[h + 54] = 1
    for offset, value in ((28, 1), (30, 1), (34, 64), (40, 72), (42, 74), (50, 4)):
        struct.pack_into("<H", image, h + offset, value)
    struct.pack_into("<4H", image, h + 64, 32, size, 0x108 if iterated else 0x100, 8)
    struct.pack_into("<H", image, h + 72, 0)
    image[h + 74 : h + 74 + 16] = b"\x08DOSCALLS\x06ByName"
    segment = bytearray(8)
    struct.pack_into("<H", segment, 2, 65535)
    image.extend(segment)
    image.extend(struct.pack("<H", 1))
    image.extend(struct.pack("<BBHHH", 5, 6 if named else 1, 2, 1, 9 if named else 47))
    return image


class ImportParserTests(unittest.TestCase):
    def test_builtin_modules_follow_the_installation_manager_table(self):
        provided = inventory.builtin_modules(Path(__file__).resolve().parents[2])
        self.assertTrue({"DOSCALLS", "PMMERGE", "REGISTRY"} <= provided)

    def test_linear_ordinals_names_lists_and_exact_locations(self):
        records = [
            struct.pack("<BBhBH", 7, 1, 32, 1, 99),
            struct.pack("<BBhBH", 7, 2, 48, 2, 1),
            struct.pack("<BBBHI Ihh".replace(" ", ""), 0x27, 0x75, 2, 1, 70000, 0x12345678, -4, 10),
        ]
        data = linear_image(
            b"\xc3", records, ["DOSCALLS", "LIB"], "IMPORTS", import_names=b"\0\x04Name"
        )
        result = inventory.parse_image(data)
        self.assertTrue(result["complete"], result)
        self.assertEqual(
            [(item["module"], item["kind"]) for item in result["imports"]],
            [("DOSCALLS", "ordinal"), ("DOSCALLS", "ordinal"), ("LIB", "name")],
        )
        ordinary = next(item for item in result["imports"] if item.get("ordinal") == 99)
        ref = ordinary["references"][0]
        self.assertEqual(ref["sourceOffsets"], [32])
        self.assertEqual(ref["preferredSources"][0]["addresses"], [0x10020])
        self.assertEqual(
            data[ref["recordOffset"] : ref["recordOffset"] + ref["recordSize"]], records[0]
        )
        listed = next(item for item in result["imports"] if item.get("ordinal") == 70000)
        self.assertEqual(listed["references"][0]["sourceOffsets"], [-4, 10])
        self.assertEqual(
            listed["references"][0]["preferredSources"][0]["addresses"], [0xFFFC, 0x1000A]
        )
        self.assertEqual(result["imports"][-1]["name"], "Name")

    def test_forwarded_exports_retain_ordinal_name_and_entry_offsets(self):
        entries = b"\x02\x04\0\0" + struct.pack("<BHI", 1, 1, 8) + struct.pack("<BHI", 0, 1, 1)
        image = linear_image(
            b"\xc3", [], ["BASE"], "FORWARD", entry_table=entries, import_names=b"\0\x06Answer"
        )
        result = inventory.parse_image(image)
        self.assertTrue(result["complete"])
        for item in result["imports"]:
            self.assertEqual(item["origins"], ["forwarder"])
            self.assertEqual(item["references"][0]["recordSize"], 7)
        by_name = next(item for item in result["imports"] if item["kind"] == "name")
        self.assertEqual(by_name["name"], "Answer")
        self.assertEqual(by_name["references"][0]["exportOrdinal"], 2)

    def test_le_common_fixups_are_supported_without_decoding_page_payloads(self):
        image = linear_image(b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 1, 282)], ["DOSCALLS"], "LE")
        image[64:66] = b"LE"
        result = inventory.parse_image(image)
        self.assertTrue(result["complete"])
        self.assertEqual(result["format"], "LE")
        self.assertEqual(result["imports"][0]["ordinal"], 282)

    def test_ne_ordinal_name_and_segment_reference(self):
        result = inventory.parse_image(segmented())
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["imports"][0]["ordinal"], 47)
        reference = result["imports"][0]["references"][0]
        self.assertEqual(reference["recordOffset"], 522)
        self.assertEqual(reference["sourceFileOffsets"], [514])
        self.assertTrue(reference["additionalChainSourcesDecoded"])
        named = inventory.parse_image(segmented(named=True))
        self.assertEqual(named["imports"][0]["name"], "ByName")
        iterated = inventory.parse_image(segmented(iterated=True))
        self.assertFalse(iterated["imports"][0]["references"][0]["additionalChainSourcesDecoded"])
        self.assertEqual(iterated["imports"][0]["references"][0]["sourceFileOffsets"], [])

    def test_corruption_unsupported_and_decoder_gaps_are_explicit(self):
        image = linear_image(b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 2, 99)], ["DOSCALLS"], "BAD")
        self.assertFalse(inventory.parse_image(image)["complete"])
        self.assertIn("invalid import module", inventory.parse_image(image)["errors"][0])
        self.assertFalse(inventory.parse_image(image[:70])["complete"])
        self.assertFalse(inventory.parse_image(b"not executable")["supported"])
        self.assertIn("zero-sized", inventory.parse_image(segmented(size=0))["errors"][0])

    def test_source_body_markers_and_loader_special_export_are_evidence_only(self):
        root = Path(__file__).resolve().parents[2]
        exported = inventory.export_inventory(root)
        self.assertEqual(exported["DOSCALLS"][295]["runtimeSpecial"], "absolute-flat-data-selector")
        self.assertTrue(exported["PMWINX"][715]["sourceLocations"])
        self.assertEqual(exported["PMWINX"][715]["name"], "WINHELP")

    def test_forwarder_exports_take_their_target_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / "os2/include/private"
            private.mkdir(parents=True)
            (private / "api16.json").write_text("{}")
            (private / "api32.json").write_text("{}")
            (root / "emulator/loader").mkdir(parents=True)
            (root / "emulator/loader/imports.c").write_text("")
            doscalls = root / "os2/baseos/doscalls"
            (doscalls / "generated").mkdir(parents=True)
            (doscalls / "queue.c").write_text("APIRET DosReadQueue(void)\n{\n    return 0;\n}\n")
            (doscalls / "generated/doscalls-lx.h").write_text(
                "    LX_NATIVE_EXPORT32(DosReadQueue, 661),\n"
            )
            quecalls = root / "os2/baseos/quecalls/generated"
            quecalls.mkdir(parents=True)
            (quecalls / "quecalls-lx.h").write_text(
                'LX_NATIVE_FORWARDER(lx_forward_9, "DOSCALLS", 661, "");\n'
                'LX_NATIVE_FORWARDER(lx_forward_2, "DOSCALLS", 675, "");\n'
                '    LX_NATIVE_EXPORT_FORWARDER(lx_forward_2, "DOSPURGEQUEUE", 2),\n'
                '    LX_NATIVE_EXPORT_FORWARDER(lx_forward_9, "DosReadQueue", 9),\n'
            )
            exported = inventory.export_inventory(root)
        self.assertEqual(sorted(exported["QUECALLS"]), [9])
        entry = exported["QUECALLS"][9]
        self.assertEqual(entry["forwarder"], {"module": "DOSCALLS", "ordinal": 661})
        self.assertEqual(entry["symbol"], "DosReadQueue")
        self.assertEqual(entry["sourceLocations"][0]["path"], "os2/baseos/doscalls/queue.c")

    def test_optional_modules_do_not_enter_application_closure_and_output_is_stable(self):
        source = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            ball = Path(directory)
            root = ball / "drive_c/LOTUSW4"
            root.mkdir(parents=True)
            manifest = {"drives": {"C": "drive_c"}, "applications": {}, "system": {}}
            (ball / "ball.json").write_text(json.dumps(manifest))

            def write(name, modules, fixups=()):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(linear_image(b"\xc3", fixups, modules, "FIXTURE"))

            write("WORDPRO/WORDPRO.EXE", ["SHARED"], [struct.pack("<BBhBH", 7, 1, 2, 1, 1)])
            write("WORDPRO/SHARED.DLL", ["DOSCALLS"], [struct.pack("<BBhBH", 7, 1, 2, 1, 282)])
            write("WORDPRO/OPTIONAL.DLL", ["PMWINX"], [struct.pack("<BBhBH", 7, 1, 2, 1, 9999)])
            write("123/MAIN123W.DLL", ["PMWINX"], [struct.pack("<BBhBH", 7, 1, 2, 1, 9998)])
            before = {p.relative_to(ball): p.read_bytes() for p in ball.rglob("*") if p.is_file()}
            report = inventory.scan(ball, source)
            wordpro = next(app for app in report["applications"] if app["id"] == "wordpro")
            self.assertEqual(len(wordpro["binaries"]), 2)
            optional = next(item for item in report["imports"] if item.get("ordinal") == 9999)
            self.assertEqual(optional["applications"], [])
            self.assertTrue(optional["optionalImporters"])
            self.assertEqual(optional["semanticCompleteness"], "not-assessed")
            dynamic = next(item for item in report["imports"] if item.get("ordinal") == 9998)
            self.assertEqual(dynamic["knownDynamicApplications"], ["123"])
            self.assertEqual(dynamic["staticApplications"], [])
            self.assertEqual(dynamic["optionalImporters"], [])
            self.assertEqual(
                before,
                {p.relative_to(ball): p.read_bytes() for p in ball.rglob("*") if p.is_file()},
            )
            self.assertEqual(inventory.render_csv(report), inventory.render_csv(report))
            self.assertEqual(inventory.render_markdown(report), inventory.render_markdown(report))


def pe_image(wide=False, old_delay=False):
    data = bytearray(4608)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    optional, size = 88, 240 if wide else 224
    struct.pack_into("<HH", data, 68, 0x8664 if wide else 0x14C, 1)
    struct.pack_into("<H", data, 84, size)
    struct.pack_into("<H", data, optional, 0x20B if wide else 0x10B)
    base = 0x140000000 if wide else 0x400000
    struct.pack_into("<Q" if wide else "<I", data, optional + (24 if wide else 28), base)
    struct.pack_into("<I", data, optional + 60, 512)
    struct.pack_into("<I", data, optional + (108 if wide else 92), 16)
    directory = optional + (112 if wide else 96)
    for number, rva, length in ((0, 0x1300, 0x180), (1, 0x1000, 40), (13, 0x1100, 64)):
        struct.pack_into("<II", data, directory + number * 8, rva, length)
    section = optional + size
    struct.pack_into("<IIII", data, section + 8, 4096, 0x1000, 4096, 512)

    def at(rva):
        return 512 + rva - 0x1000

    def string(rva, text):
        encoded = text.encode() + b"\0"
        data[at(rva) : at(rva) + len(encoded)] = encoded

    struct.pack_into("<5I", data, at(0x1000), 0x1080, 0, 0, 0x1050, 0x1090)
    string(0x1050, "DOSCALLS.dll")
    ordinal_flag = 1 << (63 if wide else 31)
    struct.pack_into("<3Q" if wide else "<3I", data, at(0x1080), 0x10A0, ordinal_flag | 282, 0)
    struct.pack_into("<H", data, at(0x10A0), 1)
    string(0x10A2, "DosOpen")
    adjustment = base if old_delay else 0
    struct.pack_into(
        "<8I",
        data,
        at(0x1100),
        0 if old_delay else 1,
        0x1150 + adjustment,
        0,
        0x1190 + adjustment,
        0x1180 + adjustment,
        0,
        0,
        0,
    )
    string(0x1150, "PMWINX.dll")
    struct.pack_into("<2Q" if wide else "<2I", data, at(0x1180), ordinal_flag | 47, 0)
    struct.pack_into("<7I", data, at(0x1300) + 12, 0x1380, 10, 2, 1, 0x1340, 0x1350, 0x1360)
    struct.pack_into("<2I", data, at(0x1340), 0x1800, 0x1370)
    struct.pack_into("<I", data, at(0x1350), 0x1390)
    struct.pack_into("<H", data, at(0x1360), 0)
    string(0x1370, "DOSCALLS.#282")
    string(0x1380, "OWN.dll")
    string(0x1390, "Direct")
    return data


def omf_record(kind, payload):
    prefix = bytes([kind]) + struct.pack("<H", len(payload) + 1) + payload
    return prefix + bytes([-sum(prefix) & 255])


def counted(value):
    value = value.encode()
    return bytes([len(value)]) + value


def omf_object(name="TEST"):
    header = omf_record(0x80, counted(name))
    ordinal = omf_record(
        0x88, b"\0\xa0\x01\x01" + counted("_DosOpen") + counted("DOSCALLS") + struct.pack("<H", 282)
    )
    named = omf_record(0x88, b"\0\xa0\x01\0" + counted("Alias") + counted("PMWINX") + b"\0")
    exported = omf_record(
        0x88, b"\0\xa0\x02\xc2" + counted("Export") + b"\0" + struct.pack("<H", 7)
    )
    return header + ordinal + named + exported + omf_record(0x8A, b"\0")


class AdditionalBinaryFormatsTests(unittest.TestCase):
    def test_pe32_pe64_delay_and_forwarded_exports(self):
        from tools.binary import os2binary

        for wide, old_delay in ((False, False), (True, False), (False, True)):
            with self.subTest(wide=wide, old_delay=old_delay):
                result = os2binary.parse_image(pe_image(wide, old_delay))
                self.assertTrue(result["complete"], result)
                self.assertEqual(result["bits"], 64 if wide else 32)
                self.assertEqual(len(result["imports"]), 3)
                normal = next(item for item in result["imports"] if item.get("name") == "DosOpen")
                self.assertEqual(normal["references"][0]["hint"], 1)
                delay = next(item for item in result["imports"] if item["module"] == "PMWINX")
                self.assertEqual(delay["origins"], ["delay-import"])
                self.assertEqual(result["exports"][0]["names"][0]["name"], "Direct")
                self.assertEqual(result["exports"][1]["forwarder"]["ordinal"], 282)

    def test_omf_imports_exports_and_aligned_library_members(self):
        from tools.binary import os2binary

        obj = omf_object()
        parsed = os2binary.parse_image(obj)
        self.assertTrue(parsed["complete"], parsed)
        self.assertEqual(len(parsed["imports"]), 2)
        self.assertEqual(parsed["imports"][1]["name"], "Alias")
        self.assertEqual(parsed["exports"][0]["ordinal"], 7)
        self.assertEqual(parsed["exports"][0]["parameterWords"], 2)
        page = 32
        header = b"\xf0" + struct.pack("<H", page - 3) + struct.pack("<IHB", 512, 1, 0)
        library = bytearray(header + bytes(page - len(header)))
        for module in (omf_object("FIRST"), omf_object("SECOND")):
            library.extend(module)
            library.extend(bytes((-len(library)) % page))
        library.extend(b"\xf1" + struct.pack("<H", 512 - len(library) - 3))
        library.extend(bytes(1024 - len(library)))
        parsed = os2binary.parse_image(bytes(library))
        self.assertTrue(parsed["complete"], parsed)
        self.assertEqual([item["name"] for item in parsed["members"]], ["FIRST", "SECOND"])
        self.assertEqual(parsed["imports"][0]["fixupCount"], 2)
        self.assertEqual(len(parsed["exports"]), 2)

    def test_coff_import_library_members_and_offline_annotations(self):
        from tools.binary import os2binary

        strings = b"_DoThing@4\0DOSCALLS.dll\0"
        obj = struct.pack("<HHHHIIHH", 0, 65535, 0, 0x14C, 0, len(strings), 0, 3 << 2) + strings
        member_name = "thing.obj/".ljust(16)
        record = f"{member_name}{0:<12}{0:<6}{0:<6}{0:<8}{len(obj):<10}`\n".encode()
        archive = b"!<arch>\n" + record + obj + (b"\n" if len(obj) & 1 else b"")
        parsed = os2binary.parse_image(archive)
        self.assertTrue(parsed["complete"], parsed)
        imported = parsed["imports"][0]
        self.assertEqual(imported["name"], "DoThing")
        self.assertEqual(imported["references"][0]["recordOffset"], 68)
        catalogue = {
            "schema": 1,
            "modules": {
                "DOSCALLS": {
                    "ordinals": {"100": {"name": "DoThing", "aliases": ["Alias"], "signatures": []}}
                }
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "not-a-library-suffix.data"
            path.write_bytes(archive)
            result = os2binary.scan([path], catalogue)
            self.assertEqual(result["files"][0]["imports"][0]["api"][0]["ordinal"], 100)
            self.assertEqual(
                os2binary.lookup(catalogue, "doscalls.dll", "#100")[0]["name"], "DoThing"
            )
            self.assertEqual(
                os2binary.lookup(catalogue, "doscalls.dll", "alias")[0]["ordinal"], 100
            )

    def test_ne_and_lx_exports_include_names_and_unnamed_entries(self):
        from tools.binary import os2binary

        lx = linear_image(b"\xc3", [], [], "OWN", exports=[0, 1], export_names=[(1, "Named")])
        parsed = os2binary.parse_image(lx)
        self.assertEqual(parsed["moduleName"], "OWN")
        self.assertEqual(parsed["exports"][0]["names"][0]["name"], "Named")
        self.assertEqual(parsed["exports"][1]["names"], [])
        self.assertEqual(parsed["exports"][0]["preferredAddress"], 0x10000)
        bare = os2binary.parse_image(lx[64:])
        self.assertTrue(bare["complete"])
        ne = segmented()
        struct.pack_into("<HH", ne, 64 + 4, 128, 6)
        ne[64 + 128 : 64 + 134] = b"\x01\x01\x01\x02\0\0"
        parsed = os2binary.parse_image(ne)
        self.assertTrue(parsed["complete"], parsed)
        self.assertEqual(parsed["exports"][0]["segment"], 1)
        self.assertEqual(parsed["exports"][0]["offset"], 2)


class PortableScannerTests(unittest.TestCase):
    def test_internal_entry_fixup_does_not_hide_later_imports(self):
        from tools.binary import os2binary

        internal = struct.pack("<BBhB", 7, 3, 2, 1)
        external = struct.pack("<BBhBH", 7, 1, 4, 1, 282)
        image = linear_image(
            b"\xc3",
            [internal, external],
            ["DOSCALLS"],
            "INTERNAL",
            exports=[0],
            export_names=[(1, "Own")],
        )
        result = os2binary.parse_image(image)
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["imports"][0]["ordinal"], 282)
        self.assertEqual(result["internalReferences"][0]["entryOrdinal"], 1)
        self.assertEqual(result["internalReferences"][0]["targetExport"]["names"][0]["name"], "Own")

    def test_exports_only_and_opaque_parameter_metadata(self):
        from tools.binary import os2binary

        entries = b"\x01\x83\x01\0\x01\0\0\0\0\0"
        image = linear_image(
            b"\xc3",
            [struct.pack("<BBhBH", 7, 1, 2, 2, 99)],
            ["DOSCALLS"],
            "EXPORTS",
            entry_table=entries,
            export_names=[(1, "Named")],
        )
        self.assertFalse(os2binary.parse_image(image)["complete"])
        result = os2binary.parse_export_table(image)
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["scope"], "exports")
        self.assertEqual(result["exports"][0]["names"][0]["name"], "Named")
        self.assertFalse(result["exports"][0]["parameterTypesDecoded"])

    def test_f3_indexed_members_and_omf_public_symbols(self):
        from tools.binary import os2binary

        public = omf_record(
            0x91, b"\x01\x02" + counted("DriverEntry") + struct.pack("<I", 0x12345678) + b"\0"
        )
        obj = omf_record(0x80, counted("DRIVER")) + public + omf_object()
        names = b"I:\\DRIVER.OBJ\0"
        header = bytearray(24)
        header[:3] = b"\xf3\x15\0"
        struct.pack_into("<I", header, 4, len(names))
        struct.pack_into("<H", header, 8, 1)
        container = header + names + struct.pack("<I", 128)
        container.extend(bytes(128 - len(container)))
        container.extend(obj)
        result = os2binary.parse_image(bytes(container))
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["publicSymbols"][0]["name"], "DriverEntry")
        self.assertEqual(result["publicSymbols"][0]["offset"], 0x12345678)
        self.assertTrue(result["publicSymbols"][0]["moduleUnknown"])
        self.assertGreaterEqual(result["imports"][0]["references"][0]["recordOffset"], 128)
        self.assertIn("opaque", result["warnings"][-1])

    def test_catalogue_ambiguity_and_unnamed_ordinal_remain_explicit(self):
        from tools.binary import os2binary

        catalogue = {
            "schema": 1,
            "domain": "win32",
            "modules": {
                "DOSCALLS": {
                    "ordinals": {
                        "282": {
                            "name": "",
                            "ambiguous": True,
                            "nameVariants": [{"name": "Versioned"}],
                        }
                    }
                },
                "PMWINX": {
                    "ordinals": {"47": {"name": "", "nameConfidence": "unnamed-original-export"}}
                },
            },
            "namedSymbols": {"DOSCALLS": {"DosOpen": [{"source": "name-only"}]}},
        }
        self.assertTrue(os2binary.lookup(catalogue, "DOSCALLS", "Versioned")[0]["ambiguous"])
        self.assertIsNone(os2binary.lookup(catalogue, "DOSCALLS", "DosOpen")[0]["ordinal"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "program.data"
            path.write_bytes(pe_image())
            result = os2binary.scan([path], catalogue)
            imports = result["files"][0]["imports"]
            self.assertEqual(
                next(item for item in imports if item.get("ordinal") == 282)["resolutionStatus"],
                "ambiguous",
            )
            self.assertEqual(
                next(item for item in imports if item["module"] == "PMWINX")["resolutionStatus"],
                "known-ordinal-unnamed",
            )

    def test_standalone_isolated_cli_with_gzip_catalogue(self):
        import gzip
        import subprocess
        import sys

        from tools.binary import os2binary

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "os2binary.py"
            script.write_bytes(Path(os2binary.__file__).read_bytes())
            target = root / "driver.ifs"
            target.write_bytes(
                linear_image(
                    b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 1, 282)], ["DOSCALLS"], "DRIVER"
                )
            )
            catalogue = root / "catalogue.json.gz"
            catalogue.write_bytes(
                gzip.compress(
                    json.dumps(
                        {
                            "schema": 1,
                            "modules": {"DOSCALLS": {"ordinals": {"282": {"name": "DosOpen"}}}},
                        }
                    ).encode(),
                    mtime=0,
                )
            )
            output = root / "result.json"
            subprocess.run(
                [
                    sys.executable,
                    "-I",
                    str(script),
                    str(target),
                    "--catalogue",
                    str(catalogue),
                    "--output",
                    str(output),
                ],
                check=True,
                cwd=root,
            )
            result = json.loads(output.read_text())
            self.assertEqual(result["files"][0]["imports"][0]["api"][0]["name"], "DosOpen")
            self.assertEqual(
                target.read_bytes(),
                linear_image(
                    b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 1, 282)], ["DOSCALLS"], "DRIVER"
                ),
            )


class EvidenceOutputTests(unittest.TestCase):
    def test_ne_exports_only_preserves_non_os2_domain(self):
        from tools.binary import os2binary

        image = segmented()
        image[64 + 54] = 2
        struct.pack_into("<HH", image, 64 + 4, 128, 6)
        image[64 + 128 : 64 + 134] = b"\x01\x01\x01\x02\0\0"
        self.assertEqual(os2binary.parse_image(image)["format"], "NE-other")
        result = os2binary.parse_export_table(image)
        self.assertTrue(result["complete"], result)
        self.assertEqual(result["targetOSCode"], 2)
        self.assertIsNone(result["importsComplete"])
        self.assertTrue(result["exportsComplete"])
        self.assertEqual(result["exports"][0]["offset"], 2)

    def test_foreign_ne_imports_decode_without_cross_domain_binding(self):
        from tools.binary import os2binary

        image = segmented()
        image[64 + 54] = 2
        neutral = os2binary.parse_image(image, allow_foreign=True)
        self.assertTrue(neutral["complete"], neutral)
        self.assertEqual(neutral["domain"], "win16")
        self.assertEqual(neutral["imports"][0]["ordinal"], 47)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "windows.dll"
            path.write_bytes(image)
            wrong = {
                "schema": 1,
                "domain": "os2",
                "modules": {"DOSCALLS": {"ordinals": {"47": {"name": "OS2Only"}}}},
            }
            rejected = os2binary.scan([path], wrong, allow_foreign=True)
            imported = rejected["files"][0]["imports"][0]
            self.assertEqual(imported["api"], [])
            self.assertEqual(imported["resolutionStatus"], "unbound-domain-mismatch")
            proper = {
                **wrong,
                "domain": "win16",
                "modules": {"DOSCALLS": {"ordinals": {"47": {"name": "Win16Only"}}}},
            }
            accepted = os2binary.scan([path], proper, allow_foreign=True, domain="win16")
            self.assertEqual(accepted["files"][0]["imports"][0]["api"][0]["name"], "Win16Only")
            self.assertEqual(accepted["files"][0]["domain"], "win16")
        lx = linear_image(
            b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 1, 282)], ["DOSCALLS"], "FOREIGN"
        )
        struct.pack_into("<H", lx, 64 + 10, 4)
        self.assertEqual(os2binary.parse_image(lx)["format"], "LX-other")
        self.assertTrue(os2binary.parse_image(lx, allow_foreign=True)["complete"])
        self.assertEqual(os2binary.parse_image(lx, allow_foreign=True)["domain"], "linear-target-4")

    def test_nested_foreign_members_and_explicit_catalogue_mismatch_stay_unbound(self):
        from tools.binary import os2binary

        image = segmented()
        image[64 + 54] = 2

        def archive(data):
            record = f"{'member/':<16}{0:<12}{0:<6}{0:<6}{0:<8}{len(data):<10}`\n".encode()
            return b"!<arch>\n" + record + data + (b"\n" if len(data) & 1 else b"")

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested.lib"
            path.write_bytes(archive(archive(bytes(image))))
            os2 = {
                "schema": 1,
                "domain": "os2",
                "modules": {"DOSCALLS": {"ordinals": {"47": {"name": "OS2Only"}}}},
            }
            parsed = os2binary.scan([path], os2, allow_foreign=True)
            self.assertEqual(parsed["files"][0]["imports"][0]["encodedDomains"], ["win16"])
            self.assertEqual(parsed["files"][0]["imports"][0]["api"], [])
            windows = {**os2, "domain": "win16"}
            path.write_bytes(segmented())
            wrong = os2binary.scan([path], windows, domain="os2")
            self.assertEqual(wrong["files"][0]["imports"][0]["api"], [])
            self.assertEqual(
                wrong["files"][0]["imports"][0]["resolutionStatus"], "unbound-domain-mismatch"
            )

    def test_export_corruption_preserves_completed_import_phase(self):
        image = linear_image(
            b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 1, 282)], ["DOSCALLS"], "IMPORTS"
        )
        # Valid imports, but counted nonresident name cannot fit its declared area.
        struct.pack_into("<II", image, 64 + 136, len(image), 1)
        image.extend(b"\x04")
        result = inventory.parse_image(image)
        self.assertTrue(result["importsComplete"], result)
        self.assertFalse(result["exportsComplete"])
        self.assertFalse(result["complete"])
        self.assertEqual(result["imports"][0]["ordinal"], 282)

    def test_reference_artifact_is_deterministic_and_indexed(self):
        import gzip

        source = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            ball = base / "ball"
            root = ball / "drive_c/LOTUSW4/WORDPRO"
            root.mkdir(parents=True)
            (ball / "ball.json").write_text(
                json.dumps({"drives": {"C": "drive_c"}, "applications": {}, "system": {}})
            )
            (root / "WORDPRO.EXE").write_bytes(
                linear_image(
                    b"\xc3", [struct.pack("<BBhBH", 7, 1, 2, 1, 282)], ["DOSCALLS"], "WORDPRO"
                )
            )
            report = inventory.scan(ball, source)
            first = inventory.write_report(report, base / "first")
            second = inventory.write_report(report, base / "second")
            self.assertEqual(first, second)
            for name in (
                "imports.json",
                "imports.csv",
                "imports.md",
                "references.jsonl.gz",
                "missing-native.csv",
                "missing-libraries.json",
                "portable-policy-gaps.csv",
            ):
                self.assertEqual(
                    (base / "first" / name).read_bytes(), (base / "second" / name).read_bytes()
                )
            lines = gzip.decompress((base / "first/references.jsonl.gz").read_bytes()).splitlines()
            reference = first["binaries"][0]["imports"][0]["references"]
            self.assertEqual(reference["firstLine"], 1)
            self.assertEqual(reference["count"], 1)
            self.assertEqual(json.loads(lines[0])["ordinal"], 282)
            self.assertEqual(first["referenceData"]["records"], len(lines))

    def test_missing_native_backlog_keeps_optional_candidates_but_excludes_original_providers(self):
        rows = [
            {
                "module": "PMWINX",
                "ordinal": 9999,
                "policyProvided": True,
                "exportPresent": False,
                "applications": [],
            },
            {
                "module": "PMWINX",
                "ordinal": 630,
                "policyProvided": True,
                "exportPresent": True,
                "applications": ["123"],
            },
            {
                "module": "SOM",
                "ordinal": 1,
                "policyProvided": False,
                "exportPresent": False,
                "applications": ["123"],
            },
        ]
        result = inventory.missing_native_report({"imports": rows, "sourceRevision": "fixture"})
        self.assertEqual(result["sourceRevision"], "fixture")
        self.assertEqual(result["imports"], [rows[0]])

    def test_portable_policy_gap_is_separate_from_missing_exports_and_runtime_data(self):
        base = {"policyProvided": True, "exportPresent": True}
        rows = [
            {**base, "export": {"abi32Policy": False, "abi16Policy": False}},
            {**base, "export": {"abi32Policy": True}},
            {**base, "export": {"abi16Policy": True}},
            {**base, "export": {"runtimeSpecial": "absolute-flat-data-selector"}},
            {**base, "exportPresent": False},
            {**base, "policyProvided": False},
        ]
        self.assertEqual(inventory.portable_policy_report({"imports": rows})["imports"], [rows[0]])


if __name__ == "__main__":
    unittest.main()
