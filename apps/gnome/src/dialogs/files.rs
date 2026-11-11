// Public domain. No rights reserved. Wholly AI-authored.
//! Host file chooser helpers built on `GtkFileDialog`.
//!
//! Each returns None when the user cancels; other failures are shown as a toast.

use std::path::PathBuf;

use gtk::prelude::*;
use gtk::{gio, glib};

use crate::window::BallWindow;

fn report(window: &BallWindow, error: &glib::Error) {
    if !error.matches(gtk::DialogError::Dismissed) && !error.matches(gtk::DialogError::Cancelled) {
        window.toast(error.message());
    }
}

fn dialog(title: &str, initial: Option<&std::path::Path>) -> gtk::FileDialog {
    let dialog = gtk::FileDialog::builder().title(title).modal(true).build();
    if let Some(initial) = initial {
        if initial.is_dir() {
            dialog.set_initial_folder(Some(&gio::File::for_path(initial)));
        } else {
            dialog.set_initial_file(Some(&gio::File::for_path(initial)));
        }
    }
    dialog
}

/// Choose one existing file.
pub async fn open_file(
    window: &BallWindow,
    title: &str,
    filters: Option<&gio::ListModel>,
) -> Option<PathBuf> {
    let dialog = dialog(title, None);
    dialog.set_filters(filters);
    match dialog.open_future(Some(window)).await {
        Ok(file) => file.path(),
        Err(error) => {
            report(window, &error);
            None
        }
    }
}

/// Choose one folder, optionally starting in `initial`.
pub async fn select_folder(
    window: &BallWindow,
    title: &str,
    initial: Option<&std::path::Path>,
) -> Option<PathBuf> {
    match dialog(title, initial)
        .select_folder_future(Some(window))
        .await
    {
        Ok(file) => file.path(),
        Err(error) => {
            report(window, &error);
            None
        }
    }
}

/// Choose a destination for a new file.
pub async fn save_file(window: &BallWindow, title: &str, initial_name: &str) -> Option<PathBuf> {
    let dialog = dialog(title, None);
    dialog.set_initial_name(Some(initial_name));
    match dialog.save_future(Some(window)).await {
        Ok(file) => file.path(),
        Err(error) => {
            report(window, &error);
            None
        }
    }
}

/// Choose several files or folders available on this computer.
pub async fn open_many(window: &BallWindow, title: &str, folders: bool) -> Option<Vec<PathBuf>> {
    let dialog = dialog(title, None);
    let result = if folders {
        dialog.select_multiple_folders_future(Some(window)).await
    } else {
        dialog.open_multiple_future(Some(window)).await
    };
    match result {
        Ok(files) => {
            let paths: Option<Vec<PathBuf>> = files
                .iter::<gio::File>()
                .map(|file| file.ok().and_then(|file| file.path()))
                .collect();
            if paths.is_none() {
                window.report_error("Choose files available on this computer.");
            }
            paths
        }
        Err(error) => {
            report(window, &error);
            None
        }
    }
}

/// A file filter list for a set of glob patterns.
pub fn filters(name: &str, patterns: &[&str]) -> gio::ListStore {
    let filter = gtk::FileFilter::new();
    filter.set_name(Some(name));
    for pattern in patterns {
        filter.add_pattern(pattern);
    }
    let store = gio::ListStore::new::<gtk::FileFilter>();
    store.append(&filter);
    store
}
