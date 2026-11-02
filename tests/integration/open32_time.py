# Public domain. No rights reserved. Wholly AI-authored.
"""FILETIME conversion preserves subsecond ticks and uses the configured local bias."""

import struct
from datetime import UTC, datetime

from images import FlatGuest, linear_image


def file_time_conversion_program(checked):
    g = FlatGuest()
    epoch = datetime(1601, 1, 1, tzinfo=UTC)
    moment = datetime(2024, 2, 29, 12, 34, 56, 789000, tzinfo=UTC)
    delta = moment - epoch
    ticks = (delta.days * 86400 + delta.seconds) * 10000000 + delta.microseconds * 10 + 1234
    input = g.field(struct.pack("<Q", ticks))
    local = g.field(bytes(8))
    restored = g.field(bytes(8))
    system = g.field(bytes(16))
    invalid = g.field(struct.pack("<Q", 1 << 63))
    zero = g.field(bytes(8))
    g.call(2, 204, input, local)
    g.result(1, 1)
    # The harness uses Pacific/Honolulu, UTC-10 without seasonal changes.
    expected = ticks - 10 * 3600 * 10000000
    g.word(local, expected & 0xFFFFFFFF, 2)
    g.word(local + 4, expected >> 32, 3)
    g.call(2, 467, local, restored)
    g.result(1, 4)
    g.word(restored, ticks & 0xFFFFFFFF, 5)
    g.word(restored + 4, ticks >> 32, 6)
    g.call(2, 205, input, system)
    g.result(1, 7)
    for offset, pair in enumerate([(2024, 2), (4, 29), (12, 34), (56, 789)]):
        g.word(system + offset * 4, pair[0] | pair[1] << 16, 8)
    g.call(2, 205, zero, system)
    g.result(1, 9)
    g.word(system, 1601 | 1 << 16, 10)
    g.word(system + 4, 1 | 1 << 16, 11)
    g.call(2, 204, invalid, restored)
    g.result(0, 12)
    g.call(2, 204, input, input)
    g.result(0, 13)
    g.call(2, 205, invalid, system)
    g.result(0, 14)
    g.word(system, 1601 | 1 << 16, 15)
    if checked:
        g.call(2, 204, input, 0xFFFFFFFF)
        g.result(0, 16)
        g.call(2, 467, 0xFFFFFFFF, restored)
        g.result(0, 17)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FTCONVERT32", data=g.data)
