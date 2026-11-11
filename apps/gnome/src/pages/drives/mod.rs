// Public domain. No rights reserved. Wholly AI-authored.
//! The Drives section: mapped drives as a boxed list with the selected
//! drive's files below.
//!
//! Each drive row describes its source, access and default folder, offers
//! media and folder actions in a menu (also on right-click) and accepts host
//! files dropped onto writable drives. Selecting a row browses that drive.

pub mod browser;
pub mod dialogs;
pub mod entry;
pub mod files;
pub mod helpers;
pub mod lazy;
pub mod model;
pub mod program_choice;

use std::cell::{Cell, RefCell};
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::rc::Rc;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, gdk, gio, glib};
use serde_json::json;

use crate::dialogs::guest_picker::{self, PickerMode};
use crate::model::{Drive, DriveKind, Snapshot, display_path};
use crate::window::BallWindow;
use browser::{GuestBrowser, Options};
use files::{dropped_paths, popup_menu};
use helpers::{drive_description, drive_icon, drive_source, drive_state, drive_writable};

/// A drive menu action handler: the page, its window and the drive letter.
type DriveAction = fn(&DrivesPage, &BallWindow, &str);

pub fn page(window: &BallWindow) -> gtk::Widget {
    DrivesPage::new(window).upcast()
}

mod imp {
    use super::{
        BTreeMap, BallWindow, Cell, CompositeTemplate, GuestBrowser, Rc, RefCell, Snapshot, gio,
        glib,
    };
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/drives-page.ui")]
    pub struct DrivesPage {
        #[template_child]
        pub content: TemplateChild<gtk::Box>,
        #[template_child]
        pub list: TemplateChild<gtk::ListBox>,

