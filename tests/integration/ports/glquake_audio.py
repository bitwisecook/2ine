#!/usr/bin/env python3
# Public domain. No rights reserved.
"""Verify game-generated spatial stereo audio through the native OS/2 DART port."""

import argparse
import array
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "program", "data", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument(
        "--unavailable", action="store_true", help="Verify startup without an audio device"
    )
    args = parser.parse_args()
    build, program, work = args.build.resolve(), args.program.resolve(), args.work.resolve()
    program_sha256 = hashlib.sha256(program.read_bytes()).hexdigest()
    work.mkdir(parents=True)
    (work / "id1").mkdir()
    shutil.copyfile(args.data, work / "id1/pak0.pak")
    audio = work / "audio.raw"
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
        SDL_VIDEO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
        SDL_AUDIO_DRIVER="2ine-no-such-driver" if args.unavailable else "disk",
        SDL_AUDIO_DISK_OUTPUT_FILE=str(audio),
        LIBGL_ALWAYS_SOFTWARE="1",
    )
    command = [
        str(build / "lx_loader"),
        str(program),
        "-width",
        "320",
        "-height",
        "240",
        "-nomouse",
        "-frames",
        "40" if args.backend == "unicorn" else "5000",
        "+map" if args.unavailable else "+playdemo",
        "e1m1" if args.unavailable else "demo1",
    ]
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, timeout=180)
    log = result.stdout + result.stderr
    (work / "runtime.log").write_bytes(log)
    assert result.returncode == 0, log[-2000:]
    assert hashlib.sha256(program.read_bytes()).hexdigest() == program_sha256, (
        "Executable changed during test"
    )
    if args.unavailable:
        assert b"S_Startup: SNDDMA_Init failed." in log
        assert b"GLQuake shutdown complete" in log
        assert b"OS/2 DART:" not in log
        (work / "results.json").write_text(
            json.dumps(
                {
                    "exit": 0,
                    "backend": args.backend,
                    "audio_unavailable": True,
                    "program_sha256": program_sha256,
                },
                indent=2,
            )
            + "\n"
        )
        print("GLQuake startup and clean shutdown without an audio device verified")
        return
    for fragment in (
        b"OS/2 DART: 22050 Hz, 16-bit stereo",
        b"Playing demo from demo1.dem",
        b"GLQuake shutdown complete",
    ):
        assert fragment in log, fragment
    data = audio.read_bytes()
    assert len(data) > 48000 and len(data) % 8 == 0
    samples = array.array("f")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    assert all(math.isfinite(value) for value in samples)
    nonzero = sum(abs(value) > 1e-5 for value in samples)
    stereo = sum(
        abs(left - right) > 1e-5 for left, right in zip(samples[::2], samples[1::2], strict=True)
    )
    assert nonzero > 1000 and stereo > 1000, (nonzero, stereo)
    peak = max(map(abs, samples))
    assert 0 < peak <= 1
    report = {
        "exit": 0,
        "backend": args.backend,
        "bytes": len(data),
        "format": "F32LE stereo 48000 Hz",
        "nonzero_samples": nonzero,
        "different_stereo_frames": stereo,
        "peak": peak,
        "program_sha256": program_sha256,
    }
    (work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("GLQuake DART spatial stereo game audio and clean shutdown verified")


if __name__ == "__main__":
    main()
