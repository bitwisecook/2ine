#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify Doom through a private X11 display, genuine input, saves and SDL audio.

The supplied OS/2 executable and shareware WAD are copied into a fresh retained
working directory. A source rebuild must be identified with --source-built.
"""

import argparse
import array
import hashlib
import json
import math
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from linux.fullscreen import exercise_fullscreen
from linux.wm import WindowManager
from linux.x11 import Display, check_errors, t, x

CONFIG = """mouse_sensitivity 5
sfx_volume 8
music_volume 5
show_messages 1
key_right 100
key_left 99
key_up 17
key_down 31
key_strafeleft 30
key_straferight 32
key_fire 29
key_use 57
key_strafe 56
key_speed 54
use_mouse 1
mouseb_fire 0
mouseb_strafe 1
mouseb_forward -1
use_joystick 0
screenblocks 10
screenwidth 320
screenheight 200
fullscreen 0
detaillevel 0
snd_channels 8
sound_freq 44100
sound_bits 16
sound_stereo 1
sound_buffer 16384
usegamma 0
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def difference(before, after):
    assert len(before) == len(after)
    return sum(before[i : i + 3] != after[i : i + 3] for i in range(0, len(before), 3)) / (
        len(before) // 3
    )


class Game:
    def __init__(self, process, display, handle, work, game):
        self.process, self.display, self.handle = process, display, handle
        self.work, self.game = work, game
        self.save_cursor = 0
        self.load_cursor = 0
        self.screenshots = {}

    def pause(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            assert self.process.poll() is None, f"Doom exited early: {self.process.returncode}"
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))

    def capture(self, name):
        self.pause(0.02)
        path = self.work / (name + ".png")
        rgb = self.display.capture(self.handle, path)
        self.screenshots[path.name] = digest(path)
        return rgb

    def key(self, symbol):
        self.display.key(symbol)
        self.pause(0.12)

    def held(self, symbol, seconds):
        code = x.XKeysymToKeycode(self.display.d, symbol)
        assert code
        t.XTestFakeKeyEvent(self.display.d, code, 1, 0)
        x.XFlush(self.display.d)
        try:
            self.pause(seconds)
        finally:
            t.XTestFakeKeyEvent(self.display.d, code, 0, 0)
            x.XFlush(self.display.d)
        self.pause(0.15)

    def save(self, slot, description):
        path = self.game / f"doomsav{slot}.dsg"
        assert not path.exists(), f"save slot was not fresh: {path}"
        self.key(0xFFBF)  # F2
        self.pause(0.2)
        for _ in range((slot - self.save_cursor) % 6):
            self.key(0xFF54)
        self.key(0xFF0D)
        for character in description:
            self.display.text(character)
            self.pause(0.06)
        self.capture(f"save-{slot}-description")
        self.key(0xFF0D)
        self.save_cursor = slot
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            self.pause(0.05)
            if path.exists():
                data = path.read_bytes()
                if len(data) > 50 and data[-1] == 0x1D:
                    break
        else:
            raise AssertionError(f"save did not complete: {path}")
        assert data[:24].split(b"\0", 1)[0] == description.encode(), data[:24]
        self.capture(f"save-{slot}-complete")
        return {
            "path": str(path),
            "sha256": digest(path),
            "size": len(data),
            "description": description,
            "leveltime": int.from_bytes(data[47:50], "big"),
            # This port archives its 32-bit player_t at offset52; ticcmd_t has
            # four ints, putting the first ammo count at offset220.
            "bullets": int.from_bytes(data[220:224], "little"),
            "health": int.from_bytes(data[96:100], "little", signed=True),
            "kills": int.from_bytes(data[268:272], "little"),
        }

    def load(self, slot):
        self.key(0xFFC0)  # F3
        self.pause(0.2)
        for _ in range((slot - self.load_cursor) % 6):
            self.key(0xFF54)
        self.capture(f"load-{slot}-menu")
        self.key(0xFF0D)
        self.load_cursor = slot
        self.pause(0.5)
        self.capture(f"load-{slot}-complete")


