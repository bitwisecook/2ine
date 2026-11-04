# Public domain. No rights reserved. Wholly AI-authored.
"""Stable application ordering independent of asynchronous icon completion."""

from typing import TYPE_CHECKING

if TYPE_CHECKING or __package__:
    from .gnome_browser_model import folded
else:
    from gnome_browser_model import folded

MODES = ("icons", "name", "program")


def mode(value):
    return value if value in MODES else "icons"


def key(name, program, has_icon, selected="icons"):
    selected = mode(selected)
    title = folded(name), name
    if selected == "icons":
        return (not has_icon, *title)
    if selected == "program":
        return (folded(program), *title)
    return title
