#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Long-lived native clients must observe and preserve other processes' INI commits."""

import os
import selectors
import subprocess
import sys
import tempfile
from contextlib import ExitStack
from pathlib import Path


class Worker:
    def __init__(self, executable, path, env, mode="worker"):
        self.process = subprocess.Popen(
            [str(executable), mode, str(path)],
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        assert self.process.stdin is not None and self.process.stdout is not None
        self.input = self.process.stdin
        self.output = self.process.stdout
        self.expect("ready")

    def send(self, command):
        self.input.write(command + "\n")
        self.input.flush()

    def expect(self, expected):
        with selectors.DefaultSelector() as selector:
            selector.register(self.output, selectors.EVENT_READ)
            assert selector.select(10), f"Timed out waiting for {expected!r}"
        actual = self.output.readline().rstrip("\n")
        assert actual == expected, (actual, expected, self.process.poll())

    def command(self, command, expected="ok"):
        self.send(command)
        self.expect(expected)

    def close(self):
        if self.process.poll() is None:
            self.input.close()
            try:
                assert self.process.wait(5) == 0
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                raise


def main():
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-profile-concurrency-") as temporary:
        work = Path(temporary)
        env = dict(
            os.environ,
            TWOINE_USER_PROFILE=str(work / "user.ini"),
            TWOINE_SYSTEM_PROFILE=str(work / "system.ini"),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
        )
        path = work / "private.ini"
        with ExitStack() as stack:
            first = Worker(executable, path, env)
            stack.callback(first.close)
            second = Worker(executable, path, env)
            stack.callback(second.close)
            # Opening/querying absent profiles is read-only: no INI or sidecar.
            first.command("get private Absent Key", "missing")
            second.command("get merged Absent Key", "missing")
            assert not list(work.iterdir())
            # Both handles are already cached before any writes. Equal-sized binary
            # updates and deletions must be visible without closing either handle.
            for scope in ("private", "user", "system"):
                first.command(f"set {scope} App A 00ff4200")
                second.command(f"set {scope} App B 11223344")
                first.command(f"get {scope} App B", "hex:11223344")
                second.command(f"get {scope} App A", "hex:00ff4200")
                second.command(f"set {scope} App A 55667788")
                first.command(f"get {scope} App A", "hex:55667788")
                first.command(f"del {scope} App A")
                second.command(f"set {scope} App C aabbccdd")
                second.command(f"get {scope} App A", "missing")
                first.command(f"get {scope} App C", "hex:aabbccdd")
                first.command(f"drop {scope} App")
                second.command(f"set {scope} Other D 00")
                first.command(f"get {scope} App B", "missing")
                first.command(f"clear {scope}")
                second.command(f"set {scope} Final E 01")
                first.command(f"get {scope} Other D", "missing")
                first.command(f"get {scope} Final E", "hex:01")
            first.command("set system Merged Key 02")
            second.command("get merged Merged Key", "hex:02")
            first.command("set user Merged Key 03")
            second.command("get merged Merged Key", "hex:03")
            first.command("del user Merged Key")
            second.command("get merged Merged Key", "hex:02")
            # Simultaneous bursts exercise contention rather than just stale caches.
            first.send("batch private Stress A 80")
            second.send("batch private Stress B 80")
            first.expect("ok")
            second.expect("ok")
            for i in range(80):
                first.command(f"get private Stress B{i}", "hex:" + f"B{i}\0".encode().hex())
                second.command(f"get private Stress A{i}", "hex:" + f"A{i}\0".encode().hex())
            # Kernel-owned sidecar locks survive owner death without a stale lock.
            holder = Worker(executable, Path(str(path) + ".prf-lock"), env, "lock")
            stack.callback(holder.close)
            first.send("set private AfterDeath Key ff")
            holder.process.kill()
            holder.process.wait(5)
            first.expect("ok")
            second.command("get private AfterDeath Key", "hex:ff")
            # A damaged replacement must not be overwritten from a cached version.
            path.write_bytes(b"invalid")
            first.command("set private Bad Key ff", "error")
            assert path.read_bytes() == b"invalid"
            first.command("get private AfterDeath Key", "missing")
            assert path.read_bytes() == b"invalid"


if __name__ == "__main__":
    main()
