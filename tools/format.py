#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Format or check every maintained source language with its pinned formatter.

Each language has one formatter and one configuration file at the repository root:

    C, C++, Objective-C   clang-format (tools/format-c.py, .clang-format)
    Python                ruff format and ruff check (pyproject.toml)
    Swift                 swift-format (.swift-format)
    CMake                 gersemi (.gersemirc)

Python formatters run through uvx so the pinned version is used without
installing it into the development environment.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUFF = "ruff@0.15.20"
GERSEMI = "gersemi@0.29.2"
LANGUAGES = ("c", "python", "swift", "cmake")

# Third-party, reference and generated trees keep the layout of their origin.
EXCLUDED_PREFIXES = ("ball/vendor/", "research/", "third_party/", "build/")


def tracked(*patterns: str) -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *patterns],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    return sorted(
        name
        for name in result.stdout.splitlines()
        if not name.startswith(EXCLUDED_PREFIXES) and (ROOT / name).is_file()
    )


def uvx(package: str, *arguments: str) -> list[str]:
    if shutil.which("uvx") is None:
        raise FileNotFoundError("uvx is required to run the pinned Python formatters")
    return ["uvx", package, *arguments]


def commands(language: str, check: bool) -> list[list[str]]:
    if language == "c":
        return [[sys.executable, "tools/format-c.py", "--check" if check else "--write"]]
    if language == "python":
        lint = ["check", "."] if check else ["check", "--fix", "."]
        layout = ["format", "--check", "."] if check else ["format", "."]
        return [uvx(RUFF, *layout), uvx(RUFF, *lint)]
    if language == "swift":
        files = tracked("*.swift")
        if shutil.which("swift-format") is None:
            raise FileNotFoundError("swift-format is required; it ships with the Swift toolchain")
        configuration = ["--configuration", ".swift-format"]
        if check:
            return [["swift-format", "lint", "--strict", *configuration, *files]]
        return [["swift-format", "format", "--in-place", *configuration, *files]]
    if language == "cmake":
        files = tracked("CMakeLists.txt", "*/CMakeLists.txt", "*.cmake")
        return [uvx(GERSEMI, "--check" if check else "-i", *files)]
    raise ValueError(f"unknown language: {language}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="report differences without writing")
    mode.add_argument("--write", action="store_true", help="format sources in place")
    parser.add_argument(
        "languages",
        nargs="*",
        choices=LANGUAGES,
        default=list(LANGUAGES),
        help="languages to process; defaults to all",
    )
    args = parser.parse_args()
    failed = []
    for language in args.languages:
        try:
            for command in commands(language, args.check):
                if subprocess.run(command, cwd=ROOT).returncode != 0:
                    failed.append(language)
                    break
        except (OSError, ValueError) as error:
            print(f"format: {language}: {error}", file=sys.stderr)
            failed.append(language)
    if failed:
        print(f"format: failed for {', '.join(failed)}", file=sys.stderr)
        return 1
    print("formatting check passed" if args.check else "sources formatted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
