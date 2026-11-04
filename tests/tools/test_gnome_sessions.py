# Public domain. No rights reserved. Wholly AI-authored.
"""Independent sessions and manager-owned startup service lease boundaries."""

import subprocess
import sys
import unittest

from tools.gnome_sessions import Sessions, StartupPool


class SessionTests(unittest.TestCase):
    def test_apps_share_a_ball_while_installers_are_exclusive_per_ball(self):
        sessions = Sessions()
        first = sessions.reserve("/tmp/a", False, "First")
        self.assertFalse(sessions.can_start("/tmp/a"))
        first.state = "running"
        second = sessions.reserve("/tmp/a", False, "Second")
        self.assertNotEqual(first.id, second.id)
        self.assertFalse(sessions.can_start("/tmp/a", True))
        self.assertTrue(sessions.can_start("/tmp/b", True))
        installer = sessions.reserve("/tmp/b", True, "Setup")
        installer.state = "running"
        self.assertFalse(sessions.can_start("/tmp/b"))
        first.state = second.state = "finished"
        self.assertTrue(sessions.can_start("/tmp/a", True))

    def test_completion_and_stop_state_are_session_specific(self):
        sessions = Sessions()
        first = sessions.reserve("/tmp/a", False, "First")
        first.state = "running"
        second = sessions.reserve("/tmp/a", False, "Second")
        second.state = "running"
        first.state, first.stop_sent = "stopping", True
        self.assertFalse(second.stop_sent)
        first.state, first.code = "finished", -15
        self.assertEqual(sessions.active("/tmp/a"), [second])
        self.assertIsNone(second.code)


class StartupLeaseTests(unittest.TestCase):
    def setUp(self):
        self.pool = StartupPool()
        self.spawned = []
        self.stopped = []
        self.plan = {
            "program": r"C:\OS2\SOMDD.EXE",
            "command": ["loader", "SOMDD.EXE"],
            "cwd": "/tmp",
            "environment": {"A": "1", "B": "2"},
            "unsetEnvironment": ["Z", "Y"],
        }

    def spawn(self, plan, index):
        child = {"plan": plan, "index": index, "alive": True}
        self.spawned.append(child)
        return child

    @staticmethod
    def alive(child):
        return child["alive"]

    def acquire(self, session, plans=None, ball="/tmp/a"):
        leases, error = self.pool.acquire(
            ball, plans or [self.plan], session, self.spawn, self.alive
        )
        self.assertIsNone(error)
        return leases

    def test_service_stays_until_the_last_app_exits_and_other_balls_do_not_share(self):
        first, second = self.acquire("one"), self.acquire("two")
        self.assertEqual(len(self.spawned), 1)
        self.pool.release(first, "one", self.stopped.append)
        self.assertFalse(self.stopped)
        other = self.acquire("other", ball="/tmp/b")
        self.assertEqual(len(self.spawned), 2)
        self.pool.release(second, "two", self.stopped.append)
        self.assertEqual(self.stopped, [self.spawned[0]])
        self.pool.release(other, "other", self.stopped.append)
        self.assertEqual(len(self.stopped), 2)

    def test_same_logical_service_rejects_settings_changes_and_reuses_identical_plan(self):
        self.acquire("one")
        for field, value in [
            ("cwd", "/other"),
            ("command", ["loader", "OTHER.EXE"]),
            ("environment", {"A": "changed"}),
        ]:
            leases, error = self.pool.acquire(
                "/tmp/a", [{**self.plan, field: value}], field, self.spawn, self.alive
            )
            self.assertFalse(leases)
            self.assertIsInstance(error, ValueError)
        self.assertEqual(len(self.spawned), 1)
        self.acquire(
            "same",
            [{**self.plan, "environment": {"B": "2", "A": "1"}, "unsetEnvironment": ["Y", "Z"]}],
        )
        self.assertEqual(len(self.spawned), 1)
        self.acquire(
            "another",
            [{**self.plan, "program": r"C:\OS2\OTHER.EXE", "command": ["loader", "OTHER.EXE"]}],
        )
        self.assertEqual(len(self.spawned), 2)

    def test_last_service_cleanup_blocks_relaunch_until_owned_child_has_exited(self):
        lease = self.acquire("one")
        self.pool.release(lease, "one", self.stopped.append)
        leases, error = self.pool.acquire("/tmp/a", [self.plan], "new", self.spawn, self.alive)
        self.assertFalse(leases)
        self.assertIsInstance(error, ValueError)
        self.spawned[0]["alive"] = False
        self.acquire("fresh")
        self.assertEqual(len(self.spawned), 2)

    def test_dead_shared_service_rejects_new_launch_until_older_leases_finish(self):
        first = self.acquire("one")
        self.spawned[0]["alive"] = False
        second, error = self.pool.acquire("/tmp/a", [self.plan], "two", self.spawn, self.alive)
        self.assertIsInstance(error, ValueError)
        self.assertIn("Close", str(error))
        self.assertFalse(second)
        self.assertEqual(len(self.spawned), 1)
        self.pool.release(first, "one", self.stopped.append)
        self.acquire("fresh")
        self.assertEqual(len(self.spawned), 2)

    def test_real_owned_child_is_reused_and_external_same_program_is_untouched(self):
        command = [sys.executable, "-c", "import time; time.sleep(30)"]
        external = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        owned = []
        plan = {**self.plan, "command": command}

        def spawn(_plan, _index):
            child = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            owned.append(child)
            return child

        def stop(child):
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=3)

        try:
            first, error = self.pool.acquire(
                "/tmp/a", [plan], "first", spawn, lambda child: child.poll() is None
            )
            self.assertIsNone(error)
            second, error = self.pool.acquire(
                "/tmp/a", [plan], "second", spawn, lambda child: child.poll() is None
            )
            self.assertIsNone(error)
            self.assertEqual(len(owned), 1)
            self.pool.release(first, "first", stop)
            self.assertIsNone(owned[0].poll())
            self.pool.release(second, "second", stop)
            self.assertIsNotNone(owned[0].poll())
            self.assertIsNone(external.poll())
        finally:
            for child in [*owned, external]:
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=3)

    def test_partial_startup_failure_releases_only_this_sessions_owned_leases(self):
        shared = self.acquire("existing")

        def fail(plan, index):
            if index:
                raise ValueError("startup failure")
            return self.spawn(plan, index)

        leases, error = self.pool.acquire(
            "/tmp/a",
            [self.plan, {**self.plan, "program": r"C:\OS2\BAD.EXE", "cwd": "/bad"}],
            "new",
            fail,
            self.alive,
        )
        self.assertIsInstance(error, ValueError)
        self.pool.release(leases, "new", self.stopped.append)
        self.assertFalse(self.stopped)
        self.pool.release(shared, "existing", self.stopped.append)
        self.assertEqual(len(self.stopped), 1)


if __name__ == "__main__":
    unittest.main()
