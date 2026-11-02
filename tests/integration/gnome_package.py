#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Extract a Linux package, audit relocation, then exercise its real GTK frontend."""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(argv, **kwargs):
    return subprocess.run([str(arg) for arg in argv], check=True, **kwargs)


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
        runtime = destination / "usr/lib/2ine-balls/runtime"
        for binary in runtime.iterdir():
            if binary.is_dir():
                continue
            with binary.open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    continue
            rpath = run(
                ["patchelf", "--print-rpath", binary], capture_output=True, text=True
            ).stdout.strip()
            assert rpath == "$ORIGIN", (binary, rpath)
            dependencies = run(["ldd", binary], capture_output=True, text=True).stdout
            assert "not found" not in dependencies, (binary, dependencies)
            assert str(ROOT) not in dependencies, (binary, dependencies)
        run([destination / "usr/bin/2ine-balls", "--help"], capture_output=True)
        frontend = destination / "usr/lib/2ine-balls/tools"
        environment = dict(
            os.environ,
            TWOINE_GNOME_FRONTEND=str(frontend),
            TWOINE_BALL_LIBRARY=str(runtime / "libtwoine_ball.so"),
        )
        run(
            [sys.executable, ROOT / "tests/integration/gnome_balls.py", "--build", runtime],
            env=environment,
        )
        print(f"{args.package.name}: relocated ELF closure and GTK/LX workflows passed")


if __name__ == "__main__":
    main()
