#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""CLI adapter for the shared native C ball engine."""

import argparse
import ctypes
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_LIBRARY = None
_BUILD_HINT = None


def configure_library(build):
    global _BUILD_HINT
    _BUILD_HINT = Path(build)


def native_request(message):
    global _LIBRARY
    if _LIBRARY is None:
        candidates = [os.environ.get("TWOINE_BALL_LIBRARY", "")]
        for directory in (
            Path(_BUILD_HINT) if _BUILD_HINT else ROOT / "build",
            Path(os.environ.get("TWOINE_BUILD", ROOT / "build")),
            ROOT / "build-ball-engine",
            ROOT / "build-portable",
        ):
            for subdirectory in (directory, directory / "Release", directory / "Debug"):
                candidates.extend(
                    str(subdirectory / name)
                    for name in ("libtwoine_ball.dylib", "libtwoine_ball.so", "twoine_ball.dll")
                )
        path = next((p for p in candidates if p and Path(p).is_file()), None)
        if path is None:
            raise ValueError(
                "Build the shared ball engine: cmake -S ball -B build-ball-engine && cmake --build build-ball-engine"
            )
        library = ctypes.CDLL(path)
        library.tb_request.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_void_p),
        ]
        library.tb_request.restype = ctypes.c_int
        library.tb_free.argtypes = [ctypes.c_void_p]
        library.tb_free.restype = None
        _LIBRARY = library
    result, error = ctypes.c_void_p(), ctypes.c_void_p()
    status = _LIBRARY.tb_request(
        json.dumps(message).encode(), ctypes.byref(result), ctypes.byref(error)
    )
    try:
        if status:
            raise ValueError(ctypes.string_at(error).decode() if error else "Ball operation failed")
        return json.loads(ctypes.string_at(result))
    finally:
        _LIBRARY.tb_free(result)
        _LIBRARY.tb_free(error)


def balls_home():
    return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "2ine/balls"


def save(path, data):
    # Generic frontend preferences/fixture writer; ball mutations use the C API.
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


def init(ball):
    native_request({"action": "command", "ball": str(ball), "arguments": ["init"]})


def load(ball):
    return native_request({"action": "snapshot", "ball": str(ball)})["manifest"]


def drive_roots(ball, data):
    result = native_request({"action": "snapshot", "ball": str(ball), "manifest": data})
    return {key: Path(value) for key, value in result["roots"].items()}


def write_config(ball, data):
    native_request({"action": "write-config", "ball": str(ball), "manifest": data})


def host_path(name, ball, data):
    return Path(
        native_request(
            {"action": "host-path", "ball": str(ball), "manifest": data, "path": str(name)}
        )
    )


def guest_path(path, ball, data):
    return native_request(
        {"action": "guest-path", "ball": str(ball), "manifest": data, "path": str(path)}
    )


def environment(ball, data, build, backend):
    result = dict(os.environ)
    for key in native_request({"action": "unset-environment"}):
        result.pop(key, None)
    result.update(
        native_request(
            {
                "action": "environment",
                "ball": str(ball),
                "manifest": data,
                "build": str(build),
                "backend": backend,
            }
        )
    )
    return result


def runtime_files(build):
    return [Path(p) for p in native_request({"action": "runtime-files", "build": str(build)})]


def sync_runtime(build, directory):
    native_request({"action": "sync-runtime", "build": str(build), "directory": str(directory)})


