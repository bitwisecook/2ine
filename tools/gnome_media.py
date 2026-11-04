# Public domain. No rights reserved. Wholly AI-authored.
"""Inline OS/2 component media browsing and independently owned background jobs."""

import uuid

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
try:
    from gi.repository import Adw, GLib, Gtk
except ImportError as error:
    raise SystemExit("GTK 4 and libadwaita are required: " + str(error)) from error

if __package__:
    from . import gnome_component_jobs, gnome_media_preferences
    from .gnome_components import ComponentTree
    from .gnome_files import FileList, display_path
    from .gnome_views import action_button
else:
    import gnome_component_jobs
    import gnome_media_preferences
    from gnome_components import ComponentTree
    from gnome_files import FileList, display_path
    from gnome_views import action_button


class OS2ComponentsPane(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12, vexpand=True)
        self.window, self.ball, self.build = window, window.ball, window.build
        self.sources, self.validation, self.directory = [], None, None
        self.focus_category = None
        self.restore_selection = None
        self.opening_sources = None
        self.queued_sources = None
        self.selected, self.plan, self.component_tree = None, None, None
        self.extraction_sources = []
        self.categories = {"cmd"}
        self.working, self.polling = False, False
        self.job = None
        self.append(Gtk.Label(label="OS/2 System Components", xalign=0, css_classes=["title-1"]))
        self.error = Adw.Banner(use_markup=False, revealed=False)
        self.append(self.error)
        bar = Adw.WrapBox(child_spacing=6, line_spacing=6)
        bar.append(action_button("Choose ISO or Archive…", self.choose_media))
        bar.append(action_button("Choose Folder…", self.choose_folder))
        self.up = action_button("Parent Folder", self.go_up, "go-up-symbolic")
        bar.append(self.up)
        bar.append(action_button("Check and Restore Libraries…", window.audit_dependencies))
        self.append(bar)
        self.address = Gtk.Label(xalign=0, wrap=True, selectable=True)
        self.append(self.address)
        self.relevant_only = Gtk.CheckButton(label="Relevant Files Only", active=True)
        self.relevant_only.connect("toggled", lambda *_: self.files.refresh())
        self.append(self.relevant_only)
        self.files = FileList(selected=self.select, activated=self.activate, accept=self.relevant)
        self.files.set_size_request(-1, 150)
        self.append(self.files)
        self.current_only = Gtk.CheckButton(label="Search only the current folder or selected item")
        self.append(self.current_only)
        self.scan = action_button("Browse Components", self.plan_sources)
        self.append(self.scan)
        self.tree_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, vexpand=True)
        self.append(self.tree_box)
        controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        self.accept = action_button("Install Selected", self.install_selected)
        self.accept.add_css_class("suggested-action")
        controls.append(self.accept)
        self.remove = action_button("Remove Selected", self.remove_selected)
        controls.append(self.remove)
        self.refresh = action_button("Refresh Installed Status", self.refresh_state)
        controls.append(self.refresh)
        self.cancel = action_button("Cancel Job", self.cancel_job)
        controls.append(self.cancel)
        self.append(controls)
        status = Gtk.Box(spacing=8)
        self.progress = Gtk.Spinner()
        status.append(self.progress)
        self.notice = Gtk.Label(
            label="Choose original OS/2 media to browse components.",
            xalign=0,
            wrap=True,
            selectable=True,
        )
        status.append(self.notice)
        self.append(status)
        self.connect("map", lambda *_: GLib.idle_add(self.shown))
        self.update_count()

    def shown(self):
        if self.plan:
            self.refresh_state()
        elif not self.sources and not self.working and self.window.ball == self.ball:
            sources = gnome_media_preferences.preferred(
                self.window.snapshot, self.window.app_preferences
            )
            if sources:
                self.open_sources(sources)

    def api(self, action, **fields):
        return self.window.component_api(
            action, ball=str(self.ball), build=str(self.build), **fields
        )

    def pending(self, active):
        self.working = active
        self.progress.set_spinning(active or bool(self.job and self.job.get("active")))
        self.scan.set_sensitive(not active and bool(self.sources))
        self.update_count()

    def run(self, work, completed):
        if self.working:
            return False
        self.error.set_revealed(False)
        self.pending(True)

        def finished(result):
            self.pending(False)
            completed(result)

        self.window.component_work(work, finished, self.error_message)
        return True

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
        if self.working:
            self.queued_sources = list(sources)
            return
        if self.window.component_active(self.ball):
            self.error_message(
                "Wait or cancel this ball’s component job before choosing different media."
            )
            return
        self.opening_sources = list(sources)
        self.notice.set_label("Looking for ISO images…")

        def loaded(payload):
            if self.queued_sources is not None:
                queued, self.queued_sources = self.queued_sources, None
                self.open_sources(queued)
                return
            result, self.validation = payload
            self.opening_sources = None
            if self.validation.get("complete"):
                self.window.remember_os2_media(self.validation)
            elif list(sources) == gnome_media_preferences.sources(
                self.window.app_preferences.get("lastOS2MediaSources")
            ):
                self.window.forget_os2_media()
            if self.sources != result["sources"]:
                self.component_tree = None
                self.restore_selection = None
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
            if not self.validation.get("complete"):
                self.notice.set_label(
                    "This source could not be verified as OS/2 installation media. Choose another ISO, folder or image set, or browse this source manually."
                )
            if self.validation.get("complete"):
                self.plan_sources()

        def open_verified():
            validation = self.api("validate-os2-media", sources=list(sources))
            opened = self.api(
                "open-os2-source",
                sources=validation["sources"] if validation.get("complete") else list(sources),
            )
            return opened, validation

        self.run(open_verified, loaded)

    def error_message(self, error):
        if self.opening_sources == gnome_media_preferences.sources(
            self.window.app_preferences.get("lastOS2MediaSources")
        ):
            self.window.forget_os2_media()
        self.opening_sources = None
        self.pending(False)
        if self.queued_sources is not None:
            queued, self.queued_sources = self.queued_sources, None
            self.open_sources(queued)
            return
        self.error.set_title(str(error))
        self.error.set_revealed(True)

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

        self.window.initial_os2_picker(picker)
        picker.open_multiple(self.window, None, chosen)

    def choose_folder(self):
        picker = Gtk.FileDialog(title="Choose OS/2 Media Folder")

        def chosen(picker, result):
            try:
                self.open_sources([picker.select_folder_finish(result).get_path()])
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.error_message(error)

        self.window.initial_os2_picker(picker, folder=True)
        picker.select_folder(self.window, None, chosen)

    def plan_sources(self):
        if not self.directory or self.working or self.window.component_active(self.ball):
            return
        self.extraction_sources = (
            [self.selected["path"] if self.selected else self.directory["path"]]
            if self.current_only.get_active()
            else list(self.sources)
        )
        self.notice.set_label("Examining OS/2 files… You can continue using other pages or balls.")
        self.run(
            lambda: self.api("plan-os2-extraction", sources=self.extraction_sources),
            self.show_options,
        )

    def show_options(self, plan):
        previous = self.restore_selection
        if previous is None and self.component_tree:
            previous = set(self.component_tree.selected)
        self.restore_selection = None
        self.plan = plan
        while (child := self.tree_box.get_first_child()) is not None:
            self.tree_box.remove(child)
        selected = (
            previous
            if previous is not None
            else {
                item["source"]
                for item in plan["files"]
                if not item["provided"] and (item["category"] in self.categories or item["present"])
            }
        )
        self.component_tree = ComponentTree(
            plan["tree"], selected, changed=self.update_count, setting=self.open_setting
        )
        self.tree_box.append(self.component_tree)
        if self.focus_category:
            self.component_tree.select_group(self.focus_category)
        self.notice.set_label(
            "Select files to install or remove. Removal preserves modified and unowned files."
        )
        self.update_count()
        self.refresh_state()

    def open_setting(self, setting):
        self.window.show_component_setting(setting)

    def resume_components(self, category):
        self.focus_category = category
        if self.plan:
            self.restore_selection = set(self.component_tree.selected)
            self.plan_sources()
        elif self.sources:
            self.plan_sources()

    def update_count(self):
        active = self.window.component_active(self.ball) or bool(
            self.window.sessions.active(self.ball)
        )
        selected = set(self.component_tree.selected) if self.component_tree else set()
        files = [file for file in (self.plan or {}).get("files", []) if file["source"] in selected]
        count = sum(not file.get("present") for file in files)
        removable = sum(file.get("removableCount", 0) for file in files)
        self.accept.set_label(f"Install {count} Files" if count else "Apply Selected Components")
        self.accept.set_sensitive(bool(selected) and not self.working and not active)
        self.remove.set_label(f"Remove {removable} Files" if removable else "Remove Selected")
        self.remove.set_sensitive(bool(removable) and not self.working and not active)
        self.refresh.set_sensitive(bool(self.plan) and not self.polling)
        self.cancel.set_sensitive(active)
        if self.component_tree:
            self.component_tree.table.set_sensitive(True)

    def install_selected(self):
        if not self.component_tree:
            return
        self.start_job(
            "import-os2-support",
            sources=list(self.extraction_sources),
            fileSelection=sorted(self.component_tree.selected),
            categories=sorted(self.categories),
            configureComponents=self.component_tree.configured_components(),
        )

    def remove_selected(self):
        if not self.component_tree:
            return
        selected = sorted(
            file["source"]
            for file in self.plan["files"]
            if file["source"] in self.component_tree.selected and file.get("removable")
        )
        if selected:
            self.start_job(
                "remove-os2-components",
                sources=list(self.extraction_sources),
                fileSelection=selected,
            )

    def start_job(self, action, **fields):
        if self.working or self.window.component_active(self.ball):
            return
        if self.window.sessions.active(self.ball) or (
            self.window.ball == self.ball and self.window.operation_busy
        ):
            self.error_message("Wait for this ball’s current operation to finish.")
            return
        operation_id = uuid.uuid4().hex
        self.job = {
            "id": operation_id,
            "active": True,
            "action": action,
            "sources": set(fields.get("fileSelection", [])),
            "paths": set(fields.get("paths", [])),
            "progress": {},
            "syncedPaths": set(),
        }
        self.window.component_jobs[str(self.ball)] = self.job
        self.window.set_busy(False)
        self.window.update_component_indicators()
        self.notice.set_label(
            "Installing components…"
            if action == "import-os2-support"
            else "Removing unchanged owned components…"
        )
        self.progress.set_spinning(True)
        self.update_count()
        self.update_row_activity()
        GLib.timeout_add(300, self.poll_job)

        def completed(result):
            self.job["active"] = False
            self.job["result"] = result
            if action == "remove-os2-components":
                self.notice.set_label(
                    f"Removed {len(result.get('removedPaths', []))} unchanged owned files; preserved {len(result.get('preserved', []))} other files."
                )
            else:
                self.notice.set_label(
                    f"Installed {len(result.get('imported', []))} files. Existing files were preserved."
                )
            self.progress.set_spinning(False)
            self.window.set_busy(False)
            self.window.update_component_indicators()
            self.update_row_activity()
            self.refresh_state()
            self.refresh_ball_summary()
            self.window.toast("OS/2 component job finished for " + self.ball.name)

        def failed(error):
            self.job["active"] = False
            self.progress.set_spinning(False)
            self.window.set_busy(False)
            self.window.update_component_indicators()
            self.update_row_activity()
            self.error_message(error)
            self.refresh_state()
            self.refresh_ball_summary()

        self.window.component_work(
            lambda: self.api(action, operationID=operation_id, **fields), completed, failed
        )

    def poll_job(self):
        if self.window.component_closed:
            return GLib.SOURCE_REMOVE
        if not self.job or not self.job.get("active"):
            return GLib.SOURCE_REMOVE
        if not self.polling:
            self.polling = True
            job = self.job
            progress = job.get("progress", {})
            paths = [
                path
                for path in progress.get("completedPaths", [])
                if path not in job["syncedPaths"]
            ]
            paths.extend(progress.get("activePaths", []))
            source_ids = set(progress.get("activeSources", []))
            changed_paths = {path.casefold() for path in paths}
            for file in (self.plan or {}).get("files", []):
                if any(
                    path.casefold() in changed_paths
                    for path in [file["path"], *file.get("actualPaths", [])]
                ):
                    source_ids.add(file["source"])
            selection = gnome_component_jobs.state_selection(
                (self.plan or {}).get("files", []), source_ids
            )
            self.window.component_work(
                lambda: self.api(
                    "os2-component-state", selection=selection, paths=[], operationID=job["id"]
                ),
                lambda result: self.progress_received(result, job),
                self.poll_failed,
                monitor=True,
            )
        return GLib.SOURCE_CONTINUE

    def poll_failed(self, _error):
        self.polling = False

    def progress_received(self, progress, job):
        self.polling = False
        if self.job is not job:
            return
        if not job.get("active"):
            self.refresh_state()
            return
        self.apply_file_state(progress)
        self.job["syncedPaths"].update(entry["path"] for entry in progress.get("files", []))
        progress = progress.get("progress") or {}
        self.job["progress"] = progress
        self.window.update_component_indicators()
        self.notice.set_label(progress.get("detail") or progress.get("phase") or "Working…")
        self.update_row_activity()

    def update_row_activity(self):
        if self.component_tree:
            job = self.job if self.job and self.job.get("active") else None
            self.component_tree.active_ids = gnome_component_jobs.activity_ids(
                self.plan["tree"], job
            )
            self.component_tree.refresh_cells()
        self.update_count()

    def cancel_job(self):
        if self.job and self.job.get("active"):
            self.window.component_work(
                lambda: self.api("cancel-os2-component-operation", operationID=self.job["id"]),
                lambda _result: self.notice.set_label(
                    "Cancellation requested; finishing the current file safely…"
                ),
                self.error_message,
                monitor=True,
            )

    def refresh_ball_summary(self):
        def received(snapshot):
            if self.window.ball == self.ball:
                snapshot["gnomeRuntimeBuild"] = str(self.build)
                self.window.apply_snapshot(snapshot)

        self.window.component_work(
            lambda: self.api("snapshot"), received, lambda _error: None, monitor=True
        )

    def refresh_state(self):
        if not self.plan or self.polling:
            return
        self.polling = True
        selection = gnome_component_jobs.state_selection(self.plan["files"])

        def received(result):
            self.polling = False
            self.apply_file_state(result)

        self.window.component_work(
            lambda: self.api("os2-component-state", selection=selection, paths=[]),
            received,
            self.poll_failed,
            monitor=True,
        )

    def apply_file_state(self, result):
        records = gnome_component_jobs.source_states(result.get("files", []))
        for file in self.plan["files"]:
            entry = records.get(file["source"])
            if entry:
                file.update(entry)
        if self.component_tree:
            for node in self.component_tree.nodes.values():
                entry = records.get(node.get("source")) if node.get("kind") == "file" else None
                if entry:
                    node.update(entry)
            self.component_tree.refresh_cells()
        self.update_count()
