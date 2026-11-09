# Public domain. No rights reserved. Wholly AI-authored.
"""Installer priority and manual edits across asynchronous media changes."""

import unittest

from apps.gnome.twoine_gnome.program_choice import (
    ProgramChoice,
    candidate_rank,
    installer_candidates,
)


class ProgramChoiceTests(unittest.TestCase):
    def test_root_installer_wins_over_nested_even_with_less_preferred_extension(self):
        candidates = [
            (r"D:\ENGLISH\INSTALL.EXE", 1),
            (r"D:\INSTALL.CMD", 0),
            (r"D:\SETUP.EXE", 0),
            (r"D:\OTHER.EXE", 0),
        ]
        ranked = sorted(
            (candidate_rank(path, depth), path)
            for path, depth in candidates
            if candidate_rank(path, depth) is not None
        )
        self.assertEqual(
            [path for _, path in ranked],
            [r"D:\INSTALL.CMD", r"D:\SETUP.EXE", r"D:\OTHER.EXE", r"D:\ENGLISH\INSTALL.EXE"],
        )

    def test_exact_depth_name_extension_then_language_priority(self):
        self.assertLess(
            candidate_rank(r"D:\SETUP.CMD", 0), candidate_rank(r"D:\ENGLISH\INSTALL.EXE", 1)
        )
        self.assertLess(
            candidate_rank(r"D:\OTHER\INSTALL.EXE", 1), candidate_rank(r"D:\ENGLISH\INSTALL.CMD", 1)
        )
        self.assertLess(
            candidate_rank(r"D:\ENGLISH\SETUP.EXE", 1), candidate_rank(r"D:\OTHER\SETUP.EXE", 1)
        )

    def test_unreadable_nested_folder_keeps_a_valid_root_suggestion(self):
        def browse(_action, path):
            if path == "D:\\":
                return {
                    "entries": [
                        {"name": "INSTALL.CMD", "path": r"D:\INSTALL.CMD", "directory": False},
                        {"name": "ENGLISH", "path": r"D:\ENGLISH", "directory": True},
                    ]
                }
            raise ValueError("Cannot read folder")

        self.assertEqual(installer_candidates(browse, "D"), [r"D:\INSTALL.CMD"])

    def test_auto_choice_clears_on_eject_and_uses_new_media_default(self):
        choice = ProgramChoice()
        self.assertEqual(choice.update([r"D:\INSTALL.CMD", r"d:\install.cmd"]), r"D:\INSTALL.CMD")
        self.assertEqual(len(choice.candidates), 1)
        self.assertEqual(choice.update([]), "")
        self.assertEqual(choice.update([r"E:\SETUP.EXE"]), r"E:\SETUP.EXE")

    def test_typing_or_browsing_keeps_manual_choice_when_scan_finishes_or_media_changes(self):
        choice = ProgramChoice()
        choice.update([r"D:\INSTALL.EXE"])
        choice.edit(r"C:\CUSTOM\MYSETUP.CMD")
        self.assertEqual(choice.update([r"E:\SETUP.EXE"]), r"C:\CUSTOM\MYSETUP.CMD")
        self.assertEqual(choice.update([]), r"C:\CUSTOM\MYSETUP.CMD")
        explicit = ProgramChoice(r"C:\CHOSEN.EXE")
        self.assertEqual(explicit.update([r"D:\INSTALL.EXE"]), r"C:\CHOSEN.EXE")


if __name__ == "__main__":
    unittest.main()
