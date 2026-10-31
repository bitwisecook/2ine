#!/usr/bin/env python3
# 2ine-derived; zlib-style license. See LICENSE.txt.
"""Filter os2imports output to services absent from the native export tables."""

from __future__ import annotations

import re
import sys
from collections.abc import Iterable, Iterator
from pathlib import Path


def implemented(directory: Path) -> set[str]:
    exports: set[str] = set()
    for header in sorted(directory.glob("*-lx.h")):
        module = header.name.removesuffix("-lx.h").upper()
        for line in header.read_text().splitlines():
            match = re.match(r"\s*LX_NATIVE_EXPORT\d*\(.*?, (\d+)\)", line)
            if match:
                exports.add(f"{module}@{match[1]}")
    return exports


def missing(lines: Iterable[str], exports: set[str]) -> Iterator[str]:
    for raw in lines:
        line = raw.rstrip("\n")
        match = re.match(r".*? \((.*?@\d+)\)", line)
        if match and match[1] in exports:
            continue
        yield re.sub(r"^(...)32", r"\1", line) + "\n"


def main() -> int:
    try:
        exports = implemented(Path(__file__).resolve().parents[1] / "os2libs/generated")
        sys.stdout.writelines(missing(sys.stdin, exports))
    except OSError as error:
        print(f"unimplemented: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
