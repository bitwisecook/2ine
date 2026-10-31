#!/usr/bin/env python3
# 2ine-derived; zlib-style license. See LICENSE.txt.
"""Convert ndisasm output from stdin or files to C byte emissions."""

from __future__ import annotations

import fileinput
import re
import sys
from collections.abc import Iterable, Iterator


def convert(lines: Iterable[str]) -> Iterator[str]:
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        match = re.fullmatch(r"\S+\s+((?:[0-9a-fA-F]{2})+)\s+(.*)", line.rstrip("\n"))
        if match is None:
            raise ValueError(f"line {number}: expected ndisasm address, bytes and instruction")
        encoded, instruction = match.groups()
        for offset in range(0, len(encoded), 2):
            comment = f"  /* {instruction} */" if offset == 0 else ""
            yield f"    *(ptr++) = 0x{encoded[offset : offset + 2]};{comment}\n"


def main() -> int:
    try:
        with fileinput.input(files=sys.argv[1:] or ("-",), encoding="utf-8") as lines:
            sys.stdout.writelines(convert(lines))
    except (OSError, ValueError) as error:
        print(f"cvtasm: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
