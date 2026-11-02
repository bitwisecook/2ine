#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native GTK 4/libadwaita manager for persistent 2ine balls."""

import argparse
import json
import os
import shlex
import sys
from datetime import UTC, datetime
from pathlib import Path

if __package__:
    from . import balls
else:
    import balls

try:
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Adw, Gio, GLib, Gtk
except (ImportError, ValueError) as error:
    raise SystemExit(
        "2ine Balls requires GTK 4, libadwaita and PyGObject. On Ubuntu install "
        "python3-gi gir1.2-gtk-4.0 gir1.2-adw-1.\n" + str(error)
    ) from error

APP_ID = "io.github.bitwisecook.Twoine.Balls"


def parse_arguments(text):
    lexer = shlex.shlex(text, posix=True)
    lexer.whitespace_split = True
    lexer.commenters = ""
    lexer.escape = ""
    return list(lexer)


def button(label, callback, icon=False):
    widget = Gtk.Button(icon_name=label) if icon else Gtk.Button(label=label)
    if icon:
        name = {
            "list-add-symbolic": "Create ball",
            "folder-open-symbolic": "Choose folder or file",
            "view-refresh-symbolic": "Refresh ball",
        }.get(label, label)
        widget.set_tooltip_text(name)
        widget.update_property([Gtk.AccessibleProperty.LABEL], [name])
    widget.connect("clicked", lambda *_: callback())
    return widget


def clear(box):
    while child := box.get_first_child():
        box.remove(child)


