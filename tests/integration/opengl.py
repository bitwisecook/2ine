#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Run real x86 LX callers through PGL and Mesa using both ordinal profiles."""

import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path

from images import FlatGuest, linear_image

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / "tools/data/opengl.json").read_text())
SIGNATURES = {f["name"]: f["arguments"] for f in SCHEMA["functions"]}
C = SCHEMA["constants"]


class Scene:
    def __init__(self, profile, double=True):
        self.g = FlatGuest()
        self.profile = profile
        self.ordinals = SCHEMA["ordinals"][profile]
        self.window = self.field(bytes(4))
        self.context = self.field(bytes(4))
        self.config = self.field(bytes(4))
        self.output = self.field(bytes(128))
        self.saved = self.field(bytes(4))
        self.status = 10
        self.g.call(2, 763, 0)  # WinInitialize
        self.expect(1)
        self.g.call(2, 716, 1, 0)  # WinCreateMsgQueue
        self.g.call(
            2,
            909,
            1,
            self.field(b"#1\0"),
            self.field(b"Mesa LX\0"),
            0x80000000,
            0,
            0,
            32,
            32,
            1,
            3,
            1,
            0,
            0,
        )
        self.save(self.window)
        self.nonzero()
        attrs = [4, 5, 12, 24, 13, 8, 0] if double else [4, 12, 24, 0]
        self.pgl("pglChooseConfig", 1, self.field(struct.pack("<" + "I" * len(attrs), *attrs)))
        self.nonzero()
        self.save(self.config)
        self.pgl("pglCreateContext", 1, (self.config,), 0, 1)
        self.nonzero()
        self.save(self.context)
        self.pgl("pglMakeCurrent", 1, (self.context,), (self.window,))
        self.expect(1)
        self.no_error()

    def field(self, data):
        return self.g.field(data)

    def save(self, address):
        self.g.code.extend(b"\xa3" + struct.pack("<I", address))

    def expect(self, value):
        self.g.result(value & 0xFFFFFFFF, self.status)
        self.status += 1

    def word(self, address, value):
        self.g.word(address, value & 0xFFFFFFFF, self.status)
        self.status += 1

    def nonzero(self):
        self.g.code.extend(b"\x85\xc0\x75\x12")
        self.g.exit(self.status)
        self.status += 1

    def pgl(self, name, *values):
        self.g.call(3, self.ordinals[name], *values)

    def gl(self, name, *values):
        wire = []
        for (kind, _), value in zip(SIGNATURES[name], values, strict=True):
            if kind == "float":
                wire.extend(struct.unpack("<I", struct.pack("<f", value)))
            elif kind == "double":
                wire.extend(struct.unpack("<II", struct.pack("<d", value)))
            else:
                wire.append(value & 0xFFFFFFFF if isinstance(value, int) else value)
        self.g.call(3, self.ordinals.get(name, 0), *wire)
        if name not in self.ordinals:
            raise ValueError(f"{name} has no ordinal in profile {self.profile}")

    def no_error(self):
        self.gl("glGetError")
        self.expect(0)

    def pixel(self, x, y, expected):
        self.gl("glReadPixels", x, y, 1, 1, C["GL_RGBA"], C["GL_UNSIGNED_BYTE"], self.output)
        self.word(self.output, expected)

    def finish(self):
        self.no_error()
        self.pgl("pglDestroyContext", 1, (self.context,))
        self.expect(0)  # A current context is busy.
        self.pgl("pglMakeCurrent", 1, 0, 0)
        self.expect(1)
        self.pgl("pglDestroyContext", 1, (self.context,))
        self.expect(1)
        self.pgl("pglDestroyContext", 1, (self.context,))
        self.expect(0)
        self.g.call_handle(2, 728, self.window)
        self.expect(1)
        self.g.call(2, 888, 1)
        self.expect(1)
        marker = b"OS/2 OpenGL Mesa guest verified\n"
        self.g.call(1, 282, 1, self.field(marker), len(marker), self.saved)
        self.g.exit(0)
        return linear_image(
            self.g.code,
            self.g.fixups,
            ["DOSCALLS", "PMWIN", "OPENGL"],
            "OPENGLTEST",
            data=self.g.data,
        )


