#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""A loader launched without a guest exits with usage before creating a CPU."""

import argparse
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()
    loader = args.build.resolve() / "lx_loader"
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith("TWOINE_") and key != "IS_2INE"
    }
    environment["TWOINE_BACKEND"] = "no-backend-needed-for-usage"
    result = subprocess.run([str(loader)], env=environment, capture_output=True, timeout=5)
    if (
        result.returncode != 1
        or b"USAGE:" not in result.stderr
        or b"<program.exe>" not in result.stderr
    ):
        raise RuntimeError(
            f"loader without a guest exit={result.returncode}\n"
            + result.stderr.decode(errors="replace")
        )
    print("Loader without a guest exits with usage before CPU initialization")


if __name__ == "__main__":
    main()
