#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native OS/2 settings editors and graphics/sound preferences."""

from gi.repository import GObject, Pango

if __package__:
    from . import gnome_views
    from .gnome_browser import Column, NativeBrowser
else:
    import gnome_views
    from gnome_browser import Column, NativeBrowser

Adw, GLib, Gtk = gnome_views.Adw, gnome_views.GLib, gnome_views.Gtk
button = gnome_views.action_button
REGISTRY_TYPES = {
    0: "REG_NONE",
    1: "REG_SZ",
    2: "REG_EXPAND_SZ",
    3: "REG_BINARY",
    4: "REG_DWORD (Little Endian)",
    5: "REG_DWORD_BIG_ENDIAN",
    6: "REG_LINK",
    7: "REG_MULTI_SZ",
    8: "REG_RESOURCE_LIST",
    9: "REG_FULL_RESOURCE_DESCRIPTOR",
    10: "REG_RESOURCE_REQUIREMENTS_LIST",
    11: "REG_QWORD (Little Endian)",
}


def registry_fold(value):
    return value.translate(
        str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
    )


def validate_registry_value(kind, data):
    if len(data) > 16 * 1024 * 1024:
        raise ValueError("Registry payloads are limited to 16 MiB.")
    if kind in (4, 5) and len(data) != 4:
        raise ValueError("DWORD values require exactly four bytes.")
    if kind == 11 and len(data) != 8:
        raise ValueError("QWORD values require exactly eight bytes.")
    if kind in (1, 2, 6) and (not data.endswith(b"\0") or b"\0" in data[:-1]):
        raise ValueError("This string type requires one terminating NUL and no embedded NUL bytes.")
    if kind == 7 and (not data.endswith(b"\0\0") or (data[:-2] and b"" in data[:-2].split(b"\0"))):
        raise ValueError(
            "REG_MULTI_SZ requires NUL-separated strings, a final double NUL and no empty interior strings."
        )


class EnvironmentPage(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, vexpand=True)
        self.window = window
        self.refresh()

    def refresh(self):
        window = self.window
        gnome_views.empty(self)
        page = Adw.PreferencesPage()
        self.append(page)
        values = Adw.PreferencesGroup(
            title="Guest Environment",
            description="CONFIG.SYS supplies the starting values. Ball overrides take precedence and may refer to inherited values with %NAME%. Application-specific overrides apply when that application launches. OS2PATH is the guest PATH; the host PATH is separate.",
        )
        page.add(values)
        values.set_header_suffix(button("Add Override…", window.env_dialog))
        configured = {}
        for line in window.snapshot["configSys"].splitlines():
            line = line.strip()
            if line.upper().startswith("SET "):
                entry = line[4:]
            elif line.upper().startswith("LIBPATH="):
                entry = line
            else:
                continue
            key, separator, value = entry.partition("=")
            if separator:
                configured[self.normalized(key)] = value.strip()
        overrides = {
            self.normalized(key): (key, value)
            for key, value in window.snapshot["manifest"].get("environment", {}).items()
        }
        effective = {
            self.normalized(key): value
            for key, value in window.snapshot.get("environmentDefaults", {}).items()
        }
        for key in sorted(configured.keys() | overrides.keys() | effective.keys()):
            override_key, override = overrides.get(key, (key, None))
            source = (
                "Ball override: " + override
                if override is not None
                else "CONFIG.SYS: " + configured[key]
                if key in configured
                else "Runtime default"
            )
            if override is not None:
                source = (
                    (
                        "CONFIG.SYS: " + configured[key]
                        if key in configured
                        else "Runtime default overridden"
                    )
                    + "\n"
                    + source
                )
            row = Adw.ActionRow(
                title=key,
                subtitle=source + "\nEffective: " + effective.get(key, ""),
                use_markup=False,
            )
            initial = override if override is not None else effective.get(key, "")
            row.add_suffix(
                button(
                    "Edit Override" if override is not None else "Override…",
                    lambda key=override_key, value=initial: window.env_dialog(key, value),
                )
            )
            if override is not None:
                row.add_suffix(
                    button(
                        "Use Inherited Value",
                        lambda key=override_key: window.execute(["unset", "env." + key]),
                        "edit-clear-symbolic",
                    )
                )
            values.add(row)

    @staticmethod
    def normalized(key):
        key = key.strip().upper()
        return "OS2PATH" if key == "PATH" else key


