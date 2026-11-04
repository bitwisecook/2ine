# Public domain. No rights reserved. Wholly AI-authored.
"""Folder disclosure caches only explicit reads and never guesses descendants."""

import unittest

from tools.gnome_lazy_folders import FolderChildren


class FolderChildrenTests(unittest.TestCase):
    def test_unknown_folder_offers_disclosure_without_reading_or_eager_recursion(self):
        folders = FolderChildren()
        folder = {"name": "OS2", "path": r"C:\OS2", "directory": True}
        children = folders.children(folder)
        self.assertTrue(children[0]["placeholder"])
        self.assertFalse(folders.pending)
        self.assertEqual(folders.children(children[0]), [])
        self.assertEqual(folders.children({**folder, "directory": False}), [])

    def test_only_explicit_expansion_reads_once_and_retains_real_child_identity(self):
        folders = FolderChildren()
        self.assertTrue(folders.begin(r"C:\OS2"))
        self.assertFalse(folders.begin(r"c:\os2"))
        child = {"name": "DLL", "path": r"C:\OS2\DLL", "directory": True}
        folders.complete(r"C:\OS2", [child])
        self.assertIs(folders.children({"path": r"c:\os2", "directory": True})[0], child)
        self.assertFalse(folders.begin(r"C:\OS2"))
        self.assertFalse(folders.pending)
        self.assertTrue(folders.children(child)[0]["placeholder"])

    def test_failure_displays_inline_and_retry_requires_another_expansion(self):
        folders = FolderChildren()
        folders.begin(r"D:\MEDIA")
        folders.fail(r"D:\MEDIA", "No medium")
        folder = {"path": r"D:\MEDIA", "directory": True}
        self.assertTrue(folders.children(folder)[0]["error"])
        self.assertFalse(folders.pending)
        self.assertTrue(folders.begin(r"D:\MEDIA"))
        folders.complete(r"D:\MEDIA", [])
        self.assertEqual(folders.children(folder), [])


if __name__ == "__main__":
    unittest.main()
