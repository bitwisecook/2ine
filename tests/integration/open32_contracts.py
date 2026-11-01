# Public domain. No rights reserved. Wholly AI-authored.
"""Generated guests for Open32 pixels, fonts, file IO and variadic formatting."""

import struct

from images import FlatGuest, linear_image

API = {
    "CreateSolidBrush": 97,
    "FillRect": 207,
    "GetDC": 263,
    "ReleaseDC": 560,
    "CreateCompatibleDC": 62,
    "CreateCompatibleBitmap": 61,
    "LoadBitmap": 458,
    "GetObject": 326,
    "SelectObject": 585,
    "GetPixel": 335,
    "BitBlt": 18,
    "DeleteDC": 135,
    "DeleteObject": 140,
    "CreateFontIndirect": 76,
    "GetStockObject": 353,
    "DrawText": 162,
    "SetBkMode": 594,
    "SetTextColor": 649,
    "TextOut": 689,
    "OpenFile": 504,
    "ReadFile": 535,
    "WriteFile": 717,
    "CloseHandle": 44,
    "_llseek": 725,
    "_lwrite": 728,
    "_lread": 727,
    "_lclose": 723,
    "wsprintf": 720,
    "wvsprintf": 721,
    "GetLastError": 300,
    "GetTextMetrics": 371,
    "EnumFontFamilies": 179,
    "InitializeCriticalSection": 419,
    "EnterCriticalSection": 175,
    "LeaveCriticalSection": 454,
    "DeleteCriticalSection": 134,
    "GetCurrentThreadId": 259,
    "CreateThread": 98,
    "GetExitCodeThread": 1006,
    "ResumeThread": 573,
    "WaitForSingleObject": 711,
    "ExitThread": 193,
    "Sleep": 673,
    "GetSystemTime": 361,
}


def store(g, address):
    g.code.extend(b"\xa3" + struct.pack("<I", address))


def call(g, name, *arguments):
    g.call(2, API[name], *arguments)


