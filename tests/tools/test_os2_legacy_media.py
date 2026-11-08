# Public domain. No rights reserved. Wholly AI-authored.
"""Legacy ZIP interoperability and preservation of full-media alternatives."""

import base64
import hashlib
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from tests.tools import test_os2_support as support
from tools import ball as ball_api


def implode_zip(
    name, payload, flags=0, bad_crc=False, bad_tree=False, reserved_local=False, method=6
):
    """Small independently framed ZIP method-6 fixture with fixed full trees."""
    trees = (bytes([15]) + bytes([0xF7]) * 16) if flags & 4 else b""
    trees += (bytes([3]) + bytes([0xF5]) * 4) * 2
    if bad_tree:
        trees = b"\x00\x00" + trees[2:]
    bits = []

    def emit(value, width):
        bits.extend((value >> bit) & 1 for bit in range(width))

    for byte in payload:
        emit(1, 1)
        value = (~int(f"{byte:08b}"[::-1], 2) & 255) if flags & 4 else byte
        emit(value, 8)
    data = bytearray(trees)
    for start in range(0, len(bits), 8):
        data.append(sum(bit << i for i, bit in enumerate(bits[start : start + 8])))
    if method == 1:
        bits = [(byte >> bit) & 1 for byte in payload for bit in range(9)]
        data = bytearray(
            sum(bit << i for i, bit in enumerate(bits[start : start + 8]))
            for start in range(0, len(bits), 8)
        )
    elif 2 <= method <= 5:
        # 256 empty follower sets (six bits each), then uncompressed literals.
        assert 0x90 not in payload
        data = bytearray(192) + payload
    name = name.encode()
    crc = zlib.crc32(payload) ^ int(bad_crc)
    descriptor = bool(flags & 8)
    local = (
        struct.pack(
            "<I5H3I2H",
            0x04034B50,
            10,
            flags,
            method,
            0,
            0,
            0 if descriptor else crc,
            0 if descriptor else len(data),
            0 if descriptor else len(payload),
            len(name),
            0,
        )
        + name
        + data
    )
    if descriptor:
        local += struct.pack("<4I", 0x08074B50, crc, len(data), len(payload))
    central = (
        struct.pack(
            "<I6H3I5H2I",
            0x02014B50,
            20,
            10,
            flags,
            method,
            0,
            0,
            crc,
            len(data),
            len(payload),
            len(name),
            0,
            0,
            0,
            0,
            0,
            0,
        )
        + name
    )
    end = struct.pack("<I4H2IH", 0x06054B50, 0, 0, 1, 1, len(central), len(local), 0)
    return local + central + end


