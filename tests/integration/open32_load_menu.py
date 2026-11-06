#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Load genuine PM-format menu resources through the Open32 caller-cleaned ABI."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def menu(items):
    body = bytearray()
    for style, attributes, identity, text, child in items:
        body.extend(struct.pack("<3H", style, attributes, identity))
        if not style & 4:
            body.extend(text + b"\0")
        if child:
            body.extend(child)
    return struct.pack("<I3H", 10 + len(body), 850, 4, len(items)) + body


def program():
    tools = menu([(1, 0, 200, b"~About", None)])
    sub = menu(
        [
            (1, 0x6000, 100, b"~Open\tCtrl+O", None),
            (4, 0, 0, b"", None),
            (17, 0, 41, b"~Tools", tools),
        ]
    )
    resource = menu([(17, 0, 40, b"~File", sub), (1, 0, 300, b"Caf\x82 & tea", None)])
    names = struct.pack("<3I", 2, 91, 0x8000005F) + b"MainMenu\0Alternative\0"
    g = FlatGuest()
    root, submenu, nested, stack, window = (g.field(bytes(4)) for _ in range(5))
    buffer = g.field(bytes(64))
    named, alternate, numeric = (g.field(s) for s in (b"mAiNmEnU\0", b"Alternative\0", b"#91\0"))
    g.code.extend(b"\x89\x25" + struct.pack("<I", stack))

    def save(address):
        g.code.extend(b"\xa3" + struct.pack("<I", address))

    def check_loaded(identity, instance, status):
        g.call(2, 462, instance, identity)
        save(root)
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", status)
        g.call(3, 834, (root,), 5)
        g.result(2, status + 1)  # Original LoadMenu uses HWND_OBJECT.
        g.call(2, 355, (root,), 0)
        save(submenu)
        g.call(2, 446, (submenu,))
        g.result(1, status + 2)
        g.call(2, 355, (submenu,), 2)
        save(nested)
        g.call(2, 308, (nested,), 0)
        g.result(200, status + 3)
        g.call(2, 309, (root,), 100, 0)
        g.result(9, status + 4)
        g.call(2, 310, (root,), 100, buffer, 64, 0)
        g.result(12, status + 5)
        g.word(buffer, int.from_bytes(b"&Ope", "little"), status + 6)
        g.call(2, 310, (root,), 300, buffer, 64, 0)
        g.result(11, status + 7)
        g.word(buffer, int.from_bytes(b"Caf\xe9", "little"), status + 8)

    check_loaded(91, 0, 10)
    # Attach the object-parented loaded root to a hidden real Open32 window.
    classname = g.field(b"LoadedMenuOwner\0")
    record = g.field(struct.pack("<10I", 0, 0x10002, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, classname))
    library = g.field(b"PMWINX\0")
    g.call(2, 319, library)
    save(window)
    g.call(2, 341, (window,), 131)
    save(record + 4)
    g.call(2, 556, record)
    g.call(2, 99, 0, classname, 0, 0, 0, 0, 100, 80, 0, 0, 0xFFFFFFFF, 0)
    save(window)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 20)
    g.call(2, 628, (window,), (root,))
    g.result(1, 21)
    g.call(2, 305, (window,))
    g.require(b"\x3b\x05" + struct.pack("<I", root), 22)
    g.call(3, 834, (root,), 5)
    g.require(b"\x3b\x05" + struct.pack("<I", window), 23)
    g.call(2, 628, (window,), 0)
    g.result(1, 24)
    g.call(2, 146, (window,))
    g.result(1, 25)
    g.call(2, 145, (root,))
    g.result(1, 26)
    for index, handle in enumerate((root, submenu, nested)):
        g.call(2, 446, (handle,))
        g.result(0, 27 + index)
    for identity, instance, status in (
        (named, 0xFFFFFFFF, 40),
        (alternate, 0, 60),
        (numeric, 0, 80),
    ):
        check_loaded(identity, instance, status)
        g.call(2, 145, (root,))
        g.result(1, status + 9)
        g.call(2, 446, (nested,))
        g.result(0, status + 10)
    g.require(b"\x3b\x25" + struct.pack("<I", stack), 100)
    g.exit(0)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX", "PMWIN"],
        "LOADMENU32",
        data=g.data,
        resources=[(3, 91, resource), (3, 95, resource), (255, 3, names)],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="load-menu-", dir=args.work_dir.resolve()) as temporary:
        work = Path(temporary)
        image = work / "load-menu.exe"
        image.write_bytes(program())
        environment = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(image)],
            cwd=work,
            env=environment,
            capture_output=True,
            timeout=20,
        )
        if result.returncode:
            raise RuntimeError(
                f"Open32 LoadMenu exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print("Open32 loads named/numeric native menu trees, attaches them and owns all descendants")


if __name__ == "__main__":
    main()
