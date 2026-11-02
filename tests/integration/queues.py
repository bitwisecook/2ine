#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise named queue ordering, opaque addresses, notifications and blocking workers."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def queue_program(order, checked):
    g = FlatGuest()
    queue, opened, owner, count, event, cursor = (g.field(bytes(4)) for _ in range(6))
    request = g.field(bytes(8))
    size, data = g.field(bytes(4)), g.field(bytes(4))
    priority = g.field(struct.pack("<I", 0xAABBCC00))
    name = g.field(b"\\QUEUES\\Fixture\0")
    alias = g.field(b"\\queues\\fixture\0")
    invalid = g.field(b"Fixture\0")
    g.call(1, 324, 0, event, 0, 0)
    g.result(0, 1)
    g.call(2, 16, queue, 3, name)
    g.result(87, 2)
    g.call(2, 16, queue, order, invalid)
    g.result(335, 3)
    g.call(2, 16, queue, order, name)
    g.result(0, 4)
    g.call(2, 16, opened, order, alias)
    g.result(332, 5)
    g.call(2, 15, owner, opened, alias)
    g.result(0, 6)
    g.load(opened)
    g.require(b"\x3b\x05" + struct.pack("<I", queue), 7)
    # A nonblocking read requires an event even before the first write.
    g.call_handle(2, 9, queue, request, size, data, 0, 1, priority, 0)
    g.result(87, 8)
    g.call_handle(2, 9, queue, request, size, data, 0, 2, priority, (event,))
    g.result(433, 9)
    g.call_handle(2, 9, queue, request, size, data, 0, 1, priority, (event,))
    g.result(342, 10)
    for n, level in enumerate((1, 14, 255)):
        g.call_handle(2, 14, queue, 10 + n, 4 + n, 0xDEADBE00 + n, level)
        g.result(0, 11)
    g.call(1, 329, (event,), 0)
    g.result(0, 12)
    g.call_handle(2, 12, queue, count)
    g.result(0, 13)
    g.word(count, 3, 14)
    sequence = (0, 1, 2) if order == 0 else (2, 1, 0)
    for n in sequence:
        g.call_handle(2, 13, queue, request, size, data, cursor, 1, priority, (event,))
        g.result(0, 15)
        g.word(request + 4, 10 + n, 16)
    g.call_handle(2, 13, queue, request, size, data, cursor, 1, priority, (event,))
    g.result(340, 17)
    # Invalid bridge outputs must be rejected before consuming the queued item.
    if checked:
        g.call_handle(2, 9, queue, request, size, 0xFFFFFFF0, 0, 1, priority, (event,))
        g.result(87, 18)
    for n in sequence:
        g.call_handle(2, 9, queue, request, size, data, 0, 1, priority, (event,))
        g.result(0, 19)
        g.word(request + 4, 10 + n, 20)
        g.load(owner)
        g.require(b"\x3b\x05" + struct.pack("<I", request), 21)
        g.word(size, 4 + n, 22)
        g.word(data, 0xDEADBE00 + n, 23)
        expected = min((1, 14, 255)[n], 15) if order == 2 else 0
        g.word(priority, 0xAABBCC00 | expected, 24)
    g.call_handle(2, 12, queue, count)
    g.word(count, 0, 25)
    g.call_handle(2, 14, queue, 99, 0, 0, 0)
    g.call_handle(2, 10, queue)
    g.result(0, 26)
    g.call_handle(2, 9, queue, request, size, data, 0, 1, priority, (event,))
    g.result(342, 27)
    g.call_handle(2, 11, opened)
    g.result(0, 28)
    g.call_handle(2, 12, queue, count)
    g.result(337, 29)
    g.call(2, 15, owner, opened, alias)
    g.result(343, 30)
    g.call(1, 326, (event,))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "QUECALLS"], "QUEUES", data=g.data)


