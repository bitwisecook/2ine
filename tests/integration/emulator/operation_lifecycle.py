#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Operation completion follows pipe-gated descendants, without timing guesses."""

import os
import select
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


def readable(fd):
    if not select.select([fd], [], [], 8)[0]:
        raise AssertionError("worker lifetime/ready pipe did not respond")
    return os.read(fd, 128)


class Operation:
    def __init__(self, root, name, service=True, result=0, ignore_term=False):
        self.marker = root / (name + ".exe")
        directory = root / name
        directory.mkdir()
        for index in range(7):
            os.mkfifo(directory / str(index))
        self.pipes: list[int] = []
        self.open_fds = set()
        for index in range(7):
            mode = os.O_RDONLY if index in (0, 2, 5) else os.O_RDWR
            fd = os.open(directory / str(index), mode | os.O_NONBLOCK)
            self.pipes.append(fd)
            self.open_fds.add(fd)
        self.directory = directory
        self.log = (root / (name + ".log")).open("wb")
        self.process = subprocess.Popen(
            [
                sys.argv[1],
                "--operation",
                "wrapper",
                str(self.marker),
                str(directory),
                str(int(service)),
                str(result),
                str(int(ignore_term)),
            ],
            stdout=self.log,
            stderr=self.log,
        )
        self.service = service
        self.worker_pid = None
        self.service_pid = None

    def await_workers(self):
        self.worker_pid = int(readable(self.pipes[1]))
        self.service_pid = int(readable(self.pipes[4])) if self.service else None
        assert readable(self.pipes[0]) == b"", "wrapper did not actually exit"
        assert self.process.poll() is None, "operation finished while its grandchild was gated"
        assert not self.marker.exists()

    def release(self, index):
        os.write(self.pipes[index], b"x")

    def worker_exited(self):
        assert readable(self.pipes[2]) == b"", "worker retained a live process lease"

    def service_alive(self):
        if self.service:
            assert not select.select([self.pipes[5]], [], [], 0)[0], "service was cancelled"

    def close(self):
        if self.process.poll() is None:
            self.process.kill()  # owner-death pipe must also clean up on harness failure
            self.process.wait(timeout=8)
        try:
            if self.service_pid:
                self.release(6)
                assert readable(self.pipes[5]) == b"", "independent service did not exit"
        finally:
            for fd in self.open_fds:
                os.close(fd)
            self.log.close()


with tempfile.TemporaryDirectory(prefix="2ine-operation-") as directory:
    root = Path(directory)
    active = []

    def start(*args, **kwargs):
        operation = Operation(*args, **kwargs)
        active.append(operation)
        operation.await_workers()
        return operation

    try:
        completed = start(root, "complete", result=23)
        completed.release(3)
        assert completed.process.wait(timeout=8) == 23
        completed.worker_exited()
        assert completed.marker.read_bytes() == b"MZinstalled"
        completed.service_alive()

        for name, sent, expected, ignored in (
            ("cancel", signal.SIGTERM, -signal.SIGTERM, False),
            ("force", signal.SIGUSR2, -signal.SIGKILL, True),
            ("owner-crash", signal.SIGKILL, -signal.SIGKILL, True),
        ):
            operation = start(root, name, ignore_term=ignored)
            operation.process.send_signal(sent)
            assert operation.process.wait(timeout=8) == expected
            operation.worker_exited()
            assert not operation.marker.exists()
            operation.service_alive()

        first = start(root, "parallel-a")
        second = start(root, "parallel-b")
        first.process.send_signal(signal.SIGUSR2)
        assert first.process.wait(timeout=8) == -signal.SIGKILL
        first.worker_exited()
        assert second.process.poll() is None
        assert not select.select([second.pipes[2]], [], [], 0)[0]
        second.release(3)
        assert second.process.wait(timeout=8) == 0 and second.marker.exists()
        second.worker_exited()
        first.service_alive()
        second.service_alive()
    finally:
        for operation in reversed(active):
            operation.close()
print(
    "Detached grandchildren, completion, cancellation, owner death and independent services passed"
)