def start_game(command, game_dir, environment, worker_log, display, work):
    process = subprocess.Popen(
        command,
        cwd=game_dir,
        env={**os.environ, **environment},
        stdout=worker_log,
        stderr=worker_log,
    )
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            assert process.poll() is None, "Doom exited during startup"
            for candidate, title in display.windows():
                if "DOOM" in title.upper() and display.geometry(candidate)[0] >= 320:
                    display.focus(candidate)
                    work.mkdir(exist_ok=True)
                    game = Game(process, display, candidate, work, game_dir)
                    game.pause(1)
                    return game
            time.sleep(0.05)
        raise AssertionError("Doom did not create a window")
    except BaseException:
        process.terminate()
        process.wait(timeout=5)
        raise


def audio_result(path, require_stereo=False):
    assert path.is_file(), "SDL disk backend did not create audio output"
    samples = array.array("f")
    data = path.read_bytes()
    assert len(data) > 48000 and len(data) % 8 == 0
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    assert all(math.isfinite(value) for value in samples), "audio contains nonfinite samples"
    nonzero = sum(abs(value) > 0.00001 for value in samples)
    assert nonzero > 1000, "audio stream remained silent"
    different = sum(
        abs(left - right) > 0.00001 for left, right in zip(samples[::2], samples[1::2], strict=True)
    )
    if require_stereo:
        assert different > 1000, "audio left and right channels remained identical"
    return {
        "path": str(path),
        "sha256": digest(path),
        "different_stereo_frames": different,
        "bytes": len(data),
        "format": "F32LE stereo, 48000 Hz (SDL disk output)",
        "nonzero_samples": nonzero,
        "peak": max(abs(value) for value in samples),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=70)
    parser.add_argument("--source-built", action="store_true")
    parser.add_argument("--audio-config", type=Path)
    parser.add_argument("--launcher", type=Path)
    parser.add_argument("--launcher-default-build", action="store_true")
    parser.add_argument("--menu-start", action="store_true")
    parser.add_argument("--monsters", action="store_true")
    parser.add_argument("--restart-load", action="store_true")
    parser.add_argument("--fullscreen-check", action="store_true")
    parser.add_argument("--require-stereo", action="store_true")
    parser.add_argument("--map", type=int, default=1, choices=range(1, 10))
    parser.add_argument("--skill", type=int, default=1, choices=range(1, 6))
    parser.add_argument("--combat-check", action="store_true")
    args = parser.parse_args()
    if args.launcher:
        args.launcher = args.launcher.resolve()
    assert args.seconds >= 0
    args.build, args.program, args.wad, args.work = (
        path.resolve() for path in (args.build, args.program, args.wad, args.work)
    )
    assert args.program.is_file() and args.wad.is_file()
    args.work.mkdir(parents=True, exist_ok=True)
    selected_bank = None
    if args.audio_config:
        args.audio_config = args.audio_config.resolve()
        settings = dict(
            line.split("=", 1)
            for line in args.audio_config.read_text().splitlines()
            if "=" in line and not line.startswith("#")
        )
        assert settings.get("midi_backend", "gm") == "gm", "this gameplay profile expects a GM bank"
        bank = Path(settings["soundfont"])
        selected_bank = {
            "path": str(bank),
            "sha256": digest(bank),
            "config": str(args.audio_config),
        }
    receipt = args.work / "result.json"
    receipt.unlink(missing_ok=True)
    game_dir = Path(tempfile.mkdtemp(prefix="game-", dir=args.work))
    program = game_dir / "os2doom.exe"
    wad = game_dir / "DOOM1.WAD"
    shutil.copyfile(args.program, program)
    shutil.copyfile(args.wad, wad)
    (game_dir / "default.cfg").write_text(CONFIG)
    # The normal launcher accepts the persistent lowercase WAD spelling.
    (game_dir / "doom1.wad").symlink_to(wad.name)
    audio = game_dir / "sdlaudio.raw"
    display, process, manager = None, None, None
    read_fd, write_fd = os.pipe()
    with (
        (args.work / "xvfb.log").open("w") as server_log,
        (args.work / "worker.log").open("w") as worker_log,
    ):
        server = subprocess.Popen(
            [
                "Xvfb",
                "-displayfd",
                str(write_fd),
                "-screen",
                "0",
                "1024x768x24",
                "-nolisten",
                "tcp",
            ],
            pass_fds=(write_fd,),
            stdout=server_log,
            stderr=server_log,
        )
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 10)[0], "private Xvfb did not start"
            number = os.read(read_fd, 32).decode().strip()
            assert number.isdecimal()
            display_name = ":" + number
            display = Display(display_name)
            if args.fullscreen_check:
                manager = WindowManager(display_name)
            environment = {
                "DISPLAY": display_name,
                "SDL_VIDEO_DRIVER": "x11",
                "SDL_RENDER_DRIVER": "software",
                "SDL_AUDIO_DRIVER": "disk",
                "SDL_AUDIO_DISK_OUTPUT_FILE": str(audio),
                "TWOINE_BACKEND": args.backend,
                "TWOINE_BUILD": str(args.build),
                "TWOINE_DOOM_HOME": str(game_dir),
                "TWOINE_LIBPATH": str(args.build),
                "LD_LIBRARY_PATH": str(args.build),
            }
            if args.launcher_default_build:
                assert args.launcher, "default build selection requires --launcher"
                environment["TWOINE_BUILD"] = ""
            if args.audio_config:
                environment["TWOINE_AUDIO_CONFIG"] = str(args.audio_config)
                environment["TWOINE_TRACE_AUDIO"] = "1"
            command = [
                str(args.build / "lx_loader"),
                program.name,
                "-warp",
                "1",
                str(args.map),
                "-skill",
                str(args.skill),
            ]
            if not args.monsters:
                command.append("-nomonsters")
            if args.launcher:
                command = [str(args.launcher), *command[2:]]
            if args.menu_start:
                assert args.monsters, "normal menu startup enables monsters"
                command = command[:1] if args.launcher else command[:2]
            identities_start = {
                "worker_sha256": digest(args.build / "lx_loader"),
                "libraries": {p.name: digest(p) for p in args.build.glob("lib*.so") if p.is_file()},
            }
            process = subprocess.Popen(
                command,
                cwd=game_dir,
                env={**os.environ, **environment},
                stdout=worker_log,
                stderr=worker_log,
            )
            deadline = time.monotonic() + 30
            handle = None
            while time.monotonic() < deadline:
                assert process.poll() is None, f"Doom exited during startup: {process.returncode}"
                for candidate, title in display.windows():
                    if "DOOM" in title.upper():
                        width, height = display.geometry(candidate)
                        if width >= 320 and height >= 200:
                            handle = candidate
                            break
                if handle:
                    break
                time.sleep(0.05)
            assert handle, "Doom did not create a game window"
            display.focus(handle)
            game = Game(process, display, handle, args.work, game_dir)
            game.pause(1)
            actual_worker = Path(f"/proc/{process.pid}/exe").resolve()
            assert actual_worker == (args.build / "lx_loader").resolve(), actual_worker
            if args.menu_start:
                game.key(0xFF1B)
                game.capture("main-menu")
                for _ in range(3):
                    game.key(0xFF0D)
                game.pause(2)
            started = time.monotonic()
            fullscreen = None
            if args.fullscreen_check:
                fullscreen = exercise_fullscreen(display, handle, process, args.work / "fullscreen")
            initial = game.capture("initial")
            colors = len(set(zip(initial[::3], initial[1::3], initial[2::3], strict=True)))
            assert colors > 64, f"game image has only {colors} colors"
            animated = None
            for _ in range(15):
                game.pause(0.2)
                frame = game.capture("animated")
                if frame != initial:
                    animated = frame
                    break
            assert animated, "game image did not animate"
            before_move = game.capture("before-move")
            game.held(ord("w"), 0.7)
            moved = game.capture("moved")
            move_difference = difference(before_move, moved)
            assert move_difference > 0.05, f"keyboard movement changed only {move_difference:.1%}"
            px, py = display.position(handle)
            width, height = display.geometry(handle)
            display.move_pointer(px + width // 2 + 35, py + height // 2)
            game.pause(0.25)
            turned = game.capture("mouse-turned")
            mouse_difference = difference(moved, turned)
            assert mouse_difference > 0.02, f"mouse turn changed only {mouse_difference:.1%}"
            game.held(0xFFE3, 0.1)  # Left Control fires the current weapon.
            fired = game.capture("fired")
            fire_difference = difference(turned, fired)
            assert fire_difference > 0.001, "fire input did not alter the rendered frame"
            first = game.save(0, "TWOINE")
            assert 0 < first["bullets"] < 50, "fire input did not consume pistol ammunition"
            game.pause(3)
            game.held(ord("w"), 0.4)
            later = game.save(1, "LATER")
            assert later["leveltime"] > first["leveltime"] + 35, "game clock did not advance"
            game.load(0)
            reloaded = game.save(2, "RELOADED")
            assert first["leveltime"] <= reloaded["leveltime"] <= first["leveltime"] + 70, (
                first,
                reloaded,
            )
            assert reloaded["leveltime"] < later["leveltime"] - 35, (
                "load did not restore earlier game time"
            )
            snapshots = []
            while time.monotonic() - started < args.seconds:
                game.pause(min(5, args.seconds - (time.monotonic() - started)))
                rgb = game.capture(f"running-{len(snapshots):02d}")
                snapshots.append(hashlib.sha256(rgb).hexdigest())
            elapsed = time.monotonic() - started
            if args.seconds >= 60:
                assert len(set(snapshots)) > 1, "rendering stopped during the sustained run"
            game.key(0xFFC7)  # F10 opens the actual quit confirmation.
            game.capture("quit-confirmation")
            display.key(ord("y"))
            status = process.wait(timeout=12)
            assert status == 0, f"Doom quit returned {status}"
            restart_save = None
            restart_command = None
            restart_audio = game_dir / "restart-audio.raw"
            if args.restart_load:
                restart_command = [*command[:1], "-loadgame", "0"]
                if not args.launcher:
                    restart_command.insert(1, program.name)
                process = subprocess.Popen(
                    restart_command,
                    cwd=game_dir,
                    env={
                        **os.environ,
                        **environment,
                        "SDL_AUDIO_DISK_OUTPUT_FILE": str(restart_audio),
                    },
                    stdout=worker_log,
                    stderr=worker_log,
                )
                deadline = time.monotonic() + 30
                handle = None
                while time.monotonic() < deadline:
                    assert process.poll() is None, "Doom exited during restart"
                    for candidate, title in display.windows():
                        if "DOOM" in title.upper() and display.geometry(candidate)[0] >= 320:
                            handle = candidate
                            break
                    if handle:
                        break
                    time.sleep(0.05)
                assert handle, "restarted Doom did not create a window"
                display.focus(handle)
                restarted = Game(process, display, handle, args.work / "restart", game_dir)
                restarted.work.mkdir(exist_ok=True)
                restarted.pause(1)
                restart_save = restarted.save(3, "RESTARTED")
                assert (
                    first["leveltime"] <= restart_save["leveltime"] <= first["leveltime"] + 175
                ), (first, restart_save)
                assert restart_save["bullets"] == first["bullets"], (
                    "restart did not restore ammunition"
                )
                restarted.key(0xFFC7)
                display.key(ord("y"))
                assert process.wait(timeout=12) == 0, "restarted Doom did not quit cleanly"
            combat = None
            combat_reloaded = None
            combat_command = None
            combat_audio = game_dir / "combat-audio.raw"
            if args.combat_check:
                combat_command = [*command[:1], "-warp", "1", "3", "-skill", "4"]
                if not args.launcher:
                    combat_command.insert(1, program.name)
                fighting = start_game(
                    combat_command,
                    game_dir,
                    {**environment, "SDL_AUDIO_DISK_OUTPUT_FILE": str(combat_audio)},
                    worker_log,
                    display,
                    args.work / "combat",
                )
                process = fighting.process
                fighting.held(ord("w"), 2)
                fighting.held(ord(" "), 0.25)
                fighting.pause(1)
                fighting.held(ord("w"), 0.6)
                fighting.held(0xFFE3, 1)
                fighting.capture("combat")
                combat = fighting.save(4, "COMBAT")
                assert combat["kills"] > 0 or combat["health"] < 100, (
                    "combat did not kill a monster or damage the player",
                    combat,
                )
                fighting.load(4)
                combat_reloaded = fighting.save(5, "BATTLELOAD")
                assert combat_reloaded["bullets"] == combat["bullets"], (
                    "large save did not restore ammunition"
                )
                assert (
                    combat["leveltime"] <= combat_reloaded["leveltime"] <= combat["leveltime"] + 70
                )
                fighting.key(0xFFC7)
                display.key(ord("y"))
                assert process.wait(timeout=12) == 0, "combat game did not quit cleanly"
            worker_log.flush()
            log = (args.work / "worker.log").read_text(errors="replace")
            assert not any(
                fragment in log
                for fragment in ("SIGSEGV", "CPU stop", "missing import", "Segmentation fault")
            ), "worker reported a runtime failure"
            assert "format=F32LE channels=2 freq=48000" in log, "unexpected SDL disk sample format"
            if selected_bank:
                assert "2ine: MIDI GM bank " + selected_bank["path"] in log, (
                    "selected bank was not opened"
                )
                assert digest(Path(selected_bank["path"])) == selected_bank["sha256"], (
                    "bank changed during execution"
                )
            check_errors()
            identities_end = {
                "worker_sha256": digest(args.build / "lx_loader"),
                "libraries": {p.name: digest(p) for p in args.build.glob("lib*.so") if p.is_file()},
            }
            if identities_start != identities_end:
                (args.work / "identity-mismatch.json").write_text(
                    json.dumps({"start": identities_start, "end": identities_end}, indent=2) + "\n"
                )
                raise AssertionError("worker or libraries changed during the gameplay run")
            result = {
                "passed": True,
                "backend": args.backend,
                "source_built": args.source_built,
                "selected_bank": selected_bank,
                "program": str(args.program),
                "program_sha256": digest(program),
                "wad": str(args.wad),
                "wad_sha256": digest(wad),
                "worker_sha256": digest(args.build / "lx_loader"),
                "libraries": {p.name: digest(p) for p in args.build.glob("lib*.so") if p.is_file()},
                "identities_start": identities_start,
                "identities_end": identities_end,
                "command": command,
                "launcher": str(args.launcher) if args.launcher else None,
                "launcher_default_build": args.launcher_default_build,
                "menu_start": args.menu_start,
                "actual_worker": str(actual_worker),
                "launcher_sha256": digest(args.launcher) if args.launcher else None,
                "monsters_enabled": args.monsters,
                "map": args.map,
                "skill": args.skill,
                "combat": combat,
                "combat_reloaded": combat_reloaded,
                "combat_command": combat_command,
                "fullscreen": fullscreen,
                "restart_command": restart_command,
                "restart_save": restart_save,
                "cwd": str(game_dir),
                "environment": environment,
                "window_size": [width, height],
                "colors": colors,
                "animation": True,
                "keyboard_move_difference": move_difference,
                "mouse_turn_difference": mouse_difference,
                "fire_difference": fire_difference,
                "saves": [first, later, reloaded],
                "elapsed_play_seconds": elapsed,
                "minute_rollover_exercised": elapsed >= 60,
                "ongoing_frame_hashes": snapshots,
                "audio": audio_result(audio, args.require_stereo),
                "restart_audio": audio_result(restart_audio) if args.restart_load else None,
                "combat_audio": audio_result(combat_audio, args.require_stereo)
                if args.combat_check
                else None,
                "exit": status,
                "normal_quit": True,
                "worker_log_sha256": digest(args.work / "worker.log"),
                "screenshots": game.screenshots,
            }
            receipt.write_text(json.dumps(result, indent=2) + "\n")
            print(f"PASS Doom {args.backend}: gameplay/input/save/load/audio/quit, {elapsed:.1f}s")
        finally:
            os.close(read_fd)
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if display:
                x.XCloseDisplay(display.d)
            if manager:
                manager.close()
            server.terminate()
            server.wait(timeout=3)


if __name__ == "__main__":
    main()
