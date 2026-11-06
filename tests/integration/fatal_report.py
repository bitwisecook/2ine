#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Fatal failures end with status 1, a report and one dialog, never a host crash."""

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image

HOST_MODES = {
    "segv": (b"SIGSEGV", "stopped unexpectedly", "crashed while running"),
    "abort": (b"SIGABRT", "stopped unexpectedly", "internal error"),
    "trap": (b"host-fault", "stopped unexpectedly", "crashed while running"),
    "raise-ill": (b"SIGILL", "stopped unexpectedly", "crashed while running"),
    "raise-bus": (b"SIGBUS", "stopped unexpectedly", "crashed while running"),
    "raise-fpe": (b"SIGFPE", "stopped unexpectedly", "crashed while running"),
    "explicit": (b"detail marker", "stopped unexpectedly", "does not provide yet"),
    "contended": (b"Contended failure", "stopped unexpectedly", "internal error"),
}


def run(command, work, env, timeout=30):
    reports = work / "reports"
    capture = work / "dialog.txt"
    capture.unlink(missing_ok=True)
    for old in reports.glob("*"):
        old.unlink()
    result = subprocess.run(
        command,
        cwd=work,
        env={
            **env,
            "TWOINE_FATAL_DIR": str(reports),
            "TWOINE_FATAL_DIALOG": f"capture:{capture}",
        },
        capture_output=True,
        timeout=timeout,
    )
    # A negative status is a signal death: the host crash this feature replaces.
    assert result.returncode == 1, (command, result.returncode, result.stderr[-2000:])
    files = list(reports.glob("fatal-*.txt"))
    assert len(files) == 1, files
    report = files[0].read_bytes()
    assert report.startswith(b"2ine execution: "), report[:200]
    assert report in result.stderr or b"-- 2ine fatal report --" in result.stderr
    dialog = capture.read_text() if capture.exists() else None
    if dialog:
        assert f"report={files[0]}\n" in dialog, dialog
    return report, dialog


def check_host_failures(build, work, env):
    tool = build / "tests" / "twoine_fatal_tests"
    for mode, (marker, title, message) in HOST_MODES.items():
        for thread in ("main", "thread"):
            report, dialog = run([str(tool), mode, thread], work, env)
            assert marker in report, (mode, report[:800])
            assert b"-- Host backtrace --" in report, mode
            assert b"-- End of 2ine fatal report --" in report, mode
            assert b"test context marker" in report, mode
            assert dialog and title in dialog and message in dialog, (mode, dialog)
    # A thread overflows onto its alternate stack; the main thread is covered too.
    for thread in ("main", "thread"):
        report, dialog = run([str(tool), "overflow", thread], work, env)
        assert b"SIGSEGV" in report or b"SIGBUS" in report, report[:800]
        assert dialog, "stack overflow has no dialog"

    report, dialog = run([str(tool), "exiting", "main"], work, env)
    assert b"already requested exit" in report and dialog is None, dialog

    report, dialog = run([str(tool), "nested", "main"], work, env)
    assert b"Nested fault" in report and dialog, dialog
    assert b"-- End of 2ine fatal report --" not in report


def missing_import():
    g = FlatGuest()
    g.call(2, 65534)
    g.exit(98)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "MISSING", data=g.data)


def guest_fault():
    g = FlatGuest()
    g.code.extend(b"\x31\xc0\x8b\x00")  # Guest null-pointer read.
    g.exit(98)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FAULT", data=g.data)


def check_guest_failures(build, work, env):
    loader = str(build / "lx_loader")
    exe = work / "missing.exe"
    exe.write_bytes(missing_import())
    report, dialog = run([loader, str(exe)], work, env)
    assert b"Missing import PMWINX:ordinal #65534" in report, report[:800]
    assert b"missing-import" in report and b"guest registers:" in report, report[:1500]
    assert b"guest return: " in report and b"MISSING object #1" in report, report[:1500]
    assert b"-- OS/2 modules --" in report and b"MISSING LX" in report, report[:4000]
    assert b"-- Guest execution --" in report, report[:4000]
    assert "PMWINX:ordinal #65534" in dialog and "does not provide yet" in dialog, dialog
    assert "missing.exe stopped unexpectedly" in dialog, dialog

    exe = work / "fault.exe"
    exe.write_bytes(guest_fault())
    report, dialog = run([loader, str(exe)], work, env)
    assert b"guest-fault" in report and b"CPU stop" in report, report[:800]
    assert b"guest instruction:" in report, report[:1500]
    assert "invalid operation" in dialog, dialog

    bad = work / "bad.exe"
    bad.write_bytes(b"not an OS/2 program")
    report, dialog = run([loader, str(bad)], work, env)
    assert b"load-failure" in report and "could not start" in dialog, dialog
    assert "Unsaved work" not in dialog, dialog

    report, dialog = run([loader, str(work / "absent.exe")], work, env)
    assert b"load-failure" in report and "was not found" in dialog, dialog


def check_defaults(build, work, env):
    """Unattended runs must never block on a dialog nobody can dismiss."""
    exe = work / "missing.exe"
    exe.write_bytes(missing_import())
    for extra in ({}, {"TWOINE_BALL": str(work)}):
        settings = {**env, **extra}
        settings.pop("TWOINE_FATAL_DIALOG", None)
        result = subprocess.run(
            [str(build / "lx_loader"), str(exe)],
            cwd=work,
            env=settings,
            capture_output=True,
            timeout=20,
            stdin=subprocess.DEVNULL,
        )
        assert result.returncode == 1, (result.returncode, result.stderr[-2000:])
        assert b"-- 2ine fatal report --" in result.stderr, result.stderr[:500]
    assert list((work / "logs").glob("fatal-*.txt")), "ball launches report into <ball>/logs"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--backend", required=True, choices=("native", "unicorn"))
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="fatal.", dir=build) as root:
        work = Path(root).resolve()
        (work / "ipc").mkdir()
        (work / "reports").mkdir()
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            DYLD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
            TWOINE_IPC_DIR=str(work / "ipc"),
        )
        env.pop("TWOINE_BALL", None)
        if args.backend == "unicorn":
            check_host_failures(build, work, env)
        check_guest_failures(build, work, env)
        check_defaults(build, work, env)
    print("Fatal failures report, present one dialog and exit 1 without a host crash")


if __name__ == "__main__":
    main()