def crc16(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value


def bit_bytes(values, msb=False):
    bits = []
    for value, width in values:
        bits.extend(
            (value >> bit) & 1 for bit in (range(width - 1, -1, -1) if msb else range(width))
        )
    result = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        result[i // 8] |= bit << (7 - i % 8 if msb else i % 8)
    return bytes(result)


def arc_bytes(name, payload, method=2, bad_crc=False, compressed=None):
    packed = payload
    if method == 3:
        packed = payload.replace(b"\x90", b"\x90\x00")
    elif method in (8, 9):
        assert len(payload) < 250  # All fixture codes remain nine bits wide.
        packed = bit_bytes([(byte, 9) for byte in payload])
        if method == 8:
            packed = b"\x0c" + packed
    elif method == 4 and compressed is None:
        assert len(set(payload)) == 1
        packed = struct.pack("<Hhh", 1, -payload[0] - 1, -257)
        packed += bit_bytes([(0, 1)] * len(payload) + [(1, 1)])
    if compressed is not None:
        packed = compressed
    header = b"\x1a" + bytes([method]) + name.encode().ljust(13, b"\0")
    header += struct.pack("<IHHH", len(packed), 0x5021, 0, crc16(payload) ^ int(bad_crc))
    if method != 1:
        header += struct.pack("<I", len(payload))
    return header + packed + b"\x1a\0"


def arj_bytes(name, payload, method=0, bad_crc=False, compressed=None):
    packed = payload
    if 1 <= method <= 3:
        codes = []
        for byte in payload:
            codes += [(1, 16), (0, 5), (0, 5), (0, 9), (byte, 9), (0, 5), (0, 5)]
        packed = bit_bytes(codes, msb=True)
    elif method == 4:
        packed = bit_bytes([code for byte in payload for code in [(0, 1), (byte, 8)]], msb=True)

    if compressed is not None:
        packed = compressed

    def header(filename, kind, compressed=b"", original=b""):
        basic = bytearray(30)
        basic[:8] = bytes([30, 3, 11, 5, 0, method if kind != 2 else 0, kind, 0])
        struct.pack_into(
            "<IIII",
            basic,
            8,
            0,
            len(compressed),
            len(original),
            zlib.crc32(original) ^ int(bad_crc and kind != 2),
        )
        basic += filename.encode() + b"\0\0"
        return (
            b"\x60\xea"
            + struct.pack("<H", len(basic))
            + basic
            + struct.pack("<I", zlib.crc32(basic))
            + b"\0\0"
        )

    return header("fixture.arj", 2) + header(name, 0, packed, payload) + packed + b"\x60\xea\0\0"


def zoo_bytes(name, payload, method=0, version=2, directory="", bad_crc=False, cycle=False):
    packed = payload
    if method == 1:
        packed = bit_bytes([(256, 9), *[(byte, 9) for byte in payload], (257, 9)])
    elif method == 2:
        codes = []
        for byte in payload:
            codes += [(1, 16), (0, 5), (0, 5), (0, 9), (byte, 9), (0, 4), (0, 4)]
        packed = bit_bytes(codes, msb=True)
    variable = bytes([len(name) + 1, len(directory) + int(bool(directory))]) + name.encode() + b"\0"
    if directory:
        variable += directory.encode() + b"\0"
    header = bytearray(51 if version == 1 else 56 + len(variable))
    data_at = 34 + len(header)
    struct.pack_into(
        "<IBBIIHHHII",
        header,
        0,
        0xFDC4A7DC,
        version,
        method,
        34 if cycle else data_at + len(packed),
        data_at,
        0x5021,
        0,
        crc16(payload) ^ int(bad_crc),
        len(payload),
        len(packed),
    )
    header[28:30] = bytes([2, 1])
    header[38:51] = b"CMD.EXE".ljust(13, b"\0")
    if version == 2:
        struct.pack_into("<H", header, 51, len(variable))
        header[53] = 127
        header[56:] = variable
        struct.pack_into("<H", header, 54, crc16(header))
    global_ = bytearray(34)
    global_[:4] = b"ZOO "
    struct.pack_into("<III", global_, 20, 0xFDC4A7DC, 34, (-34) & 0xFFFFFFFF)
    global_[32:34] = bytes([2, 1])
    trailer = struct.pack("<I", 0xFDC4A7DC) + bytes(47)
    return global_ + header + packed + trailer


class LegacyMediaTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-legacy-media-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "Test.2ineball"
        ball_api.init(self.ball)

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def media(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return str(path)

    def test_implode_all_window_literal_flags_and_data_descriptors(self):
        for flags in (0, 2, 4, 6, 8, 10, 12, 14):
            with self.subTest(flags=flags):
                source = self.media(
                    f"flags-{flags}.zip", implode_zip("CMD.EXE", support.CMD, flags)
                )
                result = self.request("import-os2-support", sources=[source])
                self.assertEqual(len(result["imported"]), 1)
                target = self.ball / "drive_c/OS2/CMD.EXE"
                self.assertEqual(target.read_bytes(), support.CMD)
                target.unlink()

    def test_shrink_and_all_reduce_factors(self):
        for method in range(1, 6):
            with self.subTest(method=method):
                source = self.media(
                    f"legacy-{method}.zip", implode_zip("CMD.EXE", support.CMD, method=method)
                )
                result = self.request("import-os2-support", sources=[source])
                self.assertEqual(len(result["imported"]), 1)
                target = self.ball / "drive_c/OS2/CMD.EXE"
                self.assertEqual(target.read_bytes(), support.CMD)
                target.unlink()

    def test_reserved_local_flag_and_self_extracting_prefix(self):
        source = self.media(
            "reserved.zip", implode_zip("CMD.EXE", support.CMD, reserved_local=True)
        )
        result = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(result["imported"]), 1)
        (self.ball / "drive_c/OS2/CMD.EXE").unlink()
        source = self.media("self.exe", b"MZ" + bytes(126) + implode_zip("CMD.EXE", support.CMD))
        result = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(result["imported"]), 1)

    def test_second_imploded_member_uses_its_actual_local_header(self):
        parts = [
            implode_zip("README.TXT", b"first unrelated member"),
            implode_zip("CMD.EXE", support.CMD),
        ]
        locals_, central = bytearray(), bytearray()
        for part in parts:
            offset = struct.unpack_from("<I", part, len(part) - 6)[0]
            entry = bytearray(part[offset:-22])
            struct.pack_into("<I", entry, 42, len(locals_))
            locals_ += part[:offset]
            central += entry
        end = struct.pack("<I4H2IH", 0x06054B50, 0, 0, 2, 2, len(central), len(locals_), 0)
        source = self.media("two.zip", locals_ + central + end)
        result = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(result["imported"]), 1)
        self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), support.CMD)

    def test_implode_crc_and_invalid_trees_reject_before_copy(self):
        for corrupt in ({"bad_crc": True}, {"bad_tree": True}):
            with self.subTest(corrupt=corrupt):
                source = self.media("corrupt.zip", implode_zip("CMD.EXE", support.CMD, **corrupt))
                with self.assertRaises(ValueError):
                    self.request("import-os2-support", sources=[source])
                self.assertFalse((self.ball / "drive_c/OS2/CMD.EXE").exists())

    def test_implode_can_wrap_nested_iso_and_pack(self):
        payload = support.pack2("CMD.EXE", support.CMD)
        image = support.iso_image(payload=payload, filename="FILES.PACK2")
        source = self.media("nested.zip", implode_zip("OS2.ISO", image, flags=6))
        result = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(result["imported"]), 1)
        self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), support.CMD)

    def test_full_import_preserves_distinct_variants_without_choosing_one(self):
        first = self.media("first.zip", support.zip_bytes("OS2/WINSHELD.EXE", b"first display"))
        second = self.media("second.zip", support.zip_bytes("OS2/WINSHELD.EXE", b"second display"))
        repeated = self.media("repeat.zip", support.zip_bytes("OS2/WINSHELD.EXE", b"first display"))
        report = self.request(
            "import-os2-support", sources=[first, second, repeated], categories=["full"]
        )
        self.assertFalse((self.ball / "drive_c/OS2/WINSHELD.EXE").exists())
        self.assertEqual(len(report["imported"]), 2)
        self.assertEqual(len(report["preservedVariants"]), 2)
        history = json.loads(Path(report["variantManifestPath"]).read_text())
        self.assertEqual(len(history), 2)
        for variant in report["preservedVariants"]:
            relative = variant["path"][3:].replace("\\", "/")
            contents = (self.ball / "drive_c" / relative).read_bytes()
            self.assertEqual(hashlib.sha256(contents).hexdigest(), variant["sha256"])
            self.assertEqual(variant["originalPath"], "C:\\OS2\\WINSHELD.EXE")
        second_report = self.request(
            "import-os2-support", sources=[first, second], categories=["full"]
        )
        self.assertEqual(second_report["imported"], [])
        self.assertEqual(len(second_report["preservedVariants"]), 2)
        with self.assertRaisesRegex(ValueError, "Different versions"):
            self.request(
                "import-os2-support",
                sources=[first, second],
                categories=["full"],
                modules=["WINSHELD.EXE"],
            )

    def test_arc_common_methods_and_crc(self):
        for method in (1, 2, 3, 8, 9):
            with self.subTest(method=method):
                source = self.media(f"arc-{method}.arc", arc_bytes("CMD.EXE", support.CMD, method))
                result = self.request("import-os2-support", sources=[source])
                self.assertEqual(len(result["imported"]), 1)
                target = self.ball / "drive_c/OS2/CMD.EXE"
                self.assertEqual(target.read_bytes(), support.CMD)
                target.unlink()
        source = self.media("bad.arc", arc_bytes("CMD.EXE", support.CMD, bad_crc=True))
        with self.assertRaises(ValueError):
            self.request("import-os2-support", sources=[source])

    def test_arc_squeeze_and_unimplemented_obsolete_modes(self):
        payload = b"A" * 100
        source = self.media("squeeze.arc", arc_bytes("TEST.TXT", payload, 4))
        report = self.request("import-os2-support", sources=[source], categories=["full"])
        self.assertEqual(len(report["imported"]), 1)
        path = report["imported"][0]["path"][3:].replace("\\", "/")
        self.assertEqual((self.ball / "drive_c" / path).read_bytes(), payload)
        source = self.media("old.arc", arc_bytes("CMD.EXE", support.CMD, 7))
        listing = self.request("browse-media", path=source)
        self.assertEqual(listing["entries"][0]["name"], "CMD.EXE")
        with self.assertRaises(ValueError):
            self.request("import-os2-support", sources=[source])

    def test_arc_squeeze_multi_level_tree_and_truncated_codes(self):
        # Node 0: 0 -> "A", 1 -> node 1; node 1: 0 -> "B", 1 -> end. Codes are read
        # least significant bit first: A = 0, B = 1 0, end = 1 1.
        payload = b"ABBABAAAB" * 7
        tree = struct.pack("<Hhhhh", 2, -ord("A") - 1, 1, -ord("B") - 1, -257)
        codes = [bit for byte in payload for bit in ([0] if byte == ord("A") else [1, 0])]
        source = self.media(
            "tree.arc",
            arc_bytes(
                "TEST.TXT",
                payload,
                4,
                compressed=tree + bit_bytes([(bit, 1) for bit in codes + [1, 1]]),
            ),
        )
        report = self.request("import-os2-support", sources=[source], categories=["full"])
        path = report["imported"][0]["path"][3:].replace("\\", "/")
        self.assertEqual((self.ball / "drive_c" / path).read_bytes(), payload)
        # Without the end code the reader runs out of bits inside the padding.
        source = self.media(
            "truncated.arc",
            arc_bytes(
                "TEST.TXT", payload, 4, compressed=tree + bit_bytes([(bit, 1) for bit in codes])
            ),
        )
        with self.assertRaises(ValueError):
            self.request("import-os2-support", sources=[source], categories=["full"])

    def test_arj_all_common_methods_and_crc(self):
        for method in range(5):
            with self.subTest(method=method):
                source = self.media(f"arj-{method}.arj", arj_bytes("CMD.EXE", support.CMD, method))
                report = self.request("import-os2-support", sources=[source])
                self.assertEqual(len(report["imported"]), 1)
                target = self.ball / "drive_c/OS2/CMD.EXE"
                self.assertEqual(target.read_bytes(), support.CMD)
                target.unlink()
        source = self.media("bad.arj", arj_bytes("CMD.EXE", support.CMD, bad_crc=True))
        with self.assertRaises(ValueError):
            self.request("import-os2-support", sources=[source])

    def test_zip_arj_arc_nested_without_intermediate_files(self):
        archive = arc_bytes("CMD.EXE", support.CMD, 8)
        archive = arj_bytes("FILES.ARC", archive, 1)
        source = self.media("nested.zip", support.zip_bytes("FILES.ARJ", archive))
        report = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(report["imported"]), 1)
        self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), support.CMD)

    def test_zoo_original_encoder_fixtures_cover_dictionary_and_huffman_growth(self):
        fixture = json.loads(
            (Path(__file__).with_name("fixtures") / "zoo-generated.json").read_text()
        )
        for method, archive in fixture["archives"].items():
            with self.subTest(method=method):
                source = self.media(f"original-{method}.zoo", base64.b64decode(archive))
                report = self.request("import-os2-support", sources=[source], categories=["full"])
                self.assertEqual(len(report["imported"]), 1)
                item = report["imported"][0]
                path = self.ball / "drive_c" / item["path"][3:].replace("\\", "/")
                self.assertEqual(path.stat().st_size, fixture["expectedSize"])
                self.assertEqual(
                    hashlib.sha256(path.read_bytes()).hexdigest(), fixture["expectedSHA256"]
                )

    def test_zoo_header_versions_methods_and_long_directories(self):
        for version in (1, 2):
            for method in range(3):
                with self.subTest(version=version, method=method):
                    source = self.media(
                        "test.zoo", zoo_bytes("CMD.EXE", support.CMD, method, version)
                    )
                    report = self.request("import-os2-support", sources=[source])
                    self.assertEqual(len(report["imported"]), 1)
                    target = self.ball / "drive_c/OS2/CMD.EXE"
                    self.assertEqual(target.read_bytes(), support.CMD)
                    target.unlink()
        source = self.media("directory.zoo", zoo_bytes("CMD.EXE", support.CMD, directory="OS2"))
        listing = self.request("browse-media", path=source)
        self.assertEqual(listing["entries"][0]["name"], "OS2")
        report = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(report["imported"]), 1)

    def test_zoo_checksums_cycles_and_unsafe_long_names(self):
        for fields in ({"bad_crc": True}, {"cycle": True, "version": 1}):
            with self.subTest(fields=fields):
                source = self.media("bad.zoo", zoo_bytes("CMD.EXE", support.CMD, **fields))
                with self.assertRaises(ValueError):
                    self.request("import-os2-support", sources=[source])
                self.assertFalse((self.ball / "drive_c/OS2/CMD.EXE").exists())
        source = self.media("unsafe.zoo", zoo_bytes("CMD.EXE", support.CMD, directory="../"))
        self.assertEqual(self.request("browse-media", path=source)["entries"], [])

    def test_arj_self_extracting_prefix_and_real_canonical_tokens(self):
        source = self.media(
            "self-arj.exe", b"MZ" + bytes(126) + arj_bytes("CMD.EXE", support.CMD, 4)
        )
        report = self.request("import-os2-support", sources=[source])
        self.assertEqual(len(report["imported"]), 1)
        (self.ball / "drive_c/OS2/CMD.EXE").unlink()
        # Complete two-symbol canonical trees, independently framed and
        # validated by 7zz in the recorded native compatibility probe.
        payload = b"AB" * 50
        codes = [(len(payload), 16), (4, 5), (0, 3), (0, 3), (1, 3), (0, 2), (1, 3)]
        codes += [(67, 9), (0, 1), (45, 9), (1, 1), (1, 1), (0, 5), (0, 5)]
        codes += [(byte - 65, 1) for byte in payload]
        source = self.media(
            "tree.arj", arj_bytes("TEST.TXT", payload, 1, compressed=bit_bytes(codes, msb=True))
        )
        report = self.request("import-os2-support", sources=[source], categories=["full"])
        item = report["imported"][0]
        self.assertEqual(
            (self.ball / "drive_c" / item["path"][3:].replace("\\", "/")).read_bytes(), payload
        )
        # One literal followed by a 99-byte self-overlapping dictionary match.
        codes = [(1, 16), (0, 5), (0, 5), (0, 9), (65, 9), (0, 5), (0, 5)]
        codes += [(1, 16), (0, 5), (0, 5), (0, 9), (352, 9), (0, 5), (0, 5)]
        source = self.media(
            "match.arj",
            arj_bytes("MATCH.TXT", b"A" * 100, 1, compressed=bit_bytes(codes, msb=True)),
        )
        report = self.request("import-os2-support", sources=[source], categories=["full"])
        item = report["imported"][0]
        self.assertEqual(
            (self.ball / "drive_c" / item["path"][3:].replace("\\", "/")).read_bytes(), b"A" * 100
        )


if __name__ == "__main__":
    unittest.main()
