# Public domain. No rights reserved. Wholly AI-authored.
"""Per-ball activity and safe component status without GTK or a host filesystem."""

import unittest
from pathlib import Path

from tools import gnome_component_jobs as jobs


class ComponentJobsTests(unittest.TestCase):
    def tree(self):
        return {
            "id": "all",
            "children": [
                {
                    "id": "help",
                    "children": [
                        {
                            "id": "directory",
                            "path": "C:\\OS2\\DLL",
                            "children": [
                                {
                                    "id": "help-file",
                                    "source": "zip!/iso!/help",
                                    "path": "C:\\OS2\\DLL\\HELPMGR.DLL",
                                }
                            ],
                        }
                    ],
                },
                {
                    "id": "fonts",
                    "children": [
                        {"id": "font-file", "source": "font", "path": "C:\\OS2\\DLL\\FONT.FON"}
                    ],
                },
            ],
        }

    def test_job_guard_applies_only_to_its_ball(self):
        records = {"/tmp/a.2ineball": {"active": True}, "/tmp/b.2ineball": {"active": False}}
        self.assertTrue(jobs.active(records, Path("/tmp/a.2ineball")))
        self.assertFalse(jobs.active(records, Path("/tmp/b.2ineball")))
        self.assertFalse(jobs.active(records, None))
        self.assertFalse(jobs.active(records, "/tmp/c.2ineball"))

    def test_exact_active_source_marks_only_file_and_ancestors(self):
        job = {
            "active": True,
            "sources": {"font"},
            "progress": {"activeSources": ["zip!/iso!/help"]},
        }
        self.assertEqual(
            jobs.activity_ids(self.tree(), job), {"all", "help", "directory", "help-file"}
        )

    def test_removal_paths_match_case_insensitively_and_completed_jobs_stop_spinners(self):
        job = {"active": True, "paths": {"c:\\os2\\dll\\font.fon"}}
        self.assertEqual(jobs.activity_ids(self.tree(), job), {"all", "fonts", "font-file"})
        job["active"] = False
        self.assertEqual(jobs.activity_ids(self.tree(), job), set())

    def test_install_planning_marks_selected_files_without_materializing_media(self):
        self.assertEqual(
            jobs.activity_ids(self.tree(), {"active": True, "sources": {"font"}}),
            {"all", "fonts", "font-file"},
        )

    def test_modified_unowned_and_protected_payloads_are_never_offered_for_removal(self):
        for status in ("modified", "protected", "unowned", "symlink", "not-regular", "unsafe"):
            self.assertFalse(
                jobs.file_state({"present": True, "managed": True, "status": status})["removable"]
            )
        self.assertFalse(jobs.file_state({"present": True, "managed": False})["removable"])
        self.assertFalse(jobs.file_state({"present": False, "managed": True})["removable"])

    def test_only_explicit_unchanged_managed_status_is_removable(self):
        self.assertTrue(
            jobs.file_state({"present": True, "managed": True, "status": "managed"})["removable"]
        )
        self.assertFalse(
            jobs.file_state({"present": True, "managed": True, "status": "unknown-future-status"})[
                "removable"
            ]
        )
        self.assertFalse(jobs.file_state({"present": True, "managed": True})["removable"])

    def test_live_ownership_provenance_preserves_the_current_plan_source(self):
        planned = {
            "source": "current.zip!/os2.iso!/file",
            "category": "help",
            "id": "current-source",
        }
        planned.update(
            jobs.file_state(
                {
                    "path": "C:\\OS2\\DLL\\HELPMGR.DLL",
                    "present": True,
                    "managed": True,
                    "status": "managed",
                    "source": "older.iso!/file",
                    "category": "full",
                }
            )
        )
        self.assertEqual(planned["source"], "current.zip!/os2.iso!/file")
        self.assertEqual(planned["category"], "help")
        self.assertEqual(planned["id"], "current-source")
        self.assertEqual(planned["installedSource"], "older.iso!/file")

    def test_retained_variant_uses_actual_destination_without_rewriting_plan(self):
        planned = {
            "source": "alternate",
            "id": "alternate",
            "category": "help",
            "path": r"C:\OS2\FILE.HLP",
        }
        actual = r"C:\OS2\INSTALL\MEDIA\hash\FILE.HLP"
        state = jobs.source_states(
            [
                {
                    "selectedSource": "alternate",
                    "plannedPath": planned["path"],
                    "path": actual,
                    "present": True,
                    "managed": True,
                    "status": "managed",
                }
            ]
        )
        planned.update(state["alternate"])
        self.assertEqual(planned["path"], r"C:\OS2\FILE.HLP")
        self.assertEqual(planned["source"], "alternate")
        self.assertEqual(planned["id"], "alternate")
        self.assertEqual(planned["category"], "help")
        self.assertEqual(planned["actualPath"], actual)
        self.assertEqual(planned["removableCount"], 1)
        planned["title"] = "Help"
        self.assertIn(actual, jobs.file_tooltip(planned))

    def test_missing_alternate_cannot_borrow_another_sources_canonical_ownership(self):
        path = r"C:\OS2\FILE.HLP"
        records = [
            {
                "selectedSource": "main",
                "path": path,
                "present": True,
                "managed": True,
                "status": "managed",
            },
            {
                "selectedSource": "alternate",
                "path": path,
                "present": False,
                "managed": False,
                "status": "missing",
            },
        ]
        result = jobs.source_states(records)
        self.assertTrue(result["main"]["present"])
        self.assertTrue(result["main"]["removable"])
        self.assertFalse(result["alternate"]["present"])
        self.assertFalse(result["alternate"]["removable"])
        self.assertEqual(result["alternate"]["actualPaths"], [])
        self.assertEqual(
            jobs.source_states(
                [{"path": path, "managed": True, "present": True, "status": "managed"}]
            ),
            {},
        )

    def test_multiple_destinations_keep_modified_records_protected_individually(self):
        result = jobs.source_states(
            [
                {
                    "selectedSource": "one",
                    "path": r"C:\OS2\FILE.HLP",
                    "present": True,
                    "managed": True,
                    "status": "managed",
                },
                {
                    "selectedSource": "one",
                    "path": r"C:\OS2\BACKUP\FILE.HLP",
                    "present": True,
                    "managed": False,
                    "status": "modified",
                    "reason": "User changes",
                },
            ]
        )["one"]
        self.assertEqual(result["removableCount"], 1)
        self.assertEqual(len(result["actualFiles"]), 2)
        self.assertEqual(result["status"], "modified")
        self.assertFalse(result["managed"])
        self.assertEqual([file["removable"] for file in result["actualFiles"]], [True, False])

    def test_source_selection_keeps_distinct_sources_at_same_planned_path(self):
        files = [
            {"source": "main", "path": "C:\\FILE"},
            {"source": "alternate", "path": "C:\\FILE"},
            {"source": "main", "path": "C:\\FILE"},
            {"source": "provided", "path": "C:\\OTHER", "provided": True},
        ]
        self.assertEqual(
            jobs.state_selection(files),
            [{"source": "main", "path": "C:\\FILE"}, {"source": "alternate", "path": "C:\\FILE"}],
        )
        self.assertEqual(
            jobs.state_selection(files, {"alternate"}),
            [{"source": "alternate", "path": "C:\\FILE"}],
        )

    def test_readonly_status_distinguishes_installed_ownership(self):
        self.assertEqual(
            jobs.state_label({"present": True, "managed": False}), "Installed · not managed"
        )
        self.assertEqual(
            jobs.state_label({"present": True, "managed": True}), "Installed · managed"
        )
        self.assertIn(
            "preserved", jobs.state_label({"present": True, "managed": True, "status": "modified"})
        )
        self.assertEqual(jobs.state_label({"present": False}), "Available")
        self.assertIn("Protected", jobs.state_label({"present": False, "status": "protected"}))


