#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native GTK 4/libadwaita manager for a persistent 2ine ball."""

import argparse
import json
import os
import shutil
import signal
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime
from pathlib import Path

if __package__:
    from . import ball as ball_api
    from . import (
        gnome_applications,
        gnome_audio,
        gnome_ball_icons,
        gnome_ball_lifecycle,
        gnome_configuration,
        gnome_media_preferences,
        gnome_navigation,
        gnome_views,
    )
else:
    import gnome_applications
    import gnome_audio
    import gnome_ball_icons
    import gnome_ball_lifecycle
    import gnome_configuration
    import gnome_media_preferences
    import gnome_navigation
    import gnome_views

    import ball as ball_api

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

APP_ID = "io.github.bitwisecook.Twoine.Ball"
VERSION = (Path(__file__).resolve().parents[1] / "VERSION").read_text().strip()


def parse_arguments(text):
    return ball_api.native_request({"action": "parse-arguments", "text": text})


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
            build or Path(os.environ.get("TWOINE_BUILD", ball_api.ROOT / "build"))
        ).resolve()
        self.build = self.default_build
        ball_api.configure_library(self.build)
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
        self.missing_balls = set()
        self.app_preferences = {}
        self.log_font_provider = Gtk.CssProvider()
        Gtk.StyleContext.add_provider_for_display(
            self.get_display(), self.log_font_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        self.connect(
            "destroy",
            lambda *_: Gtk.StyleContext.remove_provider_for_display(
                self.get_display(), self.log_font_provider
            ),
        )
        self.drafts = {}
        self.settings_drafts = {}
        self.settings_file_paths = {}
        self.settings_file_drafts = {}
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
            / "2ine/ball-gnome.json"
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
            "sidebar_menu",
            "detail_menu",
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
            ("rename-ball", self.rename_ball),
            ("trash-ball", self.trash_ball),
            ("forget-ball", self.forget_ball),
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
            ("install", lambda: self.show_page("application-installation")),
            ("run", lambda: self.program_dialog("run")),
            ("restore-libraries", self.audit_dependencies),
            ("about", self.about),
            ("preferences", self.preferences_dialog),
            ("shortcuts", lambda: gnome_navigation.shortcuts(self)),
            (
                "help",
                lambda: self.open_path(
                    "https://github.com/bitwisecook/2ine/blob/main/docs/gnome-app.md"
                ),
            ),
            ("close", self.close),
            ("find", self.find),
            ("os2-files", lambda: self.show_page("components")),
            ("add-app", lambda: self.program_dialog("add-app")),
            ("launch-selected", lambda: self.selected_application_action("launch")),
            ("edit-selected", lambda: self.selected_application_action("edit")),
            ("audio", self.audio_dialog),
            ("copy", lambda: self.get_clipboard().set(str(self.ball)) if self.ball else None),
            ("reveal", lambda: self.open_path(self.ball) if self.ball else None),
        ):
            action = Gio.SimpleAction.new(name, None)
            action.connect("activate", lambda _a, _p, callback=callback: callback())
            self.add_action(action)
        for name, callback in (
            ("rename-ball-at", self.rename_ball),
            ("trash-ball-at", self.trash_ball),
            ("forget-ball-at", self.forget_ball),
            ("launch-application", lambda name: self.execute(["launch", name])),
            ("edit-application", lambda name: self.program_dialog("add-app", name=name)),
            ("remove-application", lambda name: self.execute(["remove-app", name])),
            ("copy-application", self.copy_application_path),
        ):
            action = Gio.SimpleAction.new(name, GLib.VariantType.new("s"))
            action.connect(
                "activate",
                lambda _a, value, callback=callback: callback(
                    Path(value.get_string())
                    if _a.get_name().endswith("-ball-at")
                    else value.get_string()
                ),
            )
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
            ("preferences", "<Control>comma"),
            ("shortcuts", "<Control>question"),
            ("help", "F1"),
            ("find", "<Control>f"),
            ("close", "<Control>q"),
            ("add-app", "<Control><Shift>a"),
            ("launch-selected", "<Control>Return"),
            ("edit-selected", "<Control><Shift>e"),
        ):
            app.set_accels_for_action("win." + name, [shortcut])
        for i, page in enumerate(("drives", "apps", "settings", "activity"), 1):
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
        self.narrow_breakpoint.add_setter(self.sidebar_menu, "visible", True)
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
        future = self.worker.submit(lambda: self.prepare_ui_result(work()))

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

    def prepare_ui_result(self, result):
        # Resolve host runtime paths on the worker along with the snapshot.
        if isinstance(result, dict) and "manifest" in result and "roots" in result:
            result["gnomeRuntimeBuild"] = str(self.effective_runtime(result))
        elif isinstance(result, tuple):
            result = tuple(self.prepare_ui_result(item) for item in result)
        return result

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
            "rename-ball",
            "trash-ball",
            "rename-ball-at",
            "trash-ball-at",
            "forget-ball-at",
            "import",
            "export",
            "export-images",
            "refresh",
            "runtime",
            "install",
            "run",
            "restore-libraries",
            "os2-files",
            "add-app",
            "launch-application",
            "edit-application",
            "remove-application",
            "copy-application",
            "undo",
            "redo",
            "add-drive",
            "create-disk",
            "create-floppy",
            "audio",
            "save",
        ):
            self.lookup_action(name).set_enabled(
                not busy
                and (
                    self.snapshot is not None
                    or name
                    in (
                        "new",
                        "open",
                        "import",
                        "refresh",
                        "rename-ball-at",
                        "trash-ball-at",
                        "forget-ball-at",
                    )
                )
            )
        self.lookup_action("forget-ball").set_enabled(not busy and self.ball in self.missing_balls)
        self.lookup_action("stop").set_enabled(bool(self.process) and not self.stop_sent)
        self.lookup_action("force-stop").set_enabled(bool(self.process) and self.stop_sent)
        self.lookup_action("undo").set_enabled(not busy and bool(self.undo_commands))
        self.lookup_action("redo").set_enabled(not busy and bool(self.redo_commands))
        self.update_application_actions()
        if hasattr(self, "stack"):
            for name in ("apps", "drives", "settings"):
                child = self.stack.get_child_by_name(name)
                if child:
                    child.set_sensitive(
                        not self.operation_busy if name in ("drives", "settings") else not busy
                    )
            if hasattr(self, "general_configuration"):
                self.general_configuration.set_sensitive(not busy)
            for page in getattr(self, "profile_pages", []):
                page.refresh_editability()
            if hasattr(self, "os2_files_configuration"):
                self.os2_files_configuration.refresh_editability(busy)
            for name in (
                "display_configuration",
                "environment_configuration",
                "printing_configuration",
            ):
                if hasattr(self, name):
                    getattr(self, name).set_sensitive(not busy)

    def report_error(self, error):
        self.activity_title = "Operation could not be completed"
        self.update_activity()
        self.banner.set_title(str(error))
        self.banner.set_revealed(True)
        self.toast(error)

    def startup(self, initial):
        try:
            source = self.preferences
            if not source.exists():
                source = source.with_name("balls-gnome.json")
            prefs = json.loads(source.read_text())
        except (OSError, ValueError):
            prefs = {}
        if not isinstance(prefs, dict):
            prefs = {}
        saved_paths = prefs.get("ballPaths", prefs.get("balls", []))
        if not isinstance(saved_paths, list):
            saved_paths = []
        if not isinstance(prefs.get("selected"), str):
            prefs.pop("selected", None)
        paths = {Path(p) for p in saved_paths if isinstance(p, str)} | {
            Path(p) for p in ball_api.native_request({"action": "discover"})
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
        return sorted(paths), target, prefs

    def started(self, result):
        self.external, target, self.app_preferences = result
        size = self.app_preferences.get("logFontSize", 12)
        self.app_preferences["logFontSize"] = min(24, max(10, size)) if type(size) is int else 12
        self.app_preferences["customLogSize"] = self.app_preferences.get("customLogSize") is True
        if not isinstance(self.app_preferences.get("ballFolder"), str):
            self.app_preferences.pop("ballFolder", None)
        self.refresh(target)

    def close_requested(self, *_):
        if self.busy:
            self.toast("Wait for the running application or operation to finish.")
            return True
        if self.settings_file_drafts:
            dialog = Adw.AlertDialog(
                heading="Discard unsaved OS/2 file changes?",
                body="Return to Config Files to save them before closing.",
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard Changes")
            dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
            dialog.set_close_response("cancel")

            def finished(_dialog, response):
                if response == "discard":
                    self.settings_file_drafts.clear()
                    self.close()

            dialog.connect("response", finished)
            dialog.present(self)
            return True
        if self.drafts:
            dialog = Adw.AlertDialog(
                heading="Save CONFIG.SYS changes?",
                body="A ball has unsaved edits.",
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
                                ball_api.native_request(
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
            **self.app_preferences,
            "ballPaths": [str(p) for p in self.external],
            "selected": str(self.ball) if self.ball else None,
        }

        def save():
            self.preferences.parent.mkdir(parents=True, exist_ok=True)
            ball_api.save(self.preferences, data)

        self.request_work(save, lambda _result: None, locked=False)

    @staticmethod
    def read_snapshot(path):
        snapshot = ball_api.native_request({"action": "snapshot", "ball": str(path)})
        gnome_ball_icons.decorate(path)
        return snapshot

    def effective_runtime(self, snapshot):
        try:
            return Path(
                ball_api.native_request(
                    {
                        "action": "effective-runtime",
                        "build": str(self.default_build),
                        "path": snapshot["manifest"].get("runtime", {}).get("build"),
                    }
                )
            )
        except ValueError:
            return self.default_build

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

        self.request_work(lambda: self.read_snapshot(path), opened)

    def refresh(self, select=None):
        if self.busy:
            return
        target = select if isinstance(select, Path) else self.ball

        def discover():
            paths = sorted(
                set(self.external)
                | {Path(p) for p in ball_api.native_request({"action": "discover"})}
            )
            return paths, {path for path in paths if not path.exists()}

        def discovered(result):
            paths, self.missing_balls = result
            self.populate(paths, target)

        self.request_work(discover, discovered)

    def populate(self, paths, target):
        clear(self.ball_list)
        chosen = None
        for path in paths:
            row = Adw.ActionRow(
                title=path.name.removesuffix(".2ineball"),
                subtitle=("Not Found · " if path in self.missing_balls else "")
                + gnome_views.display_path(path),
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
            menu = Gio.Menu()
            actions = (
                (("Remove Missing Ball from List", "forget-ball-at"),)
                if path in self.missing_balls
                else (
                    ("Rename Ball…", "rename-ball-at"),
                    ("Move Ball to Trash…", "trash-ball-at"),
                )
            )
            for label, action in actions:
                item = Gio.MenuItem.new(label, None)
                item.set_action_and_target_value("win." + action, GLib.Variant("s", str(path)))
                menu.append_item(item)
            more = Gtk.MenuButton(
                icon_name="view-more-symbolic", menu_model=menu, tooltip_text="Ball Actions"
            )
            more.update_property([Gtk.AccessibleProperty.LABEL], ["Actions for " + path.name])
            row.add_suffix(more)
            gesture = Gtk.GestureClick(button=3)
            gesture.connect("pressed", self.ball_context_menu, row, menu)
            row.add_controller(gesture)
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
            self.build = Path(snapshot.get("gnomeRuntimeBuild", str(self.build)))
            self.render()
            self.set_busy(False)
            self.persist_preferences()
            self.split.set_show_content(True)

        def unavailable(error):
            self.ball, self.snapshot = path, None
            clear(self.content)
            self.detail.set_title(path.name.removesuffix(".2ineball"))
            page = Adw.StatusPage(
                title="Ball Not Found" if path in self.missing_balls else "Could Not Open Ball",
                description=str(error),
                icon_name="folder-missing-symbolic",
            )
            if path in self.missing_balls:
                page.set_child(
                    Gtk.Button(
                        label="Remove Missing Ball from List",
                        action_name="win.forget-ball",
                        halign=Gtk.Align.CENTER,
                    )
                )
            self.content.append(page)
            self.split.set_show_content(True)
            self.set_busy(False)

        self.request_work(
            lambda: (self.read_snapshot(path), self.read_receipt(path)),
            loaded,
            on_error=unavailable,
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
            if name == "config":
                name = "settings"
                self.configuration_stack.set_visible_child_name("files")
            self.stack.set_visible_child_name(name)

    def render(self):
        previous = self.stack.get_visible_child_name() if hasattr(self, "stack") else "drives"
        configuration_tab = (
            self.configuration_stack.get_visible_child_name()
            if hasattr(self, "configuration_stack")
            else "overview"
        )
        if previous == "config":
            previous = "settings"
        clear(self.content)
        data = self.snapshot["manifest"]
        roots = {key: Path(value) for key, value in self.snapshot["roots"].items()}
        self.detail.set_title(self.ball.name.removesuffix(".2ineball"))
        self.stack = Adw.ViewStack(vexpand=True, hhomogeneous=False, vhomogeneous=False)
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
        self.applications_page = gnome_applications.ApplicationsPage(self)
        apps.append(self.applications_page)
        for name, title, icon, system in (
            ("components", "OS/2 System Components", "applications-system-symbolic", True),
            (
                "application-installation",
                "Application Installation",
                "system-software-install-symbolic",
                False,
            ),
        ):
            self.page(name, title, icon).append(gnome_navigation.InstallationPage(self, system))
        configuration = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, vexpand=True)
        self.stack.add_titled_with_icon(
            configuration, "settings", "Settings", "preferences-system-symbolic"
        )
        self.configuration_stack = gnome_navigation.SettingsNavigation()
        configuration.append(self.configuration_stack)
        settings = Adw.PreferencesPage()
        self.general_configuration = settings
        self.configuration_stack.add_titled(settings, "general", "General")
        group = Adw.PreferencesGroup(title="Runtime")
        settings.add(group)
        runtime = Adw.ActionRow(
            title="Runtime Build",
            subtitle=gnome_views.display_path(self.build),
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
            title="Diagnostics", description="Reset an override to inherit the runtime default."
        )
        settings.add(group)
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
        group = Adw.PreferencesGroup(
            title="Application State",
            description="Config Files contains startup and profile editors. Registry provides its own key tree. Shell metadata stays in this ball.",
        )
        row = Adw.ActionRow(title="Open State Folder", activatable=True)
        row.connect("activated", lambda *_: self.open_path(roots["C"] / "OS2"))
        row.add_suffix(Gtk.Image(icon_name="folder-open-symbolic"))
        group.add(row)
        settings.add(group)
        self.display_configuration = gnome_configuration.display_page(self)
        self.configuration_stack.add_titled(
            self.display_configuration, "display", "Graphics & Sound"
        )
        self.environment_configuration = gnome_configuration.EnvironmentPage(self)
        self.configuration_stack.add_titled(
            self.environment_configuration, "environment", "Environment"
        )
        self.printing_configuration = gnome_configuration.PrintingPage(self)
        self.configuration_stack.add_titled(self.printing_configuration, "printing", "Printing")
        config = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.profile_pages = []
        self.config_conflict = Adw.Banner(use_markup=False, revealed=False)
        config.append(self.config_conflict)
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
        self.os2_files_configuration = gnome_configuration.Os2FilesPage(self, config)
        self.configuration_stack.add_titled(self.os2_files_configuration, "files", "Config Files")
        registry = gnome_configuration.RegistryPage(self)
        self.profile_pages.append(registry)
        self.configuration_stack.add_titled(registry, "registry", "Registry")
        self.configuration_stack.set_visible_child_name(configuration_tab or "overview")
        activity = self.page("activity", "Logs", "document-open-recent-symbolic")
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
        self.log_view.add_css_class("twoine-activity-log")
        self.apply_log_font()
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

    def audit_dependencies(self, sources=None, after=None):
        if not self.ball or self.busy:
            return
        ball = str(self.ball)
        build = str(self.build)
        selected_sources = sources or gnome_media_preferences.preferred(
            self.snapshot, self.app_preferences
        )
        self.activity_title = "Checking installed libraries…"
        self.show_page("activity")
        self.update_activity()

        def work():
            fields = {}
            warnings = []
            validation = None
            if selected_sources:
                validation = ball_api.native_request(
                    {
                        "action": "validate-os2-media",
                        "ball": ball,
                        "build": build,
                        "sources": selected_sources,
                    }
                )
                if validation.get("complete"):
                    fields["sources"] = validation["sources"]
                else:
                    fields["resolve"] = False
                    warnings = [
                        {
                            "path": "OS/2 media",
                            "reason": "The selected source is unavailable or is not verified OS/2 media; choose another source.",
                        }
                    ]
            report = ball_api.native_request(
                {"action": "audit-dependencies", "ball": ball, "build": build, **fields}
            )
            snapshot = ball_api.native_request({"action": "snapshot", "ball": ball})
            return report, snapshot, warnings, self.read_log(Path(report["logPath"])), validation

        def finished(result):
            if str(self.ball) != ball:
                return
            report, snapshot, warnings, log_text, validation = result
            if validation and validation.get("complete"):
                self.remember_os2_media(validation)
            elif selected_sources == gnome_media_preferences.sources(
                self.app_preferences.get("lastOS2MediaSources")
            ):
                self.forget_os2_media()
            self.activity_title = "Dependency audit finished"
            notices = ["Skipped " + item["path"] + ": " + item["reason"] for item in warnings]
            self.install_summary = "\n\n".join([*notices, report["summary"]])
            self.last_log = Path(report["logPath"])
            self.log_text = log_text
            self.apply_snapshot(snapshot)
            self.show_page("activity")
            self.update_activity()
            if not sources and ball_api.needs_dependency_media(report):
                self.choose_dependency_media(ball, after=after)
            elif after:
                if ball_api.needs_dependency_media(report):
                    self.report_error(
                        "The selected media did not restore the required OS/2 components. Review the dependency audit."
                    )
                else:
                    after()

        self.request_work(work, finished)

    def choose_dependency_media(self, ball, after=None):
        picker = Gtk.FileDialog(
            title="Restore Missing Libraries from OS/2 ISO, Images or Archives",
            accept_label="Restore Libraries",
        )

        def chosen(picker, result):
            try:
                files = picker.open_multiple_finish(result)
                sources = [files.get_item(i).get_path() for i in range(files.get_n_items())]
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.report_error(error)
                return
            if sources and str(self.ball) == ball:
                self.audit_dependencies(sources, after=after)

        self.initial_os2_picker(picker)
        picker.open_multiple(self, None, chosen)

    def choose_os2_support(self, categories=None, force_picker=False):
        if not self.ball or self.busy or self.process:
            return
        self.show_page("components")
        remembered = gnome_media_preferences.preferred(self.snapshot, self.app_preferences)
        if remembered and not force_picker:
            self.present_os2_media(remembered, categories)
            return
        ball = str(self.ball)
        picker = Gtk.FileDialog(
            title="Choose OS/2 Installation ISO or Floppy Images",
            accept_label="Browse Media",
        )

        def chosen(picker, result):
            try:
                files = picker.open_multiple_finish(result)
                sources = [files.get_item(i).get_path() for i in range(files.get_n_items())]
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.report_error(error)
                return

            if sources and str(self.ball) == ball:
                self.present_os2_media(sources, categories)

        self.initial_os2_picker(picker)
        picker.open_multiple(self, None, chosen)

    def initial_os2_picker(self, picker, folder=False):
        path = gnome_media_preferences.picker_path(self.app_preferences)
        if path:
            if folder and path.is_file():
                path = path.parent
            if path.is_dir():
                picker.set_initial_folder(Gio.File.new_for_path(str(path)))
            else:
                picker.set_initial_file(Gio.File.new_for_path(str(path)))

    def remember_os2_media(self, validation):
        if gnome_media_preferences.remember(self.app_preferences, validation):
            self.persist_preferences()

    def forget_os2_media(self):
        if gnome_media_preferences.forget(self.app_preferences):
            self.persist_preferences()

    def present_os2_media(self, sources, categories=None):
        if __package__:
            from .gnome_media import OS2MediaDialog
        else:
            from gnome_media import OS2MediaDialog
        dialog = OS2MediaDialog(self, sources, categories)
        self.os2_component_dialog = dialog
        dialog.present(self)

    def show_component_media(self, category):
        if not self.ball or self.busy or self.process:
            return
        self.show_page("components")
        pending = getattr(self, "os2_component_dialog", None)
        if pending and pending.ball == self.ball and pending.stage == "options":
            pending.resume_components(category)
            return
        sources = gnome_media_preferences.preferred(self.snapshot, self.app_preferences)
        if sources:
            self.present_os2_media(sources, {category})
        else:
            self.choose_os2_support({category})

    def show_component_setting(self, token):
        if token not in ("graphics.opengl", "graphics", "printing"):
            return
        self.show_page("settings")
        self.configuration_stack.set_visible_child_name(
            "printing" if token == "printing" else "display"
        )
        target = (
            self.printing_configuration.queue
            if token == "printing"
            else self.display_configuration.opengl
            if token == "graphics.opengl"
            else self.display_configuration.rows["video"]
        )
        gnome_navigation.highlight_setting(target)

    def install_opengl_component(self):
        if not self.ball or self.busy or self.process:
            return
        if self.snapshot["manifest"].get("system", {}).get("opengl", "mesa") == "original":
            self.show_component_media("opengl")
        else:
            self.commit_request(
                {"action": "command", "arguments": ["config", "system.opengl", "original"]},
                completed=lambda _: self.show_component_media("opengl"),
            )

    def support_media_dialog(self):
        self.choose_os2_support()

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

    def form(self, title, fields, callback, accept="Save", *, deferred=False):
        dialog = gnome_navigation.InputDialog(self, title, fields, callback, accept, deferred)
        dialog.present(self)
        return dialog.entries

    def new_ball(self):
        def create(values, dialog):
            name = values["name"].strip()
            if not name or name in (".", "..") or any(c in name for c in "/\\\r\n"):
                raise ValueError("Choose a ball name without slashes.")
            path = Path(values["location"]).expanduser() / (
                name if name.endswith(".2ineball") else name + ".2ineball"
            )

            def initialize():
                if path.exists():
                    raise ValueError("A ball with that name already exists.")
                snapshot = ball_api.native_request(
                    {"action": "command", "ball": str(path), "arguments": ["init"]}
                )
                gnome_ball_icons.decorate(path)
                return snapshot

            def created(snapshot):
                self.external.append(path)
                self.ball, self.snapshot = path, snapshot
                dialog.close()
                self.refresh(path)

            dialog.run(initialize, created)

        return self.form(
            "New Ball",
            [
                ("name", "Ball Name", "", None),
                ("location", "Location", str(self.default_ball_folder()), "folder"),
            ],
            create,
            "Create",
            deferred=True,
        )

    def open_ball(self):
        self.choose("Open Ball", self.remember, folder=True)

    def ball_context_menu(self, _gesture, _count, x, y, row, menu):
        if self.busy or self.process:
            return
        popover = Gtk.PopoverMenu.new_from_model(menu)
        popover.set_parent(row)
        rectangle = Gdk.Rectangle()
        rectangle.x, rectangle.y, rectangle.width, rectangle.height = int(x), int(y), 1, 1
        popover.set_pointing_to(rectangle)
        popover.connect("closed", lambda widget: widget.unparent())
        popover.popup()

    def forget_ball(self, path=None):
        if self.busy or not (path or self.ball):
            return
        source = Path(path or self.ball)

        def verify_missing():
            if source.exists() or source.is_symlink():
                raise ValueError("This ball is available. Open it or move it to the Trash instead.")

        def forgotten(_result):
            gnome_ball_lifecycle.update_references(self, source)
            self.missing_balls.discard(source)
            self.persist_preferences()
            self.refresh()
            self.toast("Removed the missing ball from the list.")

        self.request_work(verify_missing, forgotten)

    def rename_ball(self, path=None):
        if self.busy or self.process or not (path or self.ball):
            return
        try:
            source = gnome_ball_lifecycle.package(path or self.ball)
            expected = gnome_ball_lifecycle.identity(source)
        except (OSError, ValueError) as error:
            self.report_error(error)
            return

        def rename(values, dialog):
            if self.busy or self.process:
                return
            destination = gnome_ball_lifecycle.rename_destination(source, values["name"])
            if destination == source:
                dialog.close()
                return

            def renamed(result):
                target = Path(result["path"])
                selected = self.ball is not None and self.ball.resolve() == source
                gnome_ball_lifecycle.update_references(self, source, target)
                if selected:
                    self.snapshot = result["snapshot"]
                dialog.close()
                self.persist_preferences()
                self.refresh(self.ball)
                self.toast("Renamed ball to " + target.name)

            def work():
                gnome_ball_lifecycle.package(source, expected)
                return ball_api.native_request(
                    {"action": "rename-ball", "ball": str(source), "text": values["name"]}
                )

            dialog.run(work, renamed)

        return self.form(
            "Rename Ball",
            [("name", "Ball Name", source.name[:-9], None)],
            rename,
            "Rename",
            deferred=True,
        )

    def trash_ball(self, path=None):
        if self.busy or self.process or not (path or self.ball):
            return
        try:
            source = gnome_ball_lifecycle.package(path or self.ball)
            expected = gnome_ball_lifecycle.identity(source)
        except (OSError, ValueError) as error:
            self.report_error(error)
            return
        dialog = Adw.AlertDialog(
            heading="Move “" + source.name + "” to the Trash?",
            body="This moves the ball package and everything stored inside it to the Trash. Unsaved drafts for this ball will be discarded.",
        )
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("trash", "Move to Trash")
        dialog.set_response_appearance("trash", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_default_response("cancel")
        dialog.set_close_response("cancel")
        dialog.connect(
            "response",
            lambda _dialog, choice: (
                self.remove_ball_package(source, expected) if choice == "trash" else None
            ),
        )
        dialog.present(self)

    def remove_ball_package(self, source, expected, permanent=False):
        if self.busy or self.process:
            return

        def work():
            path = gnome_ball_lifecycle.package(source, expected)
            if permanent:
                shutil.rmtree(path)
                return True
            try:
                if not Gio.File.new_for_path(str(path)).trash(None):
                    raise OSError("The ball could not be moved to the Trash.")
            except GLib.Error as error:
                if error.matches(Gio.io_error_quark(), Gio.IOErrorEnum.NOT_SUPPORTED):
                    return False
                raise ValueError(str(error)) from error
            return True

        def finished(removed):
            if not removed:
                dialog = Adw.AlertDialog(
                    heading="Permanently delete “" + source.name + "”?",
                    body="Trash is not supported at this location. Permanently deleting this ball and its contents cannot be undone.",
                )
                dialog.add_response("cancel", "Cancel")
                dialog.add_response("delete", "Delete Permanently")
                dialog.set_response_appearance("delete", Adw.ResponseAppearance.DESTRUCTIVE)
                dialog.set_default_response("cancel")
                dialog.set_close_response("cancel")
                dialog.connect(
                    "response",
                    lambda _dialog, choice: (
                        self.remove_ball_package(source, expected, permanent=True)
                        if choice == "delete"
                        else None
                    ),
                )
                dialog.present(self)
                return
            gnome_ball_lifecycle.update_references(self, source)
            self.persist_preferences()
            self.refresh(self.ball)
            self.toast("Deleted ball" if permanent else "Moved ball to Trash")

        self.request_work(work, finished)

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

                def import_source():
                    snapshot = ball_api.native_request(
                        {"action": "import-ball", "ball": path, "path": str(source)}
                    )
                    gnome_ball_icons.decorate(destination)
                    return snapshot

                self.request_work(import_source, imported)
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.report_error(error.message)

        dialog.save(self, None, saved)

    def export_ball(self, include_images=False):
        if self.busy or not self.ball:
            return
        if self.ball in self.drafts or any(
            key[0] == str(self.ball) for key in self.settings_file_drafts
        ):
            self.report_error("Save the OS/2 file drafts before exporting this ball.")
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
        if not self.ball or self.busy:
            return None
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
        conflict = (
            self.ball in self.drafts and self.drafts[self.ball][0] != self.snapshot["configSys"]
        )
        self.save_button.set_sensitive(self.ball in self.drafts and not conflict)
        self.config_conflict.set_title(
            "CONFIG.SYS changed on disk. Your unsaved draft is preserved. Reload before saving."
            if conflict
            else ""
        )
        self.config_conflict.set_revealed(conflict)

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
            lambda: ball_api.native_request(
                {"action": "save-config-sys", "ball": str(path), "expected": base, "text": text}
            ),
            saved,
            allow_running=True,
        )

    def reload_config(self):
        def reload():
            path = self.ball

            def loaded(snapshot):
                self.drafts.pop(path, None)
                self.snapshot = snapshot
                self.render()

            self.request_work(
                lambda: self.read_snapshot(path),
                loaded,
                allow_running=True,
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
        return ball_api.native_request(
            {"action": action, "ball": str(self.ball), "build": str(self.build), **fields}
        )

    def apply_snapshot(self, snapshot):
        self.snapshot = snapshot
        self.build = Path(snapshot.get("gnomeRuntimeBuild", str(self.build)))
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
            inverse = {"action": "restore-runtime", **({"build": old} if old is not None else {})}
        path = self.ball
        request = {"ball": str(path), "build": str(self.build), **message}
        # Omitting build has defined reset semantics for this one backend action.
        if message["action"] in ("runtime-build", "restore-runtime") and "build" not in message:
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
            lambda: ball_api.native_request(request),
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
                if (
                    message["action"] in ("runtime-build", "restore-runtime")
                    and "build" not in message
                ):
                    request.pop("build", None)
                ball_api.native_request(request)
            return self.read_snapshot(path)

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
        receiving_build = str(fields.pop("build", self.build))
        plan = self.api("prepare-launch", arguments=arguments, build=receiving_build, **fields)
        plan["gnomeReceivingBuild"] = receiving_build
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

        def prepared(result):
            plan, log = result
            if str(self.ball) != str(path):
                return
            audit = plan.get("dependencyAudit", {})
            if ball_api.needs_dependency_media(audit):
                self.activity_title = "Additional OS/2 components are required"
                self.log_text = audit.get("summary", "")
                self.update_activity()
                self.choose_dependency_media(
                    str(path), after=lambda: self.execute(arguments, **fields)
                )
                return
            self.launch(path, plan, log)

        return self.request_work(lambda: self.prepare_launch(arguments, **fields), prepared)

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
                if (
                    force
                    and getattr(self, "operation_supervised", False)
                    and hasattr(signal, "SIGUSR2")
                ):
                    self.process.send_signal(signal.SIGUSR2)
                elif force:
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
        plan.setdefault("gnomeReceivingBuild", str(self.build))
        launcher = Gio.SubprocessLauncher.new(Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_stdout_file_path(str(log))
        launcher.set_cwd(plan["cwd"])
        for key in plan["unsetEnvironment"]:
            launcher.unsetenv(key)
        for key, value in plan["environment"].items():
            launcher.setenv(key, value, True)
        try:
            self.process = launcher.spawnv(plan["command"])
            self.operation_supervised = plan.get("operationSupervised", False)
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
        self.config_polling = False
        GLib.timeout_add(1000, self.poll_config)
        return True

    def completed(self, process, result, context):
        path, plan, log = context
        process.wait_finish(result)
        code = process.get_exit_status() if process.get_if_exited() else -process.get_term_sig()
        self.process = None
        self.operation_supervised = False
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
                    ball_api.native_request(
                        {
                            "action": "record-install",
                            "ball": str(path),
                            "build": plan["gnomeReceivingBuild"],
                            "receipt": plan["receipt"],
                            "programHost": plan["programHost"],
                            "exitCode": code,
                        }
                    )
                except (OSError, ValueError) as error:
                    receipt_error = str(error)
            return (
                self.read_snapshot(path),
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
            audit = self.snapshot.get("dependencyAudit") or {}
            if plan["installer"] and ball_api.needs_dependency_media(audit):
                self.choose_dependency_media(str(path))
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

    def poll_config(self):
        if self.process and not self.config_polling:
            self.config_polling = True
            path = self.ball

            def updated(result):
                self.config_polling = False
                if path != self.ball:
                    return
                text, environment = result
                if "OS2PATH" in environment:
                    environment["PATH"] = environment.pop("OS2PATH")
                previous = self.snapshot.get("environmentDefaults", {})
                changed = self.snapshot["configSys"] != text or previous != environment
                self.snapshot["configSys"] = text
                self.snapshot["environmentDefaults"] = environment
                if changed:
                    self.environment_configuration.refresh()
                conflict = path in self.drafts and self.drafts[path][0] != text
                self.config_conflict.set_title(
                    "CONFIG.SYS changed on disk. Your unsaved draft is preserved. Reload before saving."
                    if conflict
                    else ""
                )
                self.config_conflict.set_revealed(conflict)
                if path not in self.drafts:
                    buffer = self.config_editor.get_buffer()
                    current = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
                    if current != text:
                        buffer.set_text(text)
                self.save_button.set_sensitive(path in self.drafts and not conflict)

            def failed(error):
                self.config_polling = False
                self.config_conflict.set_title("Could not refresh CONFIG.SYS: " + str(error))
                self.config_conflict.set_revealed(True)

            self.request_work(
                lambda: (
                    self.api("config-sys", ball=str(path)),
                    self.api("guest-environment", ball=str(path)),
                ),
                updated,
                locked=False,
                on_error=failed,
            )
        return self.process is not None

    def audio_dialog(self):
        if self.ball and not self.busy:
            return gnome_audio.AudioDialog(self)
        return None

    def default_ball_folder(self):
        folder = self.app_preferences.get("ballFolder")
        return Path(folder).expanduser() if folder else ball_api.ball_home()

    def preferences_dialog(self):
        dialog = gnome_navigation.PreferencesDialog(self)
        dialog.present(self)
        return dialog

    def apply_log_font(self):
        size = self.app_preferences.get("logFontSize", 12)
        css = (
            f"textview.twoine-activity-log {{ font-size: {size}pt; }}"
            if self.app_preferences.get("customLogSize")
            else ""
        )
        self.log_font_provider.load_from_string(css)

    def find(self):
        dialog = self.get_visible_dialog()
        if dialog:
            return gnome_navigation.focus_search(dialog)
        if hasattr(self, "stack"):
            return gnome_navigation.focus_search(self.stack.get_visible_child())
        return False

    def update_application_actions(self):
        page = getattr(self, "applications_page", None)
        selected = page.selected_name() if page else None
        for action in ("launch-selected", "edit-selected"):
            self.lookup_action(action).set_enabled(
                bool(selected) and self.snapshot is not None and not self.busy
            )

    def selected_application_action(self, action):
        page = getattr(self, "applications_page", None)
        name = page.selected_name() if page else None
        if name and not self.busy:
            if action == "launch":
                self.execute(["launch", name])
            else:
                self.program_dialog("add-app", name=name)

    def copy_application_path(self, name):
        application = (
            self.snapshot["manifest"].get("applications", {}).get(name) if self.snapshot else None
        )
        if application:
            self.get_clipboard().set(application["program"])

    def about(self):
        def show(catalogue):
            dialog = Adw.AboutDialog(
                application_name="2ine",
                application_icon=APP_ID,
                developer_name="Ryan C. Gordon and 2ine Contributors",
                developers=[
                    "Ryan C. Gordon (original 2ine) https://icculus.org/",
                    "2ine Contributors https://github.com/bitwisecook/2ine",
                ],
                copyright="Original 2ine © 2016–2022 Ryan C. Gordon",
                version=VERSION,
                website="https://github.com/bitwisecook/2ine",
                license_type=Gtk.License.CUSTOM,
                license="Original 2ine: zlib-style license. The Unicorn-linked runtime is distributed under GPLv2-compatible terms. Bundled components retain their own licenses; see Legal for sources and credits.",
                comments="Created by Ryan C. Gordon (icculus). This fork builds on his original OS/2 compatibility layer.",
            )
            dialog.add_link("All Project Sources, Versions and Downloads", catalogue["catalogue"])
            dialog.add_link("Original 2ine by Ryan C. Gordon", "https://github.com/icculus/2ine")
            dialog.add_credit_section(
                "Running Frontend",
                [
                    f"Python {sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro} https://www.python.org/",
                    f"GTK {Gtk.get_major_version()}.{Gtk.get_minor_version()}.{Gtk.get_micro_version()} https://www.gtk.org/",
                    f"libadwaita {Adw.get_major_version()}.{Adw.get_minor_version()}.{Adw.get_micro_version()} https://gitlab.gnome.org/GNOME/libadwaita",
                ],
            )
            for item in catalogue["projects"]:
                links = " · ".join(
                    f'<a href="{GLib.markup_escape_text(item[key])}">{label}</a>'
                    for key, label in (
                        ("homepage", "Project"),
                        ("source", "Source"),
                        ("download", "Download"),
                    )
                )
                text = (
                    GLib.markup_escape_text(item.get("role", "Component"))
                    + "\n"
                    + GLib.markup_escape_text(item["license"])
                    + "\n"
                    + links
                )
                dialog.add_legal_section(
                    item["name"] + " — " + item["version"],
                    "; ".join(item["authors"]),
                    Gtk.License.CUSTOM,
                    text,
                )
            dialog.present(self)
            self.about_dialog = dialog

        self.request_work(
            lambda: ball_api.native_request(
                {"action": "project-sources", "build": str(self.build)}
            ),
            show,
            locked=False,
        )

    def open_path(self, path):
        try:
            Gio.AppInfo.launch_default_for_uri(
                path.as_uri() if isinstance(path, Path) else str(path), None
            )
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
        self.set_resource_base_path("/io/github/bitwisecook/Twoine/Ball")

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
