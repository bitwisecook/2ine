#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native drive browsing, application editors and media controls for 2ine."""

import base64
from pathlib import Path

import gi

try:
    gi.require_version("Gtk", "4.0")
    gi.require_version("Gdk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Adw, Gdk, GLib, Gtk
except (ImportError, ValueError) as error:
    raise SystemExit("GTK 4, libadwaita and PyGObject are required: " + str(error)) from error


if __package__:
    from .gnome_files import FileList, display_path
else:
    from gnome_files import FileList, display_path


def action_button(label, callback, icon=None):
    widget = Gtk.Button(icon_name=icon, tooltip_text=label) if icon else Gtk.Button(label=label)
    widget.set_valign(Gtk.Align.CENTER)
    widget.update_property([Gtk.AccessibleProperty.LABEL], [label])
    widget.connect("clicked", lambda *_: callback())
    return widget


def empty(box):
    while child := box.get_first_child():
        box.remove(child)


def texture(result):
    if result.get("rgba"):
        width, height = result["width"], result["height"]
        return Gdk.MemoryTexture.new(
            width,
            height,
            Gdk.MemoryFormat.R8G8B8A8,
            GLib.Bytes.new(base64.b64decode(result["rgba"])),
            width * 4,
        )
    if result.get("image"):
        return Gdk.Texture.new_from_bytes(GLib.Bytes.new(base64.b64decode(result["image"])))
    return None


class FormDialog(Adw.Dialog):
    """A native dialog that retains inputs and displays errors while work completes."""

    def __init__(self, window, title, accept="Save", width=620, height=560):
        super().__init__(title=title, content_width=width, content_height=height)
        self.window = window
        self.ball = window.ball
        self.toolbar = Adw.ToolbarView()
        self.header = Adw.HeaderBar(show_start_title_buttons=False, show_end_title_buttons=False)
        self.toolbar.add_top_bar(self.header)
        self.header.pack_start(action_button("Cancel", self.close))
        self.accept = action_button(accept, self.submit)
        self.accept.add_css_class("suggested-action")
        self.header.pack_end(self.accept)
        self.progress = Gtk.Spinner(visible=False)
        self.header.pack_end(self.progress)
        self.error = Adw.Banner(use_markup=False, revealed=False)
        self.toolbar.add_top_bar(self.error)
        self.body = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        self.toolbar.set_content(self.body)
        self.set_child(self.toolbar)

    def submit(self):
        pass

    def api(self, action, **fields):
        return self.window.api(action, **{"ball": str(self.ball), **fields})

    def error_message(self, error):
        self.pending(False)
        self.error.set_title(str(error))
        self.error.set_revealed(True)

    def pending(self, active):
        self.set_can_close(not active)
        self.accept.set_sensitive(not active)
        self.body.set_sensitive(not active)
        self.progress.set_visible(active)
        self.progress.set_spinning(active)

    def run(self, work, completed, *, allow_running=False):
        if self.window.ball != self.ball:
            self.error_message("The selected ball changed. Close this dialog and open it again.")
            return
        self.error.set_revealed(False)

        def finished(result):
            self.pending(False)
            completed(result)

        if self.window.request_work(
            work, finished, on_error=self.error_message, allow_running=allow_running
        ):
            self.pending(True)

    def group(self, title, description=None):
        group = Adw.PreferencesGroup(title=title, description=description or "")
        return group

    def page(self, title=None):
        page = Adw.PreferencesPage(title=title or self.get_title())
        self.body.append(page)
        return page


class GuestBrowser(Gtk.Box):
    """Browse canonical guest paths, including opaque image members, through the C API."""

    def __init__(
        self,
        window,
        path="C:",
        *,
        directories_only=False,
        required_drive=None,
        choose=None,
        allow_import=False,
    ):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.window = window
        self.ball = window.ball
        self.directory = None
        self.selection = None
        self.serial = 0
        self.choose = choose
        self.directories_only = directories_only
        self.allow_import = allow_import
        self.required_drive = required_drive
        self.locations = {}
        self.letters = (
            [required_drive] if required_drive else sorted(window.snapshot["manifest"]["drives"])
        )
        bar = Adw.WrapBox(child_spacing=6, line_spacing=6)
        self.drives = Gtk.DropDown(
            model=Gtk.StringList.new([letter + ":" for letter in self.letters])
        )
        self.drives.update_property([Gtk.AccessibleProperty.LABEL], ["Drive"])
        self.drives.set_selected(
            self.letters.index(path[0].upper()) if path[0].upper() in self.letters else 0
        )
        self.drive_handler = self.drives.connect("notify::selected", self.drive_changed)
        bar.append(self.drives)
        self.up = action_button("Parent Folder", self.go_up, "go-up-symbolic")
        bar.append(self.up)
        self.address = Gtk.Entry(text=path, hexpand=True, placeholder_text=r"C:\OS2")
        self.address.update_property([Gtk.AccessibleProperty.LABEL], ["OS/2 folder"])
        self.address.connect("activate", lambda *_: self.load(self.address.get_text()))
        bar.append(self.address)
        bar.append(
            action_button(
                "Go to Folder", lambda: self.load(self.address.get_text()), "go-next-symbolic"
            )
        )
        self.append(bar)
        self.status = Gtk.Label(xalign=0, wrap=True, selectable=True, css_classes=["dim-label"])
        self.append(self.status)
        self.loading = Gtk.Spinner(visible=False, halign=Gtk.Align.CENTER)
        self.append(self.loading)
        self.relevant_only = Gtk.CheckButton(label="Relevant Files Only", active=True)
        self.relevant_only.set_visible(bool(choose) or directories_only)
        self.relevant_only.connect("toggled", lambda *_: self.files.refresh())
        self.append(self.relevant_only)
        self.files = FileList(
            selected=self.selected,
            activated=self.activate,
            accept=self.accept_entry,
            dropped=self.drop_into if allow_import else None,
        )
        self.append(self.files)
        self.empty_state = Adw.StatusPage(
            title="This Folder Is Empty", icon_name="folder-symbolic", visible=False
        )
        self.append(self.empty_state)
        controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        self.copy_button = action_button("Import Files…", self.import_files)
        self.copy_button.set_visible(allow_import)
        controls.append(self.copy_button)
        self.folder_button = action_button("Import Folder…", self.import_folder)
        self.folder_button.set_visible(allow_import)
        controls.append(self.folder_button)
        self.default_button = action_button("Use as Default Folder", self.set_default)
        self.default_button.set_visible(allow_import)
        controls.append(self.default_button)
        if choose:
            self.choose_button = action_button(
                "Choose Folder" if directories_only else "Choose Program", self.confirm
            )
            self.choose_button.add_css_class("suggested-action")
            self.choose_button.set_sensitive(directories_only)
            controls.append(self.choose_button)
        self.append(controls)
        if allow_import:
            drop = Gtk.DropTarget.new(Gdk.FileList, Gdk.DragAction.COPY)
            drop.connect("drop", self.dropped)
            self.add_controller(drop)
        self.load(path)

    def api(self, action, **fields):
        return self.window.api(action, **{"ball": str(self.ball), **fields})

    def load(self, path):
        if not path or self.ball != self.window.ball:
            return
        if (
            self.required_drive
            and len(path) > 1
            and path[1] == ":"
            and path[0].upper() != self.required_drive
        ):
            self.status.set_label("Choose a folder on drive " + self.required_drive + ":")
            return
        self.serial += 1
        serial = self.serial
        self.loading.set_visible(True)
        self.loading.set_spinning(True)
        self.status.set_label("Opening folder…")

        def loaded(result):
            if serial != self.serial or self.ball != self.window.ball:
                return
            self.loading.set_visible(False)
            self.loading.set_spinning(False)
            self.directory = result
            self.address.set_text(result["path"])
            self.locations[result["path"][0]] = result["path"]
            index = (
                self.letters.index(result["path"][0]) if result["path"][0] in self.letters else None
            )
            if index is not None and self.drives.get_selected() != index:
                self.drives.handler_block(self.drive_handler)
                self.drives.set_selected(index)
                self.drives.handler_unblock(self.drive_handler)
            self.window.browse_locations[str(self.ball)] = result["path"]
            self.up.set_sensitive(bool(result.get("parent")))
            self.status.set_label(
                f"{len(result['entries'])} items · Default: "
                + self.window.snapshot["workingDirectories"].get(result["path"][0], result["path"])
            )
            self.populate(result["entries"])
            mapping = self.window.snapshot["manifest"]["drives"].get(result["path"][0])
            writable = not isinstance(mapping, dict) or not (
                mapping.get("readonly") or mapping.get("device", "cdrom") == "cdrom"
            )
            self.copy_button.set_sensitive(writable and not self.window.process)
            self.folder_button.set_sensitive(writable and not self.window.process)

        def failed(error):
            if serial != self.serial:
                return
            self.loading.set_visible(False)
            self.loading.set_spinning(False)
            self.directory = None
            self.files.set_entries([])
            self.status.set_label(str(error))
            self.copy_button.set_sensitive(False)
            self.folder_button.set_sensitive(False)
            self.default_button.set_sensitive(False)
            self.empty_state.set_title("Drive Is Unavailable")
            self.empty_state.set_description(
                "Insert media or reconnect its folder using Drive Options."
            )
            self.empty_state.set_visible(True)

        self.window.request_work(
            lambda: self.api("browse", path=path), loaded, locked=False, on_error=failed
        )

    def accept_entry(self, entry):
        if entry["directory"]:
            return True
        if self.directories_only and self.relevant_only.get_active():
            return False
        return (
            not self.choose
            or not self.relevant_only.get_active()
            or Path(entry["name"]).suffix.lower() in (".exe", ".com", ".cmd", ".bat")
        )

    def populate(self, entries):
        self.default_button.set_sensitive(not self.window.busy)
        self.files.set_entries(entries)
        self.empty_state.set_visible(not any(self.accept_entry(entry) for entry in entries))
        self.empty_state.set_title("No Matching Files")
        self.empty_state.set_description("")

    def activate(self, entry):
        if entry["directory"]:
            self.load(entry["path"])
        elif self.choose and not self.directories_only:
            self.choose(entry["path"])
        else:
            self.window.program_dialog("run", program=entry["path"])

    def selected(self, entry):
        self.selection = entry
        if self.choose:
            self.choose_button.set_sensitive(
                bool(self.directory)
                if self.directories_only
                else bool(entry and not entry["directory"])
            )

    def confirm(self):
        path = (
            self.selection["path"]
            if self.selection and self.selection["directory"] == self.directories_only
            else self.directory["path"]
            if self.directory and self.directories_only
            else None
        )
        if path and self.choose:
            self.choose(path)

    def drive_changed(self, *_):
        letter = self.letters[self.drives.get_selected()]
        self.load(self.locations.get(letter, letter + ":\\"))

    def go_up(self):
        if self.directory and self.directory.get("parent"):
            self.load(self.directory["parent"])

    def import_files(self):
        if not self.directory:
            return
        destination = self.directory["path"]
        self.window.choose_many(
            "Import Files into " + destination, lambda paths: self.copy_files(paths, destination)
        )

    def import_folder(self):
        if self.directory:
            destination = self.directory["path"]
            self.window.choose_many(
                "Import Folders into " + destination,
                lambda paths: self.copy_files(paths, destination),
                folder=True,
            )

    def drop_into(self, files, destination):
        if self.window.busy or not self.copy_button.get_sensitive():
            return False
        paths = [file.get_path() for file in files.get_files()]
        if all(paths):
            self.copy_files(paths, destination)
            return True
        return False

    def dropped(self, _target, files, _x, _y):
        if not self.directory or not self.copy_button.get_sensitive() or self.window.busy:
            return False
        paths = [file.get_path() for file in files.get_files()]
        if all(paths):
            self.copy_files(paths, self.directory["path"])
            return True
        return False

    def copy_files(self, paths, destination):
        self.window.request_work(
            lambda: self.api("import-files", path=destination, sources=[str(p) for p in paths]),
            lambda result: (self.load(result["path"]), self.window.toast("Files imported")),
        )

    def set_default(self):
        if self.directory:
            self.window.commit_request(
                {"action": "set-working-directory", "path": self.directory["path"]},
                label="Change Default Folder",
            )


class GuestPicker(FormDialog):
    def __init__(
        self, window, path, callback, *, parent=None, directories_only=False, required_drive=None
    ):
        super().__init__(
            window,
            "Choose Working Folder" if directories_only else "Choose OS/2 Program",
            "Choose",
            width=680,
            height=620,
        )
        self.accept.set_visible(False)

        def chosen(value):
            callback(value)
            self.close()

        self.browser = GuestBrowser(
            window,
            path,
            directories_only=directories_only,
            required_drive=required_drive,
            choose=chosen,
        )
        self.browser.set_margin_start(18)
        self.browser.set_margin_end(18)
        self.browser.set_margin_bottom(18)
        self.body.append(self.browser)
        self.present(parent or window)


class MediaDialog(FormDialog):
    def __init__(self, window, letter=None, source=""):
        super().__init__(window, "Drive Options" if letter else "Add Drive", "Apply", height=480)
        self.original = letter
        self.mapping = window.snapshot["manifest"]["drives"].get(letter, {})
        page = self.page()
        group = self.group("Drive")
        page.add(group)
        letters = (
            [letter]
            if letter
            else [
                c
                for c in "ABDEFGHIJKLMNOPQRSTUVWXYZ"
                if c not in window.snapshot["manifest"]["drives"]
            ]
        )
        self.letters = letters
        self.letter = Adw.ComboRow(
            title="Drive Letter", model=Gtk.StringList.new([c + ":" for c in letters])
        )
        if "D" in letters:
            self.letter.set_selected(letters.index("D"))
        group.add(self.letter)
        self.kind = Adw.ComboRow(
            title="Source",
            model=Gtk.StringList.new(["Folder", "CD Image", "Floppy Image", "Disk Image"]),
        )
        kind = (
            self.mapping.get("device", "cdrom")
            if isinstance(self.mapping, dict) and self.mapping
            else "folder"
        )
        self.kind.set_selected({"folder": 0, "cdrom": 1, "floppy": 2, "disk": 3}[kind])
        group.add(self.kind)
        initial = source or (
            self.mapping if isinstance(self.mapping, str) else self.mapping.get("image", "")
        )
        self.source = Adw.EntryRow(title="Folder or Image", text=initial)
        self.source.add_suffix(
            action_button("Choose Source", self.choose_source, "folder-open-symbolic")
        )
        group.add(self.source)
        self.notice = Adw.ActionRow(
            title="Keep a Drive Allocated",
            subtitle="Leave the image path empty to create an ejected device. Changing mappings preserves the previous files.",
            subtitle_lines=0,
            use_markup=False,
        )
        group.add(self.notice)
        self.kind.connect(
            "notify::selected",
            lambda *_: self.source.set_title(
                "Host Folder" if self.kind.get_selected() == 0 else "Image File (optional)"
            ),
        )
        self.accept.set_sensitive(bool(letters))

    def choose_source(self):
        self.window.choose(
            "Choose Folder" if self.kind.get_selected() == 0 else "Choose Image",
            lambda path: self.source.set_text(str(path)),
            folder=self.kind.get_selected() == 0,
        )

    def submit(self):
        if not self.letters:
            return
        letter = self.letters[self.letter.get_selected()]
        source = self.source.get_text().strip()
        kind = ("folder", "cdrom", "floppy", "disk")[self.kind.get_selected()]
        if kind == "folder" and not source:
            self.error_message("Choose a host folder.")
            return
        inverse = self.window.drive_inverse(letter)

        def work():
            if kind == "folder":
                return self.api("command", arguments=["map", letter, source])
            if not source:
                return self.api("command", arguments=["device", letter, kind])
            if letter not in self.window.snapshot["manifest"]["drives"]:
                self.api("command", arguments=["device", letter, kind])
            return self.api("command", arguments=["map-image", letter, source])

        def finished(snapshot):
            self.window.selected_drive = letter
            self.window.apply_snapshot(snapshot)
            self.window.add_history(
                inverse,
                {
                    "action": "command",
                    "arguments": ["restore-drive", letter, snapshot["driveRecords"][letter]],
                },
                "Change Drive",
            )
            self.close()

        self.run(work, finished, allow_running=True)


class CreateImageDialog(FormDialog):
    def __init__(self, window, *, floppy=False, letter=None):
        super().__init__(
            window, "Create Floppy Image" if floppy else "Create Disk Image", "Create", height=480
        )
        self.floppy = floppy
        page = self.page()
        group = self.group(
            "New Image",
            "The image is created and attached directly. Existing files are never replaced.",
        )
        page.add(group)
        self.letters = (
            [letter]
            if letter
            else [
                c
                for c in "ABDEFGHIJKLMNOPQRSTUVWXYZ"
                if c not in window.snapshot["manifest"]["drives"]
            ]
        )
        self.letter = Adw.ComboRow(
            title="Drive Letter", model=Gtk.StringList.new([c + ":" for c in self.letters])
        )
        if "A" in self.letters and floppy:
            self.letter.set_selected(self.letters.index("A"))
        group.add(self.letter)
        self.path = Adw.EntryRow(title="New Image File")
        self.path.add_suffix(
            action_button(
                "Choose Image Destination",
                lambda: window.choose_save(
                    "Create Image",
                    "Floppy.img" if floppy else "Disk.img",
                    lambda path: self.path.set_text(str(path)),
                ),
                "document-save-symbolic",
            )
        )
        group.add(self.path)
        if floppy:
            self.capacities = [360, 720, 1200, 1440, 2880]
            self.capacity = Adw.ComboRow(
                title="Capacity",
                model=Gtk.StringList.new([str(n) + " KiB" for n in self.capacities]),
            )
            self.capacity.set_selected(3)
            group.add(self.capacity)
        else:
            self.filesystem = Adw.ComboRow(
                title="Filesystem", model=Gtk.StringList.new(["HPFS", "FAT32", "exFAT"])
            )
            group.add(self.filesystem)
            self.capacity = Adw.SpinRow(
                title="Capacity (MiB)",
                adjustment=Gtk.Adjustment(
                    lower=8, upper=65536, value=64, step_increment=8, page_increment=64
                ),
                digits=0,
            )
            self.filesystem.connect(
                "notify::selected",
                lambda *_: self.capacity.get_adjustment().set_lower(
                    64 if self.filesystem.get_selected() == 1 else 8
                ),
            )
            group.add(self.capacity)
        self.accept.set_sensitive(bool(self.letters))

    def submit(self):
        if not self.path.get_text().strip():
            self.error_message("Choose a destination for the new image.")
            return
        letter = self.letters[self.letter.get_selected()]
        arguments = (
            [
                "create-floppy",
                letter,
                self.path.get_text(),
                str(self.capacities[self.capacity.get_selected()]),
            ]
            if self.floppy
            else [
                "create-disk",
                letter,
                self.path.get_text(),
                ("hpfs", "fat32", "exfat")[self.filesystem.get_selected()],
                str(int(self.capacity.get_value())),
            ]
        )
        inverse = self.window.drive_inverse(letter)

        def finished(snapshot):
            self.window.selected_drive = letter
            self.window.apply_snapshot(snapshot)
            self.window.add_history(
                inverse,
                {
                    "action": "command",
                    "arguments": ["restore-drive", letter, snapshot["driveRecords"][letter]],
                },
                "Create Image Drive",
            )
            self.close()

        self.run(lambda: self.api("command", arguments=arguments), finished, allow_running=True)


class DriveSettingsDialog(FormDialog):
    def __init__(self, window, letter):
        super().__init__(window, letter + ": Device Settings", height=400)
        self.letter = letter
        mapping = window.snapshot["manifest"]["drives"][letter]
        self.original = mapping
        page = self.page()
        group = self.group(
            "Media Protection",
            "Writes update the attached image. Keep a separate backup of original media.",
        )
        page.add(group)
        self.readonly = Adw.SwitchRow(
            title="Read Only",
            subtitle="Prevent changes to image contents",
            active=mapping.get("device", "cdrom") == "cdrom" or mapping.get("readonly", False),
        )
        self.readonly.set_sensitive(mapping.get("device", "cdrom") != "cdrom")
        group.add(self.readonly)
        self.locked = Adw.SwitchRow(
            title="Lock Device",
            subtitle="Prevent ejection, replacement and protection changes",
            active=mapping.get("locked", False),
        )
        self.locked.set_sensitive(bool(mapping.get("image")) or bool(mapping.get("locked")))
        group.add(self.locked)

    def submit(self):
        letter = self.letter
        readonly, locked = self.readonly.get_active(), self.locked.get_active()
        inverse = self.window.drive_inverse(letter)

        def work():
            snapshot = self.api("snapshot")
            old = snapshot["manifest"]["drives"][letter]
            if old.get("locked") and not locked:
                snapshot = self.api("command", arguments=["device-lock", letter, "off"])
            if old.get("device", "cdrom") != "cdrom" and bool(old.get("readonly")) != readonly:
                snapshot = self.api(
                    "command", arguments=["media-readonly", letter, "on" if readonly else "off"]
                )
            if bool(old.get("locked")) != locked:
                snapshot = self.api(
                    "command", arguments=["device-lock", letter, "on" if locked else "off"]
                )
            return snapshot

        def finished(snapshot):
            self.window.apply_snapshot(snapshot)
            self.window.add_history(
                inverse,
                {
                    "action": "command",
                    "arguments": ["restore-drive", letter, snapshot["driveRecords"][letter]],
                },
                "Change Device Protection",
            )
            self.close()

        self.run(work, finished, allow_running=True)


def installer_candidates(api, letter):
    pending, found, scanned = [(letter + ":\\", 0)], [], 0
    stems = ["install", "setup", "inst", "os2inst", "autorun"]
    while pending and scanned < 96:
        path, depth = pending.pop(0)
        scanned += 1
        for entry in api("browse", path=path)["entries"]:
            name = entry["name"].casefold()
            if entry["directory"] and depth < 2:
                if depth == 0 or name in ("english", "os2") or "install" in name or "setup" in name:
                    pending.append((entry["path"], depth + 1))
            elif not entry["directory"] and "." in name:
                stem, extension = name.rsplit(".", 1)
                if stem in stems and extension in ("exe", "cmd", "bat", "com"):
                    rank = (
                        stems.index(stem) * 10
                        + depth * 3
                        - (2 if "\\ENGLISH\\" in entry["path"].upper() else 0)
                    )
                    found.append((rank, entry["path"]))
    return [path for _rank, path in sorted(found)[:32]]


class ProgramDialog(FormDialog):
    def __init__(self, window, action, name=None, program=""):
        super().__init__(
            window,
            "Edit Application"
            if name
            else {
                "add-app": "Add Application",
                "run": "Run Program",
                "install": "Install Software",
            }[action],
            "Save" if action == "add-app" else "Install" if action == "install" else "Run",
            width=700,
            height=650,
        )
        self.action, self.original_name = action, name
        self.suggested_name = ""
        self.ball = window.ball
        self.initial = window.snapshot["manifest"]["applications"].get(name, {})
        self.icon_source = None
        self.preview_serial = 0
        self.directory_rows = {}
        self.environment_rows = {}
        self.defaults = {}
        self.stack = Adw.ViewStack(vexpand=True)
        switcher = Adw.InlineViewSwitcher(
            stack=self.stack,
            display_mode=Adw.InlineViewSwitcherDisplayMode.BOTH,
            halign=Gtk.Align.CENTER,
        )
        self.body.append(switcher)
        breakpoint = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 550sp"))
        breakpoint.add_setter(switcher, "display-mode", Adw.InlineViewSwitcherDisplayMode.ICONS)
        self.set_size_request(360, 360)
        self.add_breakpoint(breakpoint)
        self.body.append(self.stack)
        general = self.options_page("general", "General", "application-x-executable-symbolic")
        group = self.group("Application" if action == "add-app" else "Program")
        general.add(group)
        self.name = Adw.EntryRow(title="Application Name", text=name or "")
        if action == "add-app":
            group.add(self.name)
        self.program = Adw.EntryRow(
            title="OS/2 Executable or Command File", text=self.initial.get("program", program)
        )
        self.program.add_suffix(
            action_button(
                "Browse Drives",
                lambda: GuestPicker(
                    window,
                    self.program.get_text() or window.snapshot["currentDrive"] + ":",
                    self.program.set_text,
                    parent=self,
                ),
                "folder-open-symbolic",
            )
        )
        self.program.connect("notify::text", self.schedule_preview)
        group.add(self.program)
        self.arguments = Adw.EntryRow(title="Arguments")
        group.add(self.arguments)
        self.cwd = Adw.EntryRow(
            title="Initial Working Folder (optional)", text=self.initial.get("cwd", "")
        )
        self.cwd.add_suffix(
            action_button(
                "Choose Initial Working Folder",
                lambda: GuestPicker(
                    window,
                    self.cwd.get_text() or "C:",
                    self.cwd.set_text,
                    directories_only=True,
                    parent=self,
                ),
                "folder-open-symbolic",
            )
        )
        group.add(self.cwd)
        group.set_description(
            "Quotes group arguments; backslashes stay literal. Leave the initial folder empty to inherit per-drive defaults. Installers start in their own folder."
        )
        if action == "install":
            media = self.group(
                "Installation Media",
                "Attach an original CD or floppy image, or browse any mapped drive. CMD/BAT installers require the original OS/2 CMD.EXE.",
            )
            general.add(media)
            self.media_kind = Adw.ComboRow(
                title="Device", model=Gtk.StringList.new(["CD", "Floppy"])
            )
            media.add(self.media_kind)
            self.media_letter = Adw.ComboRow(title="Drive Letter")
            media.add(self.media_letter)
            self.media_kind.connect("notify::selected", self.media_choices)
            self.media_choices()
            row = Adw.ActionRow(
                title="Attach Installation Image",
                subtitle="Browse and suggest installers from the attached medium",
                activatable=True,
            )
            row.add_suffix(
                action_button("Attach Image", self.attach_installer, "media-optical-symbolic")
            )
            row.connect("activated", lambda *_: self.attach_installer())
            media.add(row)
            self.suggestions = Adw.ComboRow(title="Suggested Installers", visible=False)
            self.suggestions.connect("notify::selected", self.suggestion_selected)
            media.add(self.suggestions)
            self.candidates = []
        if action == "add-app":
            icon_group = self.group("Application Icon")
            general.add(icon_group)
            icon_row = Adw.ActionRow(
                title="Executable Icon", subtitle="Uses embedded OS/2 artwork when available"
            )
            self.icon = Gtk.Image(icon_name="application-x-executable-symbolic", pixel_size=48)
            self.icon.update_property([Gtk.AccessibleProperty.LABEL], ["Application icon preview"])
            icon_row.add_prefix(self.icon)
            icon_row.add_suffix(action_button("Choose Icon…", self.choose_icon))
            icon_group.add(icon_row)
            reset = Adw.ActionRow(title="Use Executable Icon", activatable=True)
            reset.add_suffix(Gtk.Image(icon_name="edit-undo-symbolic"))
            reset.connect("activated", lambda *_: self.use_executable_icon())
            icon_group.add(reset)
        folders = self.options_page("folders", "Working Folders", "folder-symbolic")
        group = self.group(
            "Per-Drive Working Folders",
            "Leave an override off to inherit that drive's ball default.",
        )
        folders.add(group)
        for letter in sorted(window.snapshot["manifest"]["drives"]):
            inherited = window.snapshot["workingDirectories"].get(letter, letter + ":\\")
            existing = self.initial.get("workingDirectories", {})
            row = Adw.ExpanderRow(
                title=letter + ":",
                subtitle="Ball default: " + inherited,
                show_enable_switch=True,
                enable_expansion=letter in existing,
                expanded=letter in existing,
            )
            entry = Adw.EntryRow(title="Working Folder", text=existing.get(letter, inherited))
            entry.add_suffix(
                action_button(
                    "Choose " + letter + ": Folder",
                    lambda entry=entry, letter=letter: GuestPicker(
                        window,
                        entry.get_text(),
                        entry.set_text,
                        directories_only=True,
                        required_drive=letter,
                        parent=self,
                    ),
                    "folder-open-symbolic",
                )
            )
            row.add_row(entry)
            group.add(row)
            self.directory_rows[letter] = (row, entry)
        environment = self.options_page("environment", "Environment", "preferences-system-symbolic")
        self.environment_group = self.group(
            "Guest Environment Overrides",
            "Off means inherit. An empty override is an explicit empty value. %NAME% expands the inherited guest value; PATH=%PATH%;C:\\DOOM appends a folder.",
        )
        environment.add(self.environment_group)
        self.environment_add = self.group("Add an Override")
        environment.add(self.environment_add)
        self.environment_name = Adw.EntryRow(title="Variable Name", show_apply_button=True)
        self.environment_name.connect("apply", lambda *_: self.add_variable())
        self.environment_add.add(self.environment_name)
        self.preview_button = action_button(
            "Preview Effective Environment", self.preview_environment
        )
        self.environment_add.set_header_suffix(self.preview_button)
        self.body.set_sensitive(False)

        def initial_values():
            return window.api(
                "format-arguments", arguments=self.initial.get("arguments", [])
            ), self.api("guest-environment")

        def initialized(result):
            arguments, defaults = result
            self.arguments.set_text(arguments)
            self.defaults = defaults
            for key in sorted(set(defaults) | set(self.initial.get("environment", {}))):
                self.add_variable(
                    key,
                    self.initial.get("environment", {}).get(key),
                    key in self.initial.get("environment", {}),
                )
            self.body.set_sensitive(True)
            self.refresh_preview()

        window.request_work(initial_values, initialized, locked=False, on_error=self.error_message)

    def options_page(self, name, title, icon):
        page = Adw.PreferencesPage()
        self.stack.add_titled_with_icon(page, name, title, icon)
        return page

    def add_variable(self, key=None, value=None, enabled=True):
        key = key or self.environment_name.get_text().strip()
        if not key:
            return
        if key in self.environment_rows:
            row, entry = self.environment_rows[key]
            row.set_enable_expansion(enabled)
            row.set_expanded(enabled)
            if value is not None:
                entry.set_text(value)
            return
        inherited = self.defaults.get(key, "")
        row = Adw.ExpanderRow(
            title=key,
            subtitle="Inherited: " + inherited,
            show_enable_switch=True,
            enable_expansion=enabled,
            expanded=enabled,
        )
        row.set_subtitle_lines(1)
        entry = Adw.EntryRow(title="Override", text=value if value is not None else inherited)
        row.add_row(entry)
        self.environment_group.add(row)
        self.environment_rows[key] = (row, entry)
        self.environment_name.set_text("")

    def overrides(self):
        directories = {
            key: entry.get_text()
            for key, (row, entry) in self.directory_rows.items()
            if row.get_enable_expansion()
        }
        environment = {
            key: entry.get_text()
            for key, (row, entry) in self.environment_rows.items()
            if row.get_enable_expansion()
        }
        return directories, environment

    def preview_environment(self):
        environment = self.overrides()[1]

        def presented(values):
            dialog = FormDialog(self.window, "Effective Guest Environment", "Close", height=560)
            dialog.accept.connect("clicked", lambda *_: dialog.close())
            group = dialog.group("Expanded Values")
            dialog.page().add(group)
            for key, value in sorted(values.items()):
                row = Adw.ActionRow(title=key, subtitle=value, subtitle_lines=0, use_markup=False)
                group.add(row)
            dialog.present(self)

        self.window.request_work(
            lambda: self.api("guest-environment", environment=environment),
            presented,
            on_error=self.error_message,
        )

    def schedule_preview(self, *_):
        self.preview_serial += 1
        serial = self.preview_serial
        GLib.timeout_add(
            250,
            lambda: self.refresh_preview() if serial == self.preview_serial else GLib.SOURCE_REMOVE,
        )

    def refresh_preview(self):
        if self.action != "add-app" or not hasattr(self, "icon") or not self.program.get_text():
            return GLib.SOURCE_REMOVE
        serial, program, source = self.preview_serial, self.program.get_text(), self.icon_source
        name = (
            self.original_name
            if source is None and program == self.initial.get("program")
            else None
        )

        def work():
            if source:
                return self.api("image-preview", path=source)
            return self.api(
                "application-icon", **({"name": name} if name else {"program": program})
            )

        def ready(result):
            if serial != self.preview_serial:
                return
            suggested = result.get("displayName")
            if self.action == "add-app" and not self.original_name and not source and suggested:
                if self.name.get_text() in ("", self.suggested_name):
                    self.name.set_text(suggested)
                self.suggested_name = suggested
            try:
                paintable = texture(result)
                if paintable:
                    self.icon.set_from_paintable(paintable)
                else:
                    self.icon.set_from_icon_name("application-x-executable-symbolic")
            except GLib.Error as error:
                self.error_message(error.message)

        self.window.request_work(work, ready, locked=False, on_error=lambda _error: None)
        return GLib.SOURCE_REMOVE

    def choose_icon(self):
        def selected(path):
            self.icon_source = str(path)
            self.preview_serial += 1
            self.refresh_preview()

        self.window.choose("Choose Application Icon", selected)

    def use_executable_icon(self):
        self.icon_source = ""
        self.preview_serial += 1
        self.refresh_preview()

    def media_choices(self, *_):
        kind = ("cdrom", "floppy")[self.media_kind.get_selected()]
        drives = self.window.snapshot["manifest"]["drives"]
        self.media_letters = [
            letter
            for letter, value in sorted(drives.items())
            if isinstance(value, dict) and value.get("device", "cdrom") == kind
        ]
        free = next(
            (
                letter
                for letter in (
                    "ABDEFGHIJKLMNOPQRSTUVWXYZ" if kind == "floppy" else "DEFGHIJKLMNOPQRSTUVWXYZAB"
                )
                if letter not in drives
            ),
            None,
        )
        if free:
            self.media_letters.append(free)
        self.media_letter.set_model(
            Gtk.StringList.new([letter + ":" for letter in self.media_letters])
        )

    def attach_installer(self):
        if not self.media_letters:
            self.error_message("No drive letter is available.")
            return
        letter = self.media_letters[self.media_letter.get_selected()]
        kind = ("cdrom", "floppy")[self.media_kind.get_selected()]

        def chosen(path):
            def work():
                if letter not in self.window.snapshot["manifest"]["drives"]:
                    self.api("command", arguments=["device", letter, kind])
                snapshot = self.api("command", arguments=["map-image", letter, str(path)])
                return snapshot, installer_candidates(self.api, letter)

            def attached(result):
                snapshot, self.candidates = result
                self.window.apply_snapshot(snapshot)
                self.suggestions.set_model(Gtk.StringList.new(self.candidates))
                self.suggestions.set_visible(bool(self.candidates))
                if self.candidates:
                    self.program.set_text(self.candidates[0])
                else:
                    self.error.set_title(
                        "No suggested installer found. Browse this drive to choose a program."
                    )
                    self.error.set_revealed(True)

            self.run(work, attached)

        self.window.choose("Choose Installation Image", chosen)

    def suggestion_selected(self, *_):
        if self.candidates and self.suggestions.get_selected() < len(self.candidates):
            self.program.set_text(self.candidates[self.suggestions.get_selected()])

    def submit(self):
        name, program, arguments = (
            self.name.get_text().strip(),
            self.program.get_text().strip(),
            self.arguments.get_text(),
        )
        if not program or (self.action == "add-app" and not name):
            self.error_message(
                "Enter an executable and an application name when saving a launcher."
            )
            return
        if (
            self.action == "add-app"
            and name != self.original_name
            and name in self.window.snapshot["manifest"]["applications"]
        ):
            self.error_message("An application with this name already exists.")
            return
        directories, environment = self.overrides()
        cwd, icon_source, original_name = self.cwd.get_text(), self.icon_source, self.original_name
        old_record = self.window.snapshot["applicationRecords"].get(original_name)

        def work():
            argv = self.api("parse-arguments", text=arguments)
            if self.action != "add-app":
                return self.window.prepare_launch(
                    [self.action, *(["--cwd", cwd] if cwd else []), program, *argv],
                    workingDirectories=directories,
                    environment=environment,
                )
            request = {
                "name": name,
                "program": program,
                "arguments": argv,
                "workingDirectories": directories,
                "environment": environment,
                "cwd": cwd,
            }
            if original_name:
                request["originalName"] = original_name
            if icon_source is not None:
                request["iconSource"] = icon_source
            return self.api("save-application", **request)

        def finished(result):
            if self.action != "add-app":
                if self.window.launch(self.ball, *result):
                    self.close()
                else:
                    self.error_message("The program could not be started. Check Logs for details.")
                return
            undo = [{"action": "command", "arguments": ["remove-app", name]}]
            if old_record:
                undo.append(
                    {"action": "command", "arguments": ["restore-app", original_name, old_record]}
                )
            redo = []
            if original_name and original_name != name:
                redo.append({"action": "command", "arguments": ["remove-app", original_name]})
            redo.append(
                {
                    "action": "command",
                    "arguments": ["restore-app", name, result["applicationRecords"][name]],
                }
            )
            self.window.apply_snapshot(result)
            self.window.add_history(
                undo, redo, "Edit Application" if original_name else "Add Application"
            )
            self.close()

        self.run(work, finished)


class DrivesPage(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.window = window
        for edge in ("top", "bottom", "start", "end"):
            getattr(self, "set_margin_" + edge)(18)
        actions = Adw.WrapBox(child_spacing=6, line_spacing=6)
        actions.append(action_button("Add Drive…", lambda: window.map_dialog()))
        actions.append(action_button("Create Disk…", lambda: window.create_image()))
        actions.append(action_button("Create Floppy…", lambda: window.create_image(floppy=True)))
        actions.set_sensitive(not window.busy)
        self.append(actions)
        self.summary = Adw.ActionRow(use_markup=False, subtitle_lines=2)
        group = Adw.PreferencesGroup()
        group.add(self.summary)
        self.menu = Gtk.MenuButton(
            icon_name="view-more-symbolic", tooltip_text="Drive Options", valign=Gtk.Align.CENTER
        )
        self.menu.update_property([Gtk.AccessibleProperty.LABEL], ["Drive options"])
        self.summary.add_suffix(self.menu)
        self.append(group)
        path = window.browse_locations.get(
            str(window.ball), (window.selected_drive or window.snapshot["currentDrive"]) + ":"
        )
        if path[0].upper() not in window.snapshot["manifest"]["drives"]:
            path = "C:"
        self.browser = GuestBrowser(window, path, allow_import=True)
        self.browser.drives.connect("notify::selected", lambda *_: self.update_summary())
        self.append(self.browser)
        self.update_summary()

    def update_summary(self):
        letter = self.browser.letters[self.browser.drives.get_selected()]
        self.window.selected_drive = letter
        mapping = self.window.snapshot["manifest"]["drives"][letter]
        unavailable = self.window.snapshot.get("unavailableDrives", {}).get(letter)
        if isinstance(mapping, str):
            source = unavailable or self.window.snapshot["roots"].get(letter, mapping)
            description = "Folder" if not unavailable else "Disconnected Folder"
            icon = "folder-symbolic"
        else:
            kind = mapping.get("device", "cdrom")
            source = self.window.snapshot.get("imageSources", {}).get(
                letter, mapping.get("image", "No medium attached")
            )
            description = {"cdrom": "CD", "floppy": "Floppy", "disk": "Disk"}[kind]
            description += (
                " · Ejected"
                if unavailable or not mapping.get("image")
                else " · Read Only"
                if kind == "cdrom" or mapping.get("readonly")
                else " · Writable"
            )
            if mapping.get("locked"):
                description += " · Locked"
            icon = {
                "cdrom": "media-optical-symbolic",
                "floppy": "media-floppy-symbolic",
                "disk": "drive-harddisk-symbolic",
            }[kind]
        self.summary.set_title(letter + ": · " + description)
        self.summary.set_subtitle(display_path(source))
        if hasattr(self, "drive_icon"):
            self.summary.remove(self.drive_icon)
        self.drive_icon = Gtk.Image(icon_name=icon)
        self.summary.add_prefix(self.drive_icon)
        popover = Gtk.Popover()
        items = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL,
            spacing=3,
            margin_top=6,
            margin_bottom=6,
            margin_start=6,
            margin_end=6,
        )

        def item(title, callback, enabled=True):
            widget = action_button(title, lambda: (popover.popdown(), callback()))
            widget.add_css_class("flat")
            widget.set_sensitive(enabled)
            items.append(widget)

        if isinstance(mapping, str):
            item(
                "Change Mountpoint…",
                lambda: self.window.map_dialog(letter, mapping),
                letter != "C" and not self.window.busy,
            )
            if not unavailable:
                item(
                    "Open Host Folder",
                    lambda: self.window.open_path(Path(self.window.snapshot["roots"][letter])),
                )
        else:
            locked = mapping.get("locked", False)
            item(
                "Insert or Replace Image…",
                lambda: self.window.attach_image(letter),
                not locked and not self.window.operation_busy,
            )
            item(
                "Eject Medium",
                lambda: self.window.media_command(["eject", letter]),
                bool(mapping.get("image")) and not locked and not self.window.operation_busy,
            )
            item(
                "Device Settings…",
                lambda: self.window.device_settings(letter),
                not self.window.operation_busy,
            )
        item(
            "Remove Drive",
            lambda: self.window.media_command(["unmap", letter]),
            letter != "C"
            and not self.window.operation_busy
            and not (isinstance(mapping, dict) and mapping.get("locked")),
        )
        popover.set_child(items)
        self.menu.set_popover(popover)


class ExportDialog(FormDialog):
    def __init__(self, window, include_images=False):
        super().__init__(window, "Export Ball", "Choose Destination…", height=380)
        page = self.page()
        group = self.group(
            "Portable ZIP Archive",
            "The archive keeps installed applications, settings, profiles and icons. An imported ball uses the receiving computer's runtime.",
        )
        page.add(group)
        self.include_images = Adw.SwitchRow(
            title="Include Disk Images",
            subtitle="Include attached media and image files stored in the ball. Archives may be large.",
            active=include_images,
        )
        group.add(self.include_images)
        group.add(
            Adw.ActionRow(
                title="External Folders",
                subtitle="External folder mappings are retained as reconnect hints. Their files are not copied into the archive.",
                subtitle_lines=0,
            )
        )
        self.ball, self.build = window.ball, window.build

    def submit(self):
        include = self.include_images.get_active()

        def selected(path):
            def completed(result):
                self.window.toast("Ball exported to " + result)
                self.close()

            self.run(
                lambda: self.api(
                    "export-ball",
                    ball=str(self.ball),
                    build=str(self.build),
                    path=str(path),
                    includeImages=include,
                    exporter={"name": "2ine for GNOME", "version": "0.1"},
                ),
                completed,
            )

        self.window.choose_save("Export Ball as ZIP", self.ball.stem + ".zip", selected)
