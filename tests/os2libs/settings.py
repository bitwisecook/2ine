#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise settings with fresh process state and generated OS/2 binary INI fixtures."""

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-settings-") as temporary:
        work = Path(temporary)
        # Native INI: header, one application record, one key record, names and data.
        fixture = bytearray(20 + 20 + 24)
        fixture.extend(b"Native\0Key\0value\0")
        struct.pack_into("<III", fixture, 0, 0xFFFFFFFF, 20, len(fixture))
        struct.pack_into("<II", fixture, 20, 0, 40)
        struct.pack_into("<H", fixture, 32, 7)
        struct.pack_into("<I", fixture, 36, 64)
        struct.pack_into("<H", fixture, 48, 4)
        struct.pack_into("<I", fixture, 52, 71)
        struct.pack_into("<H", fixture, 56, 6)
        struct.pack_into("<I", fixture, 60, 75)
        (work / "native.ini").write_bytes(fixture)
        (work / "truncated.ini").write_bytes(fixture[:-1])
        struct.pack_into("<I", fixture, 40, 40)
        (work / "cycle.ini").write_bytes(fixture)
        env = dict(
            os.environ,
            TWOINE_USER_PROFILE=str(work / "user.profile"),
            TWOINE_SYSTEM_PROFILE=str(work / "system.profile"),
            TWOINE_REGISTRY=str(work / "registry.profile"),
            TWOINE_OBJECTS=str(work / "objects.profile"),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
        )
        for mode in ("write", "read"):
            subprocess.run(
                [str(executable), mode, "native.ini"], cwd=work, env=env, check=True, timeout=15
            )


if __name__ == "__main__":
    main()
