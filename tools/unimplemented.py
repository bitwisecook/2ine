#!/usr/bin/env python3
# 2ine-derived; zlib-style license. See LICENSE.txt.
"""Filter os2imports output to services absent from the native export tables."""

from __future__ import annotations

import re
import sys
from collections.abc import Iterable, Iterator
from pathlib import Path

EXPORT = re.compile(
    r"\s*LX_NATIVE_EXPORT(?:16|32)?(?:_BRIDGED)?(?:_DIFFERENT_NAME)?\((\w+),\s*(?:\"[^\"]+\",\s*)?(\d+)\)"
)
DATA = re.compile(r"\s*LX_NATIVE_EXPORT_DATA\(\w+,\s*(\d+)\)")
FORWARDER = re.compile(r'\s*LX_NATIVE_FORWARDER\((\w+),\s*"(\w+)",\s*(\d+),\s*"(\w*)"\)')
FORWARDER_EXPORT = re.compile(r'\s*LX_NATIVE_EXPORT_FORWARDER\((\w+),\s*"[^"]+",\s*(\d+)\)')


def implemented(directory: Path) -> set[str]:
    """Return MODULE@ordinal for every export with a native implementation.

    A forwarder export counts when its target module implements the target
    entry, named by ordinal or by C symbol.
    """
    exports: set[str] = set()
    symbols: set[str] = set()
    forwarders: list[tuple[str, str, int, str]] = []
    for header in sorted(directory.rglob("*-lx.h")):
        module = header.name.removesuffix("-lx.h").upper()
        targets: dict[str, tuple[str, int, str]] = {}
        for line in header.read_text().splitlines():
            if match := EXPORT.match(line):
                exports.add(f"{module}@{match[2]}")
                symbols.add(f"{module}.{match[1]}")
            elif match := DATA.match(line):
                exports.add(f"{module}@{match[1]}")
            elif match := FORWARDER.match(line):
                targets[match[1]] = (match[2].upper(), int(match[3]), match[4])
            elif (match := FORWARDER_EXPORT.match(line)) and match[1] in targets:
                forwarders.append((f"{module}@{match[2]}", *targets[match[1]]))
    for export, target, ordinal, name in forwarders:
        if (ordinal and f"{target}@{ordinal}" in exports) or (
            not ordinal and f"{target}.{name}" in symbols
        ):
            exports.add(export)
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
        exports = implemented(Path(__file__).resolve().parents[1] / "os2")
        sys.stdout.writelines(missing(sys.stdin, exports))
    except OSError as error:
        print(f"unimplemented: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
