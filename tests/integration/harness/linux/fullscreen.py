# Public domain. No rights reserved. Wholly AI-authored.
"""Actual X11 Alt+Enter transitions for an already running SDL host window."""

import time
from pathlib import Path


def wait_for(condition, process, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(
                f"worker exited during fullscreen transition: {process.returncode}"
            )
        value = condition()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError("host fullscreen transition timed out")


def toggle_fullscreen(display, window, process, size):
    display.focus(window)
    display.key(0xFF0D, (0xFFE9,))  # Return with Alt_L.
    wait_for(lambda: display.geometry(window) == tuple(size), process)
    return display.geometry(window)


def exercise_fullscreen(display, window, process, work):
    """Enter desktop fullscreen and restore the same logical window and position."""
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    original_size = display.geometry(window)
    original_position = display.position(window)
    desktop_size = display.geometry(display.root)
    assert original_size != desktop_size, "fullscreen check requires an initially windowed app"
    toggle_fullscreen(display, window, process, desktop_size)
    time.sleep(0.15)
    rgb = display.capture(window, work / "fullscreen.png")
    assert any(rgb), "fullscreen presentation is black"
    fullscreen_position = display.position(window)
    assert fullscreen_position == (0, 0), fullscreen_position
    toggle_fullscreen(display, window, process, original_size)
    wait_for(lambda: display.position(window) == original_position, process)
    time.sleep(0.15)
    rgb = display.capture(window, work / "fullscreen-restored.png")
    assert any(rgb), "restored presentation is black"
    return dict(
        shortcut="Alt+Enter",
        window_size=original_size,
        window_position=original_position,
        fullscreen_size=desktop_size,
        fullscreen_position=fullscreen_position,
        restored_size=display.geometry(window),
        restored_position=display.position(window),
    )
