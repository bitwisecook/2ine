#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Create persistent 2ine environments and run original OS/2 installers and apps."""

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import string
import subprocess
import sys
import tempfile
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENV = {
    "OS2PATH": r"C:\OS2;C:\OS2\SYSTEM",
    "LIBPATH": r"C:\OS2\DLL",
    "DPATH": r"C:\OS2;C:\OS2\SYSTEM",
    "TEMP": r"C:\TEMP",
    "TMP": r"C:\TEMP",
    "USER_INI": r"C:\OS2\OS2.INI",
    "SYSTEM_INI": r"C:\OS2\OS2SYS.INI",
}


def bottles_home():
    return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "2ine/bottles"


def save(path, data):
    with tempfile.NamedTemporaryFile(
        mode="w", dir=path.parent, prefix=path.name + ".", delete=False
    ) as output:
        temporary = Path(output.name)
        try:
            output.write(json.dumps(data, indent=2) + "\n")
            output.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)


def load(bottle):
    data = json.loads((bottle / "bottle.json").read_text())
    if data.get("version") != 1 or "C" not in data.get("drives", {}):
        raise ValueError("unsupported or invalid bottle.json")
    return data


def init(bottle):
    bottle.mkdir(parents=True, exist_ok=True)
    if (bottle / "bottle.json").exists():
        load(bottle)
        return
    if any(bottle.iterdir()):
        raise ValueError("a new bottle must be an empty directory")
    for directory in ("drive_c/OS2/DLL", "drive_c/OS2/SYSTEM", "drive_c/TEMP", "logs", "ipc"):
        (bottle / directory).mkdir(parents=True)
    save(
        bottle / "bottle.json",
        {
            "version": 1,
            "drives": {"C": "drive_c"},
            "environment": {},
            "system": {},
            "runtime": {},
            "applications": {},
        },
    )
    lines = [
        f"SET {'PATH' if key == 'OS2PATH' else key}={value}"
        if key != "LIBPATH"
        else f"LIBPATH={value}"
        for key, value in DEFAULT_ENV.items()
    ]
    (bottle / "drive_c/CONFIG.SYS").write_text("\r\n".join(lines) + "\r\n")
    write_config(bottle, load(bottle))


def drive_roots(bottle, data):
    roots = {}
    for letter, value in data["drives"].items():
        if len(letter) != 1 or letter not in string.ascii_uppercase or not isinstance(value, str):
            raise ValueError("invalid drive mapping")
        path = (bottle / value).resolve(strict=True)
        if not path.is_dir() or any(ch in str(path) for ch in "\r\n"):
            raise ValueError(f"invalid directory for drive {letter}")
        roots[letter] = path
    return roots


def write_config(bottle, data):
    roots = drive_roots(bottle, data)
    lines = [f"mountpoint.{letter} = {roots.get(letter, '')}" for letter in string.ascii_uppercase]
    for key, value in data.get("system", {}).items():
        if key not in ("trace_native", "trace_events", "beep_volume"):
            raise ValueError(f"unknown system setting: {key}")
        if any(ch in str(value) for ch in "\r\n"):
            raise ValueError("a setting must fit on one line")
        if key == "beep_volume":
            volume = float(value)
            if not math.isfinite(volume) or not 0 <= volume <= 1:
                raise ValueError("beep_volume must be between 0 and 1")
        elif str(value).lower() not in (
            "1",
            "yes",
            "y",
            "true",
            "t",
            "on",
            "0",
            "no",
            "n",
            "false",
            "f",
            "off",
        ):
            raise ValueError(f"invalid boolean setting: {key}")
        lines.append(f"system.{key} = {value}")
    (bottle / "2ine.cfg").write_text("\n".join(lines) + "\n")


def case_path(path):
    """Resolve existing components case-insensitively; retain new file names."""
    current = Path(path.anchor)
    for part in path.parts[1:]:
        candidate = current / part
        if not candidate.exists() and current.is_dir():
            candidate = next(
                (p for p in current.iterdir() if p.name.lower() == part.lower()), candidate
            )
        current = candidate
    return current


def host_path(name, bottle, data):
    roots = drive_roots(bottle, data)
    if re.match(r"^[a-zA-Z]:[\\/]", name):
        letter = name[0].upper()
        if letter not in roots:
            raise ValueError(f"drive {letter}: is not mapped")
        path = roots[letter] / name[3:].replace("\\", "/")
    else:
        path = Path(name).absolute()
    path = case_path(path).resolve()
    if not any(path.is_relative_to(root) for root in roots.values()):
        raise ValueError(f"path is outside the mapped drives: {name}; map its directory first")
    return path


def guest_path(path, bottle, data):
    matches = [
        (len(root.parts), letter, root)
        for letter, root in drive_roots(bottle, data).items()
        if path.is_relative_to(root)
    ]
    if not matches:
        raise ValueError(f"path is outside the mapped drives: {path}")
    _, letter, root = max(matches)
    return letter + ":\\" + ("" if path == root else str(path.relative_to(root)).replace("/", "\\"))