def immediate(profile):
    s = Scene(profile)
    s.pgl("pglQueryVersion", 1, s.output, s.output + 4)
    s.word(s.output, 1)
    s.word(s.output + 4, 1)
    s.gl("glGetString", C["GL_VERSION"])
    s.nonzero()
    s.g.require(b"\x81\x38" + b"1.1 ", s.status)
    s.status += 1
    s.gl("glClearColor", 0.0, 0.0, 1.0, 1.0)
    s.gl("glClearDepth", 1.0)
    s.gl("glClear", C["GL_COLOR_BUFFER_BIT"] | C["GL_DEPTH_BUFFER_BIT"])
    s.gl("glMatrixMode", C["GL_PROJECTION"])
    s.gl("glLoadIdentity")
    s.gl("glOrtho", -1.0, 1.0, -1.0, 1.0, -1.0, 1.0)
    s.gl("glMatrixMode", C["GL_MODELVIEW"])
    s.gl("glLoadIdentity")
    s.gl("glBegin", C["GL_TRIANGLES"])
    s.gl("glColor3d", 1.0, 0.0, 0.0)
    for x, y in [(-1.0, -1.0), (1.0, -1.0), (0.0, 1.0)]:
        s.gl("glVertex2f", x, y)
    s.gl("glEnd")
    s.pixel(16, 16, 0xFF0000FF)
    s.pgl("pglSwapBuffers", 1, (s.window,))
    s.gl("glReadBuffer", C["GL_FRONT"])
    s.pixel(16, 16, 0xFF0000FF)
    s.pixel(0, 31, 0xFFFF0000)
    s.gl("glGetDoublev", C["GL_MODELVIEW_MATRIX"], s.output)
    s.word(s.output + 4, 0x3FF00000)
    s.gl("glGetBooleanv", C["GL_DOUBLEBUFFER"], s.output)
    # Compare only GLboolean's byte, preserving the neighboring destination bytes.
    s.g.require(b"\x80\x3d" + struct.pack("<I", s.output) + b"\x01", s.status)
    s.status += 1
    return s.finish()


def invalid(profile):
    s = Scene(profile, double=False)
    s.gl("glColor3fv", 0xDEADBEEF)
    s.gl("glGetError")
    s.expect(C["GL_INVALID_OPERATION"])
    s.gl("glGetDoublev", C["GL_MODELVIEW_MATRIX"], 0x2FFF8)
    s.gl("glGetError")
    s.expect(C["GL_INVALID_OPERATION"])
    # A modern variable-width query must never write past a GL 1.1 span.
    s.gl("glGetTexParameteriv", C["GL_TEXTURE_2D"], 0x8E46, s.output)
    s.gl("glGetError")
    s.expect(C["GL_INVALID_ENUM"])
    s.gl("glReadPixels", 0, 0, -1, 1, C["GL_RGBA"], C["GL_UNSIGNED_BYTE"], s.output)
    s.gl("glGetError")
    s.expect(C["GL_INVALID_VALUE"])
    s.gl("glPixelStorei", C["GL_UNPACK_ROW_LENGTH"], 0x7FFFFFFF)
    s.gl(
        "glTexImage2D",
        C["GL_TEXTURE_2D"],
        0,
        4,
        1,
        0x7FFFFFFF,
        0,
        C["GL_RGBA"],
        C["GL_UNSIGNED_BYTE"],
        s.output,
    )
    s.gl("glGetError")
    s.expect(C["GL_INVALID_VALUE"])
    s.gl("glPixelStorei", C["GL_UNPACK_ROW_LENGTH"], 0)
    s.gl("glColor3fv", s.field(struct.pack("<3f", 0.0, 1.0, 0.0)))
    s.gl("glBegin", C["GL_QUADS"])
    for x, y in [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)]:
        s.gl("glVertex2d", x, y)
    s.gl("glEnd")
    s.gl("glFlush")
    s.pixel(16, 16, 0xFF00FF00)
    return s.finish()


def arrays(profile):
    s = Scene(profile)
    positions = s.field(struct.pack("<6f", -1.0, -1.0, 1.0, -1.0, 0.0, 1.0))
    indices = s.field(bytes((0, 1, 2)))
    s.gl("glClearColor", 0.0, 0.0, 0.0, 1.0)
    s.gl("glClear", C["GL_COLOR_BUFFER_BIT"])
    s.gl("glColor3f", 0.0, 1.0, 0.0)
    s.gl("glVertexPointer", 2, C["GL_FLOAT"], 0, positions)
    s.gl("glEnableClientState", C["GL_VERTEX_ARRAY"])
    s.gl("glGetPointerv", C["GL_VERTEX_ARRAY_POINTER"], s.output)
    s.word(s.output, positions)
    s.gl("glDrawElements", C["GL_TRIANGLES"], 3, C["GL_UNSIGNED_BYTE"], indices)
    s.pixel(16, 16, 0xFF00FF00)
    s.gl("glPushClientAttrib", C["GL_CLIENT_VERTEX_ARRAY_BIT"])
    s.gl("glVertexPointer", 2, C["GL_FLOAT"], 0, 0xDEADBEEF)
    s.gl("glDrawArrays", C["GL_TRIANGLES"], 0, 3)
    s.gl("glGetError")
    s.expect(C["GL_INVALID_OPERATION"])
    s.gl("glPopClientAttrib")
    s.gl("glClear", C["GL_COLOR_BUFFER_BIT"])
    s.gl("glColor3f", 1.0, 0.0, 0.0)
    s.gl("glDrawArrays", C["GL_TRIANGLES"], 0, 3)
    s.pixel(16, 16, 0xFF0000FF)
    s.gl("glBegin", C["GL_TRIANGLES"])
    for index in (0, 1, 2):
        s.gl("glArrayElement", index)
    s.gl("glEnd")
    return s.finish()


