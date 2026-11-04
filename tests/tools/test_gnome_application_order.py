# Public domain. No rights reserved. Wholly AI-authored.
"""Deterministic launcher ordering while real icons resolve asynchronously."""

import unittest

from tools import gnome_application_order as order


class ApplicationOrderTests(unittest.TestCase):
    def rows(self, mode="icons", available=()):
        entries = {"Zulu": "A.EXE", "Café": "C.EXE", "Alpha License": "Z.EXE"}
        return sorted(
            entries, key=lambda name: order.key(name, entries[name], name in available, mode)
        )

    def test_icons_arriving_late_move_only_the_real_icon_entries_first(self):
        self.assertEqual(self.rows(), ["Alpha License", "Café", "Zulu"])
        self.assertEqual(self.rows(available={"Zulu"}), ["Zulu", "Alpha License", "Café"])
        self.assertEqual(self.rows(available={"Café", "Zulu"}), ["Café", "Zulu", "Alpha License"])

    def test_explicit_name_or_program_order_survives_late_icon_results(self):
        for mode in ("name", "program"):
            self.assertEqual(self.rows(mode), self.rows(mode, {"Zulu", "Café"}))
        self.assertEqual(self.rows("program"), ["Zulu", "Café", "Alpha License"])

    def test_unknown_saved_mode_uses_icon_first_and_duplicate_titles_are_stable(self):
        self.assertEqual(order.mode("unknown"), "icons")
        self.assertEqual(order.mode(None), "icons")
        self.assertLess(order.key("App", "a", True), order.key("app", "b", True))


if __name__ == "__main__":
    unittest.main()