def environment(bottle, data, build, backend):
    env = dict(os.environ)
    for key in (
        "IS_2INE",
        "TWOINE_CHILD_ARGUMENTS",
        "TWOINE_LIBPATH",
        "TWOINE_CONFIG",
        "TWOINE_BOTTLE",
        "TWOINE_USER_PROFILE",
        "TWOINE_SYSTEM_PROFILE",
        "TWOINE_REGISTRY",
        "TWOINE_OBJECTS",
        "TWOINE_IPC_DIR",
    ):
        env.pop(key, None)
    guest_environment = dict(DEFAULT_ENV)
    c = drive_roots(bottle, data)["C"]
    config_sys = c / "CONFIG.SYS"
    if config_sys.is_file():
        for line in config_sys.read_text(encoding="latin1").splitlines():
            line = line.strip()
            if line.upper().startswith("SET "):
                key, separator, value = line[4:].partition("=")
            elif line.upper().startswith("LIBPATH="):
                key, separator, value = line.partition("=")
            else:
                continue
            if separator:
                key = key.strip().upper()
                guest_environment["OS2PATH" if key == "PATH" else key] = value.strip()
    guest_environment.update(data["environment"])
    for key, value in guest_environment.items():
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key) or not isinstance(value, str):
            raise ValueError("invalid environment entry")
        if key in (
            "HOME",
            "PATH",
            "IS_2INE",
            "TWOINE_CHILD_ARGUMENTS",
            "TWOINE_CONFIG",
            "TWOINE_BOTTLE",
            "TWOINE_IPC_DIR",
            "TWOINE_USER_PROFILE",
            "TWOINE_SYSTEM_PROFILE",
            "TWOINE_REGISTRY",
            "TWOINE_OBJECTS",
            "TWOINE_LIBPATH",
            "TWOINE_BACKEND",
        ) or key.startswith(("LD_", "DYLD_")):
            raise ValueError(f"use OS2PATH/LIBPATH or the launcher options for {key}")
        env[key] = value
    c = drive_roots(bottle, data)["C"]
    selected_backend = (
        backend or data.get("runtime", {}).get("backend") or env.get("TWOINE_BACKEND") or "default"
    )
    if selected_backend not in ("native", "unicorn", "default"):
        raise ValueError("runtime.backend must be native or unicorn")
    identity = hashlib.sha256()
    for library in runtime_files(build):
        identity.update(library.name.encode())
        identity.update(hashlib.sha256(library.read_bytes()).digest())
    runtime_id = identity.hexdigest()
    paths = [str(c / "OS2/DLL/2INE" / selected_backend / runtime_id[:16]), str(build)]
    for name in env.get("LIBPATH", "").split(";"):
        if name:
            paths.append("." if name == "." else str(host_path(name, bottle, data)))
    env.update(
        TWOINE_IPC_DIR=str(bottle / "ipc"),
        TWOINE_BOTTLE=str(bottle),
        TWOINE_CONFIG=str(bottle / "2ine.cfg"),
        TWOINE_USER_PROFILE=str(host_path(env["USER_INI"], bottle, data)),
        TWOINE_SYSTEM_PROFILE=str(host_path(env["SYSTEM_INI"], bottle, data)),
        TWOINE_REGISTRY=str(c / "OS2/REGISTRY.INI"),
        TWOINE_OBJECTS=str(c / "OS2/OBJECTS.INI"),
        TWOINE_LIBPATH=os.pathsep.join(paths),
        TWOINE_RUNTIME_ID=runtime_id,
        LD_LIBRARY_PATH=paths[0]
        + os.pathsep
        + str(build)
        + (os.pathsep + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else ""),
    )
    backend = backend or data.get("runtime", {}).get("backend")
    if backend:
        if backend not in ("native", "unicorn"):
            raise ValueError("runtime.backend must be native or unicorn")
        env["TWOINE_BACKEND"] = backend
    return env


def runtime_files(build):
    return sorted([*build.glob("lib*.so"), *build.glob("lx_loader")])


def sync_runtime(build, directory):
    """Copy the loader and API modules; existing mappings retain their inode."""
    directory.mkdir(parents=True, exist_ok=True)
    for source in runtime_files(build):
        destination = directory / source.name
        if (
            destination.is_file()
            and hashlib.sha256(destination.read_bytes()).digest()
            == hashlib.sha256(source.read_bytes()).digest()
        ):
            continue
        with tempfile.NamedTemporaryFile(dir=directory, delete=False) as output:
            temporary = Path(output.name)
        try:
            shutil.copy2(source, temporary)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)


