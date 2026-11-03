# Public domain. No rights reserved. Wholly AI-authored.
"""Generate original test frames in AVI and exercise decoding and native MCI playback."""

import argparse
import math
import os
import struct
import subprocess
from pathlib import Path

from images import FlatGuest, linear_image


def chunk(kind, data):
    return kind + struct.pack("<I", len(data)) + data + bytes(len(data) & 1)


def avi(yuv=False, audio=False, compressed=False):
    width, height, count, rate = 64, 48, 4, 10
    fourcc = b"cvid" if compressed else b"I420" if yuv else bytes(4)
    frames = []
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
    for r, g, b in colors:
        if yuv:
            y = round(16 + 0.257 * r + 0.504 * g + 0.098 * b)
            u = round(128 - 0.148 * r - 0.291 * g + 0.439 * b)
            v = round(128 + 0.439 * r - 0.368 * g - 0.071 * b)
            frames.append(
                bytes([y]) * (width * height)
                + bytes([u]) * (width * height // 4)
                + bytes([v]) * (width * height // 4)
            )
        else:
            frames.append(bytes([b, g, r]) * width * height)
    if compressed:
        # Original flat-color frames encoded by FFmpeg 9.0.2's Cinepak encoder.
        # Retain compact packet/codebook bytes so CI needs no encoder executable.
        codebooks = ("48484848db5b", "91919191b7b7", "242424246ded", "dadadada9212")
        frames = [
            bytes.fromhex(
                "000000e8004000300001"
                + ("10" if i == 0 else "11")
                + "0000de0000000000300040200000042200000a"
                + codebook
                + "320000c4"
            )
            + bytes(192)
            for i, codebook in enumerate(codebooks)
        ]
    size = len(frames[0])
    header = struct.pack(
        "<14I",
        1000000 // rate,
        size * rate,
        0,
        0x10,
        count,
        0,
        2 if audio else 1,
        size,
        width,
        height,
        0,
        0,
        0,
        0,
    )
    stream = struct.pack(
        "<4s4sIHH8I4h",
        b"vids",
        fourcc,
        0,
        0,
        0,
        0,
        1,
        rate,
        0,
        count,
        size,
        0xFFFFFFFF,
        0,
        0,
        0,
        width,
        height,
    )
    bitmap = struct.pack(
        "<IiiHH4sIiiII", 40, width, height, 1, 12 if yuv else 24, fourcc, size, 0, 0, 0, 0
    )
    streams = chunk(b"LIST", b"strl" + chunk(b"strh", stream) + chunk(b"strf", bitmap))
    if audio:
        audio_stream = struct.pack(
            "<4s4sIHH8I4h",
            b"auds",
            bytes(4),
            0,
            0,
            0,
            0,
            4,
            44100 * 4,
            0,
            17640,
            17640,
            0xFFFFFFFF,
            4,
            0,
            0,
            0,
            0,
        )
        audio_format = struct.pack("<HHIIHH", 1, 2, 44100, 44100 * 4, 4, 16)
        streams += chunk(
            b"LIST", b"strl" + chunk(b"strh", audio_stream) + chunk(b"strf", audio_format)
        )
    hdrl = chunk(b"LIST", b"hdrl" + chunk(b"avih", header) + streams)
    movi = bytearray(b"movi")
    index = bytearray()
    for frame_number, frame in enumerate(frames):
        offset = len(movi)
        movi.extend(chunk(b"00db", frame))
        index.extend(struct.pack("<4sIII", b"00db", 0x10, offset, len(frame)))
        if audio:
            samples = bytearray()
            for sample in range(4410):
                at = frame_number * 4410 + sample
                samples.extend(
                    struct.pack(
                        "<hh", round(8192 * math.sin(at * 0.1)), round(2048 * math.cos(at * 0.071))
                    )
                )
            offset = len(movi)
            movi.extend(chunk(b"01wb", samples))
            index.extend(struct.pack("<4sIII", b"01wb", 0x10, offset, len(samples)))
    content = b"AVI " + hdrl + chunk(b"LIST", movi) + chunk(b"idx1", index)
    return b"RIFF" + struct.pack("<I", len(content)) + content


def guest_program():
    g = FlatGuest()
    device = g.field(bytes(4))
    params = g.field(b"HEAD" + bytes(32) + b"TAIL") + 4

    def store(address, value):
        g.code.extend(b"\xc7\x05" + struct.pack("<II", address, value))

    g.call(2, 763, 0)
    g.result(1, 10)
    g.call(2, 716, 1, 0)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 11)
    store(params + 8, 12)
    g.call(3, 1, 0, 1, 0x1000, params, 0)
    g.result(0, 12)
    g.load(params + 4)
    g.code.extend(b"\xa3" + struct.pack("<I", device))
    store(params + 4, g.field(b"C:\\raw.avi\0"))
    g.call(3, 1, (device,), 22, 2, params, 0)
    g.result(0, 27)
    store(params + 8, 2)
    g.call(3, 1, (device,), 10, 0x100, params, 0)
    g.result(0, 13)
    g.word(params + 4, 400, 14)
    store(params + 8, 10)
    g.call(3, 1, (device,), 9, 0x200, params, 0)
    g.result(0, 28)
    g.word(params + 4, 12, 29)
    g.call(3, 1, (device,), 4, 2, params, 0)
    g.result(0, 15)
    g.call(3, 1, (device,), 2, 0, params, 0)
    g.result(0, 16)
    g.word(params - 4, int.from_bytes(b"HEAD", "little"), 17)
    g.word(params + 32, int.from_bytes(b"TAIL", "little"), 18)
    g.call(3, 1, 0, 1, 0x1100, 0xFFFFFFF0, 0)
    g.result(5050, 19)
    reply = g.field(bytes(32) + b"TAIL")
    command = g.field(b'open "C:\\sound.avi" type digitalvideo alias movie wait\0')
    g.call(3, 2, command, reply, 32, 0, 0)
    g.result(0, 20)
    g.call(3, 2, g.field(b"status movie length\0"), reply, 32, 0, 0)
    g.result(0, 21)
    g.word(reply, int.from_bytes(b"400\0", "little"), 22)
    g.call(3, 2, g.field(b"play movie wait\0"), 0, 0, 0, 0)
    g.result(0, 23)
    g.call(3, 2, g.field(b"close movie\0"), 0, 0, 0, 0)
    g.result(0, 24)
    g.word(reply + 32, int.from_bytes(b"TAIL", "little"), 25)
    # Leave an active DART device and an allocated buffer for module teardown.
    wave = g.field(bytes(56) + b"TAIL")
    wave_id = g.field(bytes(4))
    records = g.field(bytes(32) + b"TAIL")
    store(wave + 8, 9)
    g.call(3, 1, 0, 1, 0x1000, wave, 0)
    g.result(0, 30)
    g.load(wave + 4)
    g.code.extend(b"\xa3" + struct.pack("<I", wave_id))
    for offset, value in ((4, 16), (8, 1), (12, 48000), (16, 2), (20, 4)):
        store(wave + offset, value)
    g.call(3, 1, (wave_id,), 63, 0x10000, wave, 0)
    g.result(0, 31)
    for offset, value in ((8, 1), (12, 1024), (28, records)):
        store(wave + offset, value)
    g.call(3, 1, (wave_id,), 62, 0x40000, wave, 0)
    g.result(0, 32)
    g.word(wave + 56, int.from_bytes(b"TAIL", "little"), 33)
    g.word(records + 32, int.from_bytes(b"TAIL", "little"), 34)
    g.call(2, 888, 1)
    g.result(1, 26)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWIN", "MDM"], "VIDEO", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--backend", choices=("unicorn", "native"), default="unicorn")
    parser.add_argument("--audio-driver", choices=("dummy", "disk"), default="dummy")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    raw, yuv, sound, invalid, cinepak = (
        args.work / name
        for name in ("raw.avi", "yuv.avi", "sound.avi", "invalid.avi", "cinepak.avi")
    )
    raw.write_bytes(avi())
    yuv.write_bytes(avi(True))
    sound.write_bytes(avi(audio=True))
    cinepak.write_bytes(avi(compressed=True))
    invalid.write_bytes(b"RIFF" + struct.pack("<I", 128) + b"AVI " + bytes(16))
    binary = args.build.resolve() / "tests/twoine_video_tests"
    if not binary.exists():
        binary = args.build.resolve() / "twoine_video_tests"
    env = dict(
        os.environ,
        SDL_VIDEO_DRIVER="dummy",
        SDL_AUDIO_DRIVER=args.audio_driver,
        SDL_RENDER_DRIVER="software",
    )
    capture = args.work.resolve() / "video-audio.raw"
    if args.audio_driver == "disk":
        env["SDL_AUDIO_DISK_OUTPUT_FILE"] = str(capture)
    subprocess.run(
        [
            str(binary),
            str(raw.resolve()),
            str(yuv.resolve()),
            str(sound.resolve()),
            str(invalid.resolve()),
            str(cinepak.resolve()),
        ],
        check=True,
        env=env,
        timeout=30,
    )

    guest = args.work / "video.exe"
    guest.write_bytes(guest_program())
    env.update(
        TWOINE_BACKEND=args.backend,
        TWOINE_DRIVE_C=str(args.work.resolve()),
        TWOINE_LIBPATH=str(args.build.resolve()),
    )
    subprocess.run(
        [str(args.build.resolve() / "lx_loader"), str(guest.resolve())],
        check=True,
        env=env,
        cwd=args.work.resolve(),
        timeout=30,
    )
    print("LX digital-video checked wire structures, playback, text commands and canaries passed")
    if args.audio_driver == "disk":
        data = capture.read_bytes()
        assert len(data) % 8 == 0 and len(data) >= 8000 * 8
        left, right = 0.0, 0.0
        for l_sample, r_sample in struct.iter_unpack("<ff", data):
            assert math.isfinite(l_sample) and math.isfinite(r_sample)
            left += l_sample * l_sample
            right += r_sample * r_sample
        assert left > right * 14 and left < right * 18, (left, right)
        print(
            f"Actual host stereo output: {len(data) // 8} frames, L/R energy {left:.3f}/{right:.3f}"
        )


if __name__ == "__main__":
    main()
