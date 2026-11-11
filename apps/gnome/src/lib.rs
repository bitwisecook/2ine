// Public domain. No rights reserved. Wholly AI-authored.
//! Native GNOME manager for 2ine balls.
//!
//! The shared C engine owns ball persistence, drive translation, runtime
//! selection, launch plans and installer receipts. This client calls it on a
//! serialized worker thread and updates GTK only on the main loop. No shell
//! interprets guest arguments.

pub mod application;
pub mod config;
pub mod dialogs;
pub mod engine;
pub mod host_dialog;
pub mod lifecycle;
pub mod media_menu;
pub mod model;
pub mod navigation;
pub mod pages;
pub mod preferences;
pub mod sessions;
pub mod window;

use gtk::prelude::*;
use gtk::{gio, glib};

/// Register the bundled UI resources. Safe to call more than once.
pub fn register_resources() {
    static REGISTERED: std::sync::Once = std::sync::Once::new();
    REGISTERED.call_once(|| {
        gio::resources_register_include!("twoine.gresource").expect("bundled resources register");
    });
}

/// Run the application with the process arguments.
pub fn run() -> glib::ExitCode {
    register_resources();
    application::Application::new().run()
}