def drawing_program():
    g = FlatGuest()
    screen, source, target, bitmap, canvas, old_source, old_target, font, old_font = (
        g.field(bytes(4)) for _ in range(9)
    )
    info = g.field(bytes(60))
    rectangle = g.field(struct.pack("<4i", 0, 0, 100, 40))
    text = g.field(b"Hello\0")
    logfont = g.field(
        struct.pack("<5i8B32s", -16, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, b"Courier")
    )
    call(g, "GetDC", 0)
    store(g, screen)
    for address in (source, target):
        call(g, "CreateCompatibleDC", (screen,))
        store(g, address)
    call(g, "LoadBitmap", 0, 99)
    store(g, bitmap)
    call(g, "GetObject", (bitmap,), 24, info)
    g.result(24, 1)
    g.word(info + 4, 2, 2)
    g.word(info + 8, 2, 3)
    call(g, "SelectObject", (source,), (bitmap,))
    store(g, old_source)
    for x, y, pixel in ((0, 0, 0xFF0000), (0, 1, 0xFF), (1, 0, 0xFFFFFF), (1, 1, 0xFF00)):
        call(g, "GetPixel", (source,), x, y)
        g.result(pixel, 4)
    call(g, "DeleteObject", (bitmap,))
    g.result(0, 5)
    call(g, "CreateCompatibleBitmap", (screen,), 128, 64)
    store(g, canvas)
    call(g, "SelectObject", (target,), (canvas,))
    store(g, old_target)
    call(g, "BitBlt", (target,), 1, 1, 2, 2, (source,), 0, 0, 0xCC0020)
    g.result(1, 6)
    for x, y, pixel in ((0, 0, 0), (1, 1, 0xFF0000), (1, 2, 0xFF), (2, 1, 0xFFFFFF)):
        call(g, "GetPixel", (target,), x, y)
        g.result(pixel, 7)
    brush, previous_brush = (g.field(bytes(4)) for _ in range(2))
    fill = g.field(struct.pack("<4i", 0, 50, 4, 54))
    call(g, "CreateSolidBrush", 0xFF)
    store(g, brush)
    call(g, "GetObject", (brush,), 12, info)
    g.result(12, 110)
    g.word(info, 0, 111)
    g.word(info + 4, 0xFF, 112)
    call(g, "SelectObject", (target,), (brush,))
    store(g, previous_brush)
    call(g, "DeleteObject", (brush,))
    g.result(0, 113)
    call(g, "FillRect", (target,), fill, (brush,))
    g.result(1, 114)
    call(g, "GetPixel", (target,), 1, 50)
    g.result(0xFF, 115)
    call(g, "GetPixel", (target,), 1, 49)
    g.result(0, 116)
    call(g, "GetStockObject", 5)
    store(g, old_font)
    call(g, "FillRect", (target,), fill, (old_font,))
    g.result(1, 117)
    call(g, "GetPixel", (target,), 1, 50)
    g.result(0xFF, 118)
    call(g, "DeleteObject", (old_font,))
    g.result(0, 119)
    call(g, "SelectObject", (target,), (previous_brush,))
    call(g, "DeleteObject", (brush,))
    g.result(1, 120)
    call(g, "CreateFontIndirect", logfont)
    store(g, font)
    call(g, "SelectObject", (target,), (font,))
    store(g, old_font)
    call(g, "GetObject", (font,), 60, info)
    g.result(60, 8)
    g.word(info, 0xFFFFFFF0, 9)
    g.word(info + 28, int.from_bytes(b"Cour", "little"), 10)
    call(g, "DeleteObject", (font,))
    g.result(0, 11)
    wide = g.field(struct.pack("<4i", 0, 0, 100, 40))
    call(g, "DrawText", (target,), text, 5, wide, 0x20)
    call(g, "GetPixel", (target,), 0, 0)
    g.result(0xFFFFFF, 121)
    call(g, "GetPixel", (target,), 99, 39)
    g.result(0, 122)
    call(g, "DrawText", (target,), text, 5, rectangle, 0x420)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 12)
    call(g, "SetBkMode", (target,), 1)
    g.result(2, 13)
    call(g, "SetTextColor", (target,), 0xFF)
    g.result(0, 14)
    call(g, "TextOut", (target,), 4, 16, text, 5)
    g.result(1, 15)
    # Scan the text band for actual red glyph coverage.
    x, y, colored = g.field(bytes(4)), g.field(struct.pack("<I", 16)), g.field(bytes(4))
    loop = len(g.code)
    call(g, "GetPixel", (target,), (x,), (y,))
    g.code.extend(b"\x25\xff\0\0\0\x09\xc0\x74\x0a\xc7\x05")
    g.code.extend(struct.pack("<II", colored, 1))
    g.code.extend(b"\xff\x05" + struct.pack("<I", x))
    g.code.extend(b"\x81\x3d" + struct.pack("<II", x, 80) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", x, 0))
    g.code.extend(b"\xff\x05" + struct.pack("<I", y))
    g.code.extend(b"\x81\x3d" + struct.pack("<II", y, 40) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.word(colored, 1, 16)
    call(g, "SelectObject", (target,), (old_font,))
    call(g, "DeleteObject", (font,))
    g.result(1, 17)
    for address in (source, target):
        call(g, "DeleteDC", (address,))
        g.result(1, 18)
    for address in (bitmap, canvas):
        call(g, "DeleteObject", (address,))
        g.result(1, 19)
    call(g, "ReleaseDC", 0, (screen,))
    g.result(1, 20)
    g.exit(0)
    bits = bytes.fromhex("0000ff00ff000000ff0000ffffff0000")
    image = struct.pack("<2sIHHI", b"BM", 42, 0, 0, 26)
    image += struct.pack("<IHHHH", 12, 2, 2, 1, 24) + bits
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX"], "DRAW32", data=g.data, resources=[(2, 99, image)]
    )


def file_program():
    g = FlatGuest()
    name = g.field(b"io.txt\0")
    structure, handle, count, buffer = (g.field(bytes(n)) for n in (136, 4, 4, 32))
    text = g.field(b"hello\0")
    call(g, "OpenFile", name, structure, 0x1002)
    store(g, handle)
    call(g, "WriteFile", (handle,), text, 5, count, 0)
    g.result(1, 30)
    g.word(count, 5, 31)
    call(g, "_llseek", (handle,), 0, 0)
    g.result(0, 32)
    call(g, "ReadFile", (handle,), buffer, 9, count, 0)
    g.result(1, 33)
    g.word(count, 5, 34)
    g.word(buffer, int.from_bytes(b"hell", "little"), 35)
    call(g, "ReadFile", (handle,), buffer, 9, count, 0)
    g.result(1, 36)
    g.word(count, 0, 37)
    call(g, "_llseek", (handle,), 2, 0)
    g.result(2, 38)
    call(g, "_lwrite", (handle,), 0, 0)
    g.result(0, 39)
    call(g, "CloseHandle", (handle,))
    g.result(1, 40)
    call(g, "_lclose", (handle,))
    g.result(0xFFFFFFFF, 41)
    call(g, "WriteFile", (handle,), 0, 0, count, 0)
    g.result(0, 44)
    call(g, "GetLastError")
    g.result(6, 45)

    call(g, "OpenFile", 0xDEADBEEF, structure, 0x8000)
    store(g, handle)
    call(g, "_lread", (handle,), buffer, 9)
    g.result(2, 42)
    call(g, "_lclose", (handle,))
    g.result(0, 43)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FILE32", data=g.data)


def format_program():
    g = FlatGuest()
    text = g.field(b"hello\0")
    fmt = g.field(b"%s %04x %d %u %%\0")
    short = g.field(b"%*.*s\0")
    output = g.field(bytes(1024))
    args = g.field(struct.pack("<4I", text, 42, 0xFFFFFFF9, 0xFFFFFFFF))
    expected = b"hello 002a -7 4294967295 %\0"
    for name, tail in (("wsprintf", (text, 42, 0xFFFFFFF9, 0xFFFFFFFF)), ("wvsprintf", (args,))):
        call(g, name, output, fmt, *tail)
        g.result(len(expected) - 1, 50)
        for offset in range(0, len(expected) - 3, 4):
            g.word(output + offset, int.from_bytes(expected[offset : offset + 4], "little"), 51)
    call(g, "wsprintf", output, short, 5, 3, text)
    g.result(5, 52)
    g.word(output, int.from_bytes(b"  he", "little"), 53)
    call(g, "wvsprintf", output, fmt, 0xDEADBEEF)
    g.result(0xFFFFFFFF, 54)
    call(g, "GetLastError")
    g.result(87, 55)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FORMAT32", data=g.data)


def font_program():
    g = FlatGuest()
    screen, count, stopping = (g.field(bytes(4)) for _ in range(3))
    metrics, output = g.field(bytes(56)), g.field(bytes(16))
    family, missing, fmt = g.field(b"Courier\0"), g.field(b"AbsentFont\0"), g.field(b"%u\0")
    call(g, "GetDC", 0)
    store(g, screen)
    call(g, "GetTextMetrics", (screen,), metrics)
    g.result(1, 60)
    g.load(metrics)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 61)
    call(g, "EnumFontFamilies", (screen,), family, 0x77665544, 42)
    g.result(1, 62)
    g.word(count, 4, 63)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 0))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", stopping, 2))
    call(g, "EnumFontFamilies", (screen,), 0, 0x77665544, 42)
    g.result(0, 64)
    g.word(count, 2, 65)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 0))
    call(g, "EnumFontFamilies", (screen,), missing, 0x77665544, 42)
    g.result(0, 66)
    g.word(count, 0, 67)
    call(g, "ReleaseDC", 0, (screen,))
    g.result(1, 68)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.require(b"\x81\x7c\x24\x10" + struct.pack("<I", 42), 69)
    g.code.extend(b"\x8b\x44\x24\x04")
    g.require(b"\x81\x38" + struct.pack("<I", 0xFFFFFFF0), 70)
    g.code.extend(b"\x8b\x44\x24\x08\x8b\x00")
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 71)
    call(g, "wsprintf", output, fmt, 42)
    g.result(2, 72)
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    g.load(count)
    g.code.extend(b"\x3b\x05" + struct.pack("<I", stopping) + b"\x0f\x95\xc0\x0f\xb6\xc0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x77665544), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FONTS32", data=g.data)