def texture(profile):
    s = Scene(profile)
    texel = s.field(bytes((255, 255, 0, 255)))
    s.gl("glGenTextures", 1, s.saved)
    s.gl("glBindTexture", C["GL_TEXTURE_2D"], (s.saved,))
    s.gl("glTexParameteri", C["GL_TEXTURE_2D"], C["GL_TEXTURE_MIN_FILTER"], C["GL_NEAREST"])
    s.gl("glTexParameteri", C["GL_TEXTURE_2D"], C["GL_TEXTURE_MAG_FILTER"], C["GL_NEAREST"])
    s.gl(
        "glTexImage2D",
        C["GL_TEXTURE_2D"],
        0,
        4,
        1,
        1,
        0,
        C["GL_RGBA"],
        C["GL_UNSIGNED_BYTE"],
        texel,
    )
    s.gl("glEnable", C["GL_TEXTURE_2D"])
    s.gl("glColor3f", 1.0, 1.0, 1.0)
    s.gl("glBegin", C["GL_QUADS"])
    for x, y in [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)]:
        s.gl("glTexCoord2f", 0.5, 0.5)
        s.gl("glVertex2f", x, y)
    s.gl("glEnd")
    s.pixel(16, 16, 0xFF00FFFF)
    s.gl("glPixelStorei", C["GL_PACK_ROW_LENGTH"], 3)
    s.gl("glPixelStorei", C["GL_PACK_SKIP_ROWS"], 1)
    s.gl("glPixelStorei", C["GL_PACK_SKIP_PIXELS"], 1)
    s.gl("glGetTexImage", C["GL_TEXTURE_2D"], 0, C["GL_RGBA"], C["GL_UNSIGNED_BYTE"], s.output)
    s.word(s.output + 16, 0xFF00FFFF)
    s.word(s.output, 0xFF00FFFF)  # Prefix remains from the earlier read.
    s.gl("glDeleteTextures", 1, s.saved)
    return s.finish()


def selection(profile):
    s = Scene(profile)
    s.gl("glSelectBuffer", 16, s.output)
    s.gl("glRenderMode", C["GL_SELECT"])
    s.expect(0)
    s.gl("glInitNames")
    s.gl("glPushName", 42)
    s.gl("glBegin", C["GL_TRIANGLES"])
    for x, y in [(-0.5, -0.5), (0.5, -0.5), (0.0, 0.5)]:
        s.gl("glVertex2f", x, y)
    s.gl("glEnd")
    s.gl("glRenderMode", C["GL_RENDER"])
    s.expect(1)
    s.word(s.output, 1)
    s.word(s.output + 12, 42)
    points = s.field(struct.pack("<6f", -1.0, -1.0, 0.0, 1.0, 1.0, 0.0))
    s.gl("glMap1f", C["GL_MAP1_VERTEX_3"], 0.0, 1.0, 3, 2, points)
    s.gl("glGetMapfv", C["GL_MAP1_VERTEX_3"], C["GL_COEFF"], s.output)
    s.word(s.output, 0xBF800000)
    s.word(s.output + 16, 0x3F800000)
    return s.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--video-driver", default="dummy")
    parser.add_argument("--render-driver", default="software")
    args = parser.parse_args()
    build, work = args.build.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    results = []
    for profile in ("warp", "gold11"):
        scenes = [immediate, invalid, selection]
        if profile == "gold11":
            scenes += [arrays, texture]
        for scene in scenes:
            name = f"{profile}-{scene.__name__}"
            executable = work / f"{name}.exe"
            executable.write_bytes(scene(profile))
            env = dict(
                os.environ,
                TWOINE_BACKEND=args.backend,
                TWOINE_LIBPATH=str(build),
                LD_LIBRARY_PATH=str(build),
                TWOINE_OPENGL_ABI=profile,
                SDL_VIDEO_DRIVER=args.video_driver,
                SDL_RENDER_DRIVER=args.render_driver,
            )
            if os.uname().sysname == "Linux":
                env["LIBGL_ALWAYS_SOFTWARE"] = "1"
            result = subprocess.run(
                [str(build / "lx_loader"), str(executable)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=30,
            )
            (work / f"{name}.stdout").write_bytes(result.stdout)
            (work / f"{name}.stderr").write_bytes(result.stderr)
            passed = result.returncode == 0 and b"OS/2 OpenGL Mesa guest verified" in result.stdout
            results.append(
                {
                    "scene": name,
                    "exit": result.returncode,
                    "passed": passed,
                    "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                }
            )
            (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
            if not passed:
                raise RuntimeError(
                    f"{name}: exit={result.returncode}\n" + result.stderr.decode(errors="replace")
                )
    print(f"{len(results)} LX Mesa scenes passed on {args.backend}")


if __name__ == "__main__":
    main()
