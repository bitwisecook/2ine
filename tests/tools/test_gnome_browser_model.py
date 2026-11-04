# Public domain. No rights reserved. Wholly AI-authored.
"""Shared GTK browser search and selection semantics without a display server."""

import ast
import unittest
from pathlib import Path

from tools.gnome_browser_model import CheckedTree, folded, matches, visible_tree


class BrowserModelTests(unittest.TestCase):
    def nodes(self):
        return {
            "id": "all",
            "title": "All Files",
            "kind": "group",
            "children": [
                {
                    "id": "som",
                    "title": "Object Runtime",
                    "kind": "group",
                    "requires": ["runtime"],
                    "children": [
                        {"id": "a", "title": "SOM.DLL", "kind": "file", "source": "a"},
                        {"id": "b", "title": "SOMD.DLL", "kind": "file", "source": "b"},
                        {
                            "id": "host",
                            "title": "Host API",
                            "kind": "file",
                            "source": "host",
                            "disabled": True,
                        },
                    ],
                },
                {
                    "id": "runtime",
                    "title": "C Runtime",
                    "kind": "group",
                    "children": [{"id": "c", "title": "LIBUNI.DLL", "kind": "file", "source": "c"}],
                },
                {"id": "settings", "title": "Provider", "kind": "setting"},
            ],
        }

    def checks(self, selected=()):
        return CheckedTree(
            [self.nodes()],
            selected,
            value=lambda data: (
                data["source"] if data["kind"] == "file" and not data.get("disabled") else None
            ),
        )

    def test_fuzzy_search_is_accent_insensitive_and_ordered(self):
        self.assertEqual(folded("CAFÉ"), "cafe")
        self.assertTrue(matches("Café Report.txt", "cf rpt"))
        self.assertTrue(matches("report", ""))
        self.assertFalse(matches("Café Report.txt", "trfe"))

    def test_filtered_flat_browse_keeps_the_adapter_default_filter(self):
        rows = [
            {"name": "Programs", "directory": True},
            {"name": "REPORT.EXE", "directory": False},
            {"name": "REPORT.TXT", "directory": False},
        ]

        def accepted(row):
            return row["directory"] or row["name"].endswith(".EXE")

        visible = visible_tree(
            rows, "", text=lambda row: row["name"], children=lambda _row: [], accept=accepted
        )
        self.assertEqual([node.data["name"] for node in visible], ["Programs", "REPORT.EXE"])
        found = visible_tree(
            rows, "rpt", text=lambda row: row["name"], children=lambda _row: [], accept=accepted
        )
        self.assertEqual([node.data["name"] for node in found], ["REPORT.EXE"])

    def test_tree_search_keeps_ancestors_and_original_row_identity(self):
        root = self.nodes()
        found = visible_tree(
            [root],
            "SOMD.DLL",
            text=lambda node: node["title"],
            children=lambda node: node.get("children", []),
        )
        self.assertIs(found[0].data, root)
        self.assertEqual([node.data["id"] for node in found[0].children], ["som"])
        self.assertEqual([node.data["id"] for node in found[0].children[0].children], ["b"])
        self.assertIs(found[0].children[0].children[0].data, root["children"][0]["children"][1])
        self.assertEqual(len(root["children"][0]["children"]), 3)
        self.assertEqual(
            visible_tree(
                [root],
                "absent",
                text=lambda node: node["title"],
                children=lambda node: node.get("children", []),
            ),
            [],
        )

    def test_matching_group_retains_its_descendants_without_changing_selection(self):
        checks = self.checks(["a"])
        root = checks.nodes["all"]
        found = visible_tree(
            [root],
            "Object Runtime",
            text=lambda node: node["title"],
            children=lambda node: node.get("children", []),
        )
        self.assertEqual(len(found[0].children[0].children), 3)
        self.assertEqual(checks.selected, {"a"})
        self.assertTrue(checks.state("som").mixed)
        self.assertFalse(checks.state("som").active)

    def test_parent_toggle_adds_requirements_but_leaf_omissions_remain_explicit(self):
        checks = self.checks(["arbitrary", "host"])
        self.assertEqual(checks.selected, set())
        checks.toggle("som")
        self.assertEqual(checks.selected, {"a", "b", "c"})
        self.assertTrue(checks.complete("som"))
        checks.toggle("b")
        self.assertEqual(checks.selected, {"a", "c"})
        self.assertFalse(checks.complete("som"))
        self.assertTrue(checks.state("som").mixed)
        self.assertFalse(checks.state("host").enabled)
        self.assertFalse(checks.state("settings").visible)
        checks.toggle("a")
        self.assertEqual(checks.selected, {"c"})
        self.assertFalse(checks.state("som").mixed)

    def test_cyclic_group_requirements_terminate_and_whole_root_can_clear(self):
        root = self.nodes()
        root["children"][1]["requires"] = ["som"]
        checks = CheckedTree(
            [root], [], value=lambda node: node.get("source") if not node.get("disabled") else None
        )
        checks.toggle("som")
        self.assertEqual(checks.selected, {"a", "b", "c"})
        checks.toggle("all")
        self.assertEqual(checks.selected, set())

    def test_packaging_registers_both_shared_browser_modules(self):
        source = Path(__file__).resolve().parents[2] / "tools/build_gnome_packages.py"
        tree = ast.parse(source.read_text())
        staged = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.For) and isinstance(node.iter, ast.Tuple):
                names = {
                    value.value
                    for value in node.iter.elts
                    if isinstance(value, ast.Constant) and isinstance(value.value, str)
                }
                if "gnome_files.py" in names:
                    staged.update(names)
        self.assertTrue({"gnome_browser.py", "gnome_browser_model.py"} <= staged)


if __name__ == "__main__":
    unittest.main()
