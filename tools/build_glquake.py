#!/usr/bin/env python3
# Public domain. No rights reserved.
"""Stage and build native OS/2 GLQuake from id Software's GPL source."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

UNITS = """cl_demo cl_input cl_main cl_parse cl_tent chase cmd common console crc cvar
 gl_draw gl_mesh gl_model gl_refrag gl_rlight gl_rmain gl_rmisc gl_rsurf gl_screen gl_test gl_warp
 host host_cmd keys menu mathlib net_loop net_main net_vcr net_bsd net_dgrm net_os2
 pr_cmds pr_edict pr_exec r_part sbar sv_main sv_phys sv_move sv_user
 zone view wad world cd_null snd_dma snd_mem snd_mix snd_os2 sys_os2 vid_os2 gl_os2""".split()
FLAGS = "-zq -w1 -e25 -bt=os2 -mf -5r -fp3 -fpi87 -otexan -dGLQUAKE -dOS2 -bm -j -ei".split()
NATIVE_FLAGS = ["-od" if flag == "-otexan" else flag for flag in FLAGS]
ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "watcom", "sdk", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(
        "--os2-runner", type=Path, help="Run OS/2-hosted Watcom tools through this 2ine lx_loader"
    )
    args = parser.parse_args()
    runner = args.os2_runner.resolve() if args.os2_runner else None
    upstream, watcom, sdk, output = [
        getattr(args, n).resolve() for n in ("source", "watcom", "sdk", "output")
    ]
    manifest = json.loads((ROOT / "ports/glquake/upstream.json").read_text())
    for name, expected in manifest["files"].items():
        if digest(upstream / name) != expected:
            raise ValueError("Source does not match pinned id Software release: " + name)
    output.mkdir(parents=True, exist_ok=True)
    source = output / "source"
    source.mkdir(exist_ok=True)
    for original in (upstream / "WinQuake").iterdir():
        if original.suffix.lower() in (".c", ".h", ".q1"):
            shutil.copyfile(original, source / original.name)
    for name in ("host.c", "host_cmd.c"):
        host = source / name
        host.write_text(
            host.read_text().replace("__TIME__", '"source"').replace("__DATE__", '"build"')
        )
    header = source / "glquake.h"
    header.write_text(
        header.read_text()
        .replace("#include <GL/glu.h>", '#include "gl_os2.h"')
        .replace(
            "#ifndef _WIN32\n#define APIENTRY /* */", "#ifndef APIENTRY\n#define APIENTRY /* */"
        )
    )
    # Watcom emits separate tentative definitions instead of Unix common symbols.
    for filename, declaration in (
        ("gl_warp.c", "int\t\tskytexturenum;"),
        ("gl_screen.c", "int\t\t\tsb_lines;"),
    ):
        file = source / filename
        file.write_text(file.read_text().replace(declaration, "extern " + declaration))
    datagram = source / "net_dgrm.c"
    datagram.write_text(
        datagram.read_text().replace(
            "#if defined(_WIN32)",
            "#if defined(__OS2__)\n#define TCPV40HDRS\n#include <types.h>\n#include <sys/socket.h>\n#include <netinet/in.h>\n#elif defined(_WIN32)",
            1,
        )
    )
    sound = source / "snd_dma.c"
    sound.write_text(
        sound.read_text().replace(
            "\tS_Startup ();",
            "\tS_Startup ();\n\tif (!sound_started && !fakedma)\n\t\treturn;",
            1,
        )
    )
    shutil.copyfile(upstream / "gnu.txt", source / "COPYING")
    for original in (ROOT / "ports/glquake").iterdir():
        if original.suffix in (".c", ".h", ".q1"):
            shutil.copyfile(original, source / original.name)
    headers = output / "sdk-headers"
    for original in (sdk / "h").rglob("*"):
        if not original.is_file():
            continue
        target = headers / original.relative_to(sdk / "h")
        target.parent.mkdir(parents=True, exist_ok=True)
        data = re.sub(
            rb"(^[ \t]*#[ \t]*include[^\r\n]+)",
            lambda m: m.group().replace(b"\\", b"/"),
            original.read_bytes(),
            flags=re.M,
        )
        target.write_bytes(data)
    # On a case-insensitive host these names already identify the same directory.
    if not (headers / "GL").exists() or not (headers / "gl").samefile(headers / "GL"):
        shutil.copytree(headers / "gl", headers / "GL", dirs_exist_ok=True)
    tools = output / "host-tools"
    tools.mkdir(exist_ok=True)
    for name in () if runner else ("wcc386", "wlink"):
        target = tools / name
        shutil.copyfile(watcom / "binl" / name, target)
        target.chmod(0o755)
    includes = [watcom / "h", headers, headers / "gl", watcom / "h/os2"]

    def guest_path(path):
        return "C:" + str(path).replace("/", "\\") if runner else str(path)

    def tool(name):
        if runner:
            return [str(runner), str(watcom / "binp" / (name + ".exe"))]
        return [str(tools / name)]

    environment = dict(os.environ, WATCOM=guest_path(watcom))
    if runner:
        environment["TWOINE_LIBPATH"] = (
            environment.get("TWOINE_LIBPATH", str(runner.parent)) + ":" + str(watcom / "binp/dll")
        )
    compiler_flags = NATIVE_FLAGS if runner else FLAGS
    commands = []
    for unit in UNITS:
        command = [
            *tool("wcc386"),
            unit + ".c",
            *compiler_flags,
            *("-i=" + guest_path(p) for p in includes),
            "-fo=" + unit + ".obj",
        ]
        commands.append(command)
        result = subprocess.run(command, cwd=source, env=environment, capture_output=True)
        (output / (unit + ".log")).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(
                unit + ": " + (result.stdout + result.stderr).decode(errors="replace")
            )
    shutil.copyfile(sdk / "lib/opengl.lib", source / "warpgl.lib")
    lines = [
        "format os2 lx pm",
        "option quiet",
        "option stack=2097152",
        "option map=glquake.map",
        "name glquake.exe",
        "libpath '" + guest_path(watcom / "lib386") + "'",
        "libpath '" + guest_path(watcom / "lib386/os2") + "'",
        "library os2386",
        "library warpgl",
    ]
    for name in ("mmpm2", "so32dll", "tcp32dll"):
        shutil.copyfile(sdk / ("lib/" + name + ".lib"), source / (name + ".lib"))
        lines.append("library " + name)
    lines += ["file " + u + ".obj" for u in UNITS]
    (source / "build.lnk").write_text("\n".join(lines) + "\n")
    command = [*tool("wlink"), "@build.lnk"]
    commands.append(command)
    result = subprocess.run(command, cwd=source, env=environment, capture_output=True)
    (output / "link.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
    shutil.copyfile(source / "glquake.exe", output / "glquake.exe")
    # This source bundle also builds with the OS/2-hosted Open Watcom tools.
    native_link = [line for line in lines if not line.startswith("libpath ")]
    (source / "native.lnk").write_text("\n".join(native_link) + "\n")
    makefile = (
        "# Open Watcom wmake; set INCLUDE to OS/2 SDK and Watcom headers.\n"
        "CFLAGS=" + " ".join(NATIVE_FLAGS) + "\n"
        "OBJS=" + " ".join(u + ".obj" for u in UNITS) + "\n"
        "glquake.exe: $(OBJS)\n\twlink libpath $(WATCOM)\\lib386 libpath $(WATCOM)\\lib386\\os2 @native.lnk\n"
        ".c.obj:\n\twcc386 $(CFLAGS) $<\n"
    )
    (source / "makefile").write_text(makefile)
    batch = [
        "@echo off",
        "set PATH=%WATCOM%\\binp;%PATH%",
        "set BEGINLIBPATH=%WATCOM%\\binp\\dll",
        "set LIB=%WATCOM%\\lib386;%WATCOM%\\lib386\\os2",
        "set INCLUDE=%WATCOM%\\h;%SDK%\\h;%SDK%\\h\\gl;%WATCOM%\\h\\os2",
    ]
    for unit in UNITS:
        batch += [
            "wcc386 " + " ".join(NATIVE_FLAGS) + " " + unit + ".c -fo=" + unit + ".obj",
            "if errorlevel 1 goto failed",
        ]
    batch += [
        r"wlink libpath %WATCOM%\lib386 libpath %WATCOM%\lib386\os2 @native.lnk",
        "if errorlevel 1 goto failed",
        "echo GLQuake native build passed >build.rc",
        "goto done",
        ":failed",
        "echo GLQuake native build failed >build.rc",
        ":done",
    ]
    (source / "build.cmd").write_bytes(("\r\n".join(batch) + "\r\n").encode())

    (output / "build.json").write_text(
        json.dumps(
            {
                "commands": commands,
                "upstream_revision": manifest["revision"],
                "inputs": {
                    str(p): digest(p)
                    for p in [
                        sdk / "lib/opengl.lib",
                        *(sdk / ("lib/" + n + ".lib") for n in ("mmpm2", "so32dll", "tcp32dll")),
                        *(
                            [watcom / "binp/dll/wccd386.dll", watcom / "binp/dll/wlinkd.dll"]
                            if runner
                            else []
                        ),
                        watcom / ("binp/wcc386.exe" if runner else "binl/wcc386"),
                        watcom / ("binp/wlink.exe" if runner else "binl/wlink"),
                        watcom / "lib386/os2/os2386.lib",
                        watcom / "lib386/os2/clib3r.lib",
                        watcom / "lib386/math387r.lib",
                        *(ROOT / "ports/glquake").glob("*.[ch]"),
                    ]
                },
                "executable_sha256": digest(output / "glquake.exe"),
            },
            indent=2,
        )
        + "\n"
    )
    print(output / "glquake.exe")


if __name__ == "__main__":
    main()
