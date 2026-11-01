# Public domain. No rights reserved. Wholly AI-authored.
"""Small EWMH window manager for fullscreen tests on private Xvfb displays."""

import argparse
import ctypes as C
import select
import subprocess
import sys
from pathlib import Path

try:
    from .x11 import ClientMessage, Display, Int, P, U, fn, x
except ImportError:
    from x11 import ClientMessage, Display, Int, P, U, fn, x


class MapRequest(C.Structure):
    _fields_ = [
        ("type", Int),
        ("serial", U),
        ("send_event", Int),
        ("display", P),
        ("parent", U),
        ("window", U),
    ]


class ConfigureRequest(C.Structure):
    _fields_ = [
        ("type", Int),
        ("serial", U),
        ("send_event", Int),
        ("display", P),
        ("parent", U),
        ("window", U),
        ("x", Int),
        ("y", Int),
        ("width", Int),
        ("height", Int),
        ("border_width", Int),
        ("above", U),
        ("detail", Int),
        ("value_mask", U),
    ]


class Changes(C.Structure):
    _fields_ = [
        ("x", Int),
        ("y", Int),
        ("width", Int),
        ("height", Int),
        ("border_width", Int),
        ("sibling", U),
        ("stack_mode", Int),
    ]


class Event(C.Union):
    _fields_ = [
        ("type", Int),
        ("map", MapRequest),
        ("configure", ConfigureRequest),
        ("client", ClientMessage),
        ("pad", C.c_long * 24),
    ]


fn(x, "XCreateSimpleWindow", U, [P, U, Int, Int, C.c_uint, C.c_uint, C.c_uint, U, U])
fn(x, "XChangeProperty", Int, [P, U, U, U, Int, Int, P, Int])
fn(x, "XSelectInput", Int, [P, U, C.c_long])
fn(x, "XMapWindow", Int, [P, U])
fn(x, "XRaiseWindow", Int, [P, U])
fn(x, "XMoveResizeWindow", Int, [P, U, Int, Int, C.c_uint, C.c_uint])
fn(x, "XConfigureWindow", Int, [P, U, C.c_uint, C.POINTER(Changes)])
fn(x, "XNextEvent", Int, [P, C.POINTER(Event)])
fn(x, "XSync", Int, [P, Int])


class WindowManager:
    """Run EWMH handling in its own Xlib process, independently of test input."""

    def __init__(self, display):
        self.process = subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), "--display", display],
            stdout=subprocess.PIPE,
            text=True,
        )
        assert select.select([self.process.stdout], [], [], 5)[0], "test WM did not start"
        assert self.process.stdout.readline().strip() == "ready", "test WM startup failed"

    def close(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        self.process.stdout.close()


def serve(name):
    display = Display(name)
    atoms = {
        name: x.XInternAtom(display.d, name.encode(), 0)
        for name in (
            "_NET_SUPPORTING_WM_CHECK",
            "_NET_SUPPORTED",
            "_NET_WM_STATE",
            "_NET_WM_STATE_FULLSCREEN",
            "_NET_ACTIVE_WINDOW",
            "_NET_WM_NAME",
            "UTF8_STRING",
        )
    }

    def property(window, atom, kind, values):
        data = (U * len(values))(*values)
        x.XChangeProperty(display.d, window, atoms[atom], kind, 32, 0, data, len(values))

    owner = x.XCreateSimpleWindow(display.d, display.root, -1, -1, 1, 1, 0, 0, 0)
    property(display.root, "_NET_SUPPORTING_WM_CHECK", 33, [owner])  # XA_WINDOW.
    property(owner, "_NET_SUPPORTING_WM_CHECK", 33, [owner])
    property(display.root, "_NET_SUPPORTED", 4, list(atoms.values()))  # XA_ATOM.
    title = C.create_string_buffer(b"2ine test EWMH manager")
    x.XChangeProperty(
        display.d, owner, atoms["_NET_WM_NAME"], atoms["UTF8_STRING"], 8, 0, title, len(title.value)
    )
    x.XSelectInput(display.d, display.root, (1 << 20) | (1 << 19))
    x.XSync(display.d, 0)
    print("ready", flush=True)
    restored = {}
    event = Event()
    while True:
        x.XNextEvent(display.d, C.byref(event))
        if event.type == 20:  # MapRequest.
            x.XMapWindow(display.d, event.map.window)
        elif event.type == 23:  # ConfigureRequest.
            request = event.configure
            if request.window not in restored:
                changes = Changes(
                    request.x,
                    request.y,
                    request.width,
                    request.height,
                    request.border_width,
                    request.above,
                    request.detail,
                )
                x.XConfigureWindow(display.d, request.window, request.value_mask, C.byref(changes))
        elif event.type == 33:  # ClientMessage.
            request = event.client
            if (
                request.message_type == atoms["_NET_WM_STATE"]
                and atoms["_NET_WM_STATE_FULLSCREEN"] in request.data.l[1:3]
            ):
                enabled = request.data.l[0] == 1 or (
                    request.data.l[0] == 2 and request.window not in restored
                )
                if enabled and request.window not in restored:
                    restored[request.window] = (
                        *display.position(request.window),
                        *display.geometry(request.window),
                    )
                    width, height = display.geometry(display.root)
                    property(
                        request.window, "_NET_WM_STATE", 4, [atoms["_NET_WM_STATE_FULLSCREEN"]]
                    )
                    x.XMoveResizeWindow(display.d, request.window, 0, 0, width, height)
                    x.XRaiseWindow(display.d, request.window)
                elif not enabled and request.window in restored:
                    geometry = restored.pop(request.window)
                    property(request.window, "_NET_WM_STATE", 4, [])
                    x.XMoveResizeWindow(display.d, request.window, *geometry)
            elif request.message_type == atoms["_NET_ACTIVE_WINDOW"]:
                x.XRaiseWindow(display.d, request.window)
                x.XSetInputFocus(display.d, request.window, 1, 0)
        x.XFlush(display.d)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--display", required=True)
    serve(parser.parse_args().display)