def run(args, bottle, data):
    build = args.build.resolve()
    loader = build / "lx_loader"
    if not loader.is_file():
        raise ValueError(f"no 2ine loader at {loader}; specify --build")
    program = host_path(args.program, bottle, data)
    if not program.is_file():
        raise ValueError(f"program does not exist: {program}")
    cwd = host_path(args.cwd, bottle, data) if args.cwd else program.parent
    write_config(bottle, data)
    started = datetime.now(UTC).isoformat()
    env = environment(bottle, data, build, args.backend)
    runtime = Path(env["TWOINE_LIBPATH"].split(os.pathsep)[0])
    sync_runtime(build, runtime)
    command = [str(runtime / "lx_loader"), guest_path(program, bottle, data), *args.arguments]
    result = subprocess.run(command, cwd=cwd, env=env)
    if args.action == "install":
        # A successful process exit is recorded independently of application qualification.
        save(
            bottle / "logs/last-install.json",
            {
                "command": command,
                "program_sha256": hashlib.sha256(program.read_bytes()).hexdigest(),
                "runtime_id": env["TWOINE_RUNTIME_ID"],
                "cwd": str(cwd),
                "started": started,
                "finished": datetime.now(UTC).isoformat(),
                "exit_code": result.returncode,
            },
        )
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bottle",
        type=Path,
        default=Path(os.environ.get("TWOINE_BOTTLE", bottles_home() / "default")),
    )
    parser.add_argument(
        "--build", type=Path, default=Path(os.environ.get("TWOINE_BUILD", ROOT / "build"))
    )
    parser.add_argument("--backend", choices=("native", "unicorn"))
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("init", help="create an empty bottle (existing bottles are preserved)")
    sub.add_parser("list", help="list bottles in the default data directory")
    sub.add_parser("show", help="show the persistent configuration")
    sub.add_parser("apps", help="list saved application launchers")
    app = sub.add_parser("add-app", help="save an installed application's command")
    app.add_argument("name")
    app.add_argument("program")
    app.add_argument("arguments", nargs=argparse.REMAINDER)
    launch = sub.add_parser("launch", help="run a saved application")
    launch.add_argument("name")
    launch.add_argument("arguments", nargs=argparse.REMAINDER)
    mapping = sub.add_parser("map", help="map a host directory as an OS/2 drive")
    mapping.add_argument("drive")
    mapping.add_argument("directory", type=Path)
    unmap = sub.add_parser("unmap")
    unmap.add_argument("drive")
    setting = sub.add_parser(
        "config", help="set system.trace_native, system.beep_volume, or env.NAME"
    )
    setting.add_argument("key")
    setting.add_argument("value")
    for name in ("run", "install"):
        launch = sub.add_parser(name, help="run an original guest executable inside the bottle")
        launch.add_argument("--cwd", help="guest or mapped host working directory")
        launch.add_argument("program")
        launch.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    bottle = args.bottle.resolve()
    try:
        if args.action == "list":
            for path in sorted(bottles_home().glob("*/bottle.json")):
                print(path.parent)
            return 0
        if args.action == "init":
            init(bottle)
            print(bottle)
            return 0
        data = load(bottle)
        if args.action == "show":
            print(json.dumps(data, indent=2))
        elif args.action == "apps":
            print(json.dumps(data.get("applications", {}), indent=2))
        elif args.action == "add-app":
            program = host_path(args.program, bottle, data)
            if not program.is_file():
                raise ValueError(f"program does not exist: {program}")
            data.setdefault("applications", {})[args.name] = {
                "program": guest_path(program, bottle, data),
                "arguments": args.arguments,
            }
            save(bottle / "bottle.json", data)
        elif args.action == "launch":
            app = data.get("applications", {})[args.name]
            args.program = app["program"]
            args.arguments = [*app["arguments"], *args.arguments]
            args.cwd = None
            return run(args, bottle, data)
        elif args.action in ("map", "unmap"):
            letter = args.drive.upper().rstrip(":")
            if letter not in string.ascii_uppercase or len(letter) != 1 or letter == "C":
                raise ValueError("choose a drive A-Z other than the bottle's C: drive")
            if args.action == "map":
                data["drives"][letter] = str(args.directory.resolve(strict=True))
            else:
                data["drives"].pop(letter, None)
            write_config(bottle, data)
            save(bottle / "bottle.json", data)
        elif args.action == "config":
            group, _, key = args.key.partition(".")
            if group not in ("env", "system", "runtime") or not key:
                raise ValueError("use env.NAME, system.NAME or runtime.backend")
            if group == "runtime" and key != "backend":
                raise ValueError("unknown runtime setting")
            data.setdefault("environment" if group == "env" else group, {})[key] = args.value
            environment(bottle, data, args.build.resolve(), args.backend)
            write_config(bottle, data)
            save(bottle / "bottle.json", data)
        else:
            return run(args, bottle, data)
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"2ine-bottle: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