def blocking_program():
    g = FlatGuest()
    queue, thread, started, status = (g.field(bytes(4)) for _ in range(4))
    request, size, data, priority = (
        g.field(bytes(8)),
        g.field(bytes(4)),
        g.field(bytes(4)),
        g.field(bytes(4)),
    )
    name = g.field(b"\\QUEUES\\Worker\0")
    g.call(2, 16, queue, 0, name)
    g.result(0, 1)
    g.call(1, 311, thread, 0x88776655, 0, 0, 65536)
    g.result(0, 2)
    g.call_handle(2, 9, queue, request, size, data, 0, 0, priority, 0)
    g.result(0, 3)
    g.word(request + 4, 77, 4)
    g.word(data, 0x12345678, 5)
    g.call(1, 349, thread, 0)
    g.result(0, 6)
    g.call(1, 324, 0, started, 0, 0)
    g.call(1, 311, thread, 0x77665544, 0, 0, 65536)
    g.result(0, 7)
    g.call(1, 329, (started,), 3000)
    g.result(0, 8)
    g.call(1, 229, 20)
    g.call_handle(2, 11, queue)
    g.result(0, 9)
    g.call(1, 349, thread, 0)
    g.result(0, 10)
    g.word(status, 337, 11)
    g.call(1, 326, (started,))
    g.exit(0)
    writer = 0x10000 + len(g.code)
    g.call(1, 229, 20)
    g.call_handle(2, 14, queue, 77, 4, 0x12345678, 0)
    g.result(0, 12)
    g.code.extend(b"\xc3")
    reader = 0x10000 + len(g.code)
    g.call(1, 328, (started,))
    g.call_handle(2, 9, queue, request, size, data, 0, 0, priority, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", status) + b"\xc3")
    for token, address in ((0x88776655, writer), (0x77665544, reader)):
        g.code = g.code.replace(struct.pack("<I", token), struct.pack("<I", address))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "QUECALLS"], "QUEUEWORK", data=g.data)


def cancelled_program(spin=False):
    g = FlatGuest()
    queue, thread, started, count = (g.field(bytes(4)) for _ in range(4))
    request = g.field(bytes(8))
    size, data, priority = (g.field(bytes(4)) for _ in range(3))
    name = g.field(b"\\QUEUES\\CancelFixture\0")
    g.call(2, 16, queue, 0, name)
    g.result(0, 1)
    g.call(1, 324, 0, started, 0, 0)
    g.call(1, 311, thread, 0x88776655, 0, 0, 65536)
    g.result(0, 2)
    g.call(1, 329, (started,), 3000)
    g.result(0, 3)
    g.call(1, 229, 30)
    g.call(1, 111, (thread,))
    g.result(0, 4)
    g.call(1, 349, thread, 0)
    g.result(0, 5)
    g.call_handle(2, 12, queue, count)
    g.result(0, 6)
    g.word(count, 0, 7)
    g.call_handle(2, 14, queue, 88, 4, 0x55667788, 0)
    g.result(0, 8)
    g.call_handle(2, 9, queue, request, size, data, 0, 0, priority, 0)
    g.result(0, 9)
    g.word(request + 4, 88, 10)
    g.word(data, 0x55667788, 11)
    g.call_handle(2, 11, queue)
    g.result(0, 12)
    g.call(1, 326, (started,))
    g.exit(0)
    worker = 0x10000 + len(g.code)
    g.call(1, 328, (started,))
    if spin:
        g.code.extend(b"\xeb\xfe")
    else:
        g.call_handle(2, 9, queue, request, size, data, 0, 0, priority, 0)
        g.exit(99)
    g.code = g.code.replace(struct.pack("<I", 0x88776655), struct.pack("<I", worker))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "QUECALLS"], "QUEUEKILL", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
    )
    with tempfile.TemporaryDirectory(prefix="twoine-queues-") as temporary:
        path = Path(temporary) / "queues.exe"
        env["TWOINE_IPC_DIR"] = str(Path(temporary) / "ipc")
        payloads = [
            *(queue_program(order, args.backend == "unicorn") for order in range(3)),
            blocking_program(),
        ]
        if os.name == "posix":
            payloads.append(cancelled_program())
            if args.backend == "unicorn":
                payloads.append(cancelled_program(spin=True))
        for index, payload in enumerate(payloads):
            path.write_bytes(payload)
            try:
                result = subprocess.run(
                    [str(build / "lx_loader"), str(path)], env=env, capture_output=True, timeout=15
                )
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(f"queue contract {index} timed out") from error
            if result.returncode:
                raise RuntimeError(
                    f"queue exit={result.returncode}\n" + result.stderr.decode(errors="replace")
                )
    print(
        "Queue ordering, pointer payloads, event notifications, blocking reads and close wakeups passed"
    )


if __name__ == "__main__":
    main()
