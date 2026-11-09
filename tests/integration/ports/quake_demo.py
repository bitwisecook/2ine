# Public domain. No rights reserved.
"""Read Quake protocol-15 demos to verify server-authoritative player positions."""

import struct


class Message:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, count):
        end = self.offset + count
        if count < 0 or end > len(self.data):
            raise ValueError("Truncated Quake message")
        out = self.data[self.offset : end]
        self.offset = end
        return out

    def byte(self):
        return self.take(1)[0]

    def short(self):
        return struct.unpack("<H", self.take(2))[0]

    def coord(self):
        return struct.unpack("<h", self.take(2))[0] / 8

    def string(self):
        end = self.data.index(0, self.offset)
        return self.take(end - self.offset + 1)[:-1]


def player_positions(path):
    data = path.read_bytes()
    offset = data.index(b"\n") + 1
    baseline = {}
    positions = {1: set(), 2: set()}
    packets = 0
    while offset < len(data):
        size = struct.unpack_from("<i", data, offset)[0]
        if size < 1 or size > 65536 or offset + 16 + size > len(data):
            raise ValueError("Invalid demo packet")
        m = Message(data[offset + 16 : offset + 16 + size])
        offset += 16 + size
        packets += 1
        while m.offset < len(m.data):
            command = m.byte()
            if command & 128:
                bits = command & 127
                if bits & 1:
                    bits |= m.byte() << 8
                entity = m.short() if bits & 16384 else m.byte()
                for flag in (1024, 64, 2048, 4096, 8192):
                    if bits & flag:
                        m.take(1)
                origin = list(baseline.get(entity, (0, 0, 0)))
                for axis, (coordinate, angle) in enumerate(((2, 256), (4, 16), (8, 512))):
                    if bits & coordinate:
                        origin[axis] = m.coord()
                    if bits & angle:
                        m.take(1)
                if entity in positions:
                    positions[entity].add(tuple(origin))
            elif command in (1, 2, 27, 28, 30, 33):
                pass
            elif command in (3,):
                m.take(5)
            elif command in (4, 7):
                m.take(4)
            elif command in (5, 16, 17, 32):
                m.take(2)
            elif command == 6:
                mask = m.byte()
                m.take(bool(mask & 1) + bool(mask & 2) + 9)
            elif command in (8, 9, 26, 31, 34):
                m.string()
            elif command == 10:
                m.take(3)
            elif command == 11:
                if struct.unpack("<i", m.take(4))[0] != 15:
                    raise ValueError("Unsupported Quake protocol")
                m.take(2)
                m.string()
                for _ in range(2):
                    while m.string():
                        pass
            elif command in (12, 13):
                m.take(1)
                m.string()
            elif command == 14:
                m.take(3)
            elif command == 15:
                bits = m.short()
                m.take((bits & 255).bit_count() + 4 + ((bits >> 12) & 7).bit_count() + 8)
            elif command == 18:
                m.take(11)
            elif command == 19:
                m.take(8)
            elif command in (20, 22):
                entity = m.short() if command == 22 else None
                m.take(4)
                origin = []
                for _ in range(3):
                    origin.append(m.coord())
                    m.take(1)
                if entity is not None:
                    baseline[entity] = origin
            elif command == 23:
                effect = m.byte()
                if effect in (5, 6, 9, 13):
                    m.take(14)
                elif effect == 12:
                    m.take(8)
                elif effect <= 11:
                    m.take(6)
                else:
                    raise ValueError("Unsupported temporary entity")
            elif command in (24, 25):
                m.take(1)
            elif command == 29:
                m.take(9)
            else:
                raise ValueError(f"Unknown server command {command}")
    return packets, positions
