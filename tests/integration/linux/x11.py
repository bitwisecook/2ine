# Public domain. No rights reserved. Wholly AI-authored.
"""X11 capture and genuine pointer/window events for the isolated GUI harness."""

import ctypes as C
import ctypes.util
import struct
import zlib
from pathlib import Path

x = C.CDLL(ctypes.util.find_library("X11"))
t = C.CDLL(ctypes.util.find_library("Xtst"))


def fn(lib, name, restype, args):
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = args
    return f


P = C.c_void_p
U = C.c_ulong
Int = C.c_int
fn(x, "XOpenDisplay", P, [C.c_char_p])
fn(x, "XDefaultRootWindow", U, [P])
fn(
    x,
    "XQueryTree",
    Int,
    [P, U, C.POINTER(U), C.POINTER(U), C.POINTER(C.POINTER(U)), C.POINTER(C.c_uint)],
)
fn(x, "XFetchName", Int, [P, U, C.POINTER(C.c_char_p)])
fn(x, "XFree", Int, [P])
fn(
    x,
    "XGetGeometry",
    Int,
    [
        P,
        U,
        C.POINTER(U),
        C.POINTER(Int),
        C.POINTER(Int),
        C.POINTER(C.c_uint),
        C.POINTER(C.c_uint),
        C.POINTER(C.c_uint),
        C.POINTER(C.c_uint),
    ],
)
fn(
    x,
    "XTranslateCoordinates",
    Int,
    [P, U, U, Int, Int, C.POINTER(Int), C.POINTER(Int), C.POINTER(U)],
)
fn(x, "XFlush", Int, [P])
fn(x, "XGetImage", P, [P, U, Int, Int, C.c_uint, C.c_uint, U, Int])
fn(x, "XDestroyImage", Int, [P])
fn(t, "XTestFakeMotionEvent", Int, [P, Int, Int, Int, U])
fn(t, "XTestFakeButtonEvent", Int, [P, C.c_uint, Int, U])


# SDL probes may destroy an auxiliary window between XQueryTree and XFetchName.
_error_handler_type = C.CFUNCTYPE(Int, P, P)


class XError(C.Structure):
    _fields_ = [
        ("type", Int),
        ("display", P),
        ("resource", U),
        ("serial", U),
        ("code", C.c_ubyte),
        ("request", C.c_ubyte),
        ("minor", C.c_ubyte),
    ]


_unexpected_errors = []


def record_error(display, event):
    error = C.cast(event, C.POINTER(XError)).contents
    if error.code not in (3, 9):
        _unexpected_errors.append((error.code, error.request, error.resource))
    return 0


_error_handler = _error_handler_type(record_error)
fn(x, "XSetErrorHandler", P, [_error_handler_type])(_error_handler)


class Image(C.Structure):
    _fields_ = [
        ("width", Int),
        ("height", Int),
        ("xoffset", Int),
        ("format", Int),
        ("data", P),
        ("byte_order", Int),
        ("bitmap_unit", Int),
        ("bitmap_bit_order", Int),
        ("bitmap_pad", Int),
        ("depth", Int),
        ("bytes_per_line", Int),
        ("bits_per_pixel", Int),
        ("red_mask", U),
        ("green_mask", U),
        ("blue_mask", U),
    ]