class PrintingPage(Adw.PreferencesPage):
    def __init__(self, window):
        super().__init__()
        self.window, self.ball = window, window.ball
        self.profiles = None
        self.updating = False
        self.loading = False
        group = Adw.PreferencesGroup(
            title="Printing",
            description="PDF is the runtime default and opens the system save dialog for a name and location. PCL 5 output can be saved for a compatible printer. Changes apply next launch.",
        )
        self.add(group)
        self.queue = Adw.ComboRow(title="Default Printer", use_markup=False)
        self.queue.connect("notify::selected", self.selected)
        self.queue.set_sensitive(False)
        group.add(self.queue)
        group.add(
            button(
                "Browse Original Printer Files…", lambda: window.show_component_media("printers")
            )
        )
        self.reset = button("Use Inherited Default", self.remove)
        self.reset.set_sensitive(False)
        group.set_header_suffix(self.reset)
        self.status = Adw.ActionRow(title="Reading OS/2 printer preferences…", use_markup=False)
        self.status.add_suffix(button("Reload", self.load))
        group.add(self.status)
        self.connect("map", lambda *_: self.load())

    def current(self):
        return (
            self.window.ball == self.ball
            and getattr(self.window, "printing_configuration", None) is self
        )

    @staticmethod
    def preference(profile):
        return next(
            (
                entry
                for entry in profile["entries"]
                if entry["application"].upper() == "PM_SPOOLER" and entry["key"].upper() == "QUEUE"
            ),
            None,
        )

    def failed(self, error):
        if self.current():
            if self.profiles:
                self.render()
            self.status.set_title("Printer Preferences Could Not Be Updated")
            self.status.set_subtitle(str(error) + " Reload before trying again.")

    def load(self):
        if self.loading:
            return
        self.loading = True

        def read():
            return {
                area: self.window.api("configuration-read", ball=str(self.ball), name=area)
                for area in ("user", "system")
            }

        def completed(profiles):
            self.loading = False
            if self.current():
                self.profiles = profiles
                self.render()

        def failed(error):
            self.loading = False
            self.failed(error)

        self.window.request_work(read, completed, locked=False, on_error=failed)

    def render(self):
        user = self.preference(self.profiles["user"])
        system = self.preference(self.profiles["system"])
        entry = user if user is not None else system
        stored = (
            bytes.fromhex(entry["data"]).split(b"\0", 1)[0].decode("latin1")
            if entry
            else "2INEPDF;"
        )
        name = stored.split(";", 1)[0].strip()
        self.choices = ["2INEPDF", "2INEPCL"]
        labels = ["Save as PDF", "PCL 5 Printer"]
        if name.upper() not in self.choices:
            self.choices.append(None)
            labels.append("Stored Queue: " + (name or "(none)"))
        index = self.choices.index(name.upper()) if name.upper() in self.choices else 2
        self.updating = True
        self.queue.set_model(Gtk.StringList.new(labels))
        self.queue.set_selected(index)
        self.queue.set_sensitive(True)
        self.updating = False
        self.reset.set_sensitive(user is not None)
        self.status.set_title(
            "User Profile"
            if user is not None
            else "System Profile"
            if system is not None
            else "Runtime Default"
        )
        self.status.set_subtitle(
            "Stored value: " + stored + ". Reading this page does not write defaults."
            if entry
            else "Save as PDF. No stored preference."
        )

    def selected(self, row, _property):
        if self.updating or not self.profiles or not self.current():
            return
        index = row.get_selected()
        if index < len(self.choices) and self.choices[index]:
            self.write(self.choices[index])

    def remove(self):
        self.write(None)

    def write(self, queue):
        if not self.profiles or not self.current() or self.window.busy:
            return
        user = self.profiles["user"]
        entry = self.preference(user)

        def work():
            return self.window.api(
                "configuration-write",
                ball=str(self.ball),
                name="user",
                expected=user["revision"],
                path=entry["application"] if entry else "PM_SPOOLER",
                key=entry["key"] if entry else "QUEUE",
                text=(queue + ";\0").encode("latin1").hex() if queue is not None else "",
                remove=queue is None,
            )

        def completed(profile):
            if self.current():
                self.profiles["user"] = profile
                self.render()
                for page in self.window.profile_pages:
                    if page.area == "user":
                        page.snapshot = None

        self.window.request_work(work, completed, on_error=self.failed)


