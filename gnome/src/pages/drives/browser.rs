// Public domain. No rights reserved. Wholly AI-authored.
//! Browse canonical guest paths, including image members, through the engine.
//!
//! The browser lists one guest folder at a time with lazily expanded
//! subfolders. In the Drives section it also imports host files, accepts
//! drops and sets a drive's default folder; in the guest picker it chooses a
//! program or folder. Disconnected folders and ejected devices show a status
//! page with the action that makes them available again.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::PathBuf;
use std::rc::Rc;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, gdk, gio, glib};
use serde_json::json;

use super::entry::{self, Entry, Listing};
use super::files::{FileList, FileListDelegate, dropped_paths, popup_menu};
use super::helpers::{DriveState, drive_source, drive_state, media_writable};
use super::lazy::FolderChildren;
use crate::dialogs::files;
use crate::engine::{self, Error};
use crate::model::{Drive, display_path};
use crate::window::program_ops::ProgramAction;
use crate::window::{BallWindow, Work};

/// A callback receiving the chosen guest path.
pub type Choose = Rc<dyn Fn(String)>;

/// How a [`GuestBrowser`] is used.
#[derive(Default)]
pub struct Options {
    /// The guest folder (or file, to open its folder) shown first.
    pub path: String,
    /// Choose folders instead of programs.
    pub directories_only: bool,
    /// Restrict browsing to one drive letter.
    pub required_drive: Option<String>,
    /// Called with the chosen path; enables the Choose button.
    pub choose: Option<Choose>,
    /// Offer importing host files, drops and Use as Default Folder.
    pub allow_import: bool,
    pub show_drive_picker: bool,
}

mod imp {
    use super::{
        Cell, Choose, CompositeTemplate, DriveState, Entry, FileList, FolderChildren, HashMap,
        Listing, PathBuf, Rc, RefCell, glib,
    };
    use crate::window::BallWindow;
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/drives-browser.ui")]
    pub struct GuestBrowser {
        #[template_child]
        pub drives: TemplateChild<gtk::DropDown>,
        #[template_child]
        pub up: TemplateChild<gtk::Button>,
        #[template_child]
        pub address: TemplateChild<gtk::Entry>,
        #[template_child]
        pub status: TemplateChild<gtk::Label>,
        #[template_child]
        pub loading: TemplateChild<adw::Spinner>,
        #[template_child]
        pub relevant_only: TemplateChild<gtk::CheckButton>,
        #[template_child]
        pub files: TemplateChild<FileList>,
        #[template_child]
        pub empty_state: TemplateChild<adw::StatusPage>,
        #[template_child]
        pub empty_action: TemplateChild<gtk::Button>,
        #[template_child]
        pub controls: TemplateChild<adw::WrapBox>,
        #[template_child]
        pub copy_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub folder_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub default_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub choose_button: TemplateChild<gtk::Button>,

