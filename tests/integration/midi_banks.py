#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Render original generated SF2, SF3, SFZ and DLS fixtures and saved channel maps."""

import argparse
import ctypes
import ctypes.util
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import ball as ball_api


def chunk(kind, data):
    return kind + struct.pack("<I", len(data)) + data + bytes(len(data) & 1)


def listing(kind, *chunks):
    return chunk(b"LIST", kind + b"".join(chunks))


def riff(kind, *chunks):
    return chunk(b"RIFF", kind + b"".join(chunks))


def pcm():
    return struct.pack(
        "<4096h", *[int(math.sin(i * 2 * math.pi * 440 / 22050) * 14000) for i in range(4096)]
    )


def sf2(compressed=None):
    samples = compressed if compressed is not None else pcm() + bytes(92)
    end = len(samples) if compressed is not None else 4096
    phdr = b"".join(
        struct.pack("<20sHHHIII", name, 0, bank, bag, 0, 0, 0)
        for name, bank, bag in ((b"Tone", 0, 0), (b"Drums", 128, 1), (b"EOP", 0, 2))
    )
    return riff(
        b"sfbk",
        listing(
            b"INFO",
            chunk(b"ifil", struct.pack("<HH", 3 if compressed else 2, 1)),
            chunk(b"INAM", b"2ine generated bank\0"),
        ),
        listing(b"sdta", chunk(b"smpl", samples)),
        listing(
            b"pdta",
            chunk(b"phdr", phdr),
            chunk(b"pbag", struct.pack("<6H", 0, 0, 1, 0, 2, 0)),
            chunk(b"pmod", bytes(10)),
            chunk(b"pgen", struct.pack("<6H", 41, 0, 41, 0, 0, 0)),
            chunk(b"inst", struct.pack("<20sH20sH", b"Sine", 0, b"EOI", 1)),
            chunk(b"ibag", struct.pack("<4H", 0, 0, 2, 0)),
            chunk(b"imod", bytes(10)),
            chunk(b"igen", struct.pack("<6H", 54, 1, 53, 0, 0, 0)),
            chunk(
                b"shdr",
                struct.pack(
                    "<20sIIIIIBbHH",
                    b"Sine",
                    0,
                    end,
                    0,
                    4096,
                    22050,
                    60,
                    0,
                    0,
                    17 if compressed else 1,
                )
                + bytes(46),
            ),
        ),
    )


def dls():
    sample = pcm()
    wave_data = listing(
        b"wave",
        chunk(b"fmt ", struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)),
        chunk(b"data", sample),
    )
    region = listing(
        b"rgn ",
        chunk(b"rgnh", struct.pack("<6H", 0, 127, 0, 127, 0, 0)),
        chunk(b"wlnk", struct.pack("<HHII", 0, 0, 1, 0)),
        chunk(
            b"wsmp", struct.pack("<IHhiII", 20, 60, 0, 0, 0, 1) + struct.pack("<4I", 16, 0, 0, 4096)
        ),
    )
    instrument = listing(
        b"ins ", chunk(b"insh", struct.pack("<3I", 1, 0, 0)), listing(b"lrgn", region)
    )
    return riff(
        b"DLS ",
        chunk(b"colh", struct.pack("<I", 1)),
        listing(b"lins", instrument),
        chunk(b"ptbl", struct.pack("<3I", 8, 1, 0)),
        listing(b"wvpl", wave_data),
    )


def ogg(path):
    class Info(ctypes.Structure):
        _fields_ = [
            ("frames", ctypes.c_int64),
            *[
                (name, ctypes.c_int)
                for name in ("samplerate", "channels", "format", "sections", "seekable")
            ],
        ]

    filename = "libsndfile.dylib" if sys.platform == "darwin" else "libsndfile.so.1"
    candidate = ctypes.util.find_library("sndfile")
    if not candidate:
        prefix = subprocess.run(
            ["pkg-config", "--variable=libdir", "sndfile"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        candidate = str(Path(prefix) / filename)
    library = ctypes.CDLL(candidate)
    library.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Info)]
    library.sf_open.restype = ctypes.c_void_p
    library.sf_writef_short.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_short),
        ctypes.c_int64,
    ]
    library.sf_writef_short.restype = ctypes.c_int64
    library.sf_close.argtypes = [ctypes.c_void_p]
    info = Info(0, 22050, 1, 0x200060, 0, 0)
    handle = library.sf_open(os.fsencode(path), 0x20, ctypes.byref(info))
    assert handle, "libsndfile must support the Ogg/Vorbis encoder"
    samples = (ctypes.c_short * 4096).from_buffer_copy(pcm())
    try:
        assert library.sf_writef_short(handle, samples, 4096) == 4096
    finally:
        assert library.sf_close(handle) == 0
    return path.read_bytes()


def fixtures(root, *, include_compressed=True):
    (root / "Tone.sf2").write_bytes(sf2())
    if include_compressed:
        (root / "Tone.sf3").write_bytes(sf2(ogg(root / "sample.ogg")))
    (root / "Tone.dls").write_bytes(dls())
    folder = root / "SFZ Instrument"
    folder.mkdir()
    with wave.open(str(folder / "tone.wav"), "wb") as file:
        file.setparams((1, 2, 22050, 4096, "NONE", "not compressed"))
        file.writeframes(pcm())
    (folder / "Tone.sfz").write_text(
        "<region> sample=tone.wav pitch_keycenter=60 loop_mode=loop_continuous\n"
    )
    (folder / "LICENSE.txt").write_text(
        "Public domain. No rights reserved. Original generated test samples.\n"
    )
    return [root / "Tone.sf2", root / "Tone.sf3", root / "Tone.dls", folder / "Tone.sfz"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    ball_api.configure_library(build)
    with tempfile.TemporaryDirectory(prefix="2ine-midi-banks-") as temporary:
        root = Path(temporary)
        ball = root / "MIDI.2ineball"
        ball_api.init(ball)
        for source in fixtures(root):
            library = ball_api.native_request(
                {"action": "import-soundfont", "ball": str(ball), "path": str(source)}
            )
            bank = library["banks"][-1]
            installed = ball / bank["file"]
            assert installed.read_bytes() == source.read_bytes()
            if source.suffix == ".sfz":
                assert (installed.parent / "tone.wav").is_file()
                assert (installed.parent / "LICENSE.txt").is_file()
            for profile in ("gm", "gm2", "gs", "xg", "mt32"):
                settings = library["settings"]
                settings["profile"] = profile
                settings["bank"] = bank["id"]
                # Channel 2 routes to 16; custom bank/program/type survive persistence.
                settings["channels"][1].update(target=16, program=0, bankNumber=0, percussion=0)
                saved = ball_api.native_request(
                    {"action": "save-audio", "ball": str(ball), "settings": settings}
                )
                assert saved["settings"] == settings
                config = root / "audio.conf"
                config.write_text(
                    f"soundfont={installed}\nmidi_profile={profile}\nchannel_2=16,0,0,0,0\n"
                )
                subprocess.run(
                    [build / "tests/twoine_midi_bank_tests", installed, config, "2", "audible"],
                    check=True,
                )
            config.write_text(f"soundfont={installed}\nchannel_2=16,0,0,0,1\n")
            subprocess.run(
                [build / "tests/twoine_midi_bank_tests", installed, config, "2", "muted"],
                check=True,
            )
        print(
            "SF2, SF3, DLS and SFZ imports render real finite PCM; five profiles, routing and mute passed"
        )


if __name__ == "__main__":
    main()
