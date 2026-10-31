#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify terminal output and restored echo/canonical input through a real PTY."""

import array
import fcntl
import os
import pty
import select
import subprocess
import sys
import termios
import time

master, slave = pty.openpty()
try:
    fcntl.ioctl(slave, termios.TIOCSWINSZ, array.array("H", [24, 80, 0, 0]))
    before = termios.tcgetattr(slave)
    child = subprocess.Popen(
        [sys.argv[1]],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env={**os.environ, "TERM": "xterm-256color"},
    )
    output = bytearray()
    deadline = time.monotonic() + 10
    while child.poll() is None and time.monotonic() < deadline:
        if select.select([master], [], [], 0.1)[0]:
            output.extend(os.read(master, 65536))
    if child.poll() is None:
        child.kill()
    status = child.wait()
    while select.select([master], [], [], 0)[0]:
        output.extend(os.read(master, 65536))
    assert status == 0, (status, output)
    assert b"HAL" in output and b"terminal closed" in output, output
    after = termios.tcgetattr(slave)
    mask = termios.ECHO | termios.ICANON
    assert before[3] & mask == after[3] & mask, (before, after)
    print("PTY rendering and terminal-mode restoration passed")
finally:
    os.close(master)
    os.close(slave)
