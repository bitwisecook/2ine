#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native GTK 4/libadwaita manager for persistent 2ine balls."""

import argparse
import json
import os
import signal
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime
from pathlib import Path

if __package__:
    from . import balls, gnome_views
else:
    import balls
    import gnome_views

try:
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Adw, Gdk, Gio, GLib, Gtk
except (ImportError, ValueError) as error:
    raise SystemExit(
        "2ine requires GTK 4, libadwaita and PyGObject. On Ubuntu install "
        "python3-gi gir1.2-gtk-4.0 gir1.2-adw-1.\n" + str(error)
    ) from error

APP_ID = "io.github.bitwisecook.Twoine.Balls"


def parse_arguments(text):
    return balls.native_request({"action": "parse-arguments", "text": text})


def button(label, callback, icon=False):
    widget = Gtk.Button(icon_name=label) if icon else Gtk.Button(label=label)
    if icon:
        name = {
            "list-add-symbolic": "Add",
            "folder-open-symbolic": "Choose folder or file",
            "view-refresh-symbolic": "Refresh ball",
            "media-playback-start-symbolic": "Launch application",
            "user-trash-symbolic": "Remove application launcher",
            "list-remove-symbolic": "Unmap drive",
            "edit-clear-symbolic": "Reset override",
            "document-edit-symbolic": "Edit environment variable",
        }.get(label, label)
        widget.set_tooltip_text(name)
        widget.update_property([Gtk.AccessibleProperty.LABEL], [name])
    widget.set_valign(Gtk.Align.CENTER)
    widget.connect("clicked", lambda *_: callback())
    return widget


def clear(box):
    while child := box.get_first_child():
        box.remove(child)


