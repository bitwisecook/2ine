#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Extract a Linux package, audit relocation, then start its native GTK frontend."""

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
ELF_MAGIC = b"\x7fELF"
# 2ine-dialog's exit status for a request it does not understand.
DIALOG_FAILURE_STATUS = 2
# The relocated frontend must open its window within this many seconds.
STARTUP_TIMEOUT_SECONDS = 60
STARTUP_POLL_SECONDS = 0.5


def run(argv, **kwargs):
    return subprocess.run([str(arg) for arg in argv], check=True, **kwargs)


def windows(environment):
    # Imported late: the X11 harness needs the display this test is given.
    sys.path.insert(0, str(ROOT))
    from tests.integration.harness.linux.x11 import Display

    return Display(environment["DISPLAY"]).windows()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-package-relocation-") as temporary:
        destination = Path(temporary) / "A relocated package"
        destination.mkdir()
        if args.package.suffix == ".deb":
            run(["dpkg-deb", "--extract", args.package.resolve(), destination])
        elif args.package.suffix == ".rpm":
            # Both processes are checked; extraction never invokes a shell.
            archive = run(["rpm2cpio", args.package.resolve()], capture_output=True).stdout
            run(
                ["cpio", "--extract", "--make-directories", "--quiet", "--no-absolute-filenames"],
                input=archive,
                cwd=destination,
            )
        else:
            parser.error("Supply a .deb or .rpm package")
        runtime = destination / "usr/lib/2ine-ball/runtime"
        for binary in runtime.iterdir():
            if binary.is_dir():
                continue
            with binary.open("rb") as stream:
                if stream.read(len(ELF_MAGIC)) != ELF_MAGIC:
                    continue
            rpath = run(
                ["patchelf", "--print-rpath", binary], capture_output=True, text=True
            ).stdout.strip()
            assert rpath == "$ORIGIN", (binary, rpath)
            dependencies = run(["ldd", binary], capture_output=True, text=True).stdout
            assert "not found" not in dependencies, (binary, dependencies)
            assert str(ROOT) not in dependencies, (binary, dependencies)
        frontend = destination / "usr/bin/2ine-ball"
        with frontend.open("rb") as stream:
            assert stream.read(len(ELF_MAGIC)) == ELF_MAGIC, "the frontend is a native executable"
        dependencies = run(["ldd", frontend], capture_output=True, text=True).stdout
        assert "not found" not in dependencies, dependencies
        assert "libtwoine_ball" not in dependencies, "the ball engine is linked statically"
        assert "python" not in dependencies.lower(), dependencies
        run([frontend, "--help"], capture_output=True)
        helper = runtime / "2ine-dialog"
        assert helper.is_file(), "the native dialog helper ships beside lx_loader"
        assert "libtwoine_ball" not in run(["ldd", helper], capture_output=True, text=True).stdout
        invalid = subprocess.run([helper, "unknown"], capture_output=True, text=True)
        assert invalid.returncode == DIALOG_FAILURE_STATUS, (invalid.returncode, invalid.stderr)
        home = Path(temporary) / "home"
        environment = {
            key: value for key, value in os.environ.items() if not key.startswith("TWOINE_")
        }
        environment.update(
            HOME=str(home),
            XDG_CONFIG_HOME=str(home / ".config"),
            XDG_DATA_HOME=str(home / ".local/share"),
            GDK_BACKEND="x11",
        )
        ball = home / "Relocated.2ineball"
        process = subprocess.Popen([frontend, ball], env=environment)
        try:
            # The relocated frontend opens the ball with the packaged runtime and engine.
            deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
            while time.monotonic() < deadline and process.poll() is None:
                if (ball / "ball.json").is_file() or windows(environment):
                    break
                time.sleep(STARTUP_POLL_SECONDS)
            assert process.poll() is None, "the frontend exited during startup"
            assert any(title == "2ine" for _wid, title in windows(environment))
        finally:
            process.terminate()
            process.wait(timeout=30)
        print(f"{args.package.name}: relocated ELF closure and native frontend startup passed")


if __name__ == "__main__":
    main()
