# Public domain. No rights reserved. Wholly AI-authored.
"""Persistent media indices, DEFLATE checkpoints and damaged optional fonts."""

import concurrent.futures
import io
import os
import struct
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

from tests.integration.harness.disk_images import iso_image
from tools.ball import engine as ball_api

CMD = b"MZ" + bytes(58) + struct.pack("<I", 64) + b"LX" + bytes(34)


def zip_bytes(members):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in members.items():
            archive.writestr(name, data)
    return out.getvalue()


def distant_iso():
    """Directories and payload force backwards and forwards seeks past checkpoints."""
    original = iso_image(payload=CMD, filename="CMD.EXE")
    image = bytearray(64 * 1024 * 1024)
    image[: len(original)] = original
    root, folder, payload = (offset * 1024 * 1024 // 2048 for offset in (20, 40, 60))

    def extent(at, sector):
        struct.pack_into("<I", image, at, sector)
        struct.pack_into(">I", image, at + 4, sector)

    extent(16 * 2048 + 80, len(image) // 2048)
    extent(16 * 2048 + 156 + 2, root)
    for old, new in ((20, root), (21, folder)):
        image[new * 2048 : (new + 1) * 2048] = original[old * 2048 : (old + 1) * 2048]
    for sector, records in ((root, (root, root, folder)), (folder, (folder, root, payload))):
        at = sector * 2048
        for target in records:
            extent(at + 2, target)
            at += image[at]
    image[payload * 2048 : payload * 2048 + len(CMD)] = CMD
    return image


class MediaCacheTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-media-cache-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ball = self.root / "Probe.2ineball"
        ball_api.init(self.ball)

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def media(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return str(path)

    def test_directory_index_survives_requests_and_invalidates_on_source_changes(self):
        source = self.media("media.iso", iso_image(filename="FIRST.EXE"))
        first = self.request("browse-media", path=source)
        warm = self.request("browse-media", path=source)
        self.assertGreater(first["readStats"]["directoryIndexBuilds"], 0)
        self.assertEqual(warm["readStats"]["directoryIndexBuilds"], 0)
        self.assertGreater(warm["readStats"]["directoryIndexHits"], 0)
        before = os.stat(source)
        Path(source).write_bytes(iso_image(filename="OTHER.EXE"))
        os.utime(source, ns=(before.st_atime_ns, before.st_mtime_ns))
        changed = self.request("browse-media", path=first["entries"][0]["path"])
        self.assertEqual(changed["entries"][0]["name"], "OTHER.EXE")
        replacement = self.root / "replacement.iso"
        replacement.write_bytes(iso_image(filename="THIRD.EXE"))
        os.utime(replacement, ns=(before.st_atime_ns, before.st_mtime_ns))
        os.replace(replacement, source)
        replaced = self.request("browse-media", path=first["entries"][0]["path"])
        self.assertEqual(replaced["entries"][0]["name"], "THIRD.EXE")

    def test_checkpoint_seeks_reuse_state_across_requests_and_keep_payload_intact(self):
        source = self.media("distant.zip", zip_bytes({"media.iso": distant_iso()}))
        opened = self.request("open-os2-source", sources=[source])
        folder = opened["directory"]["entries"][0]["path"]
        # Visit the directory at 40 MiB after the root at 20 MiB.
        listing = self.request("browse-media", path=folder)
        self.assertEqual(listing["entries"][0]["name"], "CMD.EXE")
        self.assertGreater(listing["readStats"]["inflateCheckpoints"], 0)
        result = self.request("import-os2-support", sources=opened["sources"], categories=["cmd"])
        self.assertEqual(len(result["imported"]), 1)
        self.assertGreater(result["readStats"]["inflateRestores"], 0)
        self.assertLess(result["readStats"]["decompressedBytes"], 25 * 1024 * 1024)
        self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), CMD)
        # Move backwards to root, then forwards to cached directory records.
        warm = self.request("browse-media", path=opened["sources"][0])
        self.assertGreater(warm["readStats"]["containerCacheHits"], 0)
        self.assertLess(warm["readStats"]["decompressedBytes"], 5 * 1024 * 1024)
        self.assertLess(warm["readStats"]["peakCacheBytes"], 64 * 1024 * 1024)
        self.assertEqual(warm["readStats"]["materializedFiles"], 0)
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            results = list(pool.map(lambda _: self.request("browse-media", path=folder), range(12)))
        self.assertTrue(all(item["entries"][0]["name"] == "CMD.EXE" for item in results))

    def test_damaged_optional_font_is_listed_while_help_installs(self):
        font = bytearray(zip_bytes({"OS2/DLL/MOEKAI.TTF": b"font bytes"}))
        # Agreeing local/central CRC fields, but deliberately wrong payload CRC.
        central = font.index(b"PK\x01\x02")
        struct.pack_into("<I", font, 14, 0x12345678)
        struct.pack_into("<I", font, central + 16, 0x12345678)
        source = self.media(
            "support.zip",
            zip_bytes(
                {
                    "moekai.zip": font,
                    "OS2/HELP/BOOK.HLP": b"help book",
                    "PSFONTS/GOOD.PFB": b"good font",
                }
            ),
        )
        result = self.request("import-os2-support", sources=[source], categories=["fonts", "help"])
        self.assertEqual(len(result["skippedFiles"]), 1)
        self.assertIn("CRC", result["skippedFiles"][0]["reason"])
        self.assertTrue(result["skippedFiles"][0]["path"].endswith("MOEKAI.TTF"))
        self.assertEqual((self.ball / "drive_c/OS2/HELP/BOOK.HLP").read_bytes(), b"help book")
        self.assertEqual((self.ball / "drive_c/PSFONTS/GOOD.PFB").read_bytes(), b"good font")
        self.assertFalse((self.ball / "drive_c/OS2/DLL/MOEKAI.TTF").exists())
        self.assertFalse(list(self.ball.glob(".2ine-*")))

    @unittest.skipUnless(hasattr(time, "tzset"), "requires POSIX timezone support")
    def test_archive_dates_use_local_timezone_and_refresh_between_requests(self):
        original = os.environ.get("TZ")
        try:
            for zone in ("UTC0", "America/New_York", "Europe/London"):
                os.environ["TZ"] = zone
                time.tzset()
                for month in (1, 7):
                    stamp = (1996, month, 24, 13, 12, 10)
                    out = io.BytesIO()
                    with zipfile.ZipFile(out, "w") as archive:
                        archive.writestr(zipfile.ZipInfo("README.TXT", date_time=stamp), b"text")
                    source = self.media("dated.zip", out.getvalue())
                    entry = self.request("browse-media", path=source)["entries"][0]
                    expected = int(time.mktime((*stamp, 0, 0, -1)))
                    self.assertEqual(entry["modified"], expected, (zone, month))
        finally:
            if original is None:
                os.environ.pop("TZ", None)
            else:
                os.environ["TZ"] = original
            time.tzset()

    def test_remembering_media_does_not_require_installing_command_support(self):
        source = self.media("support.zip", zip_bytes({"OS2/HELP/BOOK.HLP": b"help book"}))
        self.request("remember-os2-media", sources=[source])
        self.assertEqual(self.request("os2-support")["mediaSources"], [source])
        result = self.request("import-os2-support", sources=[source], categories=["help"])
        self.assertEqual(len(result["imported"]), 1)
        self.assertFalse(result["complete"])
        self.assertEqual(
            self.request("import-os2-support", sources=[source], categories=["help"])["imported"],
            [],
        )

    def test_reopening_a_selected_iso_folder_does_not_search_for_other_disc_images(self):
        source = self.media("media.iso", iso_image(filename="BROKEN.ISO", payload=b"not an ISO"))
        opened = self.request("open-os2-source", sources=[source])
        folder = opened["directory"]["entries"][0]["path"]
        self.request("remember-os2-media", sources=[folder])
        remembered = self.request("os2-support")["mediaSources"]
        reopened = self.request("open-os2-source", sources=remembered)
        self.assertEqual(reopened["sources"], [folder])
        self.assertEqual(reopened["directory"]["entries"][0]["name"], "BROKEN.ISO")
        self.assertEqual(reopened["skippedSources"], [])


if __name__ == "__main__":
    unittest.main()
