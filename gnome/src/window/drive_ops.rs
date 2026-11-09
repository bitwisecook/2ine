// Public domain. No rights reserved. Wholly AI-authored.
//! Drive and media operations reached from menus, shortcuts and other sections.

use std::path::Path;

use adw::subclass::prelude::*;
use gtk::glib;
use gtk::prelude::*;

use super::BallWindow;
use crate::dialogs::files;
use crate::pages::drives::{DrivesPage, dialogs};

impl BallWindow {
    /// Add Drive: map a folder or allocate a CD, floppy or disk device.
    pub fn map_dialog(&self, letter: Option<&str>) {
        dialogs::media_dialog(self, letter);
    }

    /// Create a floppy or disk image, optionally for an existing device letter.
    pub fn create_image_dialog(&self, floppy: bool, letter: Option<&str>) {
        dialogs::create_image_dialog(self, floppy, letter);
    }

    /// Read-only and lock settings for a removable device.
    pub fn device_settings(&self, letter: &str) {
        dialogs::device_settings_dialog(self, letter);
    }

    /// Choose an image file and attach it to a device.
    pub fn attach_image(&self, letter: &str) {
        let window = self.clone();
        let letter = letter.to_owned();
        glib::spawn_future_local(async move {
            if let Some(path) = files::open_file(&window, "Choose Disk Image", None).await {
                window.media_command(vec![
                    "map-image".into(),
                    letter,
                    path.to_string_lossy().into_owned(),
                ]);
            }
        });
    }

    /// Show the Drives section browsing a drive's root.
    pub fn browse_drive(&self, letter: &str) {
        self.show_page("drives");
        if let Some(browser) = self.drives_page().and_then(|page| page.browser()) {
            browser.load(&format!("{letter}:"));
        }
    }

    /// Copy host files into the folder shown in the Drives browser.
    pub fn import_files(&self) {
        self.show_page("drives");
        if let Some(browser) = self.drives_page().and_then(|page| page.browser()) {
            if browser.can_import() {
                browser.import_files(false);
            } else {
                self.toast("Choose a writable folder in Drives to import files.");
            }
        }
    }

    /// The selected ball's Drives section.
    pub fn drives_page(&self) -> Option<DrivesPage> {
        self.page("drives").and_downcast()
    }

    /// The guest folder last shown in the Drives browser for `ball`.
    pub fn browse_location(&self, ball: &Path) -> Option<String> {
        self.imp().browse_locations.borrow().get(ball).cloned()
    }

    /// Remember the guest folder shown in the Drives browser for `ball`.
    pub fn set_browse_location(&self, ball: &Path, path: &str) {
        self.imp()
            .browse_locations
            .borrow_mut()
            .insert(ball.to_path_buf(), path.to_owned());
    }
}
