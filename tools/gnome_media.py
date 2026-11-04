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
    from .gnome_components import ComponentTree
    from .gnome_files import FileList, display_path
    from .gnome_views import FormDialog, action_button
else:
    from gnome_components import ComponentTree
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
    def __init__(self, window, sources, categories=None):
        super().__init__(window, "Browse OS/2 Media", accept="Continue", width=820, height=680)
        self.sources = []
        self.directory = None
        self.selected = None
        self.plan = None
        self.stage = "source"
        self.categories = set(categories or {"cmd"})
        self.component_tree = None
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
        if self.component_tree is not None and categories is None:
            return [
                item
                for item in self.plan["files"]
                if item["source"] in self.component_tree.selected
                and not item["provided"]
                and not item["present"]
            ]
        categories = categories if categories is not None else self.categories
        categories = self.effective_categories(categories)
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

    def effective_categories(self, categories):
        result = set(categories)
        for _ in range(len((self.plan or {}).get("groups", [])) + 1):
            required = {
                item
                for group in (self.plan or {}).get("groups", [])
                if group["id"] in result
                for item in group.get("requires", [])
            }
            if required <= result:
                break
            result.update(required)
        return result

    def required_categories(self):
        return {
            item
            for group in (self.plan or {}).get("groups", [])
            if group["id"] in self.categories
            for item in group.get("requires", [])
        }

    def show_options(self, plan):
        self.plan = plan
        self.categories.update(item["id"] for item in plan.get("gnomeRequirements", []))
        self.categories = self.effective_categories(self.categories)
        self.stage = "options"
        self.set_title("Extract OS/2 Files")
        self.accept.set_label("Extract Files")
        if plan.get("tree"):
            selected = {
                item["source"]
                for item in plan["files"]
                if not item["provided"]
                and ("full" in self.categories or item["category"] in self.categories)
            }
            selected.update(
                item["source"] for item in plan["files"] if item["present"] and not item["provided"]
            )
            self.component_tree = ComponentTree(
                plan["tree"], selected, changed=self.update_count, setting=self.open_setting
            )
            container = Gtk.Box(
                orientation=Gtk.Orientation.VERTICAL,
                spacing=10,
                vexpand=True,
                margin_top=12,
                margin_bottom=12,
                margin_start=16,
                margin_end=16,
            )
            container.append(
                Gtk.Label(
                    label="Select whole groups, expand them to choose individual files, or follow a provider link to its 2ine settings. Existing files are preserved.",
                    wrap=True,
                    xalign=0,
                )
            )
            container.append(self.component_tree)
            container.append(action_button("Back to Media", self.back))
            old = self.stack.get_child_by_name("options")
            if old:
                self.stack.remove(old)
            self.stack.add_named(container, "options")
            self.stack.set_visible_child_name("options")
            self.update_count()
            return
        page = Adw.PreferencesPage()
        group = Adw.PreferencesGroup(
            title="Choose What to Extract",
            description="Files are copied into their C: folders. Existing files are kept.",
        )
        page.add(group)
        self.switches = {}
        definitions = plan.get("groups") or [
            {"id": key, "title": title, "detail": detail, "requires": []}
            for key, title, detail in CATEGORIES
        ]
        definitions = sorted(definitions, key=lambda item: item["id"] == "full")
        requirements = {item["id"]: item for item in plan.get("gnomeRequirements", [])}
        for definition in definitions:
            key = definition["id"]
            count = len(self.selected_files({key}))
            present = sum(
                not item["provided"] and item["present"] and item["category"] == key
                for item in plan["files"]
            )
            if present and not count:
                self.categories.add(key)
            status = (
                f"{present} installed; {count} files to add."
                if present
                else f"{count} files available."
                if count
                else "Not on this media."
            )
            required_by = requirements.get(key, {}).get("requiredBy", [])
            if required_by:
                status += (
                    " Required by "
                    + ", ".join(path.rsplit("\\", 1)[-1] for path in required_by)
                    + "."
                )
            row = Adw.SwitchRow(
                title=definition["title"], subtitle=definition["detail"] + " " + status
            )
            row.set_active(key in self.categories or "full" in self.categories)
            row.set_sensitive(
                key not in self.required_categories()
                and (key == "full" or "full" not in self.categories)
            )
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
        self.categories = self.effective_categories(self.categories)
        self.updating_categories = True
        for category, row in self.switches.items():
            row.set_active(category in self.categories or "full" in self.categories)
            row.set_sensitive(
                category not in self.required_categories()
                and (category == "full" or "full" not in self.categories)
            )
        self.updating_categories = False
        self.update_count()

    def open_setting(self, setting):
        if setting not in ("graphics.opengl", "graphics", "printing"):
            return
        self.window.os2_component_dialog = self
        self.close()
        self.window.show_page("settings")
        self.window.configuration_stack.set_visible_child_name(
            "printing" if setting == "printing" else "display"
        )

    def resume_components(self, category):
        if self.component_tree is not None:
            provider = self.window.snapshot["manifest"].get("system", {}).get("opengl", "mesa")
            for node in self.component_tree.nodes.values():
                if node.get("setting") == "graphics.opengl":
                    node["selection"] = provider
                if node.get("provider", {}).get("setting") == "graphics.opengl":
                    node["provider"]["selection"] = provider
            self.component_tree.refresh()
            self.component_tree.focus_group(category)
        self.present(self.window)

    def update_count(self):
        count = len(self.selected_files())
        if self.component_tree is not None:
            self.accept.set_label(f"Install {count} Files" if count else "Apply Components")
            self.accept.set_sensitive(bool(self.component_tree.selected))
            return
        self.accept.set_label(
            f"Extract {count} File{'s' if count != 1 else ''}" if count else "Apply Components"
        )
        self.accept.set_sensitive(
            any(
                not item["provided"]
                and ("full" in self.categories or item["category"] in self.categories)
                for item in (self.plan or {}).get("files", [])
            )
        )

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

            def plan_components():
                plan = self.api("plan-os2-extraction", sources=self.extraction_sources)
                audit = self.api("audit-dependencies", build=str(self.window.build), resolve=False)
                plan["gnomeRequirements"] = audit.get("componentGroups", [])
                return plan

            self.run(plan_components, planned)
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

            selection = {}
            if self.component_tree is not None:
                selection = {
                    "fileSelection": sorted(self.component_tree.selected),
                    "configureComponents": self.component_tree.configured_components(),
                }
            self.run(
                lambda: self.api(
                    "import-os2-support",
                    sources=self.extraction_sources,
                    categories=sorted(self.categories),
                    **selection,
                ),
                installed,
            )
        else:
            self.close()
