# Public domain. No rights reserved. Wholly AI-authored.
"""Real LX calls must honor media Off switches and exclusive OpenGL providers."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def media_program():
    g = FlatGuest()
    params = g.field(bytes(32))
    for kind, flags, failure in ((9, 0x1000, 10), (0, 0x4000, 11), (12, 0x1000, 12)):
        g.code.extend(b"\xc7\x05" + struct.pack("<II", params + 8, kind))
        g.call(2, 1, 0, 1, flags, params, 0)
        g.result(5020, failure)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "MDM"], "MEDIAOFF", data=g.data)


def gl_program():
    g = FlatGuest()
    g.call(2, 1)
    g.result(42, 10)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "OPENGL"], "GLCHOICE", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=args.work.resolve()))
    config = work / "2ine.cfg"
    (work / "MEDIAOFF.EXE").write_bytes(media_program())
    (work / "GLCHOICE.EXE").write_bytes(gl_program())
    # Original generated guest DLL, not a host native provider. Ordinal1 returns42.
    (work / "OPENGL.DLL").write_bytes(
        linear_image(b"\xb8\x2a\0\0\0\xc3", [], [], "OPENGL", library=True, exports=[0])
    )
    env = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("TWOINE_", "TRACE_", "DYLD_", "LD_"))
    }
    env.update(
        TWOINE_BACKEND=args.backend,
        TWOINE_DRIVE_C=str(work),
        TWOINE_CONFIG=str(config),
        TWOINE_LIBPATH=str(build),
        SDL_VIDEO_DRIVER="dummy",
        SDL_AUDIO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
    )
    if sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = str(build)

    def run(name, settings, success, diagnostic=None):
        config.write_text(f"mountpoint.C={work}\n" + settings)
        p = subprocess.run(
            [str(build / "lx_loader"), str(work / name)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=20,
        )
        with (work / "results.log").open("ab") as output:
            output.write(settings.encode() + p.stdout + p.stderr)
        assert (p.returncode == 0) == success, (
            name,
            settings,
            p.returncode,
            p.stderr.decode(errors="replace"),
        )
        if diagnostic:
            assert diagnostic in p.stderr.decode(errors="replace"), p.stderr.decode(
                errors="replace"
            )

    run("MEDIAOFF.EXE", "system.video=false\nsystem.pcm=false\nsystem.midi=false\n", True)
    run("GLCHOICE.EXE", "system.opengl=original\n", True)
    run("GLCHOICE.EXE", "system.opengl=off\n", False, "OPENGL is disabled")
    # Mesa must never fall back to the present original guest DLL. With Mesa
    # compiled, ordinal1 is glAccum and cannot return the fixture's42 either.
    run("GLCHOICE.EXE", "system.opengl=mesa\n", False)
    run("GLCHOICE.EXE", "", False)  # An absent preference also selects only Mesa.
    g = FlatGuest()
    g.exit(0)
    (work / "DIVEOFF.EXE").write_bytes(
        linear_image(g.code, g.fixups, ["DOSCALLS", "DIVE"], "DIVEOFF", data=g.data)
    )
    run("DIVEOFF.EXE", "system.dive=false\n", False, "DIVE is disabled")
    print("LX media switches and exclusive Original/Mesa/Off provider selection passed")


if __name__ == "__main__":
    main()