class BallWindow(Adw.ApplicationWindow):
    def __init__(self, app, build, initial=None):
        super().__init__(application=app, title="2ine", default_width=1080, default_height=760)
        self.default_build = (
            build or Path(os.environ.get("TWOINE_BUILD", balls.ROOT / "build"))
        ).resolve()
        self.build = self.default_build
        balls.configure_library(self.build)
        self.ball = None
        self.snapshot = None
        self.process = None
        self.busy = False
        self.operation_busy = False
        self.pending_operations = 0
        self.last_result = None
        self.last_log = None
        self.log_text = ""
        self.activity_title = "Ready"
        self.install_summary = ""
        self.external = []
        self.drafts = {}
        self.settings_drafts = {}
        self.undo_commands = []
        self.redo_commands = []
        self.browse_locations = {}
        self.selected_drive = None
        self.icon_cache = {}
        self.icon_requests = set()
        self.stop_sent = False
        self.worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="ball-engine")
        self.preferences = (
            Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
            / "2ine/balls-gnome.json"
        )
        self.connect("close-request", self.close_requested)
        self.connect("destroy", lambda *_: self.worker.shutdown(wait=False))
        builder = Gtk.Builder.new_from_file(str(Path(__file__).parent / "gnome/window.ui"))
        for name in (
            "overlay",
            "split",
            "sidebar",
            "ball_list",
            "header",
            "content",
            "detail",
            "banner",
            "spinner",
        ):
            setattr(self, name, builder.get_object(name))
        self.set_content(self.overlay)
        Gtk.IconTheme.get_for_display(self.get_display()).add_search_path(
            str(Path(__file__).parent / "gnome")
        )
        self.ball_list.connect("row-selected", self.selected)
        for name, callback in (
            ("new", self.new_ball),
            ("open", self.open_ball),
            ("import", self.import_ball),
            ("export", self.export_ball),
            ("export-images", lambda: self.export_ball(True)),
            ("refresh", self.refresh),
            ("runtime", self.runtime_dialog),
            ("save", self.save_config),
            ("undo", self.undo),
            ("redo", self.redo),
            ("stop", self.stop_process),
            ("force-stop", lambda: self.stop_process(force=True)),
            ("add-drive", self.map_dialog),
            ("create-disk", self.create_image),
            ("create-floppy", lambda: self.create_image(floppy=True)),
            ("install", lambda: self.program_dialog("install")),
            ("run", lambda: self.program_dialog("run")),
            ("about", self.about),
            ("copy", lambda: self.get_clipboard().set(str(self.ball)) if self.ball else None),
            ("reveal", lambda: self.open_path(self.ball) if self.ball else None),
        ):
            action = Gio.SimpleAction.new(name, None)
            action.connect("activate", lambda _a, _p, callback=callback: callback())
            self.add_action(action)
        for name, shortcut in (
            ("new", "<Control>n"),
            ("open", "<Control>o"),
            ("refresh", "<Control>r"),
            ("save", "<Control>s"),
            ("undo", "<Control>z"),
            ("redo", "<Control><Shift>z"),
            ("stop", "<Control>period"),
            ("install", "<Control>i"),
            ("run", "<Control><Shift>r"),
        ):
            app.set_accels_for_action("win." + name, [shortcut])
        for i, page in enumerate(("drives", "apps", "settings", "config", "activity"), 1):
            action = Gio.SimpleAction.new("page" + str(i), None)
            action.connect("activate", lambda _a, _p, page=page: self.show_page(page))
            self.add_action(action)
            app.set_accels_for_action("win.page" + str(i), ["<Alt>" + str(i)])
        self.medium_breakpoint = Adw.Breakpoint.new(
            Adw.BreakpointCondition.parse("max-width: 1000sp")
        )
        self.add_breakpoint(self.medium_breakpoint)
        self.narrow_breakpoint = Adw.Breakpoint.new(
            Adw.BreakpointCondition.parse("max-width: 750sp")
        )
        self.narrow_breakpoint.add_setter(self.split, "collapsed", True)
        self.add_breakpoint(self.narrow_breakpoint)
        drop = Gtk.DropTarget.new(Gdk.FileList, Gdk.DragAction.COPY)
        drop.connect("drop", self.dropped)
        self.add_controller(drop)
        self.request_work(lambda: self.startup(initial), self.started)

    def request_work(
        self, work, completed, *, locked=True, on_error=None, allow_running=False, allow_queue=False
    ):
        if (
            locked
            and not allow_queue
            and (self.operation_busy or (self.process and not allow_running))
        ):
            self.toast("Wait for the current operation to finish.")
            return False
        if locked:
            self.pending_operations += 1
            self.set_busy(False)
        future = self.worker.submit(work)

        def deliver():
            if locked:
                self.pending_operations -= 1
                self.set_busy(False)
            try:
                value = future.result()
            except (OSError, ValueError, KeyError, TypeError) as error:
                (on_error or self.report_error)(error)
                return GLib.SOURCE_REMOVE
            completed(value)
            return GLib.SOURCE_REMOVE

        future.add_done_callback(lambda _future: GLib.idle_add(deliver))
        return True

    def set_busy(self, busy):
        self.operation_busy = bool(busy or self.pending_operations)
        self.busy = bool(self.operation_busy or self.process)
        busy = self.busy
        self.sidebar.set_sensitive(not busy)
        self.spinner.set_spinning(busy)
        self.spinner.set_visible(busy)
        for name in (
            "new",
            "open",
            "import",
            "export",
            "export-images",
            "refresh",
            "runtime",
            "install",
            "run",
            "undo",
            "redo",
            "add-drive",
            "create-disk",
            "create-floppy",
            "save",
        ):
            self.lookup_action(name).set_enabled(
                not busy and (self.ball is not None or name in ("new", "open", "import", "refresh"))
            )
        self.lookup_action("stop").set_enabled(bool(self.process) and not self.stop_sent)
        self.lookup_action("force-stop").set_enabled(bool(self.process) and self.stop_sent)
        self.lookup_action("undo").set_enabled(not busy and bool(self.undo_commands))
        self.lookup_action("redo").set_enabled(not busy and bool(self.redo_commands))
        if hasattr(self, "stack"):
            for name in ("apps", "drives", "settings", "config"):
                child = self.stack.get_child_by_name(name)
                if child:
                    child.set_sensitive(not self.operation_busy if name == "drives" else not busy)

    def report_error(self, error):
        self.activity_title = "Operation could not be completed"
        self.update_activity()
        self.banner.set_title(str(error))
        self.banner.set_revealed(True)
        self.toast(error)

    def startup(self, initial):
        try:
            prefs = json.loads(self.preferences.read_text())
        except (OSError, ValueError):
            prefs = {}
        if not isinstance(prefs, dict):
            prefs = {}
        saved_paths = prefs.get("balls", [])
        if not isinstance(saved_paths, list):
            saved_paths = []
        if not isinstance(prefs.get("selected"), str):
            prefs.pop("selected", None)
        paths = {Path(p) for p in saved_paths if isinstance(p, str)} | {
            Path(p) for p in balls.native_request({"action": "discover"})
        }
        target = (
            initial.resolve()
            if initial
            else Path(prefs["selected"])
            if prefs.get("selected")
            else None
        )
        if target:
            paths.add(target)
        return sorted(paths), target

    def started(self, result):
        self.external, target = result
        self.refresh(target)

    def close_requested(self, *_):
        if self.busy:
            self.toast("Wait for the running application or operation to finish.")
            return True
        if self.drafts:
            dialog = Adw.AlertDialog(
                heading="Save CONFIG.SYS changes?",
                body="There are unsaved edits in one or more balls.",
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard Changes")
            dialog.add_response("save", "Save Changes")
            dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
            dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
            dialog.set_default_response("save")
            dialog.set_close_response("cancel")

            def response(_dialog, choice):
                if choice == "discard":
                    self.drafts.clear()
                    self.close()
                elif choice == "save":
                    drafts = dict(self.drafts)

                    def save_all():
                        saved, error = [], None
                        for path, (base, text) in drafts.items():
                            try:
                                balls.native_request(
                                    {
                                        "action": "save-config-sys",
                                        "ball": str(path),
                                        "expected": base,
                                        "text": text,
                                    }
                                )
                            except (OSError, ValueError) as failure:
                                error = str(failure)
                                break
                            saved.append(path)
                        return saved, error

                    def saved(result):
                        paths, error = result
                        for path in paths:
                            self.drafts.pop(path, None)
                        if error:
                            self.report_error(error)
                        else:
                            self.close()

                    self.request_work(save_all, saved)

            dialog.connect("response", response)
            dialog.present(self)
            return True
        return False

    def toast(self, message, undo=None):
        toast = Adw.Toast(title=str(message))
        if undo:
            toast.set_button_label("Undo")
            toast.connect("button-clicked", lambda *_: self.undo(undo))
        self.overlay.add_toast(toast)

    def persist_preferences(self):
        data = {
            "balls": [str(p) for p in self.external],
            "selected": str(self.ball) if self.ball else None,
        }

        def save():
            self.preferences.parent.mkdir(parents=True, exist_ok=True)
            balls.save(self.preferences, data)

        self.request_work(save, lambda _result: None, locked=False)

    def remember(self, path):
        path = path.resolve()
        if path.suffix.lower() == ".zip":
            self.import_ball(path)
            return

        def opened(snapshot):
            if path not in self.external:
                self.external.append(path)
            self.ball = path
            self.snapshot = snapshot
            self.refresh(path)

        self.request_work(
            lambda: balls.native_request({"action": "snapshot", "ball": str(path)}), opened
        )

    def refresh(self, select=None):
        if self.busy:
            return
        target = select if isinstance(select, Path) else self.ball
        self.request_work(
            lambda: sorted(
                set(self.external) | {Path(p) for p in balls.native_request({"action": "discover"})}
            ),
            lambda paths: self.populate(paths, target),
        )

    def populate(self, paths, target):
        clear(self.ball_list)
        chosen = None
        for path in paths:
            row = Adw.ActionRow(
                title=path.name.removesuffix(".2ineball"),
                subtitle=str(path),
                subtitle_lines=1,
                use_markup=False,
            )
            row.ball_path = path
            row.add_prefix(Gtk.Image(icon_name=APP_ID + "-symbolic"))
            drag = Gtk.DragSource(actions=Gdk.DragAction.COPY)
            drag.connect(
                "prepare",
                lambda _source, _x, _y, path=path: Gdk.ContentProvider.new_for_value(
                    Gdk.FileList.new_from_list([Gio.File.new_for_path(str(path))])
                ),
            )
            row.add_controller(drag)
            self.ball_list.append(row)
            if path == target:
                chosen = row
        self.ball_list.select_row(chosen or self.ball_list.get_row_at_index(0))
        if not paths:
            self.ball = None
            clear(self.content)
            page = Adw.StatusPage(
                title="Your OS/2 applications, at home",
                description="Keep each application's drives, settings and installed files together in a ball of 2ine.",
                icon_name=APP_ID,
            )
            create = button("Create a Ball", self.new_ball)
            create.add_css_class("suggested-action")
            create.set_halign(Gtk.Align.CENTER)
            page.set_child(create)
            self.content.append(page)
            self.set_busy(False)

    def selected(self, _list, row):
        if row is None or self.busy:
            return
        path = row.ball_path
        previous = self.ball

        def loaded(result):
            snapshot, self.install_summary = result
            if previous != path:
                self.last_log, self.last_result, self.log_text = None, None, ""
                self.activity_title = "Ready"
            self.ball, self.snapshot = path, snapshot
            self.build = Path(
                snapshot["manifest"].get("runtime", {}).get("build") or self.default_build
            )
            self.render()
            self.set_busy(False)
            self.persist_preferences()
            self.split.set_show_content(True)

        self.request_work(
            lambda: (
                balls.native_request({"action": "snapshot", "ball": str(path)}),
                self.read_receipt(path),
            ),
            loaded,
        )

    @staticmethod
    def read_receipt(path):
        try:
            receipt = json.loads((path / "logs/last-install.json").read_text())
            return f"Last installer: exit {receipt['exit_code']} · runtime {receipt['runtime_id'][:16]}"
        except FileNotFoundError:
            return "No installer receipt yet"
        except (OSError, ValueError, KeyError, TypeError):
            return "The last installer receipt could not be read"

    def show_page(self, name):
        if hasattr(self, "stack"):
            self.stack.set_visible_child_name(name)

    def render(self):
        previous = self.stack.get_visible_child_name() if hasattr(self, "stack") else "drives"
        clear(self.content)
        data = self.snapshot["manifest"]
        roots = {key: Path(value) for key, value in self.snapshot["roots"].items()}
        self.detail.set_title(self.ball.name.removesuffix(".2ineball"))
        self.stack = Adw.ViewStack(vexpand=True)
        if not hasattr(self, "switcher"):
            self.switcher = Adw.InlineViewSwitcher(
                display_mode=Adw.InlineViewSwitcherDisplayMode.BOTH, halign=Gtk.Align.CENTER
            )
            self.switcher.set_margin_top(6)
            self.switcher.set_margin_bottom(6)
            for breakpoint in (self.medium_breakpoint, self.narrow_breakpoint):
                breakpoint.add_setter(
                    self.switcher, "display-mode", Adw.InlineViewSwitcherDisplayMode.ICONS
                )
        self.switcher.set_stack(self.stack)
        self.content.append(self.switcher)
        self.content.append(self.stack)
        self.drives_page = gnome_views.DrivesPage(self)
        self.stack.add_titled_with_icon(
            self.drives_page, "drives", "Drives", "drive-harddisk-symbolic"
        )
        apps = self.page("apps", "Applications", "application-x-executable-symbolic")
        controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        install = button("Install Software…", lambda: self.program_dialog("install"))
        install.add_css_class("suggested-action")
        controls.append(install)
        controls.append(button("Run Program…", lambda: self.program_dialog("run")))
        controls.append(button("Add Application…", lambda: self.program_dialog("add-app")))
        apps.append(controls)
        group = Adw.PreferencesGroup(title="Applications")
        apps.append(group)
        for name, app in sorted(data.get("applications", {}).items()):
            row = Adw.ActionRow(
                title=name,
                subtitle=app["program"] + (" · " + app["cwd"] if app.get("cwd") else ""),
                use_markup=False,
                activatable=True,
            )
            icon = Gtk.Image(icon_name="application-x-executable-symbolic", pixel_size=40)
            row.add_prefix(icon)
            self.load_application_icon(name, icon)
            row.add_suffix(
                button(
                    "document-edit-symbolic",
                    lambda name=name: self.program_dialog("add-app", name=name),
                    True,
                )
            )
            row.connect("activated", lambda _r, name=name: self.execute(["launch", name]))
            row.add_suffix(
                button(
                    "media-playback-start-symbolic",
                    lambda name=name: self.execute(["launch", name]),
                    True,
                )
            )
            row.add_suffix(
                button(
                    "user-trash-symbolic",
                    lambda name=name: self.execute(["remove-app", name]),
                    True,
                )
            )
            group.add(row)
        if not data.get("applications"):
            apps.append(
                Adw.StatusPage(
                    title="A home for your applications",
                    description="Install software from your OS/2 media, then add its executable to keep it here.",
                    icon_name="application-x-executable-symbolic",
                )
            )
        settings = self.page("settings", "Settings", "preferences-system-symbolic")
        group = Adw.PreferencesGroup(title="Runtime")
        settings.append(group)
        runtime = Adw.ActionRow(
            title="Runtime Build",
            subtitle=str(self.build),
            subtitle_lines=1,
            use_markup=False,
            activatable=True,
        )
        runtime.connect("activated", lambda *_: self.runtime_dialog())
        runtime.add_suffix(button("folder-open-symbolic", self.runtime_dialog, True))
        group.add(runtime)
        group.add(self.runtime_inheritance_row())
        backend = Adw.ComboRow(
            title="Execution Backend",
            model=Gtk.StringList.new(["Build Default", "Native (x86 Linux)", "Unicorn"]),
        )
        backend.set_selected(
            {"native": 1, "unicorn": 2}.get(data.get("runtime", {}).get("backend"), 0)
        )
        backend.connect("notify::selected", self.backend_changed)
        group.add(backend)
        group = Adw.PreferencesGroup(
            title="2ine Settings", description="Reset an override to inherit the runtime default."
        )
        settings.append(group)
        for key, title in (
            ("trace_native", "Trace Native Calls"),
            ("trace_events", "Trace Events"),
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
            row.add_suffix(
                button(
                    "edit-clear-symbolic",
                    lambda key=key: self.execute(["unset", "system." + key]),
                    True,
                )
            )
            group.add(row)
        volume = Adw.EntryRow(
            title="Beep Volume (0–1)",
            text=self.settings_drafts.get(str(self.ball), {}).get(
                "beep_volume", str(data.get("system", {}).get("beep_volume", "1"))
            ),
            show_apply_button=True,
        )
        volume.connect(
            "notify::text",
            lambda row, _prop, ball_key=str(self.ball): self.settings_drafts.setdefault(
                ball_key, {}
            ).update(beep_volume=row.get_text()),
        )
        volume.connect(
            "apply", lambda row: self.execute(["config", "system.beep_volume", row.get_text()])
        )
        volume.add_suffix(
            button(
                "edit-clear-symbolic", lambda: self.execute(["unset", "system.beep_volume"]), True
            )
        )
        group.add(volume)
        group = Adw.PreferencesGroup(
            title="Environment Overrides",
            description="Overrides take precedence over CONFIG.SYS. Use OS2PATH for guest programs; PATH belongs to the host.",
        )
        group.set_header_suffix(button("list-add-symbolic", self.env_dialog, True))
        settings.append(group)
        for key, value in sorted(data.get("environment", {}).items()):
            row = Adw.ActionRow(title=key, subtitle=value, use_markup=False, activatable=True)
            row.connect("activated", lambda _r, key=key, value=value: self.env_dialog(key, value))
            row.add_suffix(
                button(
                    "document-edit-symbolic",
                    lambda key=key, value=value: self.env_dialog(key, value),
                    True,
                )
            )
            row.add_suffix(
                button(
                    "edit-clear-symbolic",
                    lambda key=key: self.execute(["unset", "env." + key]),
                    True,
                )
            )
            group.add(row)
        group = Adw.PreferencesGroup(
            title="Application State",
            description="Profiles, registry and shell metadata stay in C:\\OS2. Binary state is managed by installed applications.",
        )
        row = Adw.ActionRow(title="Open State Folder", activatable=True)
        row.connect("activated", lambda *_: self.open_path(roots["C"] / "OS2"))
        row.add_suffix(Gtk.Image(icon_name="folder-open-symbolic"))
        group.add(row)
        settings.append(group)
        config = self.page("config", "CONFIG.SYS", "document-edit-symbolic")
        config.append(
            Gtk.Label(
                label="SET and LIBPATH apply at launch. Drivers and boot commands are not executed.",
                wrap=True,
                xalign=0,
            )
        )
        self.config_editor = Gtk.TextView(
            monospace=True,
            wrap_mode=Gtk.WrapMode.NONE,
            left_margin=12,
            right_margin=12,
            top_margin=12,
            bottom_margin=12,
        )
        self.config_editor.update_property([Gtk.AccessibleProperty.LABEL], ["CONFIG.SYS contents"])
        self.config_editor.get_buffer().set_text(
            self.drafts.get(self.ball, (None, self.snapshot["configSys"]))[1]
        )
        self.config_editor.get_buffer().set_enable_undo(True)
        self.config_editor.get_buffer().connect("changed", self.config_changed)
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=280, has_frame=True)
        scroll.set_child(self.config_editor)
        config.append(scroll)
        controls = Gtk.Box(spacing=12)
        controls.append(button("Reload from Disk", self.reload_config))
        self.save_button = button("Save CONFIG.SYS", self.save_config)
        self.save_button.add_css_class("suggested-action")
        self.save_button.set_sensitive(self.ball in self.drafts)
        controls.append(self.save_button)
        config.append(controls)
        activity = self.page("activity", "Activity", "document-open-recent-symbolic")
        self.status = Gtk.Label(xalign=0, wrap=True)
        self.status.add_css_class("heading")
        activity.append(self.status)
        operation_controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        stop = Gtk.Button(label="Stop Operation", action_name="win.stop")
        stop.add_css_class("destructive-action")
        operation_controls.append(stop)
        operation_controls.append(Gtk.Button(label="Force Stop", action_name="win.force-stop"))
        operation_controls.append(
            button(
                "Open Full Log", lambda: self.open_path(self.last_log) if self.last_log else None
            )
        )
        activity.append(operation_controls)
        activity.append(Gtk.Label(label=self.install_summary, wrap=True, xalign=0, selectable=True))
        activity.append(
            Gtk.Label(
                label="Output preview shows the last 64 KiB. Complete logs are kept in the ball.",
                wrap=True,
                xalign=0,
                css_classes=["dim-label"],
            )
        )
        controls = Gtk.Box(spacing=12)
        controls.append(button("Open Logs", lambda: self.open_path(self.ball / "logs")))
        controls.append(button("Open Ball Folder", lambda: self.open_path(self.ball)))
        activity.append(controls)
        self.log_view = Gtk.TextView(
            editable=False,
            monospace=True,
            wrap_mode=Gtk.WrapMode.WORD_CHAR,
            left_margin=12,
            right_margin=12,
            top_margin=12,
        )
        self.log_view.update_property([Gtk.AccessibleProperty.LABEL], ["Application output"])
        scroll = Gtk.ScrolledWindow(min_content_height=220, vexpand=True, has_frame=True)
        scroll.set_child(self.log_view)
        activity.append(scroll)
        self.update_activity()
        self.stack.set_visible_child_name(previous or "drives")
        self.set_busy(self.operation_busy)

    def page(self, name, title, icon):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        for edge in ("top", "bottom", "start", "end"):
            getattr(box, "set_margin_" + edge)(18)
        clamp = Adw.Clamp(maximum_size=780)
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
            path = Path(values["location"]).expanduser() / (
                name if name.endswith(".2ineball") else name + ".2ineball"
            )

            def initialize():
                if path.exists():
                    raise ValueError("A ball with that name already exists.")
                return balls.native_request(
                    {"action": "command", "ball": str(path), "arguments": ["init"]}
                )

            def created(snapshot):
                self.external.append(path)
                self.ball, self.snapshot = path, snapshot
                self.refresh(path)

            self.request_work(initialize, created)

        return self.form(
            "New Ball",
            [
                ("name", "Ball Name", "", None),
                ("location", "Location", str(balls.balls_home()), "folder"),
            ],
            create,
            "Create",
        )

    def open_ball(self):
        self.choose("Open Ball", self.remember, folder=True)

    def import_ball(self, source=None):
        if self.busy:
            return
        if source is None:
            self.choose("Import Ball from ZIP", self.import_ball)
            return
        dialog = Gtk.FileDialog(
            title="Import into a New Ball", initial_name=source.stem + ".2ineball"
        )

        def saved(dialog, result):
            try:
                path = dialog.save_finish(result).get_path()
                if not path:
                    return
                destination = Path(path)

                def imported(snapshot):
                    self.ball, self.snapshot = destination, snapshot
                    if destination not in self.external:
                        self.external.append(destination)
                    self.render()
                    if snapshot.get("unavailableDrives"):
                        self.show_page("drives")
                    self.persist_preferences()
                    self.refresh(destination)

                self.request_work(
                    lambda: balls.native_request(
                        {"action": "import-ball", "ball": path, "path": str(source)}
                    ),
                    imported,
                )
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.report_error(error.message)

        dialog.save(self, None, saved)

    def export_ball(self, include_images=False):
        if self.busy or not self.ball:
            return
        if self.ball in self.drafts:
            self.report_error("Save CONFIG.SYS before exporting this ball.")
            self.show_page("config")
            return
        dialog = gnome_views.ExportDialog(self, include_images)
        dialog.present(self)
        return dialog

    def dropped(self, _target, files, _x, _y):
        if not self.busy and len(files.get_files()) == 1:
            path = files.get_files()[0].get_path()
            if path:
                self.remember(Path(path))
                return True
        return False

    def runtime_dialog(self):
        if self.ball:
            self.choose(
                "Choose 2ine Runtime Build",
                lambda path: self.commit_request(
                    {"action": "runtime-build", "build": str(path)}, label="Change Runtime"
                ),
                folder=True,
            )

    def runtime_inheritance_row(self):
        row = Adw.ActionRow(
            title="Use Packaged Runtime",
            subtitle="Clear this ball's external build selection",
            activatable=True,
        )
        row.add_suffix(Gtk.Image(icon_name="edit-undo-symbolic"))
        row.connect(
            "activated",
            lambda *_: self.commit_request({"action": "runtime-build"}, label="Reset Runtime"),
        )
        return row

    def attach_image(self, letter):
        self.choose(
            "Choose Disk Image", lambda path: self.media_command(["map-image", letter, str(path)])
        )

    def map_dialog(self, letter=None, folder=""):
        dialog = gnome_views.MediaDialog(self, letter, folder)
        dialog.present(self)
        return dialog

    def create_image(self, floppy=False, letter=None):
        dialog = gnome_views.CreateImageDialog(self, floppy=floppy, letter=letter)
        dialog.present(self)
        return dialog

    def device_settings(self, letter):
        dialog = gnome_views.DriveSettingsDialog(self, letter)
        dialog.present(self)
        return dialog

    def env_dialog(self, key="", value=""):
        return self.form(
            "Environment variable",
            [("key", "Variable name", key, None), ("value", "Value", value, None)],
            lambda values: self.execute(["config", "env." + values["key"], values["value"]]),
        )

    def program_dialog(self, action, name=None, program=""):
        dialog = gnome_views.ProgramDialog(self, action, name=name, program=program)
        dialog.present(self)
        return dialog

    def config_changed(self, buffer):
        text = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
        base = self.drafts.get(self.ball, (self.snapshot["configSys"], None))[0]
        if text == base:
            self.drafts.pop(self.ball, None)
        else:
            self.drafts[self.ball] = (base, text)
        self.save_button.set_sensitive(self.ball in self.drafts)

    def save_config(self):
        if self.ball not in self.drafts:
            return
        path = self.ball
        base, text = self.drafts[path]

        def saved(snapshot):
            self.drafts.pop(path, None)
            self.snapshot = snapshot
            self.render()
            self.toast("CONFIG.SYS saved")

        self.request_work(
            lambda: balls.native_request(
                {"action": "save-config-sys", "ball": str(path), "expected": base, "text": text}
            ),
            saved,
        )

    def reload_config(self):
        def reload():
            path = self.ball

            def loaded(snapshot):
                self.drafts.pop(path, None)
                self.snapshot = snapshot
                self.render()

            self.request_work(
                lambda: balls.native_request({"action": "snapshot", "ball": str(path)}), loaded
            )

        if self.ball not in self.drafts:
            reload()
            return
        dialog = Adw.AlertDialog(
            heading="Discard CONFIG.SYS changes?",
            body="Your unsaved edits will be replaced with the file on disk.",
        )
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("discard", "Discard Changes")
        dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_close_response("cancel")
        dialog.connect("response", lambda _d, choice: reload() if choice == "discard" else None)
        dialog.present(self)

    def api(self, action, **fields):
        return balls.native_request(
            {"action": action, "ball": str(self.ball), "build": str(self.build), **fields}
        )

    def apply_snapshot(self, snapshot):
        self.snapshot = snapshot
        self.build = Path(
            snapshot["manifest"].get("runtime", {}).get("build") or self.default_build
        )
        self.banner.set_revealed(False)
        self.render()

    def drive_inverse(self, letter):
        record = self.snapshot.get("driveRecords", {}).get(letter)
        return {
            "action": "command",
            "arguments": ["restore-drive", letter, record] if record else ["unmap", letter],
        }

    def inverse(self, arguments):
        data = self.snapshot["manifest"]
        action = arguments[0]
        if action in (
            "map",
            "unmap",
            "map-image",
            "device",
            "eject",
            "restore-drive",
            "create-floppy",
            "create-disk",
        ):
            return self.drive_inverse(arguments[1].upper().rstrip(":"))
        if action in ("device-lock", "media-readonly"):
            old = data["drives"].get(arguments[1], {})
            key = "locked" if action == "device-lock" else "readonly"
            return {
                "action": "command",
                "arguments": [
                    action,
                    arguments[1],
                    "on" if isinstance(old, dict) and old.get(key) else "off",
                ],
            }
        if action == "remove-app":
            record = self.snapshot["applicationRecords"].get(arguments[1])
            return (
                {"action": "command", "arguments": ["restore-app", arguments[1], record]}
                if record
                else None
            )
        if action == "add-app":
            name = arguments[3 if arguments[1] == "--cwd" else 1]
            record = self.snapshot["applicationRecords"].get(name)
            return {
                "action": "command",
                "arguments": ["restore-app", name, record] if record else ["remove-app", name],
            }
        if action in ("config", "unset"):
            group, key = arguments[1].split(".", 1)
            old = data.get("environment" if group == "env" else group, {}).get(key)
            value = str(old).lower() if isinstance(old, bool) else str(old)
            return {
                "action": "command",
                "arguments": ["config", arguments[1], value]
                if old is not None
                else ["unset", arguments[1]],
            }
        return None

    def add_history(self, inverse, forward, label):
        if inverse is None:
            return
        item = {"ball": self.ball, "undo": inverse, "redo": forward, "label": label}
        self.undo_commands.append(item)
        self.redo_commands.clear()
        self.set_busy(self.operation_busy)
        self.toast(label, item)

    def commit_request(
        self,
        message,
        inverse=None,
        label="Update Ball",
        *,
        allow_running=False,
        completed=None,
        on_error=None,
    ):
        if inverse is None and message["action"] == "command":
            inverse = self.inverse(message["arguments"])
        if inverse is None and message["action"] == "runtime-build":
            old = self.snapshot["manifest"].get("runtime", {}).get("build")
            inverse = {"action": "runtime-build", **({"build": old} if old else {})}
        path = self.ball
        request = {"ball": str(path), "build": str(self.build), **message}
        # Omitting build has defined reset semantics for this one backend action.
        if message["action"] == "runtime-build" and "build" not in message:
            request.pop("build", None)

        def changed(snapshot):
            argv = message.get("arguments", [])
            if (
                message["action"] == "command"
                and len(argv) > 1
                and argv[0] in ("config", "unset")
                and argv[1] == "system.beep_volume"
            ):
                self.settings_drafts.get(str(path), {}).pop("beep_volume", None)
            self.apply_snapshot(snapshot)
            self.add_history(inverse, message, label)
            if completed:
                completed(snapshot)

        return self.request_work(
            lambda: balls.native_request(request),
            changed,
            allow_running=allow_running,
            on_error=on_error,
        )

    def request(self, message, undo=None):
        if isinstance(undo, list):
            undo = {"action": "command", "arguments": undo}
        return self.commit_request(message, undo)

    def history_operation(self, item, reverse):
        messages = item["undo" if reverse else "redo"]
        messages = messages if isinstance(messages, list) else [messages]
        path = item["ball"]

        def work():
            for message in messages:
                request = {"ball": str(path), "build": str(self.build), **message}
                if message["action"] == "runtime-build" and "build" not in message:
                    request.pop("build", None)
                balls.native_request(request)
            return balls.native_request({"action": "snapshot", "ball": str(path)})

        def changed(snapshot):
            source, target = (
                (self.undo_commands, self.redo_commands)
                if reverse
                else (self.redo_commands, self.undo_commands)
            )
            source.remove(item)
            target.append(item)
            self.ball = path
            self.apply_snapshot(snapshot)
            self.persist_preferences()
            self.refresh(path)
            self.toast(("Undid " if reverse else "Redid ") + item["label"])

        self.request_work(work, changed)

    def undo(self, item=None):
        focus = self.get_focus()
        if item is None and isinstance(focus, Gtk.TextView):
            if focus.get_buffer().get_can_undo():
                focus.get_buffer().undo()
            return
        if item is None and isinstance(focus, Gtk.Editable):
            focus.activate_action("text.undo", None)
            return
        item = item or (self.undo_commands[-1] if self.undo_commands else None)
        if item in self.undo_commands and not self.busy:
            self.history_operation(item, True)

    def redo(self):
        focus = self.get_focus()
        if isinstance(focus, Gtk.TextView):
            if focus.get_buffer().get_can_redo():
                focus.get_buffer().redo()
            return
        if isinstance(focus, Gtk.Editable):
            focus.activate_action("text.redo", None)
            return
        if self.redo_commands and not self.busy:
            self.history_operation(self.redo_commands[-1], False)

    def media_command(self, arguments):
        return self.commit_request(
            {"action": "command", "arguments": arguments}, label="Change Media", allow_running=True
        )

    def prepare_launch(self, arguments, **fields):
        plan = self.api("prepare-launch", arguments=arguments, **fields)
        if arguments[0] == "install":
            program = arguments[3 if len(arguments) > 1 and arguments[1] == "--cwd" else 1]
            if len(program) > 1 and program[1] == ":":
                plan["gnomeMediaDrive"] = program[0].upper()
        stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
        log = self.ball / "logs" / f"gnome-{stamp}.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        return plan, log

    def execute(self, arguments, **fields):
        if self.busy or not self.ball:
            return False
        self.banner.set_revealed(False)
        if arguments[0] not in ("run", "install", "launch"):
            return self.commit_request({"action": "command", "arguments": arguments})
        path = self.ball
        self.activity_title = "Preparing application…"
        self.show_page("activity")
        return self.request_work(
            lambda: self.prepare_launch(arguments, **fields),
            lambda result: self.launch(path, *result),
        )

    def choose_save(self, title, initial, callback):
        dialog = Gtk.FileDialog(title=title, initial_name=initial)

        def chosen(dialog, result):
            try:
                path = dialog.save_finish(result).get_path()
                if path:
                    callback(Path(path))
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.report_error(error.message)

        dialog.save(self, None, chosen)

    def choose_many(self, title, callback, *, folder=False):
        dialog = Gtk.FileDialog(title=title)

        def chosen(dialog, result):
            try:
                files = (
                    dialog.select_multiple_folders_finish(result)
                    if folder
                    else dialog.open_multiple_finish(result)
                )
                paths = [files.get_item(i).get_path() for i in range(files.get_n_items())]
                if all(paths):
                    callback([Path(path) for path in paths])
                else:
                    self.report_error("Choose files available on this computer.")
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.report_error(error.message)

        if folder:
            dialog.select_multiple_folders(self, None, chosen)
        else:
            dialog.open_multiple(self, None, chosen)

    def load_application_icon(self, name, image):
        path = self.ball
        key = (str(path), name, self.snapshot["applicationRecords"].get(name))
        if key in self.icon_cache:
            if self.icon_cache[key]:
                image.set_from_paintable(self.icon_cache[key])
            return

        def loaded(result):
            try:
                paintable = gnome_views.texture(result)
                if len(self.icon_cache) > 256:
                    self.icon_cache.clear()
                self.icon_cache[key] = paintable
                if paintable:
                    image.set_from_paintable(paintable)
            except GLib.Error:
                self.icon_cache[key] = None

        self.request_work(
            lambda: self.api("application-icon", ball=str(path), name=name),
            loaded,
            locked=False,
            on_error=lambda _error: None,
        )

    def stop_process(self, force=False):
        if not self.process:
            return
        dialog = Adw.AlertDialog(
            heading="Force Stop Application?" if force else "Stop Application?",
            body="Unsaved work in the guest application may be lost.",
        )
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("stop", "Force Stop" if force else "Stop Application")
        dialog.set_response_appearance("stop", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_close_response("cancel")

        def response(_dialog, choice):
            if choice == "stop" and self.process:
                if force:
                    self.process.force_exit()
                else:
                    self.process.send_signal(signal.SIGTERM)
                self.stop_sent = True
                self.activity_title = "Waiting for application to exit…"
                self.set_busy(False)
                self.update_activity()

        dialog.connect("response", response)
        dialog.present(self)

    def launch(self, path, plan, log):
        launcher = Gio.SubprocessLauncher.new(Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_stdout_file_path(str(log))
        launcher.set_cwd(plan["cwd"])
        for key in plan["unsetEnvironment"]:
            launcher.unsetenv(key)
        for key, value in plan["environment"].items():
            launcher.setenv(key, value, True)
        try:
            self.process = launcher.spawnv(plan["command"])
        except GLib.Error as error:
            self.report_error(error.message)
            self.activity_title = "Could not start application"
            self.update_activity()
            return False
        self.last_log, self.last_result, self.log_text = log, None, ""
        self.activity_title = "Running… Close the application's windows to finish."
        self.stop_sent = False
        self.set_busy(False)
        self.show_page("activity")
        self.update_activity()
        self.process.wait_async(None, self.completed, (path, plan, log))
        self.log_polling = False
        GLib.timeout_add(250, self.poll_log)
        return True

    def completed(self, process, result, context):
        path, plan, log = context
        process.wait_finish(result)
        code = process.get_exit_status() if process.get_if_exited() else -process.get_term_sig()
        self.process = None
        self.last_result = code
        self.activity_title = (
            f"Application finished: exit {code}"
            if code >= 0
            else f"Application ended by signal {-code}"
        )

        def finish():
            receipt_error = None
            if plan["installer"]:
                try:
                    balls.native_request(
                        {
                            "action": "record-install",
                            "ball": str(path),
                            "receipt": plan["receipt"],
                            "programHost": plan["programHost"],
                            "exitCode": code,
                        }
                    )
                except (OSError, ValueError) as error:
                    receipt_error = str(error)
            return (
                balls.native_request({"action": "snapshot", "ball": str(path)}),
                self.read_log(log),
                receipt_error,
                self.read_receipt(path),
            )

        def finished(result):
            self.snapshot, self.log_text, error, self.install_summary = result
            self.render()
            self.show_page("activity")
            if error:
                self.report_error("Installer receipt could not be saved: " + error)
            self.toast(self.activity_title)
            letter = plan.get("gnomeMediaDrive")
            mapping = self.snapshot["manifest"]["drives"].get(letter)
            if isinstance(mapping, dict) and mapping.get("image") and not mapping.get("locked"):
                toast = Adw.Toast(
                    title="Installer finished. Installation media is still attached.",
                    button_label="Eject",
                )
                toast.connect("button-clicked", lambda *_: self.media_command(["eject", letter]))
                self.overlay.add_toast(toast)

        self.set_busy(False)
        self.request_work(finish, finished, allow_queue=True)

    @staticmethod
    def read_log(path):
        if not path or not path.exists():
            return ""
        with path.open("rb") as log:
            log.seek(max(0, path.stat().st_size - 65536))
            return log.read().decode("utf-8", errors="replace").replace("\x00", "")

    def update_activity(self):
        if hasattr(self, "status"):
            self.status.set_label(self.activity_title)
            self.log_view.get_buffer().set_text(self.log_text)

    def poll_log(self):
        if self.process and not self.log_polling:
            self.log_polling = True
            path = self.last_log

            def updated(text):
                self.log_polling = False
                if self.last_log == path:
                    self.log_text = text
                    self.update_activity()

            self.request_work(lambda: self.read_log(path), updated, locked=False)
        return self.process is not None

    def about(self):
        Adw.AboutDialog(
            application_name="2ine",
            application_icon=APP_ID,
            developer_name="2ine Contributors",
            version="0.1",
            website="https://github.com/bitwisecook/2ine",
            license_type=Gtk.License.ZLIB,
            comments="OS/2 applications, comfortably at home on GNOME.",
        ).present(self)

    def open_path(self, path):
        try:
            Gio.AppInfo.launch_default_for_uri(path.as_uri(), None)
        except GLib.Error as error:
            self.toast(error.message)


class BallApplication(Adw.Application):
    def __init__(self, build=None, ball=None):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.HANDLES_OPEN)
        self.build = build
        self.initial = ball
        quit_action = Gio.SimpleAction.new("quit", None)
        quit_action.connect(
            "activate",
            lambda *_: (
                self.get_active_window().close() if self.get_active_window() else self.quit()
            ),
        )
        self.add_action(quit_action)
        self.set_accels_for_action("app.quit", ["<Control>q"])
        self.set_resource_base_path("/io/github/bitwisecook/Twoine/Balls")

    def do_open(self, files, _count, _hint):
        self.do_activate()
        path = files[0].get_path() if files else None
        if path:
            window = self.get_active_window()

            def opened():
                if window.busy:
                    return GLib.SOURCE_CONTINUE
                window.remember(Path(path))
                return GLib.SOURCE_REMOVE

            GLib.timeout_add(50, opened)

    def do_activate(self):
        window = self.get_active_window()
        if window is None:
            window = BallWindow(self, self.build, self.initial)
        window.present()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="2ine 0.1")
    parser.add_argument("--build", type=Path, help="2ine build directory containing lx_loader")
    parser.add_argument("--ball", type=Path, help="open an existing ball")
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    files = ([args.ball] if args.ball else []) + args.files
    return BallApplication(args.build).run([sys.argv[0], *(str(path.resolve()) for path in files)])


if __name__ == "__main__":
    raise SystemExit(main())