        pub window: glib::WeakRef<BallWindow>,
        pub browser: RefCell<Option<GuestBrowser>>,
        /// Drive rows by letter, with their menu actions.
        pub rows: RefCell<BTreeMap<String, (adw::ActionRow, gio::SimpleActionGroup)>>,
        /// True while the list selection follows the browser's drive.
        pub syncing: Cell<bool>,
        /// The drive shown first when the ball has no remembered folder.
        pub selected_drive: RefCell<Option<String>>,
        pub snapshot: RefCell<Option<Rc<Snapshot>>>,
        pub handlers: RefCell<Vec<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for DrivesPage {
        const NAME: &'static str = "TwoineDrivesPage";
        type Type = super::DrivesPage;
        type ParentType = adw::Bin;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    impl ObjectImpl for DrivesPage {
        fn dispose(&self) {
            if let Some(window) = self.window.upgrade() {
                for handler in self.handlers.take() {
                    window.disconnect(handler);
                }
            }
        }
    }

    impl WidgetImpl for DrivesPage {}
    impl BinImpl for DrivesPage {}
}

glib::wrapper! {
    pub struct DrivesPage(ObjectSubclass<imp::DrivesPage>)
        @extends adw::Bin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

impl DrivesPage {
    pub fn new(window: &BallWindow) -> Self {
        let page: Self = glib::Object::new();
        let imp = page.imp();
        imp.window.set(Some(window));
        imp.snapshot.replace(window.snapshot());
        let browser = GuestBrowser::new(
            window,
            Options {
                path: page.initial_path(),
                allow_import: true,
                show_drive_picker: false,
                ..Options::default()
            },
        );
        imp.content.append(&browser);
        browser.connect_drive_selected(glib::clone!(
            #[weak]
            page,
            move || page.browser_drive_changed()
        ));
        imp.browser.replace(Some(browser));
        imp.list.connect_row_selected(glib::clone!(
            #[weak]
            page,
            move |_, row| page.row_selected(row)
        ));
        page.build_rows();
        page.browser_drive_changed();

        let handlers = vec![
            window.connect_snapshot_changed(glib::clone!(
                #[weak]
                page,
                move |_| page.snapshot_changed()
            )),
            window.connect_busy_notify(glib::clone!(
                #[weak]
                page,
                move |_| page.busy_changed()
            )),
            window.connect_sessions_changed(glib::clone!(
                #[weak]
                page,
                move |_| page.busy_changed()
            )),
        ];
        imp.handlers.replace(handlers);
        page
    }

    fn window(&self) -> Option<BallWindow> {
        self.imp().window.upgrade()
    }

    pub fn browser(&self) -> Option<GuestBrowser> {
        self.imp().browser.borrow().clone()
    }

    /// Show `letter` first when the ball has no remembered folder.
    pub fn set_selected_drive(&self, letter: &str) {
        self.imp().selected_drive.replace(Some(letter.to_owned()));
    }

    /// The remembered folder, else the selected or current drive, else `C:`.
    fn initial_path(&self) -> String {
        let Some(window) = self.window() else {
            return "C:".into();
        };
        let Some(snapshot) = window.snapshot() else {
            return "C:".into();
        };
        let path = window
            .ball()
            .and_then(|ball| window.browse_location(&ball))
            .unwrap_or_else(|| {
                let letter = self
                    .imp()
                    .selected_drive
                    .borrow()
                    .clone()
                    .unwrap_or_else(|| snapshot.current_drive.clone());
                format!("{letter}:")
            });
        match entry::letter(&path) {
            Some(letter) if snapshot.drive(&letter).is_some() => path,
            _ => "C:".into(),
        }
    }

    fn snapshot_changed(&self) {
        let Some(window) = self.window() else { return };
        let snapshot = window.snapshot();
        let unchanged = match (&snapshot, self.imp().snapshot.borrow().as_ref()) {
            (Some(new), Some(old)) => Rc::ptr_eq(new, old),
            _ => false,
        };
        if unchanged {
            return;
        }
        self.imp().snapshot.replace(snapshot);
        self.build_rows();
        if let Some(browser) = self.browser() {
            browser.snapshot_changed(&self.initial_path());
        }
        self.browser_drive_changed();
    }

    fn busy_changed(&self) {
        if let Some(browser) = self.browser() {
            browser.busy_changed();
        }
    }

    fn build_rows(&self) {
        let imp = self.imp();
        let Some(window) = self.window() else { return };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        imp.syncing.set(true);
        imp.list.remove_all();
        imp.rows.borrow_mut().clear();
        for letter in snapshot.manifest.drives.keys() {
            let (row, actions) = self.drive_row(&snapshot, letter);
            imp.list.append(&row);
            imp.rows.borrow_mut().insert(letter.clone(), (row, actions));
        }
        imp.syncing.set(false);
    }

    fn drive_row(
        &self,
        snapshot: &Snapshot,
        letter: &str,
    ) -> (adw::ActionRow, gio::SimpleActionGroup) {
        let source = drive_source(snapshot, letter);
        let working = snapshot
            .working_directories
            .get(letter)
            .cloned()
            .unwrap_or_else(|| format!("{letter}:\\"));
        let row = adw::ActionRow::builder()
            .title(format!(
                "{letter}: · {}",
                drive_description(snapshot, letter)
            ))
            .subtitle(format!(
                "{}\nDefault folder: {working}",
                source.map_or_else(|| "No medium attached".to_owned(), display_path)
            ))
            .subtitle_lines(2)
            .use_markup(false)
            .activatable(true)
            .build();
        row.add_prefix(&gtk::Image::from_icon_name(drive_icon(snapshot, letter)));
        if drive_state(snapshot, letter).is_some() {
            let warning = gtk::Image::builder()
                .icon_name("dialog-warning-symbolic")
                .tooltip_text("Unavailable")
                .build();
            warning.update_property(&[gtk::accessible::Property::Label("Unavailable")]);
            row.add_suffix(&warning);
        }
        let (actions, menu) = self.drive_menu(snapshot, letter);
        row.insert_action_group("drive", Some(&actions));
        let options = gtk::MenuButton::builder()
            .icon_name("view-more-symbolic")
            .menu_model(&menu)
            .tooltip_text("Drive Options")
            .valign(gtk::Align::Center)
            .css_classes(["flat"])
            .build();
        options.update_property(&[gtk::accessible::Property::Label(&format!(
            "Options for drive {letter}"
        ))]);
        let owned = letter.to_owned();
        options.set_create_popup_func(glib::clone!(
            #[weak(rename_to = page)]
            self,
            move |_| page.update_actions(&owned)
        ));
        row.add_suffix(&options);

        let owned = letter.to_owned();
        let click = gtk::GestureClick::builder()
            .button(gdk::BUTTON_SECONDARY)
            .build();
        click.connect_pressed(glib::clone!(
            #[weak(rename_to = page)]
            self,
            #[weak]
            row,
            #[strong]
            menu,
            move |_, _, x, y| page.context_menu(&owned, &row, &menu, x, y)
        ));
        row.add_controller(click);
        let owned = letter.to_owned();
        let drop = gtk::DropTarget::new(gdk::FileList::static_type(), gdk::DragAction::COPY);
        drop.connect_drop(glib::clone!(
            #[weak(rename_to = page)]
            self,
            #[upgrade_or]
            false,
            move |_, value, _, _| page.drop(&owned, value)
        ));
        row.add_controller(drop);
        (row, actions)
    }

    fn drive_menu(&self, snapshot: &Snapshot, letter: &str) -> (gio::SimpleActionGroup, gio::Menu) {
        let group = gio::SimpleActionGroup::new();
        let callbacks: [(&str, DriveAction); 9] = [
            ("change-folder", |_, window, letter| {
                window.map_dialog(Some(letter));
            }),
            ("open-folder", |_, window, letter| {
                if let Some(root) = window
                    .snapshot()
                    .and_then(|snapshot| snapshot.roots.get(letter).cloned())
                {
                    window.open_path(&root);
                }
            }),
            ("attach-image", |_, window, letter| {
                window.attach_image(letter);
            }),
            ("create-image", |_, window, letter| {
                let floppy = window
                    .snapshot()
                    .and_then(|snapshot| snapshot.drive(letter).map(Drive::kind))
                    == Some(DriveKind::Floppy);
                window.create_image_dialog(floppy, Some(letter));
            }),
            ("show-image", |_, window, letter| {
                if let Some(source) = window
                    .snapshot()
                    .and_then(|snapshot| drive_source(&snapshot, letter))
                {
                    window.reveal_path(&PathBuf::from(source));
                }
            }),
            ("eject", |_, window, letter| {
                window.media_command(vec!["eject".into(), letter.into()]);
            }),
            ("settings", |_, window, letter| {
                window.device_settings(letter);
            }),
            ("default-folder", |page, _, letter| {
                page.choose_default_folder(letter);
            }),
            ("remove", |_, window, letter| {
                window.media_command(vec!["unmap".into(), letter.into()]);
            }),
        ];
        for (name, callback) in callbacks {
            let action = gio::SimpleAction::new(name, None);
            let letter = letter.to_owned();
            action.connect_activate(glib::clone!(
                #[weak(rename_to = page)]
                self,
                move |_, _| {
                    if let Some(window) = page.window() {
                        callback(&page, &window, &letter);
                    }
                }
            ));
            group.add_action(&action);
        }
        let (menu, media, folder, last) = (
            gio::Menu::new(),
            gio::Menu::new(),
            gio::Menu::new(),
            gio::Menu::new(),
        );
        if let Some(Drive::Device(device)) = snapshot.drive(letter) {
            let label = if device.image.is_some() {
                "Replace Image…"
            } else {
                "Attach Image…"
            };
            media.append(Some(label), Some("drive.attach-image"));
            match device.device {
                DriveKind::Floppy => {
                    media.append(Some("Create Floppy Image…"), Some("drive.create-image"));
                }
                DriveKind::Disk => {
                    media.append(Some("Create Disk Image…"), Some("drive.create-image"));
                }
                DriveKind::Cdrom | DriveKind::Folder => {}
            }
            media.append(Some("Show Image"), Some("drive.show-image"));
            media.append(Some("Eject Medium"), Some("drive.eject"));
            media.append(Some("Device Settings…"), Some("drive.settings"));
        } else {
            media.append(Some("Change Folder…"), Some("drive.change-folder"));
            media.append(Some("Open Host Folder"), Some("drive.open-folder"));
        }
        folder.append(Some("Set Default Folder…"), Some("drive.default-folder"));
        last.append(Some("Remove Drive"), Some("drive.remove"));
        for section in [media, folder, last] {
            menu.append_section(None, &section);
        }
        self.update_group(snapshot, letter, &group);
        (group, menu)
    }

    fn update_actions(&self, letter: &str) {
        let Some(snapshot) = self.window().and_then(|window| window.snapshot()) else {
            return;
        };
        let group = self
            .imp()
            .rows
            .borrow()
            .get(letter)
            .map(|(_, group)| group.clone());
        if let Some(group) = group {
            self.update_group(&snapshot, letter, &group);
        }
    }

    fn update_group(&self, snapshot: &Snapshot, letter: &str, group: &gio::SimpleActionGroup) {
        let Some(window) = self.window() else { return };
        let device = snapshot.drive(letter).and_then(Drive::device);
        let available = drive_state(snapshot, letter).is_none();
        let idle = !window.operation_busy() && !window.busy();
        let locked = device.is_some_and(|device| device.locked);
        let enabled = [
            ("change-folder", letter != "C" && idle),
            (
                "open-folder",
                available && snapshot.roots.contains_key(letter),
            ),
            ("attach-image", !locked && idle),
            ("create-image", !locked && idle),
            (
                "show-image",
                device.is_some() && drive_source(snapshot, letter).is_some(),
            ),
            (
                "eject",
                device.is_some_and(|device| device.image.is_some()) && !locked && idle,
            ),
            ("settings", idle),
            ("default-folder", available && !window.busy()),
            ("remove", letter != "C" && idle && !locked),
        ];
        for (name, state) in enabled {
            if let Some(action) = group
                .lookup_action(name)
                .and_downcast::<gio::SimpleAction>()
            {
                action.set_enabled(state);
            }
        }
    }

    fn context_menu(&self, letter: &str, row: &adw::ActionRow, menu: &gio::Menu, x: f64, y: f64) {
        if self.window().is_none_or(|window| window.busy()) {
            return;
        }
        self.update_actions(letter);
        popup_menu(row.upcast_ref(), menu, x, y);
    }

    fn drop(&self, letter: &str, value: &glib::Value) -> bool {
        let Some(window) = self.window() else {
            return false;
        };
        let (Some(snapshot), Some(ball)) = (window.snapshot(), window.ball()) else {
            return false;
        };
        let Some(paths) = dropped_paths(value) else {
            return false;
        };
        if !drive_writable(&snapshot, letter) || window.busy() || window.ball_locked(&ball) {
            return false;
        }
        if let Some(browser) = self.browser() {
            browser.copy_files(&paths, &format!("{letter}:\\"));
        }
        true
    }

    fn choose_default_folder(&self, letter: &str) {
        let Some(window) = self.window() else { return };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        let initial = snapshot
            .working_directories
            .get(letter)
            .cloned()
            .unwrap_or_else(|| format!("{letter}:\\"));
        let letter = letter.to_owned();
        glib::spawn_future_local(async move {
            if let Some(path) = guest_picker::pick(
                &window,
                "Choose Working Folder",
                PickerMode::Folder,
                Some(initial),
                Some(letter),
            )
            .await
            {
                window.commit(
                    json!({"action": "set-working-directory", "path": path}),
                    None,
                    "Change Default Folder",
                    false,
                );
            }
        });
    }

    fn row_selected(&self, row: Option<&gtk::ListBoxRow>) {
        let imp = self.imp();
        let Some(row) = row else { return };
        if imp.syncing.get() {
            return;
        }
        let letter = imp
            .rows
            .borrow()
            .iter()
            .find(|(_, (known, _))| known.upcast_ref::<gtk::ListBoxRow>() == row)
            .map(|(letter, _)| letter.clone());
        let Some(letter) = letter else { return };
        if let Some(browser) = self.browser() {
            browser.select_letter(&letter);
        }
        imp.selected_drive.replace(Some(letter));
    }

    fn browser_drive_changed(&self) {
        let imp = self.imp();
        let Some(letter) = self.browser().and_then(|browser| browser.selected_letter()) else {
            return;
        };
        let row = imp.rows.borrow().get(&letter).map(|(row, _)| row.clone());
        imp.selected_drive.replace(Some(letter));
        if let Some(row) = row
            && imp.list.selected_row().as_ref() != Some(row.upcast_ref())
        {
            imp.syncing.set(true);
            imp.list.select_row(Some(&row));
            imp.syncing.set(false);
        }
    }
}
