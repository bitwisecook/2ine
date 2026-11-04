# Public domain. No rights reserved. Wholly AI-authored.
"""Hierarchical media choices, provider links and exact file extraction."""

import io
import json
import unittest
import zipfile
from pathlib import Path

from tests.tools import test_os2_support as fixtures


def walk(node):
    yield node
    for child in node.get("children", []):
        yield from walk(child)


def eligible_sources(node):
    if node.get("disabled") or node["kind"] == "setting":
        return set()
    if node["kind"] == "file":
        return {node["source"]} if not node.get("provided") and not node.get("present") else set()
    return set().union(*(eligible_sources(child) for child in node.get("children", [])))


def hierarchy_iso(files):
    directories = {""}
    for name in files:
        parent = Path(name).parent
        while str(parent) != ".":
            directories.add(str(parent))
            parent = parent.parent
    ordered = sorted(directories, key=lambda name: (name.count("/"), name))
    if ordered[0] != "":
        ordered.remove("")
        ordered.insert(0, "")
    sectors = {name: 20 + i for i, name in enumerate(ordered)}
    extents = {name: 20 + len(ordered) + i for i, name in enumerate(files)}
    data = bytearray((21 + len(ordered) + len(files)) * 2048)

    def both(at, value, size=4):
        data[at : at + size] = value.to_bytes(size, "little")
        data[at + size : at + size * 2] = value.to_bytes(size, "big")

    def record(at, sector, size, raw, directory=False):
        length = (33 + len(raw) + 1) & ~1
        data[at] = length
        both(at + 2, sector)
        both(at + 10, size)
        data[at + 25] = 2 if directory else 0
        both(at + 28, 1, 2)
        data[at + 32] = len(raw)
        data[at + 33 : at + 33 + len(raw)] = raw
        return length

    data[16 * 2048 : 16 * 2048 + 7] = b"\x01CD001\x01"
    data[17 * 2048 : 17 * 2048 + 7] = b"\xffCD001\x01"
    both(16 * 2048 + 80, len(data) // 2048)
    both(16 * 2048 + 128, 2048, 2)
    record(16 * 2048 + 156, sectors[""], 2048, b"\0", True)
    for folder in ordered:
        parent = str(Path(folder).parent) if folder else ""
        if parent == ".":
            parent = ""
        at = sectors[folder] * 2048
        at += record(at, sectors[folder], 2048, b"\0", True)
        at += record(at, sectors[parent], 2048, b"\1", True)
        for child in ordered:
            if child and str(Path(child).parent).replace(".", "") == folder:
                at += record(at, sectors[child], 2048, Path(child).name.encode(), True)
        for name, payload in files.items():
            if str(Path(name).parent).replace(".", "") == folder:
                at += record(at, extents[name], len(payload), (Path(name).name + ";1").encode())
                start = extents[name] * 2048
                data[start : start + len(payload)] = payload
    return bytes(data)


class OS2TreeTests(unittest.TestCase):
    root: Path
    ball: Path
    setUp = fixtures.OS2SupportTests.setUp
    request = fixtures.OS2SupportTests.request
    media = fixtures.OS2SupportTests.media

    def source(self, entries, name="tree.zip"):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as archive:
            for path, payload in entries.items():
                archive.writestr(path, payload)
        return self.media(name, output.getvalue())

    def test_tree_has_all_groups_exact_sources_and_typed_subgroups(self):
        entries = {
            "OS2/DLL/FACE.FON": b"font",
            "OS2/DLL/FACE.TTF": b"font",
            "OS2/DLL/FACE.OTF": b"font",
            "PSFONTS/FACE.PFB": b"font",
            "PSFONTS/FACE.PFA": b"font",
            "PSFONTS/PFM/FACE.PFM": b"metrics",
            "OS2/DLL/HELPMGR.DLL": fixtures.CMD,
            "OS2/VIEW.EXE": fixtures.CMD,
            "OS2/HELP/TEST.HLP": b"help",
            "OS2/BOOK/TEST.INF": b"reference",
            "OS2/APPS/EPM.EXE": fixtures.CMD,
            "OS2/APPS/EPM.ICO": b"icon",
            "OS2/KEYBOARD.DCP": b"codepage",
            "LANGUAGE/DE/LOCALE.DAT": b"locale",
            "OS2/PSTAT.EXE": fixtures.CMD,
            "OS2/MDOS/WINOS2/TOOL.EXE": fixtures.CMD,
            "OS2/ETC/UNKNOWN.DAT": b"other",
            "OS2/DLL/PMWIN.DLL": b"provided",
        }
        source = self.source(entries)
        plan = self.request("plan-os2-extraction", sources=[source, source])
        tree = plan["tree"]
        self.assertEqual(tree["id"], "all")
        self.assertEqual(len(tree["children"]), 15)
        nodes = list(walk(tree))
        ids = {node["id"] for node in nodes}
        for id in (
            "fonts.bitmap",
            "fonts.truetype",
            "fonts.opentype",
            "fonts.postscript",
            "fonts.metrics",
            "help.engine",
            "help.viewers",
            "help.books",
            "help.references",
            "utilities",
            "languages",
            "diagnostics",
            "legacy.windows",
            "full.other",
        ):
            self.assertIn(id, ids)
        leaves = [node for node in nodes if node["kind"] == "file"]
        files = [file for file in plan["files"] if not file["provided"]]
        self.assertEqual(len(leaves), len(files))
        self.assertEqual(len({node["id"] for node in leaves}), len(leaves))
        self.assertEqual({node["source"] for node in leaves}, {file["source"] for file in files})
        self.assertTrue(all(node["id"] == node["source"] for node in leaves))
        classified = {
            Path(file["path"].replace("\\", "/")).name: file["category"] for file in files
        }
        self.assertEqual(classified["EPM.EXE"], "utilities")
        self.assertEqual(classified["EPM.ICO"], "utilities")
        self.assertEqual(classified["KEYBOARD.DCP"], "languages")
        self.assertEqual(classified["PSTAT.EXE"], "diagnostics")
        self.assertEqual(classified["TOOL.EXE"], "legacy")

    def test_native_api_payloads_are_hidden_but_original_opengl_remains_selectable(self):
        source = self.source(
            {
                f"OS2/DLL/{name}.DLL": fixtures.CMD
                for name in ("PMWIN", "MDM", "MMIO", "DIVE", "OPENGL", "PGL")
            }
        )
        self.request("command", arguments=["config", "system.opengl", "off"])
        plan = self.request("plan-os2-extraction", sources=[source])
        leaves = [node for node in walk(plan["tree"]) if node["kind"] == "file"]
        self.assertEqual({node["title"] for node in leaves}, {"OPENGL.DLL", "PGL.DLL"})
        self.assertTrue(all(not node["disabled"] for node in leaves))
        groups = {node["id"]: node for node in plan["tree"]["children"]}
        self.assertEqual(groups["opengl"]["provider"]["selection"], "off")
        self.assertEqual(groups["opengl"]["provider"]["setting"], "graphics.opengl")
        self.assertEqual(groups["printers"]["provider"]["setting"], "printing")
        self.assertTrue(
            any(
                node["kind"] == "setting" and not node["selectable"]
                for node in walk(groups["multimedia"])
            )
        )
        result = self.request(
            "import-os2-support",
            sources=[source],
            fileSelection=[leaf["source"] for leaf in leaves],
        )
        self.assertEqual(len(result["imported"]), 2)
        self.assertEqual(
            json.loads((self.ball / "ball.json").read_text())["system"]["opengl"], "off"
        )

    def test_provider_policy_disables_original_native_groups_but_keeps_settings_actionable(self):
        source = self.source(
            {
                "OS2/DLL/OPENGL.DLL": fixtures.CMD,
                "OS2/DLL/PGL.DLL": fixtures.CMD,
                "OS2/DLL/PRINTER/OUTPUT.PDR": fixtures.CMD,
                "MMOS2/PLAYER/PLAYER.EXE": fixtures.CMD,
                "OS2/HELP/BOOK.HLP": b"help",
            }
        )
        for choice in (None, "mesa", "original", "off"):
            with self.subTest(choice=choice):
                if choice is not None:
                    self.request("command", arguments=["config", "system.opengl", choice])
                plan = self.request("plan-os2-extraction", sources=[source])
                groups = {group["id"]: group for group in plan["tree"]["children"]}
                native = choice in (None, "mesa")
                for id in ("opengl", "printers", "multimedia"):
                    group = groups[id]
                    disabled = native if id == "opengl" else True
                    self.assertEqual(group["disabled"], disabled)
                    self.assertEqual(group["provider"]["nativeSelected"], disabled)
                    self.assertEqual(group["provider"]["choiceSupported"], id == "opengl")
                    self.assertTrue(group["provider"]["originalAvailable"])
                    for node in walk(group):
                        self.assertEqual(
                            node["disabled"], False if node["kind"] == "setting" else disabled
                        )
                        if node["kind"] == "setting":
                            self.assertFalse(node["selectable"])
                            self.assertTrue(node["setting"])
                        else:
                            self.assertEqual(node["selectable"], not disabled)
                    self.assertEqual(bool(eligible_sources(group)), not disabled)
                selected = eligible_sources(plan["tree"])
                selected_categories = {
                    file["category"] for file in plan["files"] if file["source"] in selected
                }
                self.assertNotIn("printers", selected_categories)
                self.assertNotIn("multimedia", selected_categories)
                self.assertEqual("opengl" in selected_categories, not native)
        # The UI policy does not forbid deliberate original-file staging.
        self.request("command", arguments=["config", "system.opengl", "mesa"])
        plan = self.request("plan-os2-extraction", sources=[source])
        original = [file for file in plan["files"] if file["category"] == "opengl"]
        self.assertTrue(all(not file["provided"] for file in original))
        result = self.request(
            "import-os2-support",
            sources=[source],
            fileSelection=[file["source"] for file in original],
        )
        self.assertEqual(len(result["imported"]), 2)
        self.assertEqual(
            json.loads((self.ball / "ball.json").read_text())["system"]["opengl"], "mesa"
        )

    def test_destination_directory_paths_are_stable_case_insensitive_and_scoped_to_subgroup(self):
        source = self.source(
            {
                "OS2/DLL/FIRST.TTF": b"first",
                "os2/dll/SECOND.TTF": b"second",
                "OS2/DLL/BITMAP.FON": b"bitmap font",
                "OS2/HELP/BOOK.HLP": b"help",
                "OS2/DLL/PRINTER/OUTPUT.PDR": fixtures.CMD,
            }
        )
        plan = self.request("plan-os2-extraction", sources=[source])
        nodes = {node["id"]: node for node in walk(plan["tree"])}
        truetype = nodes["fonts.truetype"]
        drive = next(node for node in truetype["children"] if node["kind"] == "directory")
        self.assertEqual(drive["title"], "C:\\")
        self.assertEqual(drive["path"], "C:\\")
        self.assertEqual(len(drive["children"]), 1)
        os2 = drive["children"][0]
        dll = os2["children"][0]
        self.assertEqual(os2["path"], "C:\\OS2")
        self.assertEqual(dll["path"], "C:\\OS2\\DLL")
        self.assertEqual({node["title"] for node in dll["children"]}, {"FIRST.TTF", "SECOND.TTF"})
        bitmap_drive = next(
            node for node in nodes["fonts.bitmap"]["children"] if node["kind"] == "directory"
        )
        self.assertNotEqual(bitmap_drive["id"], drive["id"])
        self.assertEqual(bitmap_drive["path"], drive["path"])
        for node in nodes.values():
            if node["kind"] == "directory":
                self.assertEqual(
                    node["category"],
                    "printers"
                    if node["disabled"]
                    else "fonts"
                    if node["id"].startswith("fonts.")
                    else "help",
                )
                self.assertNotIn("requires", node)
        leaves = [node for node in nodes.values() if node["kind"] == "file"]
        self.assertEqual(len(leaves), len(plan["files"]))
        self.assertEqual(
            {node["source"] for node in leaves}, {file["source"] for file in plan["files"]}
        )
        again = self.request("plan-os2-extraction", sources=[source])
        self.assertEqual(
            {node["id"] for node in walk(again["tree"]) if node["kind"] == "directory"},
            {node["id"] for node in nodes.values() if node["kind"] == "directory"},
        )

    def test_exact_file_selection_overrides_categories_requires_and_rejects_injection(self):
        source = self.source(
            {
                "OS2/DLL/SOMD.DLL": fixtures.CMD,
                "OS2/DLL/LIBUNI.DLL": fixtures.CMD,
                "OS2/DLL/PMWIN.DLL": fixtures.CMD,
                "OS2/HELP/TEST.HLP": b"help",
            }
        )
        plan = self.request("plan-os2-extraction", sources=[source])
        som = next(file for file in plan["files"] if file["path"].endswith("SOMD.DLL"))
        result = self.request(
            "import-os2-support",
            sources=[source],
            categories=["som", "full"],
            fileSelection=[som["source"]],
        )
        self.assertEqual([file["path"] for file in result["imported"]], ["C:\\OS2\\DLL\\SOMD.DLL"])
        self.assertFalse((self.ball / "drive_c/OS2/DLL/LIBUNI.DLL").exists())
        self.assertFalse(result["automaticRequirementsApplied"])
        self.assertFalse(result["dependenciesChecked"])
        self.assertEqual(result["configuredComponents"], [])
        self.assertNotIn("startupPrograms", json.loads((self.ball / "ball.json").read_text()))
        with self.assertRaisesRegex(ValueError, "not in the current media plan"):
            self.request("import-os2-support", sources=[source], fileSelection=["/arbitrary/file"])
        empty = self.request(
            "import-os2-support", sources=[source], categories=["full"], fileSelection=[]
        )
        self.assertEqual(empty["imported"], [])
        self.assertEqual(empty["selectedCategories"], [])

    def test_explicit_alternate_install_and_full_selection_preserve_both_versions(self):
        source = self.source({"FACE.PFB": b"alternate", "PSFONTS/FACE.PFB": b"preferred"})
        plan = self.request("plan-os2-extraction", sources=[source])
        files = plan["files"]
        preferred = next(file for file in files if file["preferred"])
        alternate = next(file for file in files if not file["preferred"])
        self.request("import-os2-support", sources=[source], fileSelection=[alternate["source"]])
        target = self.ball / "drive_c/PSFONTS/FACE.PFB"
        self.assertEqual(target.read_bytes(), b"alternate")
        target.unlink()
        result = self.request(
            "import-os2-support", sources=[source], fileSelection=[file["source"] for file in files]
        )
        self.assertEqual(target.read_bytes(), b"preferred")
        self.assertEqual(len(result["preservedVariants"]), 1)
        self.assertIn(preferred["source"], [file["source"] for file in files])

    def test_whole_som_configuration_is_explicit_and_requires_every_som_leaf(self):
        companions = (
            "SOMDD.EXE",
            "SOMDSVR.EXE",
            "SOM.IR",
            "SOMDIMPL.DAT",
            "SOMDIMPL.TOC",
            "SOMDCLS.DAT",
            "SOMDCLS.TOC",
            "SOMD.MSG",
            "SOMK.MSG",
            "SOME_OS2.MSG",
        )
        entries = {"OS2/DLL/SOMD.DLL": fixtures.CMD, "OS2/DLL/LIBUNI.DLL": fixtures.CMD}
        for name in companions:
            folder = (
                "OS2/ETC"
                if name == "SOM.IR"
                else "OS2/ETC/DSOM"
                if name.startswith(("SOMDIMPL", "SOMDCLS"))
                else "OS2/SYSTEM"
                if name.endswith(".MSG")
                else "OS2"
            )
            entries[f"{folder}/{name}"] = fixtures.CMD if name.endswith(".EXE") else b"original"
        source = self.source(entries)
        plan = self.request("plan-os2-extraction", sources=[source])
        chosen = [file["source"] for file in plan["files"] if file["category"] == "som"]
        with self.assertRaisesRegex(ValueError, "Select every SOM file"):
            self.request(
                "import-os2-support",
                sources=[source],
                fileSelection=chosen[:-1],
                configureComponents=["som"],
            )
        result = self.request(
            "import-os2-support",
            sources=[source],
            fileSelection=chosen,
            configureComponents=["som"],
        )
        self.assertEqual(result["configuredComponents"], ["som"])
        self.assertFalse((self.ball / "drive_c/OS2/DLL/LIBUNI.DLL").exists())
        self.assertEqual(
            json.loads((self.ball / "ball.json").read_text())["startupPrograms"][0]["owner"],
            "os2-som",
        )

    def test_iso_normalization_includes_verified_siblings_but_direct_folder_plan_stays_scoped(self):
        image = hierarchy_iso(
            {
                "OS2IMAGE/OS2/CMD.EXE": fixtures.CMD,
                "OS2IMAGE/OS2/SYSLEVEL.OS2": fixtures.LEVEL,
                "CID/SERVER/MPTS/MPTN/BIN/PING.EXE": fixtures.CMD,
                "CID/SERVER/MPTS/IBMCOM/DLL/PROTOCOL.DLL": fixtures.CMD,
                "CID/SERVER/BOOKS/NETWORK.INF": b"reference",
                "CID/SERVER/IBMLS/SETUP.EXE": fixtures.CMD,
            }
        )
        source = self.media("server.zip", fixtures.zip_bytes("OS2.ISO", image))
        opened = self.request("open-os2-source", sources=[source])
        self.assertEqual(len(opened["sources"]), 4)
        base = opened["sources"][0]
        normalized_again = self.request("open-os2-source", sources=[base])
        self.assertEqual(normalized_again["sources"], opened["sources"])
        scoped = self.request("plan-os2-extraction", sources=[base])
        self.assertFalse(any(file["category"] == "network" for file in scoped["files"]))
        complete = self.request("plan-os2-extraction", sources=opened["sources"])
        paths = {file["path"] for file in complete["files"]}
        self.assertIn("C:\\MPTN\\BIN\\PING.EXE", paths)
        self.assertIn("C:\\IBMCOM\\DLL\\PROTOCOL.DLL", paths)
        self.assertIn("C:\\OS2\\BOOK\\NETWORK.INF", paths)
        self.assertFalse(any(path.endswith("SETUP.EXE") for path in paths))

    def test_kernel_driver_classes_are_hidden_without_hiding_country_data_or_printers(self):
        entries = {
            "OS2/BOOT/STORAGE.ADD": fixtures.CMD,
            "OS2/BOOT/VOLUME.IFS": fixtures.CMD,
            "OS2/BOOT/FILTER.FLT": fixtures.CMD,
            "OS2/BOOT/DISK.DMD": fixtures.CMD,
            "OS2/BOOT/KBD.SYS": fixtures.CMD,
            "OS2/MDOS/VDPMI.SYS": fixtures.CMD,
            "MMOS2/SOUND.SYS": fixtures.CMD,
            "IBMCOM/MACS/ADAPTER.OS2": fixtures.CMD,
            "OS2/COUNTRY.SYS": b"country data",
            "OS2/BOOT/KEYBOARD.DCP": b"code page",
            "OS2/APPS/DATA.SYS": b"application data",
            "OS2/DLL/PRINTER/OUTPUT.PDR": fixtures.CMD,
            "OS2/DLL/PRINTER/OUTPUT.DRV": fixtures.CMD,
        }
        source = self.source(entries)
        plan = self.request("plan-os2-extraction", sources=[source])
        hidden = [file for file in plan["files"] if file["provided"]]
        self.assertEqual(len(hidden), 8)
        self.assertTrue(all("kernel driver" in file["providedReason"] for file in hidden))
        leaves = {node["title"] for node in walk(plan["tree"]) if node["kind"] == "file"}
        for name in ("COUNTRY.SYS", "KEYBOARD.DCP", "DATA.SYS", "OUTPUT.PDR", "OUTPUT.DRV"):
            self.assertIn(name, leaves)

    def test_media_validation_is_read_only_and_returns_nested_physical_sources(self):
        payload = self.source({"OS2/CMD.EXE": fixtures.CMD, "OS2/SYSLEVEL.OS2": fixtures.LEVEL})
        image = fixtures.iso_image(payload=Path(payload).read_bytes(), filename="FILES.ZIP")
        source = self.media("nested-media.zip", fixtures.zip_bytes("OS2.iso", image))
        remembered = self.ball / "os2-media.json"
        remembered.write_text('{"sources":["previous"],"selectedCategories":["help"]}\n')
        before = {
            str(path.relative_to(self.ball)): path.read_bytes()
            for path in self.ball.rglob("*")
            if path.is_file()
        }
        result = self.request("validate-os2-media", sources=[source])
        self.assertTrue(result["complete"])
        self.assertTrue(result["sources"][0].startswith("/@2ine-media/"))
        self.assertEqual(result["physicalSources"], [source])
        self.assertEqual(result["media"][0]["status"], "verified-os2-media")
        self.assertEqual(result["media"][0]["coreIndexCache"], "disabled")
        self.assertTrue(result["media"][0]["commandSignature"])
        self.assertTrue(result["media"][0]["versionSignature"])
        self.assertEqual(result["errors"], [])
        after = {
            str(path.relative_to(self.ball)): path.read_bytes()
            for path in self.ball.rglob("*")
            if path.is_file()
        }
        self.assertEqual(after, before)

    def test_media_validation_rejects_application_iso_alone_or_with_valid_os2_media(self):
        valid = self.source(
            {"OS2/CMD.EXE": fixtures.CMD, "OS2/SYSLEVEL.OS2": fixtures.LEVEL}, "os2.zip"
        )
        application = self.media("Lotus.iso", hierarchy_iso({"LOTUS/INSTALL.EXE": fixtures.CMD}))
        before = {
            str(path.relative_to(self.ball)): path.read_bytes()
            for path in self.ball.rglob("*")
            if path.is_file()
        }
        for sources in ([application], [valid, application]):
            with self.subTest(sources=sources):
                result = self.request("validate-os2-media", sources=sources)
                self.assertFalse(result["complete"])
                wrong = next(
                    item
                    for item in result["media"]
                    if not item.get("commandSignature") and not item.get("versionSignature")
                )
                self.assertEqual(wrong["status"], "unverified")
                self.assertEqual(result["sources"], [] if len(sources) == 1 else [valid])
                self.assertFalse((self.ball / "os2-media.json").exists())
        after = {
            str(path.relative_to(self.ball)): path.read_bytes()
            for path in self.ball.rglob("*")
            if path.is_file()
        }
        self.assertEqual(after, before)

    def test_media_validation_accepts_signature_split_set_and_normalized_siblings(self):
        command = self.source({"OS2/CMD.EXE": fixtures.CMD}, "disk1.zip")
        level = self.source({"OS2/SYSLEVEL.OS2": fixtures.LEVEL}, "disk2.zip")
        checked = self.request("validate-os2-media", sources=[command, level])
        self.assertTrue(checked["complete"])
        self.assertEqual({item["status"] for item in checked["media"]}, {"verified-os2-set"})
        image = hierarchy_iso(
            {
                "OS2IMAGE/OS2/CMD.EXE": fixtures.CMD,
                "OS2IMAGE/OS2/SYSLEVEL.OS2": fixtures.LEVEL,
                "CID/SERVER/MPTS/MPTN/BIN/PING.EXE": fixtures.CMD,
            }
        )
        iso = self.media("server.iso", image)
        checked = self.request("validate-os2-media", sources=[iso])
        self.assertTrue(checked["complete"])
        self.assertEqual(len(checked["sources"]), 2)
        self.assertEqual(
            {item["status"] for item in checked["media"]},
            {"verified-os2-media", "verified-os2-set"},
        )
        self.assertEqual(checked["physicalSources"], [iso])
        self.assertFalse((self.ball / "os2-media.json").exists())


if __name__ == "__main__":
    unittest.main()