        pub window: glib::WeakRef<BallWindow>,
        /// The ball this browser lists; requests stop when another ball is selected.
        pub ball: RefCell<Option<PathBuf>>,
        pub directory: RefCell<Option<Rc<Listing>>>,
        pub selection: RefCell<Option<Rc<Entry>>>,
        /// Identifies the latest folder request; older results are discarded.
        pub serial: Cell<u64>,
        pub choose: RefCell<Option<Choose>>,
        pub directories_only: Cell<bool>,
        pub allow_import: Cell<bool>,
        pub required_drive: RefCell<Option<String>>,
        /// The last folder shown on each drive letter.
        pub locations: RefCell<HashMap<String, String>>,
        pub folder_children: RefCell<FolderChildren>,
        pub letters: RefCell<Vec<String>>,
        pub unavailable: RefCell<Option<(String, DriveState)>>,
        pub context_entry: RefCell<Option<Rc<Entry>>>,
        pub drive_handler: RefCell<Option<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for GuestBrowser {
        const NAME: &'static str = "TwoineGuestBrowser";
        type Type = super::GuestBrowser;
        type ParentType = gtk::Box;

        fn class_init(klass: &mut Self::Class) {
            FileList::ensure_type();
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[gtk::template_callbacks]
    impl GuestBrowser {
        #[template_callback]
        fn on_go_up(&self) {
            self.obj().go_up();
        }

        #[template_callback]
        fn on_go(&self) {
            let obj = self.obj();
            obj.load(&self.address.text());
        }

        #[template_callback]
        fn on_relevant_toggled(&self) {
            let obj = self.obj();
            obj.imp().files.refresh();
            let entries = obj
                .imp()
                .directory
                .borrow()
                .as_ref()
                .map(|listing| listing.entries.clone());
            if let Some(entries) = entries {
                obj.update_empty_state(&entries);
            }
        }

        #[template_callback]
        fn on_recover(&self) {
            self.obj().recover_drive();
        }

        #[template_callback]
        fn on_import_files(&self) {
            self.obj().import_files(false);
        }

        #[template_callback]
        fn on_import_folder(&self) {
            self.obj().import_files(true);
        }

        #[template_callback]
        fn on_set_default(&self) {
            self.obj().set_default();
        }

        #[template_callback]
        fn on_confirm(&self) {
            self.obj().confirm();
        }
    }

    impl ObjectImpl for GuestBrowser {}
    impl WidgetImpl for GuestBrowser {}
    impl BoxImpl for GuestBrowser {}
}

glib::wrapper! {
    pub struct GuestBrowser(ObjectSubclass<imp::GuestBrowser>)
        @extends gtk::Box, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::Orientable;
}

/// A context menu action handler.
type BrowserAction = fn(&GuestBrowser);

/// Forwards file list events to a browser without keeping it alive.
struct Delegate(glib::WeakRef<GuestBrowser>);

impl FileListDelegate for Delegate {
    fn children(&self, entry: &Entry) -> Vec<Rc<Entry>> {
        self.0
            .upgrade()
            .map(|browser| browser.imp().folder_children.borrow().children(entry))
            .unwrap_or_default()
    }

    fn accept(&self, entry: &Entry) -> bool {
        self.0
            .upgrade()
            .is_none_or(|browser| browser.accept_entry(entry))
    }

    fn activity(&self, entry: &Entry) -> bool {
        (entry.placeholder && !entry.error)
            || self.0.upgrade().is_some_and(|browser| {
                browser
                    .imp()
                    .folder_children
                    .borrow()
                    .is_pending(&entry.path)
            })
    }

    fn selected(&self, entry: Option<Rc<Entry>>) {
        if let Some(browser) = self.0.upgrade() {
            browser.selected(entry);
        }
    }

    fn activated(&self, entry: Rc<Entry>) {
        if let Some(browser) = self.0.upgrade() {
            browser.activate(&entry);
        }
    }

    fn expanded(&self, entry: Rc<Entry>, expanded: bool) {
        if let Some(browser) = self.0.upgrade() {
            browser.folder_expanded(&entry, expanded);
        }
    }

    fn context(&self, entry: Rc<Entry>, widget: &gtk::Widget, x: f64, y: f64) {
        if let Some(browser) = self.0.upgrade() {
            browser.context_menu(entry, widget, x, y);
        }
    }

    fn dropped(&self, paths: Vec<PathBuf>, destination: &str) -> bool {
        self.0
            .upgrade()
            .is_some_and(|browser| browser.drop_into(&paths, destination))
    }
}

/// Parse a `browse` result.
fn listing(result: Result<serde_json::Value, Error>) -> Result<Listing, Error> {
    Ok(serde_json::from_value(result?)?)
}

impl GuestBrowser {
    pub fn new(window: &BallWindow, options: Options) -> Self {
        let browser: Self = glib::Object::new();
        let imp = browser.imp();
        imp.window.set(Some(window));
        imp.ball.replace(window.ball());
        imp.directories_only.set(options.directories_only);
        imp.allow_import.set(options.allow_import);
        let choosing = options.choose.is_some();
        imp.choose.replace(options.choose);
        imp.required_drive.replace(options.required_drive.clone());
        browser.set_letters(options.required_drive.map_or_else(
            || {
                window
                    .snapshot()
                    .map(|snapshot| snapshot.manifest.drives.keys().cloned().collect())
                    .unwrap_or_default()
            },
            |letter| vec![letter],
        ));
        let letter = entry::letter(&options.path);
        let index = letter.and_then(|letter| {
            imp.letters
                .borrow()
                .iter()
                .position(|known| *known == letter)
        });
        imp.drives.set_selected(
            index
                .and_then(|index| u32::try_from(index).ok())
                .unwrap_or(0),
        );
        imp.drives.set_visible(options.show_drive_picker);
        let handler = imp.drives.connect_selected_notify(glib::clone!(
            #[weak]
            browser,
            move |_| browser.drive_changed()
        ));
        imp.drive_handler.replace(Some(handler));

        imp.relevant_only
            .set_visible(choosing || options.directories_only);
        imp.files
            .setup(Rc::new(Delegate(browser.downgrade())), options.allow_import);
        for button in [&*imp.copy_button, &*imp.folder_button, &*imp.default_button] {
            button.set_visible(options.allow_import);
        }
        imp.choose_button.set_visible(choosing);
        imp.choose_button.set_label(if options.directories_only {
            "Choose Folder"
        } else {
            "Choose Program"
        });
        imp.choose_button.set_sensitive(options.directories_only);
        imp.controls.set_visible(options.allow_import || choosing);

        let actions = gio::SimpleActionGroup::new();
        let entries: [(&str, BrowserAction); 3] = [
            ("open-folder", Self::open_context_folder),
            ("show-in-files", Self::show_context_file),
            ("show-image", Self::show_context_image),
        ];
        for (name, callback) in entries {
            let action = gio::SimpleAction::new(name, None);
            action.connect_activate(glib::clone!(
                #[weak]
                browser,
                move |_, _| callback(&browser)
            ));
            actions.add_action(&action);
        }
        browser.insert_action_group("browser", Some(&actions));

        if options.allow_import {
            let target = gtk::DropTarget::new(gdk::FileList::static_type(), gdk::DragAction::COPY);
            target.connect_drop(glib::clone!(
                #[weak]
                browser,
                #[upgrade_or]
                false,
                move |_, value, _, _| browser.dropped(value)
            ));
            browser.add_controller(target);
        }
        browser.load(&options.path);
        browser
    }

    fn window(&self) -> Option<BallWindow> {
        self.imp().window.upgrade()
    }

    /// The window, when this browser still shows its window's selected ball.
    fn current_window(&self) -> Option<BallWindow> {
        self.window()
            .filter(|window| *self.imp().ball.borrow() == window.ball() && window.ball().is_some())
    }

    /// The drive letters offered by the drive selector.
    pub fn letters(&self) -> Vec<String> {
        self.imp().letters.borrow().clone()
    }

    /// The drive selected in the drive selector.
    pub fn selected_letter(&self) -> Option<String> {
        let index = usize::try_from(self.imp().drives.selected()).ok()?;
        self.imp().letters.borrow().get(index).cloned()
    }

    /// Select a drive and show its last folder, as choosing it in the drive selector does.
    pub fn select_letter(&self, letter: &str) {
        let index = self
            .imp()
            .letters
            .borrow()
            .iter()
            .position(|known| known == letter);
        if let Some(index) = index.and_then(|index| u32::try_from(index).ok())
            && self.imp().drives.selected() != index
        {
            self.imp().drives.set_selected(index);
        }
    }

    pub fn connect_drive_selected<F: Fn() + 'static>(&self, callback: F) -> glib::SignalHandlerId {
        self.imp()
            .drives
            .connect_selected_notify(move |_| callback())
    }

    /// The folder currently listed.
    pub fn directory_path(&self) -> Option<String> {
        self.imp()
            .directory
            .borrow()
            .as_ref()
            .map(|listing| listing.path.clone())
    }

    fn set_letters(&self, letters: Vec<String>) {
        let imp = self.imp();
        let labels: Vec<String> = letters.iter().map(|letter| format!("{letter}:")).collect();
        let labels: Vec<&str> = labels.iter().map(String::as_str).collect();
        imp.letters.replace(letters);
        self.without_drive_handler(|| imp.drives.set_model(Some(&gtk::StringList::new(&labels))));
    }

    fn without_drive_handler(&self, change: impl FnOnce()) {
        let imp = self.imp();
        let handler = imp.drive_handler.borrow();
        if let Some(handler) = handler.as_ref() {
            imp.drives.block_signal(handler);
        }
        change();
        if let Some(handler) = handler.as_ref() {
            imp.drives.unblock_signal(handler);
        }
    }

    fn show_letter(&self, letter: &str) {
        let imp = self.imp();
        let index = imp
            .letters
            .borrow()
            .iter()
            .position(|known| known == letter);
        if let Some(index) = index.and_then(|index| u32::try_from(index).ok())
            && imp.drives.selected() != index
        {
            self.without_drive_handler(|| imp.drives.set_selected(index));
        }
    }

    /// Refresh after the ball's snapshot changed: update the drives and reread `path`.
    pub fn snapshot_changed(&self, path: &str) {
        let Some(window) = self.current_window() else {
            return;
        };
        if self.imp().required_drive.borrow().is_none()
            && let Some(snapshot) = window.snapshot()
        {
            let letters: Vec<String> = snapshot.manifest.drives.keys().cloned().collect();
            if letters != *self.imp().letters.borrow() {
                self.set_letters(letters);
            }
        }
        self.imp().folder_children.borrow_mut().clear();
        self.load(path);
    }

    /// List a guest folder, or show why its drive is unavailable.
    pub fn load(&self, path: &str) {
        let imp = self.imp();
        let Some(window) = self.current_window() else {
            return;
        };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        if path.is_empty() {
            return;
        }
        let letter = entry::letter(path);
        if let Some(required) = imp.required_drive.borrow().as_deref()
            && let Some(letter) = letter.as_deref()
            && letter != required
        {
            imp.status
                .set_label(&format!("Choose a folder on drive {required}:"));
            return;
        }
        imp.serial.set(imp.serial.get() + 1);
        let serial = imp.serial.get();
        if let Some(letter) = letter.filter(|letter| imp.letters.borrow().contains(letter))
            && let Some(state) = drive_state(&snapshot, &letter)
        {
            self.show_unavailable(&letter, state);
            return;
        }
        imp.unavailable.replace(None);
        imp.files.set_visible(true);
        imp.controls
            .set_visible(imp.allow_import.get() || imp.choose.borrow().is_some());
        imp.loading.set_visible(true);
        imp.status.set_label("Opening folder…");
        let ball = imp.ball.borrow().clone();
        let path = path.to_owned();
        let browser = self.downgrade();
        glib::spawn_future_local(async move {
            let result = listing(
                window
                    .api("browse", json!({"ball": ball, "path": path}))
                    .await,
            );
            let Some(browser) = browser.upgrade() else {
                return;
            };
            if serial != browser.imp().serial.get() || browser.current_window().is_none() {
                return;
            }
            match result {
                Ok(listing) => browser.loaded(listing),
                Err(error) => browser.failed(&error),
            }
        });
    }

    fn loaded(&self, listing: Listing) {
        let imp = self.imp();
        let Some(window) = self.window() else { return };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        imp.loading.set_visible(false);
        let entries: Vec<Rc<Entry>> = listing.entries.iter().cloned().map(Rc::new).collect();
        imp.folder_children
            .borrow_mut()
            .complete(&listing.path, entries.clone());
        imp.address.set_text(&listing.path);
        let letter = listing.letter();
        imp.locations
            .borrow_mut()
            .insert(letter.clone(), listing.path.clone());
        self.show_letter(&letter);
        if imp.allow_import.get()
            && let Some(ball) = imp.ball.borrow().as_ref()
        {
            window.set_browse_location(ball, &listing.path);
        }
        imp.up.set_sensitive(listing.parent.is_some());
        let count = listing.entries.len();
        let default = snapshot
            .working_directories
            .get(&letter)
            .cloned()
            .unwrap_or_else(|| listing.path.clone());
        imp.status.set_label(&format!(
            "{count} item{} · Default: {default}",
            if count == 1 { "" } else { "s" }
        ));
        imp.directory.replace(Some(Rc::new(listing)));
        self.populate(&entries);
        let writable = media_writable(&snapshot, &letter);
        let running = imp
            .ball
            .borrow()
            .as_deref()
            .is_some_and(|ball| window.ball_locked(ball));
        imp.copy_button.set_sensitive(writable && !running);
        imp.folder_button.set_sensitive(writable && !running);
    }

    fn failed(&self, error: &Error) {
        let imp = self.imp();
        imp.loading.set_visible(false);
        imp.directory.replace(None);
        imp.files.set_entries(Vec::new());
        imp.status.set_label(&error.to_string());
        for button in [&*imp.copy_button, &*imp.folder_button, &*imp.default_button] {
            button.set_sensitive(false);
        }
        imp.empty_state.set_title("Drive Is Unavailable");
        imp.empty_state.set_description(Some(
            "Insert media or reconnect its folder using Drive Options.",
        ));
        imp.empty_state
            .set_icon_name(Some("dialog-warning-symbolic"));
        imp.empty_action.set_visible(false);
        imp.empty_state.set_visible(true);
    }

    fn show_unavailable(&self, letter: &str, state: DriveState) {
        let imp = self.imp();
        let Some(window) = self.window() else { return };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        imp.unavailable.replace(Some((letter.to_owned(), state)));
        imp.loading.set_visible(false);
        imp.directory.replace(None);
        imp.selection.replace(None);
        imp.files.set_entries(Vec::new());
        imp.files.set_visible(false);
        imp.address.set_text(&format!("{letter}:\\"));
        imp.up.set_sensitive(false);
        self.show_letter(letter);
        for button in [&*imp.copy_button, &*imp.folder_button, &*imp.default_button] {
            button.set_sensitive(false);
        }
        let choosing = imp.choose.borrow().is_some();
        imp.controls.set_visible(choosing);
        imp.choose_button.set_sensitive(false);
        imp.status.set_label("");
        match state {
            DriveState::Disconnected => {
                let previous = drive_source(&snapshot, letter).unwrap_or_default();
                imp.empty_state.set_icon_name(Some("folder-symbolic"));
                imp.empty_state
                    .set_title(&format!("{letter}: Is Disconnected"));
                imp.empty_state.set_description(Some(&format!(
                    "The previous folder is unavailable: {}",
                    display_path(previous)
                )));
                imp.empty_action.set_label("Choose Folder…");
                imp.empty_action
                    .set_sensitive(letter != "C" && !window.busy());
            }
            DriveState::Ejected => {
                let missing = snapshot
                    .unavailable_drives
                    .get(letter)
                    .filter(|path| !path.is_empty());
                imp.empty_state.set_icon_name(Some("media-eject-symbolic"));
                imp.empty_state.set_title(&format!("{letter}: Is Ejected"));
                imp.empty_state.set_description(Some(&missing.map_or_else(
                    || "Attach a disk image to browse this drive.".to_owned(),
                    |missing| {
                        format!(
                            "The attached image is unavailable: {}",
                            display_path(missing)
                        )
                    },
                )));
                imp.empty_action.set_label("Attach Image…");
                let locked = snapshot
                    .drive(letter)
                    .and_then(Drive::device)
                    .is_some_and(|device| device.locked);
                imp.empty_action
                    .set_sensitive(!locked && !window.operation_busy());
            }
        }
        imp.empty_action.set_visible(imp.allow_import.get());
        imp.empty_state.set_visible(true);
    }

    fn recover_drive(&self) {
        let Some(window) = self.current_window() else {
            return;
        };
        let Some((letter, state)) = self.imp().unavailable.borrow().clone() else {
            return;
        };
        match state {
            DriveState::Ejected => window.attach_image(&letter),
            DriveState::Disconnected => {
                glib::spawn_future_local(async move {
                    let title = format!("Choose Folder for {letter}:");
                    if let Some(path) = files::select_folder(&window, &title, None).await {
                        window.media_command(vec![
                            "map".into(),
                            letter,
                            path.to_string_lossy().into_owned(),
                        ]);
                    }
                });
            }
        }
    }

    /// The host image behind a device letter.
    fn image_source(&self, letter: &str) -> Option<String> {
        let snapshot = self.window()?.snapshot()?;
        matches!(snapshot.drive(letter), Some(Drive::Device(_)))
            .then(|| drive_source(&snapshot, letter))
            .flatten()
    }

    fn context_menu(&self, entry: Rc<Entry>, widget: &gtk::Widget, x: f64, y: f64) {
        if entry.placeholder {
            return;
        }
        let Some(snapshot) = self.window().and_then(|window| window.snapshot()) else {
            return;
        };
        let menu = gio::Menu::new();
        if entry.directory {
            menu.append(Some("Open Folder"), Some("browser.open-folder"));
        }
        let letter = entry::letter(&entry.path).unwrap_or_default();
        if matches!(snapshot.drive(&letter), Some(Drive::Device(_))) {
            if self.image_source(&letter).is_some() {
                menu.append(Some("Show Image"), Some("browser.show-image"));
            }
        } else if entry.host_path.is_some() {
            menu.append(Some("Show in Files"), Some("browser.show-in-files"));
        }
        self.imp().context_entry.replace(Some(entry));
        if menu.n_items() > 0 {
            popup_menu(widget, &menu, x, y);
        }
    }

    fn open_context_folder(&self) {
        let entry = self.imp().context_entry.borrow().clone();
        if let Some(entry) = entry.filter(|entry| entry.directory) {
            self.load(&entry.path);
        }
    }

    fn show_context_file(&self) {
        let entry = self.imp().context_entry.borrow().clone();
        if let Some(window) = self.window()
            && let Some(host) = entry.and_then(|entry| entry.host_path.clone())
        {
            window.reveal_path(std::path::Path::new(&host));
        }
    }

    fn show_context_image(&self) {
        let entry = self.imp().context_entry.borrow().clone();
        if let Some(window) = self.window()
            && let Some(letter) = entry.and_then(|entry| entry::letter(&entry.path))
            && let Some(source) = self.image_source(&letter)
        {
            window.reveal_path(std::path::Path::new(&source));
        }
    }

    fn folder_expanded(&self, entry: &Entry, expanded: bool) {
        if !expanded || entry.placeholder || !entry.directory {
            return;
        }
        let Some(window) = self.current_window() else {
            return;
        };
        let imp = self.imp();
        if !imp.folder_children.borrow_mut().begin(&entry.path) {
            return;
        }
        imp.files.refresh_cells();
        let ball = imp.ball.borrow().clone();
        let path = entry.path.clone();
        let browser = self.downgrade();
        glib::spawn_future_local(async move {
            let result = listing(
                window
                    .api("browse", json!({"ball": ball, "path": path}))
                    .await,
            );
            let Some(browser) = browser.upgrade() else {
                return;
            };
            let imp = browser.imp();
            match result {
                Ok(listing) => imp
                    .folder_children
                    .borrow_mut()
                    .complete(&path, listing.entries.into_iter().map(Rc::new).collect()),
                Err(error) => imp
                    .folder_children
                    .borrow_mut()
                    .fail(&path, &error.to_string()),
            }
            imp.files.update_children(&path);
        });
    }

    /// Whether a listed entry is shown under the current filter.
    fn accept_entry(&self, entry: &Entry) -> bool {
        let imp = self.imp();
        if entry.placeholder || entry.directory {
            return true;
        }
        let relevant = imp.relevant_only.is_active();
        if imp.directories_only.get() && relevant {
            return false;
        }
        imp.choose.borrow().is_none() || !relevant || entry.is_program()
    }

    fn populate(&self, entries: &[Rc<Entry>]) {
        let imp = self.imp();
        imp.default_button
            .set_sensitive(self.window().is_some_and(|window| !window.busy()));
        imp.files.set_entries(entries.to_vec());
        let entries: Vec<Entry> = entries.iter().map(|entry| (**entry).clone()).collect();
        self.update_empty_state(&entries);
    }

    fn update_empty_state(&self, entries: &[Entry]) {
        let imp = self.imp();
        if imp.unavailable.borrow().is_some() {
            return;
        }
        imp.empty_state
            .set_visible(!entries.iter().any(|entry| self.accept_entry(entry)));
        imp.empty_state.set_title(if entries.is_empty() {
            "This Folder Is Empty"
        } else {
            "No Matching Files"
        });
        imp.empty_state.set_description(None);
        imp.empty_state.set_icon_name(Some("folder-symbolic"));
        imp.empty_action.set_visible(false);
    }

    fn activate(&self, entry: &Entry) {
        if entry.placeholder {
            return;
        }
        let imp = self.imp();
        if entry.directory {
            self.load(&entry.path);
            return;
        }
        let choose = imp.choose.borrow().clone();
        match choose {
            Some(choose) if !imp.directories_only.get() => choose(entry.path.clone()),
            _ => {
                if let Some(window) = self.current_window() {
                    window.program_dialog_for(ProgramAction::Run, None, &entry.path);
                }
            }
        }
    }

    fn selected(&self, entry: Option<Rc<Entry>>) {
        let imp = self.imp();
        let entry = entry.filter(|entry| !entry.placeholder);
        if imp.choose.borrow().is_some() {
            imp.choose_button
                .set_sensitive(if imp.directories_only.get() {
                    imp.directory.borrow().is_some()
                } else {
                    entry.as_ref().is_some_and(|entry| !entry.directory)
                });
        }
        imp.selection.replace(entry);
    }

    fn confirm(&self) {
        let imp = self.imp();
        let directories_only = imp.directories_only.get();
        let selection = imp.selection.borrow().clone();
        let path = match selection {
            Some(entry) if entry.directory == directories_only => Some(entry.path.clone()),
            _ if directories_only => self.directory_path(),
            _ => None,
        };
        let choose = imp.choose.borrow().clone();
        if let Some(path) = path
            && let Some(choose) = choose
        {
            choose(path);
        }
    }

    fn drive_changed(&self) {
        let Some(letter) = self.selected_letter() else {
            return;
        };
        let path = self
            .imp()
            .locations
            .borrow()
            .get(&letter)
            .cloned()
            .unwrap_or_else(|| format!("{letter}:\\"));
        self.load(&path);
    }

    fn go_up(&self) {
        let parent = self
            .imp()
            .directory
            .borrow()
            .as_ref()
            .and_then(|listing| listing.parent.clone());
        if let Some(parent) = parent {
            self.load(&parent);
        }
    }

    /// Choose host files (or folders) and copy them into the listed folder.
    pub fn import_files(&self, folders: bool) {
        let imp = self.imp();
        let Some(window) = self.current_window() else {
            return;
        };
        let Some(destination) = self.directory_path() else {
            return;
        };
        if !imp.copy_button.is_sensitive() {
            return;
        }
        let browser = self.downgrade();
        glib::spawn_future_local(async move {
            let title = format!(
                "Import {} into {destination}",
                if folders { "Folders" } else { "Files" }
            );
            if let Some(paths) = files::open_many(&window, &title, folders).await
                && let Some(browser) = browser.upgrade()
            {
                browser.copy_files(&paths, &destination);
            }
        });
    }

    /// Whether files can be imported into the listed folder now.
    pub fn can_import(&self) -> bool {
        let imp = self.imp();
        imp.allow_import.get()
            && imp.directory.borrow().is_some()
            && imp.copy_button.is_sensitive()
            && self.window().is_some_and(|window| !window.busy())
    }

    fn drop_into(&self, paths: &[PathBuf], destination: &str) -> bool {
        if !self.can_import() {
            return false;
        }
        self.copy_files(paths, destination);
        true
    }

    fn dropped(&self, value: &glib::Value) -> bool {
        let Some(destination) = self.directory_path() else {
            return false;
        };
        match dropped_paths(value) {
            Some(paths) => self.drop_into(&paths, &destination),
            None => false,
        }
    }

    /// Copy host files into a guest folder; existing files are never replaced.
    pub fn copy_files(&self, paths: &[PathBuf], destination: &str) {
        let Some(window) = self.current_window() else {
            return;
        };
        let Some(ball) = self.imp().ball.borrow().clone() else {
            return;
        };
        if window.ball_locked(&ball) {
            window.toast("Wait for this ball’s applications or component job before changing it.");
            return;
        }
        let message = window.message(
            "import-files",
            json!({"ball": ball, "path": destination, "sources": paths}),
        );
        let destination = destination.to_owned();
        let browser = self.downgrade();
        glib::spawn_future_local(async move {
            match window
                .run(Work::default(), move || engine::request(&message))
                .await
            {
                Ok(result) => {
                    if let Some(browser) = browser.upgrade() {
                        browser.load(result["path"].as_str().unwrap_or(&destination));
                    }
                    window.toast("Files imported");
                }
                Err(error) => window.report_error(error),
            }
        });
    }

    /// Start programs on the listed folder's drive in that folder.
    fn set_default(&self) {
        if let Some(window) = self.current_window()
            && let Some(path) = self.directory_path()
        {
            window.commit(
                json!({"action": "set-working-directory", "path": path}),
                None,
                "Change Default Folder",
                false,
            );
        }
    }

    /// Update action sensitivity after the window's busy state changes.
    pub fn busy_changed(&self) {
        let imp = self.imp();
        let Some(window) = self.window() else { return };
        if imp.directory.borrow().is_some() {
            imp.default_button.set_sensitive(!window.busy());
        }
    }
}