def run(args, ball, data):
    if args.action == "launch":
        arguments = ["launch", args.name, *args.arguments]
    else:
        arguments = [
            args.action,
            *(["--cwd", args.cwd] if args.cwd else []),
            args.program,
            *args.arguments,
        ]
    plan = native_request(
        {
            "action": "prepare-launch",
            "ball": str(ball),
            "manifest": data,
            "build": str(args.build.resolve()),
            "backend": args.backend,
            "arguments": arguments,
        }
    )
    env = dict(os.environ)
    for key in plan["unsetEnvironment"]:
        env.pop(key, None)
    env.update(plan["environment"])
    result = subprocess.run(plan["command"], cwd=Path(plan["cwd"]), env=env)
    if plan["installer"]:
        native_request(
            {
                "action": "record-install",
                "ball": str(ball),
                "receipt": plan["receipt"],
                "programHost": plan["programHost"],
                "exitCode": result.returncode,
            }
        )
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ball",
        type=Path,
        default=Path(os.environ.get("TWOINE_BALL", balls_home() / "default")),
    )
    parser.add_argument(
        "--build", type=Path, default=Path(os.environ.get("TWOINE_BUILD", ROOT / "build"))
    )
    parser.add_argument("--backend", choices=("native", "unicorn"))
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("init", help="create an empty ball (existing balls are preserved)")
    sub.add_parser("list", help="list balls in the default data directory")
    sub.add_parser("show", help="show the persistent configuration")
    sub.add_parser("apps", help="list saved application launchers")
    export = sub.add_parser("export", help="export the ball as a ZIP archive")
    export.add_argument("archive", type=Path)
    export.add_argument("--include-images", action="store_true", help="bundle disk images")
    archive_import = sub.add_parser("import", help="import a ZIP into a new --ball directory")
    archive_import.add_argument("archive", type=Path)
    app = sub.add_parser("add-app", help="save an installed application's command")
    app.add_argument("--cwd", help="guest or mapped host working directory")
    app.add_argument("name")
    app.add_argument("program")
    app.add_argument("arguments", nargs=argparse.REMAINDER)
    remove = sub.add_parser("remove-app", help="remove a saved launcher without deleting files")
    remove.add_argument("name")
    launch = sub.add_parser("launch", help="run a saved application")
    launch.add_argument("name")
    launch.add_argument("arguments", nargs=argparse.REMAINDER)
    mapping = sub.add_parser("map", help="map a host directory as an OS/2 drive")
    mapping.add_argument("drive")
    mapping.add_argument("directory", type=Path)
    image = sub.add_parser("map-image", help="map an ISO or FAT floppy image internally, read-only")
    image.add_argument("drive")
    image.add_argument("image", type=Path)
    device = sub.add_parser("device", help="allocate an ejected CD or floppy device")
    device.add_argument("drive")
    device.add_argument("kind", choices=["cdrom", "floppy", "disk"])
    eject = sub.add_parser("eject", help="eject media while retaining its drive letter")
    eject.add_argument("drive")
    create = sub.add_parser("create-floppy", help="create a formatted FAT12 image and attach it")
    create.add_argument("drive")
    create.add_argument("image", type=Path)
    create.add_argument("kib", type=int, choices=[360, 720, 1200, 1440, 2880])
    disk = sub.add_parser("create-disk", help="create and attach a formatted filesystem image")
    disk.add_argument("drive")
    disk.add_argument("image", type=Path)
    disk.add_argument("filesystem", choices=["fat32", "exfat", "hpfs"])
    disk.add_argument("mib", type=int)
    for name in ("device-lock", "media-readonly"):
        option = sub.add_parser(name)
        option.add_argument("drive")
        option.add_argument("state", choices=["on", "off"])
    unmap = sub.add_parser("unmap")
    unmap.add_argument("drive")
    setting = sub.add_parser(
        "config", help="set system.trace_native, system.beep_volume, or env.NAME"
    )
    setting.add_argument("key")
    setting.add_argument("value")
    unset = sub.add_parser("unset", help="restore an inherited setting or environment variable")
    unset.add_argument("key")
    for name in ("run", "install"):
        launch = sub.add_parser(name, help="run an original guest executable inside the ball")
        launch.add_argument("--cwd", help="guest or mapped host working directory")
        launch.add_argument("program")
        launch.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    configure_library(args.build)
    ball = args.ball.resolve()
    try:
        if args.action in ("import", "export"):
            result = native_request(
                {
                    "action": args.action + "-ball",
                    "ball": str(ball),
                    "path": str(args.archive.resolve()),
                    "includeImages": getattr(args, "include_images", False),
                    "build": str(args.build.resolve()),
                }
            )
            print(result if args.action == "export" else ball)
            return 0
        if args.action == "list":
            for path in native_request({"action": "discover"}):
                print(path)
            return 0
        if args.action in ("run", "install", "launch"):
            return run(args, ball, load(ball))
        if args.action in ("show", "apps"):
            data = load(ball)
            print(
                json.dumps(
                    data if args.action == "show" else data.get("applications", {}), indent=2
                )
            )
            return 0
        if args.action == "init":
            command = ["init"]
        elif args.action == "add-app":
            command = [
                "add-app",
                *(["--cwd", args.cwd] if args.cwd else []),
                args.name,
                args.program,
                *args.arguments,
            ]
        elif args.action == "remove-app":
            command = ["remove-app", args.name]
        elif args.action == "map":
            command = ["map", args.drive, str(args.directory)]
        elif args.action == "map-image":
            command = ["map-image", args.drive, str(args.image.resolve())]
        elif args.action == "device":
            command = ["device", args.drive, args.kind]
        elif args.action == "eject":
            command = ["eject", args.drive]
        elif args.action == "create-floppy":
            command = ["create-floppy", args.drive, str(args.image.resolve()), str(args.kib)]
        elif args.action == "create-disk":
            command = [
                "create-disk",
                args.drive,
                str(args.image.resolve()),
                args.filesystem,
                str(args.mib),
            ]
        elif args.action in ("device-lock", "media-readonly"):
            command = [args.action, args.drive, args.state]
        elif args.action == "unmap":
            command = ["unmap", args.drive]
        else:
            command = [args.action, args.key, *([args.value] if args.action == "config" else [])]
        native_request(
            {
                "action": "command",
                "ball": str(ball),
                "build": str(args.build.resolve()),
                "backend": args.backend,
                "arguments": command,
            }
        )
        if args.action == "init":
            print(ball)
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"2ine-ball: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
