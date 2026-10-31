#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Format maintained C/H sources using the repository's pinned clang-format."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def formatter() -> str:
    version = (ROOT / "tools/clang-format-version").read_text().strip()
    override = os.environ.get("CLANG_FORMAT")
    candidates = (
        [override] if override else [f"clang-format-{version.split('.')[0]}", "clang-format"]
    )
    for candidate in candidates:
        executable = shutil.which(candidate)
        if executable is None:
            continue
        result = subprocess.run(
            [executable, "--version"], capture_output=True, text=True, check=True
        )
        match = re.search(r"\bversion\s+(\d+\.\d+\.\d+)\b", result.stdout)
        if match and match[1] == version:
            return executable
    raise ValueError(
        f"clang-format {version} is required; install with "
        f"'python3 -m pip install clang-format=={version}' or set CLANG_FORMAT to its executable"
    )


def sources(paths: list[Path]) -> list[str]:
    if paths:
        files = []
        for path in paths:
            path = path.resolve(strict=True)
            path.relative_to(ROOT)
            if path.is_dir():
                raise ValueError(f"expected a C/H file: {path}")
            if path.suffix not in (".c", ".h"):
                raise ValueError(f"expected a C/H file: {path}")
            files.append(str(path))
        return sorted(set(files))
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--", "*.c", "*.h"],
        cwd=ROOT,
        capture_output=True,
        check=True,
    )
    # Include untracked additions; exclude deleted tracked files and build output.
    return sorted(
        {
            os.fsdecode(name)
            for name in result.stdout.split(b"\0")
            if name and (ROOT / os.fsdecode(name)).is_file()
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--check", action="store_true", help="report formatting differences without writing"
    )
    mode.add_argument("--write", action="store_true", help="format sources in place")
    parser.add_argument(
        "paths", nargs="*", type=Path, help="C/H files; defaults to all maintained sources"
    )
    args = parser.parse_args()
    try:
        executable = formatter()
        files = sources(args.paths)
        if not files:
            raise ValueError("no C/H sources selected")
        command = [
            executable,
            "--style=file",
            "--fallback-style=none",
            "--Werror",
            "--fail-on-incomplete-format",
        ]
        command += ["--dry-run", "--ferror-limit=3"] if args.check else ["-i"]
        result = subprocess.run([*command, *files], cwd=ROOT)
        if result.returncode == 0:
            print("C/H formatting check passed" if args.check else "C/H sources formatted")
        return result.returncode
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"format-c: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
