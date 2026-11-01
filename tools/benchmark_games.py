#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Run a complete Doom or GLQuake demo1 timedemo in a fresh working directory."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", choices=("doom", "glquake"))
    for name in ("build", "program", "data", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--config", type=Path, help="Doom default.cfg (required for Doom)")
    parser.add_argument("--backend", default="unicorn", choices=("unicorn", "native"))
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--timeout", type=float, default=480)
    args = parser.parse_args()
    if args.game == "doom" and args.config is None:
        parser.error("Doom requires --config so audio and rendering settings are explicit")
    if args.width < 320 or args.height < 200 or args.timeout <= 0:
        parser.error("Resolution must be at least 320x200 and timeout must be positive")
    build, program, data, work = (
        value.resolve() for value in (args.build, args.program, args.data, args.work)
    )
    work.mkdir(parents=True)
    result = {
        "game": args.game,
        "demo": "demo1",
        "backend": args.backend,
        "width": args.width,
        "height": args.height,
        "program_sha256": digest(program),
        "data_sha256": digest(data),
        "runtime_sha256": {
            path.name: digest(path)
            for path in sorted(build.glob("lib2ine.*"))
            if path.is_file() and path.suffix in (".so", ".dylib")
        },
    }
    if args.game == "glquake":
        (work / "id1").mkdir()
        shutil.copyfile(data, work / "id1/pak0.pak")
        command = [
            str(program),
            "-width",
            str(args.width),
            "-height",
            str(args.height),
            "-nomouse",
            "-frames",
            "1000",
            "+timedemo",
            "demo1",
        ]
    else:
        shutil.copyfile(data, work / "doom1.wad")
        config = args.config.read_bytes()
        result["config_sha256"] = hashlib.sha256(config).hexdigest()
        for key, value in (("screenwidth", args.width), ("screenheight", args.height)):
            config, count = re.subn(
                rb"(?m)^" + key.encode() + rb"[ \t]+[0-9]+",
                key.encode() + b"\t\t" + str(value).encode(),
                config,
            )
            if count != 1:
                raise ValueError(f"Expected exactly one {key} setting in --config")
        (work / "default.cfg").write_bytes(config)
        result["effective_config_sha256"] = hashlib.sha256(config).hexdigest()
        command = [str(program), "-timedemo", "demo1"]
    command.insert(0, str(build / "lx_loader"))
    result["command"] = command
    env = {**os.environ, "TWOINE_BACKEND": args.backend, "TWOINE_LIBPATH": str(build)}
    start = time.monotonic()
    with (work / "runtime.log").open("wb") as log:
        try:
            process = subprocess.run(
                command,
                cwd=work,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=args.timeout,
            )
            result["returncode"] = process.returncode
        except subprocess.TimeoutExpired:
            result["returncode"] = "timeout"
    result["wall_seconds"] = time.monotonic() - start
    text = (work / "runtime.log").read_text(errors="replace")
    if args.game == "glquake":
        match = re.search(r"(\d+) frames\s+([\d.]+) seconds\s+([\d.]+) fps", text)
        if match:
            result.update(frames=int(match[1]), seconds=float(match[2]), fps=float(match[3]))
        renderer = re.search(r"GL_RENDERER:\s*(.*)", text)
        if renderer:
            result["renderer"] = renderer[1].strip()
    else:
        match = re.search(r"timed (\d+) gametics in (\d+) realtics", text)
        if match and int(match[2]):
            result.update(
                gametics=int(match[1]),
                realtics=int(match[2]),
                fps=35 * int(match[1]) / int(match[2]),
            )
    (work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    if digest(program) != result["program_sha256"]:
        raise RuntimeError("The game executable changed during the benchmark")
    if result["returncode"] != 0 or "fps" not in result:
        raise SystemExit(f"Timedemo did not complete; see {work / 'runtime.log'}")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
