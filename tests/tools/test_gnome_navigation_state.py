# Public domain. No rights reserved. Wholly AI-authored.
"""Core navigation facts select the initial view, never a metadata refresh view."""

import unittest

from apps.gnome.twoine_gnome.navigation_state import PAGES, initial_page


class NavigationStateTests(unittest.TestCase):
    def test_requested_visible_order_keeps_installation_next_to_applications(self):
        self.assertEqual(
            PAGES,
            ("drives", "components", "application-installation", "apps", "settings", "activity"),
        )

    def test_new_ball_starts_in_drives_regardless_of_fact_state(self):
        self.assertEqual(initial_page({"page": "applications"}, newly_created=True), "drives")

    def test_existing_ball_uses_verified_core_readiness(self):
        for source, page in (
            ("system-components", "components"),
            ("application-installation", "application-installation"),
            ("applications", "apps"),
        ):
            self.assertEqual(initial_page({"page": source}), page)
        self.assertEqual(initial_page({}), "components")


if __name__ == "__main__":
    unittest.main()