class Display:
    def __init__(self, name):
        self.d = x.XOpenDisplay(name.encode())
        assert self.d
        self.root = x.XDefaultRootWindow(self.d)

    def windows(self):
        r, p = U(), U()
        children = C.POINTER(U)()
        n = C.c_uint()
        x.XQueryTree(self.d, self.root, C.byref(r), C.byref(p), C.byref(children), C.byref(n))
        result = []
        for w in children[: n.value]:
            name = C.c_char_p()
            x.XFetchName(self.d, w, C.byref(name))
            result.append((w, name.value.decode(errors="replace") if name.value else ""))
            if name:
                x.XFree(name)
        x.XFree(children)
        return result

    def geometry(self, w):
        r = U()
        a, b = Int(), Int()
        width, height, border, depth = [C.c_uint() for _ in range(4)]
        status = x.XGetGeometry(
            self.d,
            w,
            C.byref(r),
            C.byref(a),
            C.byref(b),
            C.byref(width),
            C.byref(height),
            C.byref(border),
            C.byref(depth),
        )
        if not status:
            raise OSError("X11 window disappeared during enumeration")
        return width.value, height.value

    def capture(self, w, path):
        width, height = self.geometry(w)
        p = x.XGetImage(self.d, w, 0, 0, width, height, 0xFFFFFFFF, 2)
        assert p
        im = C.cast(p, C.POINTER(Image)).contents
        assert im.bits_per_pixel == 32 and im.byte_order == 0 and im.red_mask == 0xFF0000
        data = C.string_at(im.data, im.bytes_per_line * height)
        raw = b"".join(
            data[y * im.bytes_per_line : y * im.bytes_per_line + width * 4] for y in range(height)
        )
        rgb = bytearray(width * height * 3)
        rgb[0::3] = raw[2::4]
        rgb[1::3] = raw[1::4]
        rgb[2::3] = raw[0::4]
        x.XDestroyImage(p)

        def chunk(k, data):
            return struct.pack(">I", len(data)) + k + data + struct.pack(">I", zlib.crc32(k + data))

        rows = b"".join(b"\0" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))
        Path(path).write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows))
            + chunk(b"IEND", b"")
        )
        return bytes(rgb)

    def click(self, w, px, py, button):
        rx, ry = Int(), Int()
        child = U()
        x.XTranslateCoordinates(
            self.d, w, self.root, px, py, C.byref(rx), C.byref(ry), C.byref(child)
        )
        t.XTestFakeMotionEvent(self.d, -1, rx.value, ry.value, 0)
        t.XTestFakeButtonEvent(self.d, button, 1, 0)
        t.XTestFakeButtonEvent(self.d, button, 0, 0)
        x.XFlush(self.d)


class ClientData(C.Union):
    _fields_ = [("b", C.c_char * 20), ("s", C.c_short * 10), ("l", C.c_long * 5)]


class ClientMessage(C.Structure):
    _fields_ = [
        ("type", Int),
        ("serial", U),
        ("send_event", Int),
        ("display", P),
        ("window", U),
        ("message_type", U),
        ("format", Int),
        ("data", ClientData),
    ]


class Event(C.Union):
    _fields_ = [("client", ClientMessage), ("pad", C.c_long * 24)]


fn(x, "XInternAtom", U, [P, C.c_char_p, Int])
fn(x, "XSendEvent", Int, [P, U, Int, C.c_long, C.POINTER(Event)])
fn(x, "XResizeWindow", Int, [P, U, C.c_uint, C.c_uint])
fn(x, "XMoveWindow", Int, [P, U, Int, Int])
fn(x, "XCloseDisplay", Int, [P])


def close(self, w):
    e = Event()
    e.client.type = 33
    e.client.display = self.d
    e.client.window = w
    e.client.message_type = x.XInternAtom(self.d, b"WM_PROTOCOLS", 0)
    e.client.format = 32
    e.client.data.l[0] = x.XInternAtom(self.d, b"WM_DELETE_WINDOW", 0)
    x.XSendEvent(self.d, w, 0, 0, C.byref(e))
    x.XFlush(self.d)


def resize(self, w, width, height):
    # Without a window manager, keep the expanded capture rectangle on-screen.
    screen_width, screen_height = self.geometry(self.root)
    px, py = self.position(w)
    x.XMoveWindow(
        self.d, w, max(0, min(px, screen_width - width)), max(0, min(py, screen_height - height))
    )
    x.XResizeWindow(self.d, w, width, height)
    x.XFlush(self.d)


Display.close = close
Display.resize = resize


def position(self, w):
    rx, ry = Int(), Int()
    child = U()
    x.XTranslateCoordinates(self.d, w, self.root, 0, 0, C.byref(rx), C.byref(ry), C.byref(child))
    return rx.value, ry.value


def move_pointer(self, px, py):
    t.XTestFakeMotionEvent(self.d, -1, px, py, 0)
    x.XFlush(self.d)


Display.position = position
Display.move_pointer = move_pointer


def check_errors():
    assert not _unexpected_errors, _unexpected_errors