class BallWindow(Adw.ApplicationWindow):
    def __init__(self, app, build, initial=None):
        super().__init__(
            application=app, title="2ine Balls", default_width=1080, default_height=760
        )
        self.build = build
        self.ball = None
        self.process = None
        self.last_result = None
        self.last_log = None
        self.preferences = (
            Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
            / "2ine/balls-gnome.json"
        )
        self.external = []
        try:
            prefs = json.loads(self.preferences.read_text())
            self.external = [Path(path) for path in prefs.get("balls", [])]
            if build is None and prefs.get("build"):
                self.build = Path(prefs["build"])
        except FileNotFoundError:
            pass
        except (OSError, ValueError, TypeError):
            self.external = []
        self.build = (
            self.build or Path(os.environ.get("TWOINE_BUILD", balls.ROOT / "build"))
        ).resolve()
        balls.configure_library(self.build)
        self.connect("close-request", self.close_requested)
        self.overlay = Adw.ToastOverlay()
        self.set_content(self.overlay)
        self.split = Adw.NavigationSplitView(min_sidebar_width=220, max_sidebar_width=300)
        self.overlay.set_child(self.split)
        sidebar = self.sidebar = Adw.ToolbarView()
        header = Adw.HeaderBar(show_end_title_buttons=False)
        header.pack_start(button("list-add-symbolic", self.new_ball, True))
        header.pack_end(button("folder-open-symbolic", self.open_ball, True))
        sidebar.add_top_bar(header)
        self.ball_list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.SINGLE)
        self.ball_list.add_css_class("navigation-sidebar")
        self.ball_list.connect("row-selected", self.selected)
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_child(self.ball_list)
        sidebar.set_content(scroll)
        self.split.set_sidebar(Adw.NavigationPage.new(sidebar, "Balls"))
        view = Adw.ToolbarView()
        self.header = Adw.HeaderBar()
        self.header.pack_start(button("view-refresh-symbolic", self.refresh, True))
        self.header.pack_end(button("Runtime…", self.runtime_dialog))
        view.add_top_bar(self.header)
        self.content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        view.set_content(self.content)
        self.detail = Adw.NavigationPage.new(view, "2ine Balls")
        self.split.set_content(self.detail)
        if initial:
            self.remember(initial.resolve())
        self.refresh(initial.resolve() if initial else None)

    def close_requested(self, *_):
        if self.process:
            self.toast("Close the running application before closing Balls.")
            return True
        return False

    def toast(self, message):
        self.overlay.add_toast(Adw.Toast(title=str(message)))

    def remember(self, path):
        balls.load(path)
        if path not in self.external:
            self.external.append(path)
        self.persist_preferences()

    def persist_preferences(self):
        self.preferences.parent.mkdir(parents=True, exist_ok=True)
        balls.save(
            self.preferences,
            {"build": str(self.build), "balls": [str(path) for path in self.external]},
        )

    def refresh(self, select=None):
        if self.process:
            self.toast("Wait for the current operation to finish.")
            return
        target = select or self.ball
        clear(self.ball_list)
        paths = set(self.external) | {
            path.parent for path in balls.balls_home().glob("*/ball.json")
        }
        chosen = None
        for path in sorted(paths):
            row = Adw.ActionRow(title=path.name, subtitle=str(path))
            row.ball_path = path
            self.ball_list.append(row)
            if path == target:
                chosen = row
        self.ball_list.select_row(chosen or self.ball_list.get_row_at_index(0))
        if not paths:
            self.ball = None
            clear(self.content)
            self.content.append(
                Adw.StatusPage(
                    title="Your OS/2 applications, at home",
                    description="Create a ball, map your installation media, and install an application.",
                    icon_name="application-x-executable-symbolic",
                    vexpand=True,
                )
            )

    def selected(self, _list, row):
        if row is None:
            return
        if self.ball != row.ball_path:
            self.last_log = None
            self.last_result = None
        self.ball = row.ball_path
        self.render()
        self.split.set_show_content(True)

    def render(self):
        clear(self.content)
        try:
            data = balls.load(self.ball)
            roots = balls.drive_roots(self.ball, data)
        except (OSError, ValueError, KeyError) as error:
            self.content.append(Adw.StatusPage(title="Cannot open ball", description=str(error)))
            return
        self.detail.set_title(self.ball.name)
        self.stack = Adw.ViewStack(vexpand=True)
        switcher = Adw.ViewSwitcher(stack=self.stack, policy=Adw.ViewSwitcherPolicy.WIDE)
        self.content.append(switcher)
        self.content.append(self.stack)
        apps = self.page("apps", "Applications", "application-x-executable-symbolic")
        controls = Gtk.Box(spacing=12)
        controls.append(button("Install software…", lambda: self.program_dialog("install")))
        controls.append(button("Run executable…", lambda: self.program_dialog("run")))
        controls.append(button("Add application…", lambda: self.program_dialog("add-app")))
        apps.append(controls)
        group = Adw.PreferencesGroup(title="Saved applications")
        apps.append(group)
        for name, app in sorted(data.get("applications", {}).items()):
            row = Adw.ActionRow(title=name, subtitle=app["program"])
            row.add_suffix(button("Run", lambda name=name: self.execute(["launch", name])))
            row.add_suffix(button("Remove", lambda name=name: self.execute(["remove-app", name])))
            group.add(row)
        if not data.get("applications"):
            group.set_description("Add installed programs here to launch them again.")
        drives = self.page("drives", "Drives", "drive-harddisk-symbolic")
        group = Adw.PreferencesGroup(
            title="Drive mappings",
            description="Map a folder or extracted installation CD to an OS/2 drive.",
        )
        drives.append(group)
        for letter, path in roots.items():
            row = Adw.ActionRow(title=letter + ":", subtitle=str(path))
            row.add_suffix(button("Open", lambda path=path: self.open_path(path)))
            if letter != "C":
                row.add_suffix(
                    button("Unmap", lambda letter=letter: self.execute(["unmap", letter]))
                )
            group.add(row)
        drives.append(button("Map drive…", self.map_dialog))
        settings = self.page("settings", "Settings", "preferences-system-symbolic")
        group = Adw.PreferencesGroup(title="2ine runtime")
        settings.append(group)
        backend = Adw.ComboRow(
            title="Execution backend",
            model=Gtk.StringList.new(["Build default", "Native", "Unicorn"]),
        )
        backend.set_selected(
            {"native": 1, "unicorn": 2}.get(data.get("runtime", {}).get("backend"), 0)
        )
        backend.connect("notify::selected", self.backend_changed)
        group.add(backend)
        for key, title in (
            ("trace_native", "Trace native calls"),
            ("trace_events", "Trace events"),
        ):
            row = Adw.SwitchRow(title=title)
            row.set_active(
                str(data.get("system", {}).get(key, "0")).lower()
                in ("1", "yes", "y", "true", "t", "on")
            )
            row.connect(
                "notify::active",
                lambda row, _, key=key: self.execute(
                    ["config", "system." + key, str(int(row.get_active()))]
                ),
            )
            group.add(row)
        volume = Adw.EntryRow(title="Beep volume (0–1)")
        volume.set_text(str(data.get("system", {}).get("beep_volume", "1")))
        volume.set_show_apply_button(True)
        volume.connect(
            "apply", lambda row: self.execute(["config", "system.beep_volume", row.get_text()])
        )
        group.add(volume)
        settings.append(
            button("Edit CONFIG.SYS…", lambda: self.config_dialog(roots["C"] / "CONFIG.SYS"))
        )
        group = Adw.PreferencesGroup(
            title="Environment overrides", description="Overrides take precedence over CONFIG.SYS."
        )
        settings.append(group)
        for key, value in sorted(data.get("environment", {}).items()):
            row = Adw.ActionRow(title=key, subtitle=value)
            row.add_suffix(button("Edit", lambda key=key, value=value: self.env_dialog(key, value)))
            row.add_suffix(button("Reset", lambda key=key: self.execute(["unset", "env." + key])))
            group.add(row)
        settings.append(button("Add environment variable…", self.env_dialog))
        activity = self.page("activity", "Activity", "document-open-recent-symbolic")
        self.status = Gtk.Label(xalign=0, wrap=True)
        activity.append(self.status)
        activity.append(button("Open ball folder", lambda: self.open_path(self.ball)))
        activity.append(button("Open logs", lambda: self.open_path(self.ball / "logs")))
        self.log_view = Gtk.TextView(
            editable=False, monospace=True, wrap_mode=Gtk.WrapMode.WORD_CHAR
        )
        log_scroll = Gtk.ScrolledWindow(min_content_height=300, vexpand=True)
        log_scroll.set_child(self.log_view)
        activity.append(log_scroll)
        receipt = self.ball / "logs/last-install.json"
        if receipt.exists():
            try:
                result = json.loads(receipt.read_text())
                activity.append(
                    Gtk.Label(label=f"Last installer exit code: {result['exit_code']}", xalign=0)
                )
            except (OSError, ValueError, KeyError):
                pass
        self.update_activity()
        self.content.set_sensitive(self.process is None)

    def page(self, name, title, icon):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        for edge in ("top", "bottom", "start", "end"):
            getattr(box, "set_margin_" + edge)(24)
        clamp = Adw.Clamp(maximum_size=850)
        clamp.set_child(box)
        scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroll.set_child(clamp)
        self.stack.add_titled_with_icon(scroll, name, title, icon)
        return box

    def backend_changed(self, row, _):
        selected = row.get_selected()
        self.execute(
            ["unset", "runtime.backend"]
            if selected == 0
            else ["config", "runtime.backend", ("native", "unicorn")[selected - 1]]
        )

    def choose(self, title, callback, folder=False):
        dialog = Gtk.FileDialog(title=title)

        def done(dialog, result):
            try:
                file = dialog.select_folder_finish(result) if folder else dialog.open_finish(result)
                path = file.get_path()
                if path:
                    callback(Path(path))
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.toast(error.message)

        if folder:
            dialog.select_folder(self, None, done)
        else:
            dialog.open(self, None, done)

    def form(self, title, fields, callback, accept="Save"):
        dialog = Adw.Dialog(title=title, content_width=520)
        view = Adw.ToolbarView()
        header = Adw.HeaderBar()
        view.add_top_bar(header)
        group = Adw.PreferencesGroup(
            margin_top=18, margin_bottom=24, margin_start=24, margin_end=24
        )
        view.set_content(group)
        entries = {}
        for key, label, value, browse in fields:
            row = Adw.EntryRow(title=label, text=value)
            if browse:
                row.add_suffix(
                    button(
                        "folder-open-symbolic",
                        lambda row=row, browse=browse, label=label: self.choose(
                            label, lambda path: row.set_text(str(path)), folder=browse == "folder"
                        ),
                        True,
                    )
                )
            group.add(row)
            entries[key] = row

        def submit():
            try:
                callback({key: row.get_text() for key, row in entries.items()})
            except (OSError, ValueError, KeyError) as error:
                self.toast(error)
                return
            dialog.close()

        apply = button(accept, submit)
        apply.add_css_class("suggested-action")
        header.pack_start(button("Cancel", dialog.close))
        header.pack_end(apply)
        dialog.set_child(view)
        dialog.present(self)
        return entries

    def new_ball(self):
        def create(values):
            name = values["name"].strip()
            if not name or name in (".", "..") or any(c in name for c in "/\\\r\n"):
                raise ValueError("Choose a ball name without slashes.")
            path = balls.balls_home() / name
            if path.exists():
                raise ValueError("A ball with that name already exists.")
            balls.init(path)
            self.refresh(path)

        return self.form("New ball", [("name", "Ball name", "", None)], create, "Create")

    def open_ball(self):
        def opened(path):
            try:
                self.remember(path)
                self.refresh(path)
            except (OSError, ValueError, KeyError) as error:
                self.toast(error)

        self.choose("Open ball folder", opened, folder=True)

    def runtime_dialog(self):
        def update(values):
            path = Path(values["build"]).expanduser().resolve()
            if not (path / "lx_loader").is_file():
                raise ValueError("Choose the build folder containing lx_loader.")
            self.build = path
            self.persist_preferences()

        self.form(
            "Runtime location", [("build", "2ine build folder", str(self.build), "folder")], update
        )

    def map_dialog(self):
        return self.form(
            "Map drive",
            [("drive", "Drive letter", "D", None), ("folder", "Host folder", "", "folder")],
            lambda values: self.execute(["map", values["drive"], values["folder"]]),
            "Map",
        )

    def env_dialog(self, key="", value=""):
        return self.form(
            "Environment variable",
            [("key", "Variable name", key, None), ("value", "Value", value, None)],
            lambda values: self.execute(["config", "env." + values["key"], values["value"]]),
        )

    def program_dialog(self, action):
        fields = []
        if action == "add-app":
            fields.append(("name", "Application name", "", None))
        fields.extend(
            [
                ("program", "Executable (OS/2 path or mapped host file)", "", "file"),
                ("arguments", "Arguments (quote values containing spaces)", "", None),
            ]
        )
        if action != "add-app":
            fields.append(("cwd", "Working directory (optional)", "", "folder"))

        def launch(values):
            if not values["program"] or (action == "add-app" and not values["name"].strip()):
                raise ValueError(
                    "Enter an executable and an application name when saving a launcher."
                )
            arguments = [action]
            if action == "add-app":
                arguments.append(values["name"].strip())
            elif values["cwd"]:
                arguments.extend(["--cwd", values["cwd"]])
            arguments.extend([values["program"], *parse_arguments(values["arguments"])])
            self.execute(arguments)

        self.form(
            {"install": "Install software", "run": "Run executable", "add-app": "Add application"}[
                action
            ],
            fields,
            launch,
            "Save" if action == "add-app" else "Run",
        )

    def config_dialog(self, path):
        if self.process:
            self.toast("Wait for the current operation to finish.")
            return
        dialog = Adw.Dialog(title="CONFIG.SYS", content_width=720, content_height=500)
        view = Adw.ToolbarView()
        header = Adw.HeaderBar()
        view.add_top_bar(header)
        editor = Gtk.TextView(monospace=True, left_margin=12, right_margin=12, top_margin=12)
        try:
            editor.get_buffer().set_text(path.read_text(encoding="latin1"))
        except OSError as error:
            self.toast(error)
            return
        scroll = Gtk.ScrolledWindow()
        scroll.set_child(editor)
        view.set_content(scroll)

        def save():
            buffer = editor.get_buffer()
            text = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
            try:
                contents = text.replace("\r\n", "\n").replace("\n", "\r\n").encode("latin1")
                temporary = path.with_suffix(".SYS.tmp")
                temporary.write_bytes(contents)
                temporary.replace(path)
            except (OSError, UnicodeError) as error:
                self.toast(error)
                return
            dialog.close()

        header.pack_start(button("Cancel", dialog.close))
        header.pack_end(button("Save", save))
        dialog.set_child(view)
        dialog.present(self)

    def execute(self, arguments):
        if self.process:
            raise ValueError("Wait for the current operation to finish.")
        path = self.ball
        (path / "logs").mkdir(exist_ok=True)
        stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
        self.last_log = path / "logs" / f"desktop-{stamp}.log"
        self.last_result = None
        launcher = Gio.SubprocessLauncher.new(Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_stdout_file_path(str(self.last_log))
        try:
            self.process = launcher.spawnv(
                [
                    sys.executable,
                    str(Path(balls.__file__).resolve()),
                    "--ball",
                    str(path),
                    "--build",
                    str(self.build),
                    *arguments,
                ]
            )
        except GLib.Error as error:
            self.toast(error.message)
            return
        self.content.set_sensitive(False)
        self.sidebar.set_sensitive(False)
        self.header.set_sensitive(False)
        self.stack.set_visible_child_name("activity")
        self.update_activity()
        self.process.wait_async(None, self.completed, path)
        GLib.timeout_add(500, self.poll_log)

    def completed(self, process, result, path):
        try:
            process.wait_finish(result)
            self.last_result = (
                process.get_exit_status()
                if process.get_if_exited()
                else 128 + process.get_term_sig()
            )
        except GLib.Error as error:
            self.last_result = str(error)
        self.process = None
        self.sidebar.set_sensitive(True)
        self.header.set_sensitive(True)
        self.refresh(path)
        self.stack.set_visible_child_name("activity")
        self.toast(f"Operation finished: exit {self.last_result}")

    def update_activity(self):
        if self.process:
            self.status.set_label("Running… Close the application's windows to finish.")
        elif self.last_result is not None:
            self.status.set_label(f"Last operation exit: {self.last_result}")
        else:
            self.status.set_label("Ready")
        if self.last_log and self.last_log.exists():
            with self.last_log.open("rb") as log:
                log.seek(max(0, self.last_log.stat().st_size - 65536))
                self.log_view.get_buffer().set_text(
                    log.read().decode("utf-8", errors="replace").replace("\x00", "")
                )

    def poll_log(self):
        self.update_activity()
        return self.process is not None

    def open_path(self, path):
        try:
            Gio.AppInfo.launch_default_for_uri(path.as_uri(), None)
        except GLib.Error as error:
            self.toast(error.message)


class BallApplication(Adw.Application):
    def __init__(self, build=None, ball=None):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.build = build
        self.initial = ball

    def do_activate(self):
        window = self.get_active_window()
        if window is None:
            window = BallWindow(self, self.build, self.initial)
        window.present()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, help="2ine build directory containing lx_loader")
    parser.add_argument("--ball", type=Path, help="open an existing ball")
    args = parser.parse_args()
    return BallApplication(args.build, args.ball).run([sys.argv[0]])


if __name__ == "__main__":
    raise SystemExit(main())