class ProfilePage(Gtk.Box):
    def __init__(self, window, area):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.window, self.ball, self.area = window, window.ball, area
        self.snapshot = None
        self.entries = []
        bar = Adw.WrapBox(child_spacing=6, line_spacing=6)
        bar.append(button("Reload", self.load))
        self.add = button("Add Value…", lambda: self.edit(None))
        bar.append(self.add)
        self.append(bar)
        self.path = Gtk.Label(
            xalign=0,
            selectable=True,
            hexpand=True,
            max_width_chars=1,
            ellipsize=Pango.EllipsizeMode.MIDDLE,
            css_classes=["dim-label"],
        )
        self.append(self.path)
        self.notice = Adw.Banner(use_markup=False, revealed=False)
        self.append(self.notice)
        self.values = NativeBrowser(
            [
                Column(
                    "name",
                    "Key",
                    lambda row: row["name"],
                    width=300,
                    expand=True,
                    sort_key=lambda row: row["name"].casefold(),
                ),
                Column(
                    "value",
                    "Value",
                    lambda row: row["summary"],
                    width=320,
                    expand=True,
                    compact=True,
                    sort_key=lambda row: row["summary"].casefold(),
                ),
                Column(
                    "edit",
                    "Edit",
                    lambda _row: "",
                    width=110,
                    compact=True,
                    action=lambda row: {
                        "label": "Edit Value…",
                        "activate": lambda: self.edit(row["entry"]),
                    },
                ),
            ],
            title="Profile and registry values",
            search_hint="Search keys and values",
            identity=lambda row: (row["entry"]["application"], row["entry"]["key"]),
            searchable=lambda row: row["name"] + " " + row["summary"],
            activated=lambda row: self.edit(row["entry"]),
            single_click=True,
            height=220,
        )
        self.search = self.values.search
        self.append(self.values)
        self.connect("map", lambda *_: self.load() if self.snapshot is None else None)
        self.refresh_editability()

    def api(self, action, **fields):
        return self.window.api(action, ball=str(self.ball), name=self.area, **fields)

    def load(self):
        def completed(snapshot):
            if self.ball == self.window.ball:
                self.snapshot = snapshot
                self.entries = snapshot["entries"]
                self.path.set_label(snapshot["path"])
                self.notice.set_revealed(False)
                self.render()

        self.window.request_work(
            lambda: self.api("configuration-read"),
            completed,
            locked=False,
            on_error=lambda error: self.show_error(str(error)),
        )

    def show_error(self, message):
        self.notice.set_title(message)
        self.notice.set_revealed(True)

    def refresh_editability(self):
        self.add.set_sensitive(self.snapshot is not None and not self.window.busy)
        self.values.table.set_sensitive(not self.window.busy)

    def render(self):
        rows = []
        for entry in sorted(self.entries, key=lambda value: (value["application"], value["key"])):
            registry = self.area == "registry"
            if registry and not entry["key"].startswith("v:"):
                continue
            key = entry["key"][2:] if registry else entry["key"]
            data = bytes.fromhex(entry["data"])
            short_header = registry and len(data) < 4
            kind = int.from_bytes(data[:4], "little") if registry and not short_header else None
            data = data[4:] if registry and not short_header else data
            if short_header:
                summary = "Incomplete type header: " + data.hex()
            elif kind == 4 and len(data) == 4:
                summary = f"DWORD: {int.from_bytes(data, 'little')}"
            elif data.endswith(b"\0") and b"\0" not in data[:-1]:
                summary = data[:-1].decode("latin1").replace("\n", " ↵ ")
            else:
                summary = f"{len(data)} bytes: {data[:32].hex()}"
            name = entry["application"] + " · " + (key or "(Default)")
            if registry:
                name = key or "(Default)"
                summary = (
                    summary
                    if short_header
                    else REGISTRY_TYPES.get(kind, "Type " + str(kind)) + " · " + summary
                )
            rows.append({"name": name, "summary": summary, "entry": entry})
        self.values.set_rows(rows)
        self.refresh_editability()

    def edit(self, entry):
        if self.window.busy or self.snapshot is None:
            self.show_error("Stop the guest before editing profiles or the registry.")
            return
        dialog = ProfileValueDialog(self, entry)
        dialog.present(self.window)


class FileProfilePage(ProfilePage):
    def __init__(self, window, record):
        self.file_path = record["path"]
        super().__init__(window, record["role"])

    def api(self, action, **fields):
        if self.area in ("user", "system"):
            return self.window.api(action, ball=str(self.ball), name=self.area, **fields)
        if action == "configuration-read":
            return self.window.api(
                "configuration-file-read", ball=str(self.ball), path=self.file_path
            )
        return self.window.api(
            "configuration-file-write",
            ball=str(self.ball),
            path=self.file_path,
            expected=fields["expected"],
            application=fields["path"],
            key=fields["key"],
            data=fields["text"],
            remove=fields.get("remove", False),
        )


