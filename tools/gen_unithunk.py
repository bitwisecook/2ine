#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Assemble the x86 guest universal thunk into a host-independent byte array."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="twoine-unithunk-") as directory:
        work = Path(directory)
        obj = work / "thunk.o"
        data = work / "thunk.bin"
        subprocess.run(
            ["as", "--32", str(ROOT / "runtime/x86/unithunk.s"), "-o", str(obj)], check=True
        )
        subprocess.run(
            ["objcopy", "-O", "binary", "-j", ".rodata", str(obj), str(data)], check=True
        )
        names = subprocess.check_output(["nm", "-n", str(obj)], text=True)
        labels = {
            name: int(address, 16)
            for address, _, name in (line.split() for line in names.splitlines())
        }
        blob = data.read_bytes()
    lines = [
        "/* Public domain. No rights reserved. Wholly AI-authored. */",
        "/* Generated from unithunk.s by tools/gen_unithunk.py. */",
        "/* clang-format off */",
        "static const unsigned char UniversalThunk[] = {",
    ]
    for offset in range(0, len(blob), 12):
        lines.append(
            "    " + ", ".join(f"0x{byte:02x}" for byte in blob[offset : offset + 12]) + ","
        )
    lines.append("};")
    for name in ("data", "flat_address", "flat_cs", "return_cs", "flat", "sel_to_flat"):
        lines.append(f"#define UNITHUNK_{name.upper()} {labels['twoine_unithunk_' + name]}")
    lines.append("/* clang-format on */")
    result = "\n".join(lines) + "\n"
    output = ROOT / "runtime/x86/unithunk_bytes.h"
    if args.check:
        if output.read_text() != result:
            raise SystemExit("unithunk_bytes.h is out of date")
    else:
        output.write_text(result)


if __name__ == "__main__":
    main()
