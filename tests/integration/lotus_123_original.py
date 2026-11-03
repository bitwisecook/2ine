#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check original 1-2-3 startup with user-supplied SOM DLLs in an isolated ball."""

import argparse
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SOM_MODULES = ("SOM", "SOMD", "SOMSEC", "SOMEM", "SOMS", "SOMU", "SOMTC", "SOMIR")


def original_libraries(root):
    found = {}
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        name = path.name.upper().split("OS2_DLL_")[-1]
        if name not in {module + ".DLL" for module in SOM_MODULES}:
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        previous = found.get(name)
        if previous and previous[1] != digest:
            raise ValueError("Different original DLL versions found: " + name)
        found[name] = (path, digest)
    missing = [module + ".DLL" for module in SOM_MODULES if module + ".DLL" not in found]
    if missing:
        raise ValueError("Original OS/2 SOM files missing: " + ", ".join(missing))
    return found


def stop_group(process):
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def main():
    sys.path.insert(0, str(ROOT))
    from tools import ball as ball_api

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ball", required=True, type=Path)
    parser.add_argument("--os2-dlls", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    work = args.work.resolve()
    if work.exists() and any(work.iterdir()):
        parser.error("--work must be a new or empty directory")
    libraries = original_libraries(args.os2_dlls.resolve())
    work.mkdir(parents=True, exist_ok=True)
    ball = work / "Probe.2ineball"

    def ignore(directory, names):
        return [
            name
            for name in names
            if name == "logs" or (Path(directory).name.upper() == "DLL" and name.upper() == "2INE")
        ]

    shutil.copytree(args.ball.resolve(), ball, symlinks=True, ignore=ignore)
    manifest_path = ball / "ball.json"
    manifest = json.loads(manifest_path.read_text())
    # All runtime writes stay in the copied C: drive; detached media and external
    # mappings from the supplied ball are not inherited by the probe.
    manifest["drives"] = {"C": "drive_c"}
    manifest["runtime"] = {}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    destination = ball / "drive_c/OS2/DLL"
    destination.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name, (source, digest) in libraries.items():
        shutil.copy2(source, destination / name)
        hashes[name] = {"source": str(source), "sha256": digest}
    (work / "original-inputs.json").write_text(json.dumps(hashes, indent=2) + "\n")
    plan = ball_api.native_request(
        {
            "action": "prepare-launch",
            "ball": str(ball),
            "build": str(args.build.resolve()),
            "backend": args.backend,
            "arguments": ["run", "--cwd", "C:\\LOTUSW4", "C:\\LOTUSW4\\123\\123W.EXE"],
        }
    )
    (work / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    environment = dict(os.environ)
    for key in plan["unsetEnvironment"]:
        environment.pop(key, None)
    environment.update(plan["environment"])
    environment.update(
        TRACE_NATIVE="1", TRACE_EVENTS="1", SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy"
    )
    error_path = work / "stderr.log"
    ready = False
    with (work / "stdout.log").open("wb") as output, error_path.open("wb") as errors:
        process = subprocess.Popen(
            plan["command"],
            cwd=plan["cwd"],
            env=environment,
            stdout=output,
            stderr=errors,
            start_new_session=True,
        )
        try:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline and process.poll() is None:
                text = error_path.read_text(errors="replace")
                title = text.find("'1-2-3'")
                if title >= 0 and "WinGetMsg(" in text[title:]:
                    ready = True
                    break
                time.sleep(0.1)
        finally:
            stop_group(process)
    text = error_path.read_text(errors="replace")
    failures = ("Missing import", "Failed to load dependency", "2ine execution: CPU stop")
    report = {
        "startupCheckpoint": ready,
        "returncodeAfterStop": process.returncode,
        "backend": args.backend,
        "originalSOMModules": list(hashes),
    }
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    if not ready or any(marker in text for marker in failures):
        raise RuntimeError("Original 1-2-3 startup checkpoint failed; see " + str(error_path))
    print("Original 1-2-3 with the IBM SOM runtime created its window and entered its message loop")


if __name__ == "__main__":
    main()
