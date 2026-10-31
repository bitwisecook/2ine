#!/usr/bin/env python3
# 2ine-derived; zlib-style license. See LICENSE.txt.
"""Resolve lx_dump NE/LX import ordinals using the bundled symbol catalogue."""

from __future__ import annotations

import fileinput
import json
import re
import sys
from collections.abc import Iterable
from pathlib import Path
from typing import cast

Catalogue = dict[str, dict[int, str]]


def load_catalogue(path: Path) -> Catalogue:
    raw: object = json.loads(path.read_text())
    if not isinstance(raw, dict):
        raise ValueError("expected a module catalogue object")
    catalogue: Catalogue = {}
    for module, value in cast(dict[str, object], raw).items():
        if module == "_notice":
            continue
        if not isinstance(value, dict):
            raise ValueError(f"{module}: expected ordinal table")
        symbols: dict[int, str] = {}
        for ordinal, symbol in cast(dict[str, object], value).items():
            if not isinstance(symbol, str):
                raise ValueError(f"{module}@{ordinal}: expected symbol name")
            symbols[int(ordinal)] = symbol
        catalogue[module] = symbols
    return catalogue


def resolve(lines: Iterable[str], catalogue: Catalogue) -> tuple[list[str], list[str]]:
    modules: dict[int, str] = {}
    imports: set[int] = set()
    output: list[str] = []
    errors: list[str] = []
    stream = iter(lines)
    for raw in stream:
        line = raw.rstrip("\n")
        match = re.fullmatch(r"\s*Fixup type: IMPORTORDINAL \(module (\d+), ordinal (\d+)\)", line)
        if match:
            module, ordinal = map(int, match.groups())
            imports.add((module << 16) | ordinal)
        elif re.match(r"\s*Fixup type: IMPORTNAME \(module (\d+), ordinal (\d+)\)", line):
            output.append("WARNING: Saw IMPORTNAME, currently unsupported\n")
        elif line == "Import by ordinal fixup record:":
            module_line = next(stream, "").rstrip("\n")
            ordinal_line = next(stream, "").rstrip("\n")
            module_match = re.fullmatch(r"Module ordinal:\s*(\d+)", module_line)
            ordinal_match = re.fullmatch(r"Import ordinal:\s*(\d+)", ordinal_line)
            if module_match and ordinal_match:
                imports.add((int(module_match[1]) << 16) | int(ordinal_match[1]))
        elif re.match(r"(?:Module reference table|Import modules) \(", line):
            for entry in stream:
                reference = re.fullmatch(r"\s*(\d+):\s*(.*?)", entry.rstrip("\n"))
                if reference is None:
                    break
                modules[int(reference[1])] = reference[2].upper()
    unknown: set[int] = set()
    # Lexical decimal ordering keeps import reports deterministic.
    for packed in sorted(imports, key=str):
        module = (packed >> 16) & 0xFFFF
        if module in unknown:
            continue
        name = modules.get(module)
        if name is None:
            errors.append(f"*** UNKNOWN MODULE ORDINAL {module}!\n")
            unknown.add(module)
        elif name not in catalogue:
            errors.append(f"*** UNKNOWN MODULE {name}!\n")
            unknown.add(module)
    for packed in sorted(imports, key=str):
        module, ordinal = (packed >> 16) & 0xFFFF, packed & 0xFFFF
        if module in unknown:
            continue
        name = modules[module]
        symbol = catalogue[name].get(ordinal)
        if symbol is None:
            errors.append(f"*** UNKNOWN ORDINAL {ordinal} IN MODULE {name}!\n")
        else:
            output.append(f"{symbol} ({name}@{ordinal})\n")
    return output, errors


def main() -> int:
    try:
        catalogue = load_catalogue(Path(__file__).parent / "data/os2imports.json")
        with fileinput.input(files=sys.argv[1:] or ("-",), encoding="utf-8") as lines:
            output, errors = resolve(lines, catalogue)
        sys.stdout.writelines(output)
        sys.stderr.writelines(errors)
    except (OSError, ValueError) as error:
        print(f"os2imports: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
