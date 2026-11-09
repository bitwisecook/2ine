# Public domain. No rights reserved. Wholly AI-authored.
"""Live component jobs and conservative ownership-based removal, private fixtures only."""

import json
import os
import threading
import time
import unittest
from pathlib import Path

from tests.environment import test_os2_dependencies as dependency_fixtures
from tests.environment import test_os2_support as fixtures
from tests.environment import test_os2_tree as tree_fixtures
from tools.ball import engine as ball_api


class ComponentOperationTests(unittest.TestCase):
    root: Path
    ball: Path
    setUp = fixtures.OS2SupportTests.setUp
    request = fixtures.OS2SupportTests.request
    media = fixtures.OS2SupportTests.media
    source = tree_fixtures.OS2TreeTests.source

    def install(self, entries, operation="install-fixture"):
        source = self.source(entries)
        result = self.request(
            "import-os2-support", sources=[source], categories=["full"], operationID=operation
        )
        return source, result

    def state(self, *paths, operation=None):
        fields = {"paths": list(paths)}
        if operation is not None:
            fields["operationID"] = operation
        return self.request("os2-component-state", **fields)

    def test_new_files_owned_preexisting_protected_and_only_unchanged_removed(self):
        existing = self.ball / "drive_c/OS2/EXIST.TXT"
        existing.parent.mkdir(parents=True, exist_ok=True)
        existing.write_bytes(b"preexisting")
        _, result = self.install(
            {"OS2/NEW.TXT": b"new", "OS2/CHANGED.TXT": b"original", "OS2/EXIST.TXT": b"replacement"}
        )
        self.assertEqual(result["progress"]["status"], "complete")
        self.assertEqual(result["progress"]["completedCount"], 2)
        (self.ball / "drive_c/OS2/CHANGED.TXT").write_bytes(b"user edit")
        paths = [r"C:\OS2\NEW.TXT", r"C:\OS2\CHANGED.TXT", r"C:\OS2\EXIST.TXT"]
        state = self.state(*paths)
        self.assertEqual(
            [file["status"] for file in state["files"]], ["managed", "modified", "unowned"]
        )
        removed = self.request("remove-os2-components", paths=paths, operationID="remove-fixture")
        self.assertEqual(removed["removedPaths"], paths[:1])
        self.assertEqual(len(removed["preserved"]), 2)
        self.assertFalse((self.ball / "drive_c/OS2/NEW.TXT").exists())
        self.assertEqual(existing.read_bytes(), b"preexisting")
        self.assertEqual((self.ball / "drive_c/OS2/CHANGED.TXT").read_bytes(), b"user edit")
        self.assertEqual(self.state(paths[0])["files"][0]["status"], "missing")
        self.assertTrue(Path(removed["progress"]["logPath"]).is_file())

    def test_cancel_before_start_and_unknown_progress_are_safe(self):
        self.assertIsNone(self.state(operation="not-started")["progress"])
        cancelled = self.request("cancel-os2-component-operation", operationID="cancel-first")
        self.assertTrue(cancelled["cancelRequested"])
        self.assertIsNone(cancelled["progress"])
        source = self.source({"OS2/NEW.TXT": b"new"})
        with self.assertRaisesRegex(ValueError, "cancelled"):
            self.request(
                "import-os2-support",
                sources=[source],
                categories=["full"],
                operationID="cancel-first",
            )
        self.assertEqual(self.state(operation="cancel-first")["progress"]["status"], "cancelled")
        self.assertFalse((self.ball / "drive_c/OS2/NEW.TXT").exists())
        for token in ["../escape", "", "x" * 65, "a/b"]:
            with self.assertRaisesRegex(ValueError, "Invalid component operation ID"):
                self.state(operation=token)

    def test_private_paths_links_and_cache_cannot_be_removed(self):
        self.install({"OS2/NEW.TXT": b"new"})
        outside = self.root / "outside.txt"
        outside.write_bytes(b"external")
        linked = self.ball / "drive_c/LINK.TXT"
        linked.symlink_to(outside)
        paths = [
            r"C:\LINK.TXT",
            r"C:\..\outside.txt",
            r"D:\outside.txt",
            r"C:\OS2\DLL\2INE\cached.dll",
        ]
        result = self.request("remove-os2-components", paths=paths, operationID="protected")
        self.assertEqual(result["removedPaths"], [])
        self.assertTrue(all(file["status"] == "protected" for file in result["preserved"]))
        self.assertTrue(linked.is_symlink())
        self.assertEqual(outside.read_bytes(), b"external")

    def test_modified_metadata_same_content_still_owned(self):
        self.install({"OS2/NEW.TXT": b"new"})
        file = self.ball / "drive_c/OS2/NEW.TXT"
        os.utime(file, (100, 100))
        self.assertEqual(self.state(r"C:\OS2\NEW.TXT")["managedPaths"], [r"C:\OS2\NEW.TXT"])
        self.request("remove-os2-components", paths=[r"C:\OS2\NEW.TXT"], operationID="same-content")
        self.assertFalse(file.exists())

    def test_partial_commit_failure_keeps_ownership_and_final_failure_log(self):
        blocked = self.ball / "drive_c/OS2/Z.TXT"
        blocked.mkdir(parents=True)
        source = self.source(
            {"OS2/A.TXT": b"committed first", "OS2/Z.TXT": b"cannot replace directory"}
        )
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.request(
                "import-os2-support", sources=[source], categories=["full"], operationID="partial"
            )
        state = self.state(r"C:\OS2\A.TXT", operation="partial")
        self.assertEqual(state["managedPaths"], [r"C:\OS2\A.TXT"])
        self.assertEqual(state["progress"]["completedPaths"], [r"C:\OS2\A.TXT"])
        self.assertEqual(state["progress"]["status"], "failed")
        self.assertTrue(blocked.is_dir())
        self.assertEqual(
            json.loads(Path(state["progress"]["logPath"]).read_text())["completedCount"], 1
        )
        self.request("remove-os2-components", paths=[r"C:\OS2\A.TXT"], operationID="remove-partial")
        self.assertFalse((self.ball / "drive_c/OS2/A.TXT").exists())

    def test_service_cleanup_only_removes_managed_record_for_essential_file(self):
        self.install(
            {
                "OS2/ETC/SOM.IR": b"repository",
                "OS2/SYSTEM/SOMD.MSG": b"messages",
                "OS2/DLL/OREXXSOM.DLL": b"optional integration",
            }
        )
        manifest = ball_api.load(self.ball)
        manual = {"program": r"C:\OS2\SOMDD.EXE", "cwd": r"C:\OS2", "arguments": ["manual"]}
        managed = {
            "owner": "os2-som",
            "program": r"C:\OS2\SOMDD.EXE",
            "cwd": r"C:\OS2",
            "arguments": [],
        }
        manifest["startupPrograms"] = [manual, managed]
        ball_api.save(self.ball / "ball.json", manifest)
        self.request(
            "remove-os2-components", paths=[r"C:\OS2\SYSTEM\SOMD.MSG"], operationID="remove-message"
        )
        self.assertEqual(ball_api.load(self.ball)["startupPrograms"], [manual, managed])
        self.request(
            "remove-os2-components",
            paths=[r"C:\OS2\DLL\OREXXSOM.DLL"],
            operationID="remove-optional",
        )
        self.assertEqual(ball_api.load(self.ball)["startupPrograms"], [manual, managed])
        self.request(
            "remove-os2-components", paths=[r"C:\OS2\ETC\SOM.IR"], operationID="remove-repo"
        )
        self.assertEqual(ball_api.load(self.ball)["startupPrograms"], [manual])

    def test_read_only_state_and_complete_journal_rows_survive_interrupted_job(self):
        self.install({"OS2/A.TXT": b"owned"})
        ledger = self.ball / "os2-components.json"
        entries = json.loads(ledger.read_text())
        journal = self.ball / "logs/os2-components/ownership.jsonl"
        journal.write_text(json.dumps(entries[r"C:\OS2\A.TXT"]) + "\n" + '{"path":')
        ledger.write_text("{}")
        before = {str(path): path.read_bytes() for path in self.ball.rglob("*") if path.is_file()}
        self.assertEqual(self.state(r"C:\OS2\A.TXT")["files"][0]["status"], "managed")
        after = {str(path): path.read_bytes() for path in self.ball.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        # A new job folds complete rows and discards an interrupted trailing row.
        self.request("remove-os2-components", paths=[r"C:\OS2\A.TXT"], operationID="resume-journal")
        self.assertEqual(self.state(r"C:\OS2\A.TXT")["files"][0]["status"], "missing")
        self.assertEqual(journal.read_bytes(), b"")

    def test_exact_sources_remove_preserved_variants_without_guessing_destination(self):
        sources = [
            self.source({"OS2/ALT.TXT": content}, name=f"v{i}.zip")
            for i, content in enumerate([b"one", b"two"])
        ]
        plan = self.request("plan-os2-extraction", sources=sources)
        selection = [file["source"] for file in plan["files"]]
        installed = self.request(
            "import-os2-support", sources=sources, fileSelection=selection, operationID="variants"
        )
        actual = {file["path"] for file in installed["imported"]}
        self.assertEqual(len(actual), 2)
        state = self.request(
            "os2-component-state",
            selection=[{"source": file["source"], "path": file["path"]} for file in plan["files"]],
        )
        self.assertEqual({file["path"] for file in state["files"]}, actual)
        self.assertEqual({file["selectedSource"] for file in state["files"]}, set(selection))
        self.assertTrue(
            all(file["status"] == "managed" and file["present"] for file in state["files"])
        )
        self.assertTrue(all(file["plannedPath"] == r"C:\OS2\ALT.TXT" for file in state["files"]))
        removed = self.request(
            "remove-os2-components",
            sources=sources,
            fileSelection=selection,
            operationID="remove-variants",
        )
        self.assertEqual(set(removed["removedPaths"]), actual)

    def test_uninstalled_alternate_cannot_remove_another_sources_owned_file(self):
        sources = [
            self.source({"OS2/ALT.TXT": value}, name=f"alt{i}.zip")
            for i, value in enumerate([b"installed main", b"uninstalled alternate"])
        ]
        plan = self.request("plan-os2-extraction", sources=sources)
        selected = [file["source"] for file in plan["files"]]
        self.request(
            "import-os2-support",
            sources=sources,
            fileSelection=selected[:1],
            operationID="main-only",
        )
        state = self.request(
            "os2-component-state",
            paths=[r"C:\OS2\ALT.TXT"],
            selection=[{"source": source, "path": r"C:\OS2\ALT.TXT"} for source in selected],
        )
        self.assertTrue(state["files"][0]["present"])  # Old paths-only behavior is retained.
        by_source = {
            file["selectedSource"]: file for file in state["files"] if "selectedSource" in file
        }
        self.assertEqual(by_source[selected[0]]["status"], "managed")
        self.assertEqual(by_source[selected[1]]["status"], "missing")
        self.assertFalse(by_source[selected[1]]["present"])
        self.assertFalse(by_source[selected[1]]["managed"])
        result = self.request(
            "remove-os2-components",
            sources=sources,
            fileSelection=selected[1:],
            operationID="other-source",
        )
        self.assertEqual(result["removedPaths"], [])
        self.assertEqual(result["preserved"][0]["status"], "protected")
        self.assertEqual(result["preserved"][0]["selectedSource"], selected[1])
        self.assertIn("does not own", result["preserved"][0]["reason"])
        self.assertEqual(self.state(r"C:\OS2\ALT.TXT")["files"][0]["status"], "managed")
        self.assertTrue((self.ball / "drive_c/OS2/ALT.TXT").is_file())

    def test_source_state_is_read_only_and_does_not_require_media(self):
        source, _ = self.install({"OS2/KNOWN.TXT": b"owned"})
        plan = self.request("plan-os2-extraction", sources=[source])
        selection = [{"source": file["source"], "path": file["path"]} for file in plan["files"]]
        Path(source).unlink()
        before = {str(path): path.read_bytes() for path in self.ball.rglob("*") if path.is_file()}
        state = self.request("os2-component-state", selection=selection)
        self.assertEqual(state["files"][0]["status"], "managed")
        self.assertEqual(state["files"][0]["selectedSource"], selection[0]["source"])
        self.assertEqual(
            before,
            {str(path): path.read_bytes() for path in self.ball.rglob("*") if path.is_file()},
        )

    def test_exact_source_removal_is_offline_and_keeps_modified_variants(self):
        sources = [
            self.source({"OS2/ALT.TXT": value}, name=f"offline-{i}.zip")
            for i, value in enumerate([b"first original", b"second original"])
        ]
        plan = self.request("plan-os2-extraction", sources=sources)
        pairs = [{"source": file["source"], "path": file["path"]} for file in plan["files"]]
        self.request(
            "import-os2-support",
            sources=sources,
            fileSelection=[p["source"] for p in pairs],
            operationID="offline-install",
        )
        state = self.request("os2-component-state", selection=pairs)
        by_source = {file["selectedSource"]: file for file in state["files"]}
        changed = (
            self.ball / "drive_c" / by_source[pairs[1]["source"]]["path"][3:].replace("\\", "/")
        )
        changed.write_bytes(b"user modified")
        for source in sources:
            Path(source).unlink()
        unknown = {"source": "not-installed-source", "path": by_source[pairs[0]["source"]]["path"]}
        protected = self.request(
            "remove-os2-components", selection=[unknown], operationID="offline-other"
        )
        self.assertEqual(protected["removedPaths"], [])
        self.assertEqual(protected["preserved"][0]["status"], "protected")
        result = self.request(
            "remove-os2-components", selection=pairs, operationID="offline-remove"
        )
        self.assertEqual(result["removedPaths"], [by_source[pairs[0]["source"]]["path"]])
        self.assertEqual(result["preserved"][0]["status"], "modified")
        self.assertEqual(changed.read_bytes(), b"user modified")
        for invalid in ["bad", [{}], [{"source": "id"}], [{"source": 4, "path": r"C:\A"}]]:
            with self.assertRaisesRegex(ValueError, "selection|pairs"):
                self.request("os2-component-state", selection=invalid)

    def test_dependency_recovery_records_new_original_files(self):
        app = self.ball / "drive_c/Apps/APP.EXE"
        app.parent.mkdir()
        app.write_bytes(dependency_fixtures.linear(["CUSTOM"]))
        source = self.source(
            {
                "OS2/CMD.EXE": fixtures.CMD,
                "OS2/SYSLEVEL.OS2": fixtures.LEVEL,
                "OS2/DLL/CUSTOM.DLL": dependency_fixtures.linear(["DOSCALLS"]),
            }
        )
        audit = self.request("audit-dependencies", sources=[source])
        self.assertEqual(audit["missing"], [])
        state = self.state(r"C:\OS2\DLL\CUSTOM.DLL")
        self.assertEqual(state["managedPaths"], [r"C:\OS2\DLL\CUSTOM.DLL"])
        self.assertEqual(state["progress"]["status"], "complete")

    def test_progress_visible_and_second_mutation_rejected_while_source_blocked(self):
        if os.name == "nt":
            self.skipTest("POSIX FIFO provides a controlled staging gate")
        media = self.root / "media"
        (media / "OS2").mkdir(parents=True)
        gate = media / "OS2/GATE.TXT"
        os.mkfifo(gate)
        failures = []

        def install():
            try:
                self.request(
                    "import-os2-support",
                    sources=[str(media)],
                    categories=["full"],
                    operationID="gated",
                )
            except ValueError as error:
                failures.append(str(error))

        thread = threading.Thread(target=install, daemon=True)
        thread.start()
        deadline = time.monotonic() + 5
        progress = None
        while time.monotonic() < deadline:
            progress = self.state(operation="gated")["progress"]
            if progress and progress["phase"] == "extracting":
                break
            time.sleep(0.005)
        try:
            self.assertIsNotNone(progress)
            assert progress is not None
            self.assertEqual(progress["status"], "running")
            self.assertEqual(progress["activePaths"], [r"C:\OS2\GATE.TXT"])
            with self.assertRaisesRegex(ValueError, "already running"):
                self.request("remove-os2-components", paths=[], operationID="concurrent")
            self.assertTrue(thread.is_alive())
        finally:
            # Release a waiting reader without hanging if an earlier assertion failed
            # before the worker reached its controlled source-open gate.
            release_deadline = time.monotonic() + 2
            while thread.is_alive() and time.monotonic() < release_deadline:
                try:
                    writer = os.open(gate, os.O_WRONLY | os.O_NONBLOCK)
                    os.close(writer)
                    break
                except OSError:
                    time.sleep(0.005)
            thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertTrue(failures)
        final = self.state(operation="gated")["progress"]
        self.assertEqual(final["status"], "failed")
        self.assertTrue(Path(final["logPath"]).is_file())
        log = json.loads(Path(final["logPath"]).read_text())
        self.assertEqual(log["status"], "failed")


if __name__ == "__main__":
    unittest.main()