def critical_program():
    g = FlatGuest()
    section = g.field(bytes(24))
    call(g, "InitializeCriticalSection", section)
    g.word(section + 4, 0xFFFFFFFF, 80)
    call(g, "GetCurrentThreadId")
    g.result(1, 81)
    call(g, "EnterCriticalSection", section)
    call(g, "EnterCriticalSection", section)
    g.word(section + 4, 1, 82)
    g.word(section + 8, 2, 83)
    g.word(section + 12, 1, 84)
    call(g, "DeleteCriticalSection", section)
    call(g, "GetLastError")
    g.result(170, 85)
    call(g, "LeaveCriticalSection", section)
    call(g, "LeaveCriticalSection", section)
    g.word(section + 4, 0xFFFFFFFF, 86)
    g.word(section + 12, 0, 87)
    call(g, "DeleteCriticalSection", section)
    call(g, "EnterCriticalSection", section)
    call(g, "GetLastError")
    g.result(87, 88)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "CRIT32", data=g.data)


def thread_program():
    g = FlatGuest()
    section, handle, tid, result = (g.field(bytes(n)) for n in (24, 4, 4, 4))
    before, after = g.field(bytes(4)), g.field(bytes(4))
    call(g, "InitializeCriticalSection", section)
    call(g, "EnterCriticalSection", section)
    call(g, "CreateThread", 0, 0, 0x66554433, 0xBEEF, 4, tid)
    store(g, handle)
    g.word(tid, 2, 90)
    call(g, "WaitForSingleObject", (handle,), 0)
    g.result(258, 91)
    call(g, "GetExitCodeThread", (handle,), result)
    g.result(1, 92)
    g.word(result, 259, 93)
    call(g, "ResumeThread", (handle,))
    g.result(1, 94)
    # The worker announces its attempt before waiting on our critical section.
    loop = len(g.code)
    call(g, "Sleep", 1)
    g.load(before)
    g.code.extend(b"\x85\xc0\x0f\x84" + struct.pack("<i", loop - len(g.code) - 8))
    g.word(after, 0, 95)
    call(g, "LeaveCriticalSection", section)
    call(g, "WaitForSingleObject", (handle,), 3000)
    g.result(0, 96)
    g.word(after, 1, 97)
    call(g, "GetExitCodeThread", (handle,), result)
    g.result(1, 98)
    g.word(result, 43, 99)
    call(g, "CloseHandle", (handle,))
    g.result(1, 100)
    call(g, "GetExitCodeThread", (handle,), result)
    g.result(0, 101)
    call(g, "DeleteCriticalSection", section)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.require(b"\x81\x7c\x24\x04" + struct.pack("<I", 0xBEEF), 102)
    call(g, "GetCurrentThreadId")
    g.result(2, 103)
    call(g, "GetLastError")
    g.result(0, 104)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", before, 1))
    call(g, "EnterCriticalSection", section)
    g.word(section + 12, 2, 105)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", after, 1))
    call(g, "LeaveCriticalSection", section)
    g.code.extend(b"\xb8\x2b\0\0\0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x66554433), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "THREAD32", data=g.data)


def time_program():
    g = FlatGuest()
    output, actual = g.field(bytes(16)), g.field(bytes(4))
    call(g, "GetSystemTime", output)
    g.call(1, 282, 1, output, 16, actual)
    g.result(0, 130)
    g.word(actual, 16, 131)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "TIME32", data=g.data)
