#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Build OS/2 Doom 1.10 with a monotonic clock, bounded save storage, audio shutdown and networking.

Requires the separately supplied source ZIP, OS/2 Toolkit 4.5 and Linux Open
Watcom 1.9 with i386 execution support. The original executable is never read
or patched. Third-party source and resulting binaries stay in the output tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import zipfile
from datetime import UTC, datetime
from pathlib import Path

SOURCE_SHA256 = "b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9"
OPTIONS = ["-w1", "-e25", "-d+", "-j", "-zq", "-otexan", "-of+", "-mf", "-5r", "-bt=os2", "-ei"]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command: list[str], cwd: Path, env: dict[str, str], log: Path) -> None:
    with log.open("w") as stream:
        stream.write(json.dumps(command) + "\n")
        stream.flush()
        subprocess.run(
            command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True
        )


def link_name(path: Path) -> str:
    if "'" in str(path):
        raise ValueError("Watcom linker paths cannot contain an apostrophe")
    return "'" + str(path) + "'"


def build(archive: Path, sdk: Path, watcom: Path, output: Path, build_epoch: int = 0) -> Path:
    if digest(archive) != SOURCE_SHA256:
        raise ValueError("source archive does not match the supported OS/2 Doom 1.10 release")
    output.mkdir(parents=True, exist_ok=True)
    source = output / "source"
    source.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            name = Path(member.filename)
            if name.is_absolute() or ".." in name.parts or len(name.parts) != 1:
                raise ValueError(f"unsupported archive member: {member.filename}")
            if member.is_dir():
                continue
            # Lowercase local headers and DOS EOF handling preserve source tokens.
            destination = source / name.name.lower()
            text = package.read(member).replace(b"\x1a", b"").decode("latin1")
            destination.write_text(text.replace("\r\n", "\n"), encoding="latin1")
    timestamp = datetime.fromtimestamp(build_epoch, UTC)
    months = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split()
    build_date = f"{months[timestamp.month - 1]} {timestamp.day:2} {timestamp.year:04}"
    build_time = timestamp.strftime("%H:%M:%S")
    main_source = source / "i_main.c"
    main_source.write_text(
        main_source.read_text()
        .replace("__DATE__", json.dumps(build_date))
        .replace("__TIME__", json.dumps(build_time))
    )
    patch = Path(__file__).resolve().parents[1] / "research/patches/doom110-clock.patch"
    environment = dict(os.environ, WATCOM=str(watcom))
    execute(
        ["patch", "--batch", "-p1", "-i", str(patch)], source, environment, output / "patch.log"
    )

    # The Linux compiler interprets include path separators literally. Copy the
    # authentic SDK, changing only backslashes in preprocessor include operands.
    headers = output / "sdk-headers"
    for original in (sdk / "h").rglob("*"):
        if not original.is_file():
            continue
        target = headers / original.relative_to(sdk / "h")
        target.parent.mkdir(parents=True, exist_ok=True)
        data = original.read_bytes()
        data = re.sub(
            rb"(^[ \t]*#[ \t]*include[^\r\n]+)",
            lambda match: match.group(1).replace(b"\\", b"/"),
            data,
            flags=re.MULTILINE,
        )
        target.write_bytes(data)
    environment["INCLUDE"] = os.pathsep.join(
        str(p) for p in (headers, watcom / "h", watcom / "h/os2")
    )
    tools = output / "toolchain"
    tools.mkdir(exist_ok=True)
    for name in ("wcc386", "wasm", "wlink"):
        destination = tools / name
        shutil.copyfile(watcom / "binl" / name, destination)
        destination.chmod(0o755)
    units = re.findall(r"^(\w+)\.obj\s*:", (source / "os2doom.mk1").read_text(), re.MULTILINE)
    if len(units) != 68:
        raise ValueError("unexpected original Doom makefile unit inventory")
    for unit in units:
        execute(
            [str(tools / "wcc386"), unit + ".c", *OPTIONS, "-fo=" + unit + ".obj"],
            source,
            environment,
            output / ("compile-" + unit + ".log"),
        )
    execute(
        [str(tools / "wasm"), "-fo=tmap.obj", "tmap.asm"],
        source,
        environment,
        output / "assemble.log",
    )
    lines = [
        "format os2 lx",
        "option quiet",
        "option stack=128000",
        "option caseexact",
        "option map=os2doom.map",
        "option description 'OS/2 Doom 1.10 compatibility source build'",
        "name " + link_name(output / "os2doom.exe"),
        "libpath " + link_name(watcom / "lib386"),
        "libpath " + link_name(watcom / "lib386/os2"),
        "library os2386",
    ]
    lines += ["file " + unit + ".obj" for unit in [*units, "tmap"]]
    libraries = [sdk / "lib" / (name + ".lib") for name in ("mmpm2", "so32dll", "tcp32dll")]
    lines += ["library " + link_name(path) for path in libraries]
    # The SDK's pmddi.h declares these exact PMGRE engine dispatch imports.
    lines += ["import Gre32Entry3 PMGRE.63", "import Gre32Entry5 PMGRE.65"]
    (source / "build.lnk").write_text("\n".join(lines) + "\n")
    execute([str(tools / "wlink"), "@build.lnk"], source, environment, output / "link.log")
    executable = output / "os2doom.exe"
    inputs = [archive, patch, *(tools / name for name in ("wcc386", "wasm", "wlink")), *libraries]
    # Compiler-emitted default-library directives select these matching runtimes.
    inputs += [
        watcom / "lib386/os2" / name
        for name in ("os2386.lib", "clib3r.lib", "math387r.lib", "emu387.lib")
    ]
    used_libraries = re.findall(r"^(/.+\.lib)$", (source / "os2doom.map").read_text(), re.MULTILINE)
    watcom_headers = sorted(path for path in (watcom / "h").rglob("*") if path.is_file())
    header_inventory = "\n".join(
        str(path.relative_to(watcom / "h")) + " " + digest(path) for path in watcom_headers
    )
    receipt = {
        "libraries_used": {name: digest(Path(name)) for name in used_libraries},
        "watcom_headers_tree_sha256": hashlib.sha256(header_inventory.encode()).hexdigest(),
        "source_date_epoch": build_epoch,
        "build_date": build_date,
        "build_time": build_time,
        "program": str(executable),
        "sha256": digest(executable),
        "source_units": units,
        "inputs": {str(path): digest(path) for path in inputs if path.is_file()},
        "sdk_headers": {
            str(path.relative_to(headers)): digest(path)
            for path in sorted(headers.rglob("*"))
            if path.is_file()
        },
        "patched_sources": {
            path.name: digest(path)
            for path in sorted(source.iterdir())
            if path.suffix in {".c", ".h", ".asm"}
        },
        "compiler_options": OPTIONS,
    }
    (output / "build.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return executable


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-zip", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--watcom", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--build-epoch", type=int, default=int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    )
    args = parser.parse_args()
    print(
        build(
            args.source_zip.resolve(),
            args.sdk.resolve(),
            args.watcom.resolve(),
            args.output.resolve(),
            args.build_epoch,
        )
    )


if __name__ == "__main__":
    main()
