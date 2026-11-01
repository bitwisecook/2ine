#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Build unchanged SDK SIMPLE.C with the AUX fixture adapter and verify pixels."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--watcom", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    build, work = args.build.resolve(), args.work.resolve()
    watcom, sdk = args.watcom.resolve(), args.sdk.resolve()
    work.mkdir(parents=True, exist_ok=True)
    sample = sdk / "samples/opengl/auxdemo/simple.c"
    adapter = ROOT / "tests/programs/opengl_sdk_aux.c"
    binaries = watcom / "binl"
    if not os.access(binaries / "wcl386", os.X_OK):
        binaries = work / "host-tools"
        binaries.mkdir(exist_ok=True)
        for name in ("wcl386", "wcc386", "wlink", "wlink.lnk", "wlsystem.lnk"):
            target = binaries / name
            shutil.copyfile(watcom / "binl" / name, target)
            if not name.endswith(".lnk"):
                target.chmod(0o755)
    compiler = binaries / "wcl386"
    env = dict(
        os.environ, WATCOM=str(watcom), PATH=str(binaries) + os.pathsep + os.environ.get("PATH", "")
    )
    common = [
        str(compiler),
        "-q",
        "-bt=os2",
        "-dOS2",
        "-bm",
        f"-i={watcom / 'h/os2'}",
        f"-i={watcom / 'h'}",
        f"-i={sdk / 'h/gl'}",
    ]
    commands = [
        [*common, "-c", "-dmain=sdk_simple_main", "-fo=simple.obj", str(sample)],
        [*common, "-c", "-fo=adapter.obj", str(adapter)],
        [
            str(compiler),
            "-q",
            "-bt=os2",
            "-bm",
            "-l=os2v2_pm",
            "-fe=sdk-simple.exe",
            "simple.obj",
            "adapter.obj",
            str(watcom / "lib386/os2/opengl.lib"),
        ],
    ]
    for i, command in enumerate(commands):
        result = subprocess.run(command, cwd=work, env=env, capture_output=True, timeout=60)
        (work / f"build-{i}.log").write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"SDK build step {i}: {result.stdout!r} {result.stderr!r}")
    env.update(
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
        TWOINE_OPENGL_ABI="gold11",
        SDL_VIDEO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
    )
    if os.uname().sysname == "Linux":
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    command = [str(build / "lx_loader"), str(work / "sdk-simple.exe")]
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, timeout=30)
    (work / "stdout").write_bytes(result.stdout)
    (work / "stderr").write_bytes(result.stderr)
    evidence = {
        "backend": args.backend,
        "commands": [*commands, command],
        "exit": result.returncode,
        "inputs": {
            str(p): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [sample, adapter, watcom / "lib386/os2/opengl.lib"]
        },
        "executable_sha256": hashlib.sha256((work / "sdk-simple.exe").read_bytes()).hexdigest(),
    }
    (work / "results.json").write_text(json.dumps(evidence, indent=2) + "\n")
    if (
        result.returncode
        or b"SDK SIMPLE.C rendered white rectangle on black with Mesa" not in result.stdout
    ):
        raise RuntimeError(
            f"SDK guest exit={result.returncode}: {result.stderr!r} {result.stdout!r}"
        )
    print("SDK SIMPLE.C rendered white rectangle on black with Mesa")


if __name__ == "__main__":
    main()