class DesiredComponentsTests(unittest.TestCase):
    @staticmethod
    def actual(status="managed", present=True):
        return {
            "present": present,
            "status": status,
            "managed": status == "managed",
            "removable": status == "managed" and present,
        }

    def test_default_cmd_selection_immediately_produces_real_install_delta(self):
        queue = jobs.DesiredComponents({"cmd", "font"}, {"cmd"})
        self.assertEqual(queue.next_step(), ("install", {"cmd"}))
        queue.update_actual({"cmd": self.actual()})
        self.assertEqual(queue.next_step(), (None, set()))

    def test_restart_after_prior_operation_does_not_reinstall_unchecked_cmd_default(self):
        queue = jobs.DesiredComponents({"cmd"}, {"cmd"})
        queue.initialize(has_history=True)
        self.assertEqual(queue.next_step(), (None, set()))
        explicit = jobs.DesiredComponents({"cmd"}, {"cmd"})
        explicit.initialize(has_history=True, explicit=True)
        self.assertEqual(explicit.next_step(), ("install", {"cmd"}))
        first = jobs.DesiredComponents({"cmd"}, {"cmd"})
        first.initialize(has_history=False)
        self.assertEqual(first.next_step(), ("install", {"cmd"}))

    def test_external_installed_presence_is_observed_without_automatic_uninstall(self):
        queue = jobs.DesiredComponents({"font"}, set())
        queue.initialize(has_history=True)
        queue.update_actual({"font": self.actual()})
        self.assertEqual(queue.desired, {"font"})
        self.assertEqual(queue.next_step(), (None, set()))
        queue.update_actual({"font": self.actual("missing", False)})
        self.assertEqual(queue.desired, set())
        self.assertEqual(queue.next_step(), (None, set()))

    def test_initial_status_does_not_overwrite_an_explicit_uncheck_while_reading(self):
        queue = jobs.DesiredComponents({"font"}, {"font"})
        queue.edit(set())
        queue.update_actual({"font": self.actual()})
        queue.initialize(has_history=True, explicit=True)
        self.assertEqual(queue.next_step(), ("remove", {"font"}))

    def test_toggles_during_install_coalesce_then_remove_committed_file(self):
        queue = jobs.DesiredComponents({"cmd"}, {"cmd"})
        queue.batch({"cmd"})
        queue.edit(set())
        queue.update_actual({"cmd": self.actual()})
        self.assertEqual(queue.next_step(), ("remove", {"cmd"}))
        queue.edit({"cmd"})
        self.assertEqual(queue.next_step(), (None, set()))

    def test_parent_uncheck_clamps_modified_unowned_and_protected_presence(self):
        queue = jobs.DesiredComponents({"owned", "modified", "unowned", "protected"}, set())
        queue.update_actual(
            {
                name: self.actual(status)
                for name, status in (
                    ("owned", "managed"),
                    ("modified", "modified"),
                    ("unowned", "unowned"),
                    ("protected", "protected"),
                )
            }
        )
        blocked = queue.edit(set())
        self.assertEqual(blocked, {"modified", "unowned", "protected"})
        self.assertEqual(queue.desired, blocked)
        self.assertEqual(queue.next_step(), ("remove", {"owned"}))

    def test_successful_zero_copy_batch_cannot_retry_preserved_other_source_forever(self):
        queue = jobs.DesiredComponents({"alternate"}, {"alternate"})
        batch = queue.batch({"alternate"})
        queue.update_actual({"alternate": self.actual("missing", False)})
        self.assertEqual(queue.settle(batch), {"alternate"})
        self.assertEqual(queue.next_step(), (None, set()))
        self.assertFalse(queue.pending())

    def test_failed_unchanged_intent_snaps_to_actual_and_never_retries(self):
        queue = jobs.DesiredComponents({"cmd"}, {"cmd"})
        batch = queue.batch({"cmd"})
        queue.update_actual({"cmd": self.actual("missing", False)})
        queue.failed(batch)
        self.assertEqual(queue.desired, set())
        self.assertEqual(queue.next_step(), (None, set()))

    def test_edits_during_failed_batch_survive_using_per_source_revisions(self):
        queue = jobs.DesiredComponents({"cmd", "font"}, {"cmd"})
        batch = queue.batch({"cmd"})
        queue.edit(set())
        queue.edit({"cmd", "font"})
        queue.update_actual({"cmd": self.actual("missing", False)})
        queue.failed(batch)
        self.assertEqual(queue.next_step(), ("install", {"cmd", "font"}))

    def test_partial_cancel_keeps_successful_files_checked_and_missing_files_off(self):
        queue = jobs.DesiredComponents({"cmd", "rexx"}, {"cmd", "rexx"})
        batch = queue.batch({"cmd", "rexx"})
        queue.update_actual({"cmd": self.actual(), "rexx": self.actual("missing", False)})
        queue.failed(batch)
        self.assertEqual(queue.desired, {"cmd"})
        self.assertFalse(queue.pending())

    def test_unrelated_new_selection_survives_a_failure_without_retrying_old_source(self):
        queue = jobs.DesiredComponents({"cmd", "font"}, {"cmd"})
        batch = queue.batch({"cmd"})
        queue.edit({"cmd", "font"})
        queue.failed(batch)
        self.assertEqual(queue.next_step(), ("install", {"font"}))

    def test_native_disabled_sources_are_never_inferred_as_removal_targets(self):
        queue = jobs.DesiredComponents({"cmd"}, set())
        queue.update_actual({"opengl-native-disabled": self.actual()})
        self.assertEqual(queue.next_step(), (None, set()))


if __name__ == "__main__":
    unittest.main()
