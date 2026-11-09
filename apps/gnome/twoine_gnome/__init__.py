# Public domain. No rights reserved. Wholly AI-authored.
"""Native GTK 4/libadwaita frontend for 2ine balls."""

# The GTK modules import gi.repository at import time, in whatever order the
# importer reaches them; selecting the versions here, before any of them,
# keeps PyGObject from loading another GTK. The GTK-free models stay
# importable without PyGObject, and the GTK modules report a missing library.
try:
    import gi
except ImportError:
    pass
else:
    try:
        for namespace, version in (("Gtk", "4.0"), ("Gdk", "4.0"), ("Adw", "1")):
            gi.require_version(namespace, version)
    except ValueError:
        pass
