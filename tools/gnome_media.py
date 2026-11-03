# Public domain. No rights reserved. Wholly AI-authored.
"""Review and extract original OS/2 files from nested installation media."""

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
try:
    from gi.repository import Adw, GLib, Gtk
except ImportError as error:
    raise SystemExit("GTK 4 and libadwaita are required: " + str(error)) from error

if __package__:
    from .gnome_files import FileList, display_path
    from .gnome_views import FormDialog, action_button
else:
    from gnome_files import FileList, display_path
    from gnome_views import FormDialog, action_button

CATEGORIES = (
    (
        "cmd",
        "CMD and REXX Support",
        "Command interpreter, installer support files and original REXX.",
    ),
    ("fonts", "Fonts", "Original OS/2 font files."),
    ("assets", "Sounds, Images and Video", "Desktop images, sounds and sample videos."),
    ("printers", "Printer Support", "Original printer drivers and supporting files."),
    ("help", "Full Help", "Original help manager, viewers, help files and books."),
    ("full", "Full OS File Set", "All available files except components supplied by 2ine."),
)


class OS2MediaDialog(FormDialog):
    def __init__(self, window, sources):
        super().__init__(window, "Browse OS/2 Media", accept="Continue", width=820, height=680)
        self.sources = []
        self.directory = None
        self.selected = None
        self.plan = None
        self.stage = "source"
        self.categories = {"cmd"}
        self.updating_categories = False
        self.stack = Gtk.Stack(vexpand=True)
        self.body.append(self.stack)
        self.source_page = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL,
            spacing=12,
            margin_top=16,
            margin_bottom=16,
            margin_start=20,
            margin_end=20,
        )
        self.stack.add_named(self.source_page, "source")
        self.source_page.append(
            Gtk.Label(
                label="Browse the installation media, or continue to search the selected media. Folders and archives can be opened inside other archives.",
                wrap=True,
                xalign=0,
            )
        )
        bar = Adw.WrapBox(child_spacing=6, line_spacing=6)
        bar.append(action_button("Choose ISO or Archive…", self.choose_media))
        bar.append(action_button("Choose Folder…", self.choose_folder))
        self.up = action_button("Parent Folder", self.go_up, "go-up-symbolic")
        bar.append(self.up)
        self.source_page.append(bar)
        self.address = Gtk.Label(xalign=0, wrap=True, selectable=True)
        self.source_page.append(self.address)
        self.relevant_only = Gtk.CheckButton(label="Relevant Files Only", active=True)
        self.relevant_only.connect("toggled", lambda *_: self.files.refresh())
        self.source_page.append(self.relevant_only)
        self.files = FileList(selected=self.select, activated=self.activate, accept=self.relevant)
        self.source_page.append(self.files)
        self.current_only = Gtk.CheckButton(label="Search only the current folder or selected item")
        self.source_page.append(self.current_only)
        self.notice = Gtk.Label(xalign=0, wrap=True, selectable=True, css_classes=["dim-label"])
        self.body.append(self.notice)
        self.open_sources(sources)

    def relevant(self, entry):
        if not self.relevant_only.get_active() or entry["directory"] or entry["container"]:
            return True
        ext = entry["name"].rsplit(".", 1)[-1].lower() if "." in entry["name"] else ""
        return (
            not ext
            or ext.endswith("_")
            or ext
            in {
                "exe",
                "com",
                "cmd",
                "bat",
                "dll",
                "sys",
                "os2",
                "msg",
                "hlp",
                "inf",
                "fon",
                "fnt",
                "ttf",
                "otf",
                "ttc",
                "pfb",
                "pfm",
                "afm",
                "ofm",
                "drv",
                "pdr",
                "ppd",
                "bmp",
                "ico",
                "ptr",
                "met",
                "wav",
                "mid",
                "avi",
                "mpg",
                "mpeg",
                "jpg",
                "jpeg",
                "png",
                "gif",
                "tif",
                "tiff",
                "au",
                "aiff",
                "aif",
                "mov",
                "mp4",
                "fli",
                "flc",
                "mkv",
                "webm",
            }
        )

    def select(self, entry):
        self.selected = entry

    def activate(self, entry):
        if entry["directory"] or entry["container"]:
            self.navigate(entry["path"])

    def show_directory(self, directory):
        self.directory = directory
        self.selected = None
        self.address.set_label(display_path(directory["label"]))
        self.up.set_sensitive(bool(directory["parent"]))
        self.files.set_entries(directory["entries"])

    def go_up(self):
        if self.directory and self.directory["parent"]:
            self.navigate(self.directory["parent"])

    def navigate(self, path):
        self.run(lambda: self.api("browse-media", path=path), self.show_directory)

    def open_sources(self, sources):
        self.notice.set_label("Looking for ISO images…")

        def loaded(result):
            self.sources = result["sources"]
            self.show_directory(result["directory"])
            self.current_only.set_active(False)
            self.notice.set_label(
                "Found the OS/2 installation file set."
                if result.get("installationDirectory")
                else "Detected ISO image inside the selected media."
                if result.get("detectedISO")
                else ""
            )
            warnings = [
                "Skipped " + item["path"] + ": " + item["reason"]
                for item in result.get("skippedSources", [])
            ]
            if warnings:
                self.notice.set_label(self.notice.get_label() + "\n" + "\n".join(warnings))

        self.run(lambda: self.api("open-os2-source", sources=list(sources)), loaded)

    def choose_media(self):
        picker = Gtk.FileDialog(title="Choose OS/2 ISO, Floppy Images or Archives")

        def chosen(picker, result):
            try:
                files = picker.open_multiple_finish(result)
                sources = [files.get_item(i).get_path() for i in range(files.get_n_items())]
                if sources:
                    self.open_sources(sources)
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.error_message(error)

        picker.open_multiple(self.window, None, chosen)

    def choose_folder(self):
        picker = Gtk.FileDialog(title="Choose OS/2 Media Folder")

        def chosen(picker, result):
            try:
                self.open_sources([picker.select_folder_finish(result).get_path()])
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.error_message(error)

        picker.select_folder(self.window, None, chosen)

    def selected_files(self, categories=None):
        if not self.plan:
            return []
        categories = categories if categories is not None else self.categories
        files = [
            file
            for file in self.plan["files"]
            if not file["provided"]
            and not file["present"]
            and ("full" in categories or file["category"] in categories)
        ]
        priorities = {}
        for file in files:
            key = file["path"].casefold()
            priorities[key] = max(priorities.get(key, -1), file.get("priority", 0))
        unique = {}
        for file in files:
            key = file["path"].casefold()
            if file.get("priority", 0) == priorities[key]:
                unique[key] = file
        return list(unique.values())

    def show_options(self, plan):
        self.plan = plan
        self.stage = "options"
        self.set_title("Extract OS/2 Files")
        self.accept.set_label("Extract Files")
        page = Adw.PreferencesPage()
        group = Adw.PreferencesGroup(
            title="Choose What to Extract",
            description="Files are copied into their C: folders. Existing files are kept.",
        )
        page.add(group)
        self.switches = {}
        for key, title, description in CATEGORIES:
            count = len(self.selected_files({key}))
            row = Adw.SwitchRow(
                title=title,
                subtitle=f"{description} {count} available file{'s' if count != 1 else ''}.",
            )
            row.set_active(key in self.categories)
            row.connect(
                "notify::active",
                lambda row, _, key=key: self.toggle_category(key, row.get_active()),
            )
            self.switches[key] = row
            group.add(row)
        controls = Gtk.Box(
            spacing=8, margin_top=12, margin_bottom=12, margin_start=20, margin_end=20
        )
        controls.append(action_button("Back", self.back))
        container = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, vexpand=True)
        container.append(page)
        container.append(controls)
        old = self.stack.get_child_by_name("options")
        if old:
            self.stack.remove(old)
        self.stack.add_named(container, "options")
        self.stack.set_visible_child_name("options")
        self.update_count()

    def toggle_category(self, key, enabled):
        if self.updating_categories:
            return
        if enabled:
            self.categories.add(key)
        else:
            self.categories.discard(key)
        self.updating_categories = True
        for category, row in self.switches.items():
            row.set_active(category in self.categories or "full" in self.categories)
            row.set_sensitive(category == "full" or "full" not in self.categories)
        self.updating_categories = False
        self.update_count()

    def update_count(self):
        count = len(self.selected_files())
        self.accept.set_label(f"Extract {count} File{'s' if count != 1 else ''}")
        self.accept.set_sensitive(count > 0)

    def back(self):
        self.stage = "source"
        self.set_title("Browse OS/2 Media")
        self.stack.set_visible_child_name("source")
        self.accept.set_label("Continue")
        self.accept.set_sensitive(True)
        self.notice.set_label("")

    def submit(self):
        if self.stage == "source":
            if not self.directory:
                return
            self.extraction_sources = (
                [self.selected["path"] if self.selected else self.directory["path"]]
                if self.current_only.get_active()
                else list(self.sources)
            )
            self.notice.set_label("Examining OS/2 files…")

            def planned(plan):
                self.notice.set_label("")
                self.show_options(plan)

            self.run(
                lambda: self.api("plan-os2-extraction", sources=self.extraction_sources), planned
            )
        elif self.stage == "options":
            self.notice.set_label("Extracting selected files…")

            def installed(result):
                self.stage = "finished"
                self.set_title("OS/2 Extraction Complete")
                self.accept.set_label("Done")
                self.accept.set_sensitive(True)
                page = Adw.StatusPage(
                    title="OS/2 Files Extracted",
                    description=f"Copied {len(result.get('imported', []))} files into this ball’s C: drive.",
                    icon_name="emblem-ok-symbolic",
                )
                self.stack.add_named(page, "finished")
                self.stack.set_visible_child_name("finished")
                self.notice.set_label("You can return to your installer or application.")
                self.window.request({"action": "snapshot"})

            self.run(
                lambda: self.api(
                    "import-os2-support",
                    sources=self.extraction_sources,
                    categories=sorted(self.categories),
                ),
                installed,
            )
        else:
            self.close()
