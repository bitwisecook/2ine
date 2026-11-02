#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise named mutex recursion, worker contention, abandonment and lifetime."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def image(child):
    g = FlatGuest()
    mutex, alias = g.field(bytes(4)), g.field(bytes(4))
    events = [g.field(bytes(4)), g.field(bytes(4))]
    name = g.field(b"\\SEM32\\NamedMutexFixture\0")
    folded = g.field(b"\\sem32\\namedmutexfixture\0")
    ready_names = [g.field(b"\\SEM32\\MutexReady1\0"), g.field(b"\\SEM32\\MutexReady2\0")]
    if child:
        g.call(1, 332, folded, mutex)
        g.result(0, 40)
        for name_pointer, event in zip(ready_names, events, strict=True):
            g.call(1, 325, name_pointer, event)
            g.result(0, 41)
        g.call_handle(1, 335, mutex)
        g.result(288, 42)
        for index, timeout in enumerate((0, 20)):
            g.call_handle(1, 334, mutex, timeout)
            g.result(640, 43 + index)
            g.call_handle(1, 328, events[index])
            g.result(0, 45 + index)
        g.call_handle(1, 334, mutex, 0xFFFFFFFF)
        g.result(0, 47)
        g.call_handle(1, 334, mutex, 0)
        g.result(0, 48)
        g.call_handle(1, 335, mutex)
        g.result(0, 49)
        # Terminating while still owning the recursive object abandons it.
        g.exit(0)
    else:
        result, waited, pid = g.field(bytes(8)), g.field(bytes(8)), g.field(bytes(4))
        filename, command = g.field(b"child.exe\0"), g.field(b"child.exe\0\0")
        g.call(1, 332, name, mutex)
        g.result(187, 1)
        g.call(1, 331, name, mutex, 0, 1)
        g.result(0, 2)
        g.call(1, 331, folded, alias, 0, 0)
        g.result(285, 3)
        g.call(1, 332, folded, alias)
        g.result(0, 4)
        g.call_handle(1, 334, alias, 0)
        g.result(0, 5)
        g.call_handle(1, 333, mutex)
        g.result(301, 6)
        for name_pointer, event in zip(ready_names, events, strict=True):
            g.call(1, 324, name_pointer, event, 0, 0)
            g.result(0, 7)
        g.call(1, 283, 0, 0, 2, command, 0, result, filename)
        g.result(0, 8)
        g.call_handle(1, 329, events[0], 3000)
        g.result(0, 9)
        g.call_handle(1, 335, alias)
        g.result(0, 10)
        g.call_handle(1, 329, events[1], 3000)
        g.result(0, 11)
        g.call_handle(1, 335, mutex)
        g.result(0, 12)
        g.call(1, 280, 0, 0, waited, pid, (result,))
        g.result(0, 13)
        g.word(waited + 4, 0, 14)
        g.call_handle(1, 334, mutex, 0)
        g.result(105, 15)
        g.call_handle(1, 335, mutex)
        g.result(0, 16)
        g.call_handle(1, 333, alias)
        g.result(0, 17)
        g.call_handle(1, 333, mutex)
        g.result(0, 18)
        g.call(1, 332, name, mutex)
        g.result(187, 19)
        g.call(1, 331, name, mutex, 0, 0)
        g.result(0, 20)
        g.call_handle(1, 334, mutex, 0)
        g.result(0, 21)
        g.call_handle(1, 335, mutex)
        g.result(0, 22)
        g.call_handle(1, 333, mutex)
        g.result(0, 23)
        g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "NAMEMTX", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-namedmutex-") as temporary:
        work = Path(temporary)
        path = work / "parent.exe"
        path.write_bytes(image(False))
        (work / "child.exe").write_bytes(image(True))
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
            LD_LIBRARY_PATH=str(build),
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(path)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"named mutex exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
    print("Named mutex recursion, cross-worker timeout, owner death and lifetime passed")


if __name__ == "__main__":
    main()
