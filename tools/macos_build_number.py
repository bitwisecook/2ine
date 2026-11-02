#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Reserve monotonically increasing macOS bundle builds in persistent, locked state."""

from __future__ import annotations

import argparse
import os
import plistlib
import re
import tempfile
from pathlib import Path


def counter_directory() -> Path:
    override = os.environ.get("TWOINE_BUILD_COUNTER_DIR")
    return Path(override) if override else Path.home() / "Library/Application Support/2ine/builds"


def atomic_write(path: Path, data: bytes, mode: int = 0o600, temporary: Path | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if temporary is None:
        descriptor, name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
        temporary = Path(name)
    else:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, mode)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            os.fchmod(stream.fileno(), mode)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def next_build_number(counter: Path | None = None, minimum: int = 1) -> int:
    """Reserve a number before emitting a bundle; failed builds never reuse it."""
    import fcntl

    if minimum < 0:
        raise ValueError("The minimum build number must be non-negative")
    counter = counter if counter is not None else counter_directory() / "build-number"
    counter.parent.mkdir(parents=True, exist_ok=True)
    # A separate, stable lock inode remains valid when the counter is atomically replaced.
    with counter.with_name(counter.name + ".lock").open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        previous = 1
        if counter.exists():
            stored = counter.read_text().strip()
            if not re.fullmatch(r"[1-9][0-9]*", stored):
                raise ValueError(f"Invalid build counter; refusing to reset it: {counter}")
            previous = int(stored)
        number = max(previous, minimum) + 1
        atomic_write(counter, f"{number}\n".encode("ascii"))
    return number


def bundle_build(plist: Path) -> int:
    if not plist.exists():
        return 1
    value = str(plistlib.loads(plist.read_bytes()).get("CFBundleVersion", "1"))
    if not re.fullmatch(r"[1-9][0-9]*", value):
        raise ValueError(f"Expected an integer CFBundleVersion in {plist}: {value}")
    return int(value)


def stamp(plist: Path, template: Path | None = None, counter: Path | None = None) -> int:
    info = plistlib.loads((template if template is not None else plist).read_bytes())
    minimum = max(
        bundle_build(plist), bundle_build(Path("/Applications/2ine.app/Contents/Info.plist"))
    )
    number = next_build_number(counter, minimum)
    info["CFBundleVersion"] = str(number)
    # Xcode declares both fixed paths to its script sandbox. The build system
    # serializes writers to this derived product; the persistent counter has its own lock.
    atomic_write(
        plist, plistlib.dumps(info), mode=0o644, temporary=plist.with_name(plist.name + ".tmp")
    )
    return number


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plist", required=True, type=Path)
    parser.add_argument("--template", type=Path)
    parser.add_argument("--counter", type=Path)
    args = parser.parse_args()
    print(stamp(args.plist, args.template, args.counter))


if __name__ == "__main__":
    main()