class Os2FilesPage(Gtk.Box):
    def __init__(self, window, config_widget):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.window, self.ball, self.config_widget = window, window.ball, config_widget
        self.records, self.current_record, self.profile = [], None, None
        self.updating = False
        self.serial = 0
        self.file = NativeBrowser(
            [
                Column(
                    "label",
                    "OS/2 File",
                    lambda record: record["label"],
                    width=240,
                    expand=True,
                    sort_key=lambda record: record["label"].casefold(),
                ),
                Column(
                    "path",
                    "Path",
                    lambda record: record["path"],
                    width=320,
                    expand=True,
                    sort_key=lambda record: record["path"].casefold(),
                ),
            ],
            title="OS/2 configuration files",
            search_hint="Search Configuration Files",
            identity=lambda record: record["path"],
            selected=self.selected,
            height=180,
        )
        group = Adw.PreferencesGroup(
            description="CONFIG.SYS controls startup settings. OS2.INI and OS2SYS.INI hold user and system profile values; installers may use additional INI files. Registry keys have their own Settings tab."
        )
        group.add(self.file)
        group.set_header_suffix(button("Find Files", self.discover))
        self.append(group)
        self.notice = Adw.Banner(use_markup=False)
        self.append(self.notice)
        self.content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.append(self.content)
        self.file_path = window.settings_file_paths.get(str(self.ball), r"C:\CONFIG.SYS")
        self.connect("map", lambda *_: self.discover() if not self.records else None)

    def current(self):
        return (
            self.window.ball == self.ball
            and getattr(self.window, "os2_files_configuration", None) is self
        )

    def refresh_editability(self, busy):
        config = self.current_record and self.current_record["role"] == "config"
        self.content.set_sensitive(not busy or bool(config))

    def error(self, error):
        if self.current():
            self.notice.set_title(str(error))
            self.notice.set_revealed(True)

    def discover(self):
        def completed(result):
            if not self.current():
                return
            self.records = [record for record in result["files"] if record["role"] != "registry"]
            self.updating = True
            self.file.set_rows(self.records)
            selected = next(
                (
                    record
                    for record in self.records
                    if record["path"].upper() == self.file_path.upper()
                ),
                self.records[0] if self.records else None,
            )
            if selected:
                self.file.focus(selected["path"])
            self.updating = False
            self.notice.set_title(
                "File discovery reached its limit. Additional files were not listed."
                if result["truncated"]
                else ""
            )
            self.notice.set_revealed(result["truncated"])
            self.selected(self.file.selected_data())

        self.window.request_work(
            lambda: self.window.api("configuration-files", ball=str(self.ball)),
            completed,
            locked=False,
            on_error=self.error,
        )

    def selected(self, record):
        if self.updating or not self.current() or record is None:
            return
        self.file_path = record["path"]
        self.window.settings_file_paths[str(self.ball)] = self.file_path
        self.current_record = record
        self.refresh_editability(self.window.busy)
        self.serial += 1
        serial = self.serial
        if self.profile in self.window.profile_pages:
            self.window.profile_pages.remove(self.profile)
        self.profile = None
        gnome_views.empty(self.content)
        if record["role"] == "config":
            self.content.append(self.config_widget)
            return
        if record["kind"] == "profile" and record["editable"]:
            self.profile = FileProfilePage(self.window, record)
            self.window.profile_pages.append(self.profile)
            self.content.append(self.profile)
            return

        def completed(snapshot):
            if self.current() and serial == self.serial:
                self.show_text(snapshot)

        self.window.request_work(
            lambda: self.window.api(
                "configuration-file-read", ball=str(self.ball), path=record["path"]
            ),
            completed,
            locked=False,
            on_error=self.error,
        )

    def show_text(self, snapshot):
        key = (str(self.ball), snapshot["path"])
        text = snapshot.get("text", snapshot.get("data", ""))
        draft = self.window.settings_file_drafts.get(key)
        editor = Gtk.TextView(monospace=True, vexpand=True, editable=snapshot["editable"])
        self.text_editor = editor
        self.file_snapshot = snapshot
        buffer = editor.get_buffer()
        buffer.set_text(draft[1] if draft else text)
        buffer.set_enable_undo(True)
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=280, has_frame=True)
        scroll.set_child(editor)
        self.content.append(
            Gtk.Label(
                label=snapshot["path"] + " · " + snapshot.get("reason", ""),
                wrap=True,
                xalign=0,
                selectable=True,
            )
        )
        self.content.append(scroll)
        if not snapshot["editable"]:
            return
        revision = draft[0] if draft else snapshot["revision"]
        if draft and revision != snapshot["revision"]:
            self.notice.set_title(
                "This file changed on disk. Your draft is preserved; reload before saving."
            )
            self.notice.set_revealed(True)

        def changed(_buffer):
            value = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
            if value == text:
                self.window.settings_file_drafts.pop(key, None)
            else:
                self.window.settings_file_drafts[key] = (revision, value)

        buffer.connect("changed", changed)

        def save():
            if self.window.busy or not self.current():
                return
            value = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
            try:
                value.encode("latin1")
            except UnicodeError:
                self.error("OS/2 text files require Latin-1 bytes.")
                return

            def completed(_snapshot):
                self.window.settings_file_drafts.pop(key, None)
                if self.current():
                    self.notice.set_revealed(False)
                    self.selected(self.file.selected_data())

            self.window.request_work(
                lambda: self.window.api(
                    "configuration-file-write",
                    ball=str(self.ball),
                    path=snapshot["path"],
                    expected=revision,
                    text=value,
                ),
                completed,
                on_error=self.error,
            )

        def reload_file():
            def discard():
                self.window.settings_file_drafts.pop(key, None)
                self.notice.set_revealed(False)
                self.selected(self.file.selected_data())

            if key not in self.window.settings_file_drafts:
                discard()
                return
            dialog = Adw.AlertDialog(
                heading="Discard this file's changes?",
                body="Your draft will be replaced with the current file on disk.",
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard Changes")
            dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
            dialog.set_close_response("cancel")
            dialog.connect(
                "response", lambda _dialog, choice: discard() if choice == "discard" else None
            )
            dialog.present(self.window)

        controls = Gtk.Box(spacing=12)
        self.save_file = button("Save File", save)
        controls.append(self.save_file)
        controls.append(button("Reload from Disk", reload_file))
        self.content.append(controls)


class RegistryPage(ProfilePage):
    def __init__(self, window):
        self.selected_key = "hkcu"
        self.building_tree = False
        super().__init__(window, "registry")
        self.search.set_placeholder_text("Search values in selected key")
        values_scroll = self.get_last_child()
        self.remove(values_scroll)
        self.tree_browser = NativeBrowser(
            [
                Column(
                    "name",
                    "Registry Key",
                    lambda node: node["name"],
                    width=240,
                    expand=True,
                    sort_key=lambda node: registry_fold(node["name"]),
                )
            ],
            title="Registry keys",
            search_hint="Search Registry Keys",
            identity=lambda node: node["path"],
            searchable=lambda node: node["name"] + " " + node["path"],
            tooltip=lambda node: node["path"],
            selected=self.selection_changed,
            height=220,
        )
        self.tree_view = self.tree_browser.table
        self.split = Adw.OverlaySplitView(
            min_sidebar_width=180,
            max_sidebar_width=260,
            sidebar=self.tree_browser,
            content=values_scroll,
            vexpand=True,
        )
        self.keys_button = Gtk.ToggleButton(
            icon_name="sidebar-show-symbolic",
            tooltip_text="Show Registry Keys",
            visible=False,
        )
        self.keys_button.update_property([Gtk.AccessibleProperty.LABEL], ["Show registry keys"])
        self.keys_button.bind_property(
            "active",
            self.split,
            "show-sidebar",
            GObject.BindingFlags.BIDIRECTIONAL | GObject.BindingFlags.SYNC_CREATE,
        )
        self.split.set_show_sidebar(True)
        self.get_first_child().prepend(self.keys_button)
        self.layout = Adw.BreakpointBin(
            child=self.split, vexpand=True, width_request=280, height_request=220
        )
        breakpoint = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 600sp"))
        breakpoint.add_setter(self.split, "collapsed", True)
        breakpoint.add_setter(self.keys_button, "visible", True)
        self.layout.add_breakpoint(breakpoint)
        self.append(self.layout)
        self.key_path = Gtk.Label(xalign=0, selectable=True, wrap=True)
        self.append(self.key_path)

    def select_key(self, path):
        self.tree_browser.focus(path)

    def selection_changed(self, selected):
        if self.building_tree or selected is None:
            return
        self.selected_key = selected["path"]
        self.render_values()
        if self.split.get_collapsed():
            self.split.set_show_sidebar(False)

    def render(self):
        if self.building_tree:
            return
        self.building_tree = True
        roots, nodes = [], {}
        for key, title in (
            ("hkcr", "HKEY_CLASSES_ROOT"),
            ("hkcu", "HKEY_CURRENT_USER"),
            ("hklm", "HKEY_LOCAL_MACHINE"),
            ("hku", "HKEY_USERS"),
        ):
            nodes[key] = {"name": title, "path": key, "children": []}
            roots.append(nodes[key])
        for entry in sorted(self.entries, key=lambda item: item["application"]):
            pieces = registry_fold(entry["application"]).split("\\")
            parent = nodes.get(pieces[0])
            for index in range(1, len(pieces)):
                path = "\\".join(pieces[: index + 1])
                if parent is None:
                    break
                if path not in nodes:
                    nodes[path] = {"name": pieces[index], "path": path, "children": []}
                    parent["children"].append(nodes[path])
                parent = nodes[path]
        if self.selected_key not in nodes:
            self.selected_key = "hkcu"
        self.tree_browser.set_rows(roots)
        self.select_key(self.selected_key)
        self.building_tree = False
        self.render_values()

    def render_values(self):
        entries = self.entries
        self.entries = [
            entry for entry in entries if registry_fold(entry["application"]) == self.selected_key
        ]
        ProfilePage.render(self)
        self.entries = entries
        self.key_path.set_label(self.selected_key)

    def edit(self, entry):
        if self.window.busy or self.snapshot is None:
            self.show_error("Stop the guest before editing the registry.")
            return
        dialog = ProfileValueDialog(self, entry)
        if entry is None:
            dialog.application.set_text(self.selected_key)
        dialog.present(self.window)


class ProfileValueDialog(gnome_views.FormDialog):
    def __init__(self, page, entry):
        super().__init__(page.window, "Add Value" if entry is None else "Edit Value", height=650)
        self.profile_page, self.entry, self.revision = page, entry, page.snapshot["revision"]
        self.registry = page.area == "registry"
        self.mode = 0
        self.converting = False
        group = self.group("Value")
        preferences = self.page()
        preferences.add(group)
        self.application = Adw.EntryRow(
            title="Registry Key" if self.registry else "Application",
            text=entry["application"] if entry else ("hkcu\\software\\" if self.registry else ""),
        )
        self.key = Adw.EntryRow(
            title="Value Name (empty for default)" if self.registry else "Key",
            text=(entry["key"][2:] if self.registry else entry["key"]) if entry else "",
        )
        for row in (self.application, self.key):
            row.set_sensitive(entry is None)
            group.add(row)
        data = bytes.fromhex(entry["data"]) if entry else b""
        self.short_header = self.registry and entry is not None and len(data) < 4
        self.kind = Adw.EntryRow(
            title="Registry Type", text=str(int.from_bytes(data[:4], "little") if entry else 3)
        )
        if self.registry:
            self.type_codes = list(REGISTRY_TYPES)
            code = int(self.kind.get_text())
            self.type_selector = Adw.ComboRow(
                title="Registry Type",
                model=Gtk.StringList.new(list(REGISTRY_TYPES.values()) + ["Custom Type"]),
            )
            self.type_selector.set_selected(
                self.type_codes.index(code) if code in REGISTRY_TYPES else len(self.type_codes)
            )
            self.kind.set_title("Custom Type Code (0–4294967295)")
            self.kind.set_visible(code not in REGISTRY_TYPES)

            def type_selected(row, _property):
                index = row.get_selected()
                self.kind.set_visible(index == len(self.type_codes))
                if index < len(self.type_codes):
                    self.kind.set_text(str(self.type_codes[index]))

            self.type_selector.connect("notify::selected", type_selected)

            def type_code_changed(row, _property):
                try:
                    value = int(row.get_text(), 10)
                except ValueError:
                    return
                index = (
                    self.type_codes.index(value)
                    if value in REGISTRY_TYPES
                    else len(self.type_codes)
                )
                if self.type_selector.get_selected() != index:
                    self.type_selector.set_selected(index)

            self.kind.connect("notify::text", type_code_changed)
            group.add(self.type_selector)
            group.add(self.kind)
            self.repair_header = Adw.SwitchRow(
                title="Replace Malformed Type Header",
                visible=self.short_header,
                subtitle="Original record bytes are shown as payload. Choose a type and explicitly enable replacement to repair the header.",
            )
            group.add(self.repair_header)
            data = data if self.short_header else data[4:]
        self.format = Adw.ComboRow(
            title="Edit As",
            model=Gtk.StringList.new(["Hexadecimal", "Latin-1 Text", "DWORD (Little Endian)"]),
        )
        group.add(self.format)
        self.terminated = Adw.SwitchRow(
            title="Include Terminating NUL Byte", visible=False, active=True
        )
        group.add(self.terminated)
        self.editor = Gtk.TextView(
            monospace=True,
            wrap_mode=Gtk.WrapMode.WORD_CHAR,
            left_margin=12,
            right_margin=12,
            top_margin=12,
            bottom_margin=12,
        )
        self.editor.get_buffer().set_text(data.hex())
        self.editor.update_property([Gtk.AccessibleProperty.LABEL], ["Configuration value data"])
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=180, has_frame=True)
        scroll.set_child(self.editor)
        self.body.append(scroll)
        self.body.append(
            Gtk.Label(
                label="Unedited values retain their exact bytes. Saving checks for external changes. Stop the guest before editing; changes apply next launch.",
                xalign=0,
                wrap=True,
                css_classes=["dim-label"],
            )
        )
        if entry:
            self.body.append(button("Delete Value…", self.confirm_delete))
        self.format.connect("notify::selected", self.convert)

    def text(self):
        buffer = self.editor.get_buffer()
        return buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)

    def data(self):
        text = self.text()
        if self.mode == 0:
            return bytes.fromhex(text)
        if self.mode == 1:
            if "\0" in text:
                raise ValueError("Text cannot contain NUL. Use hexadecimal for binary values.")
            return text.encode("latin1") + (b"\0" if self.terminated.get_active() else b"")
        return int(text.strip(), 10).to_bytes(4, "little", signed=False)

    def convert(self, row, _property):
        if self.converting:
            return
        new = row.get_selected()
        try:
            data = self.data()
            if new == 0:
                text = data.hex()
            elif new == 1:
                terminated = data.endswith(b"\0")
                value = data[:-1] if terminated else data
                if b"\0" in value:
                    raise ValueError("Embedded NUL bytes require hexadecimal editing.")
                text = value.decode("latin1")
                self.terminated.set_active(terminated)
            else:
                if len(data) != 4:
                    raise ValueError("An integer requires exactly four bytes.")
                text = str(int.from_bytes(data, "little"))
            self.editor.get_buffer().set_text(text)
            self.mode = new
            self.terminated.set_visible(new == 1)
            self.error.set_revealed(False)
        except (ValueError, OverflowError, UnicodeError) as error:
            self.converting = True
            row.set_selected(self.mode)
            self.converting = False
            self.error_message(error)

    def submit(self, remove=False):
        try:
            data = b"" if remove else self.data()
            if self.registry:
                if self.short_header and not remove and not self.repair_header.get_active():
                    raise ValueError(
                        "This record has an incomplete type header. Explicitly enable header replacement before saving a repair."
                    )
                kind = int(self.kind.get_text(), 10)
                if not remove:
                    validate_registry_value(kind, data)
                data = kind.to_bytes(4, "little", signed=False) + data
            application, key = self.application.get_text(), self.key.get_text()
            if self.registry:
                if "\0" in application or "\0" in key:
                    raise ValueError("Registry names cannot contain NUL bytes.")
                if (
                    len(application.encode("latin1")) > 32767
                    or len(key.encode("latin1")) + 2 > 32767
                ):
                    raise ValueError(
                        "Registry paths and v:-prefixed value names are limited to 32767 bytes."
                    )
                application, key = registry_fold(application), "v:" + registry_fold(key)
        except (ValueError, OverflowError, UnicodeError) as error:
            self.error_message(error)
            return

        def completed(snapshot):
            self.profile_page.snapshot = snapshot
            self.profile_page.entries = snapshot["entries"]
            if self.registry and hasattr(self.profile_page, "selected_key"):
                self.profile_page.selected_key = application
            self.profile_page.render()
            self.close()

        self.run(
            lambda: self.profile_page.api(
                "configuration-write",
                expected=self.revision,
                path=application,
                key=key,
                text=data.hex(),
                remove=remove,
            ),
            completed,
        )

    def confirm_delete(self):
        dialog = Adw.AlertDialog(
            heading="Delete this value?", body="Other profile and registry values will be kept."
        )
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("delete", "Delete Value")
        dialog.set_response_appearance("delete", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_close_response("cancel")
        dialog.connect(
            "response",
            lambda _dialog, choice: self.submit(remove=True) if choice == "delete" else None,
        )
        dialog.present(self.window)


class GraphicsMediaPage(Adw.PreferencesPage):
    """Portable per-ball preferences, resolved against the receiving runtime."""

    def __init__(self, window):
        super().__init__()
        self.window, self.ball, self.build = window, window.ball, window.build
        self.settings = window.snapshot["manifest"].get("system", {})
        self.features = None
        self.loading = False
        self.updating = False
        support = Adw.PreferencesGroup(title="Runtime Support")
        self.support_group = support
        self.support_row = Adw.ActionRow(
            title="Checking graphics and media support…", use_markup=False
        )
        self.support_row.add_suffix(button("Retry", self.load))
        support.add(self.support_row)
        self.add(support)
        display = Adw.PreferencesGroup(
            title="OS/2 Display",
            description="Applies next launch. Fonts, dialog units and controls scale together. Printer resolution remains 300 DPI.",
        )
        self.add(display)
        dpi = int(float(self.settings.get("display_dpi", 96)))
        values = [96, 120, 144, 192]
        if dpi not in values:
            values.append(dpi)
        row = Adw.ComboRow(
            title="Display DPI",
            model=Gtk.StringList.new([f"{value} DPI ({value * 100 // 96}%)" for value in values]),
        )
        row.set_selected(values.index(dpi))
        row.connect(
            "notify::selected",
            lambda row, _prop: self.save("display_dpi", str(values[row.get_selected()])),
        )
        display.add(row)
        chrome_values = ["native", "os2", "both"]
        chrome = Adw.ComboRow(
            title="Title Bars",
            subtitle="Applies next launch. Document windows keep their OS/2 controls.",
            model=Gtk.StringList.new(["Native", "OS/2", "Both"]),
        )
        chrome.set_selected(chrome_values.index(self.settings.get("window_chrome", "both")))
        chrome.connect(
            "notify::selected",
            lambda row, _prop: self.save("window_chrome", chrome_values[row.get_selected()]),
        )
        display.add(chrome)
        graphics = Adw.PreferencesGroup(
            title="OpenGL",
            description="Original OS/2 uses your own OPENGL.DLL and its dependencies. Its compatibility is not yet qualified.",
        )
        self.add(graphics)
        self.providers = [None, "mesa", "original", "off"]
        self.opengl = Adw.ComboRow(
            title="OpenGL Renderer",
            model=Gtk.StringList.new(
                ["Receiving Runtime Default", "Mesa", "Original OS/2 (Unqualified)", "Off"]
            ),
            use_markup=False,
        )
        requested = self.settings.get("opengl")
        self.opengl.set_selected(self.providers.index(requested))
        self.opengl.connect("notify::selected", self.opengl_changed)
        graphics.add(self.opengl)
        graphics.add(
            button("Install Original OpenGL Files…", lambda: window.show_component_media("opengl"))
        )
        media = Adw.PreferencesGroup(
            title="Playback and DIVE",
            description="Changes apply next launch. Reset an override to follow the receiving runtime. Unavailable options can be saved for another computer; enabling them requires a supporting runtime before launch.",
        )
        self.add(media)
        media.add(
            button(
                "Browse Original Multimedia Files…",
                lambda: window.show_component_media("multimedia"),
            )
        )
        self.rows, self.resets = {}, {}
        self.descriptions = {
            "video": "Movie playback. PCM audio controls movie sound.",
            "pcm": "Sampled audio, DART and movie sound. PC speaker has its own Beep Volume.",
            "midi": "MIDI synthesis uses the sound banks and channel mapping below.",
            "dive": "Direct video access used by OS/2 applications and games.",
        }
        for key, title in (
            ("video", "Video Playback"),
            ("pcm", "PCM Audio"),
            ("midi", "MIDI Synthesis"),
            ("dive", "DIVE Graphics"),
        ):
            row = Adw.SwitchRow(title=title, use_markup=False)
            row.set_active(
                str(self.settings.get(key, "0")).lower() in ("1", "yes", "y", "true", "t", "on")
            )
            row.set_subtitle(self.descriptions[key] + " Checking runtime support…")
            row.connect(
                "notify::active", lambda row, _prop, key=key: self.feature_changed(key, row)
            )
            reset = button(
                "Use Receiving Runtime Default",
                lambda key=key: self.save(key, None),
                "edit-clear-symbolic",
            )
            reset.set_sensitive(key in self.settings)
            row.add_suffix(reset)
            self.rows[key], self.resets[key] = row, reset
            media.add(row)
        sound = Adw.PreferencesGroup(title="Sound")
        self.add(sound)
        self.beep_volume = Adw.EntryRow(
            title="Beep Volume (0–1)",
            text=window.settings_drafts.get(str(self.ball), {}).get(
                "beep_volume", str(self.settings.get("beep_volume", "1"))
            ),
            show_apply_button=True,
        )
        self.beep_volume.connect(
            "notify::text",
            lambda row, _prop: window.settings_drafts.setdefault(str(self.ball), {}).update(
                beep_volume=row.get_text()
            ),
        )
        self.beep_volume.connect("apply", lambda row: self.save("beep_volume", row.get_text()))
        self.beep_volume.add_suffix(
            button(
                "Reset Beep Volume", lambda: self.save("beep_volume", None), "edit-clear-symbolic"
            )
        )
        sound.add(self.beep_volume)
        banks = Adw.ActionRow(
            title="Sound Banks and Channel Mapping",
            subtitle="Import SF2, SF3, SFZ and DLS; configure all 16 MIDI channels.",
            activatable=True,
        )

        def open_banks():
            return window.audio_dialog() if self.current() else None

        banks.add_suffix(button("Open Sound Banks and MIDI", open_banks))
        banks.connect("activated", lambda *_: open_banks())
        sound.add(banks)
        self.connect("map", lambda *_: self.load() if self.features is None else None)

    def current(self):
        return (
            self.window.ball == self.ball
            and self.window.build == self.build
            and getattr(self.window, "display_configuration", None) is self
        )

    def save(self, key, value):
        if self.updating or not self.current():
            return
        arguments = (
            ["unset", "system." + key] if value is None else ["config", "system." + key, value]
        )
        self.window.execute(arguments)

    def opengl_changed(self, row, _property):
        self.save("opengl", self.providers[row.get_selected()])

    def feature_changed(self, key, row):
        self.save(key, str(row.get_active()).lower())

    def load(self):
        if self.loading:
            return
        self.loading = True
        self.support_row.set_title("Checking graphics and media support…")
        self.support_row.set_subtitle("")

        def completed(features):
            self.loading = False
            if not self.current():
                return
            self.features = features
            self.support_group.set_visible(False)
            self.update_capabilities()

        def failed(error):
            self.loading = False
            if self.current():
                self.support_row.set_title("Runtime Support Could Not Be Checked")
                self.support_row.set_subtitle(
                    str(error) + " You can still save preferences for another runtime."
                )
                self.opengl.set_subtitle("Runtime support has not been checked.")
                for key, row in self.rows.items():
                    row.set_subtitle(
                        self.descriptions[key] + " Runtime support has not been checked."
                    )

        self.window.request_work(
            lambda: self.window.api("runtime-features", ball=str(self.ball), build=str(self.build)),
            completed,
            locked=False,
            on_error=failed,
        )

    def update_capabilities(self):
        self.updating = True
        try:
            gl = self.features["opengl"]
            labels = ["Receiving Runtime Default"]
            for key, title in (
                ("mesa", "Mesa"),
                ("original", "Original OS/2 (Unqualified)"),
                ("off", "Off"),
            ):
                provider = gl["providers"][key]
                labels.append(title if provider["available"] else title + " — Unavailable Here")
            requested = gl["requested"]
            self.opengl.set_model(Gtk.StringList.new(labels))
            self.opengl.set_selected(self.providers.index(requested))
            chosen = requested if requested is not None else gl["default"]
            title = {"mesa": "Mesa", "original": "Original OS/2", "off": "Off"}[chosen]
            status = ("Runtime default: " if requested is None else "Saved choice: ") + title + "."
            provider = gl["providers"][chosen]
            if not provider["available"]:
                status += " Unavailable here."
            if provider["reason"]:
                status += " " + provider["reason"]
            self.opengl.set_subtitle(status)
            for key, row in self.rows.items():
                feature = self.features[key]
                requested = feature["requested"]
                selected = feature["default"] if requested is None else requested
                row.set_active(selected)
                status = ("Runtime default: " if requested is None else "Saved choice: ") + (
                    "On." if selected else "Off."
                )
                if not feature["available"]:
                    status += " Unavailable here. " + feature["reason"]
                row.set_subtitle(self.descriptions[key] + " " + status)
                self.resets[key].set_sensitive(requested is not None)
        finally:
            self.updating = False


def display_page(window):
    return GraphicsMediaPage(window)
