// Public domain. No rights reserved. Wholly AI-authored.
//! The `AdwApplication`: command line, file opening and application actions.

use std::cell::RefCell;
use std::ops::ControlFlow;
use std::path::PathBuf;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};

use crate::config;
use crate::window::BallWindow;

mod imp {
    use super::*;

    #[derive(Default)]
    pub struct Application {
        pub build: RefCell<Option<PathBuf>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for Application {
        const NAME: &'static str = "TwoineBallApplication";
        type Type = super::Application;
        type ParentType = adw::Application;
    }

    impl ObjectImpl for Application {
        fn constructed(&self) {
            self.parent_constructed();
            let application = self.obj();
            application.add_main_option(
                "build",
                glib::Char::from(b'b'),
                glib::OptionFlags::NONE,
                glib::OptionArg::Filename,
                "Runtime build directory",
                Some("DIRECTORY"),
            );
            let quit = gio::ActionEntry::builder("quit")
                .activate(|application: &super::Application, _, _| {
                    match application.active_window() {
                        Some(window) => window.close(),
                        None => application.quit(),
                    }
                })
                .build();
            application.add_action_entries([quit]);
            application.set_accels_for_action("app.quit", &["<Control>q"]);
        }
    }

    impl ApplicationImpl for Application {
        fn handle_local_options(&self, options: &glib::VariantDict) -> ControlFlow<glib::ExitCode> {
            if let Some(build) = options.lookup::<PathBuf>("build").ok().flatten() {
                self.build.replace(Some(build));
            }
            ControlFlow::Continue(())
        }

        fn startup(&self) {
            self.parent_startup();
            gtk::Window::set_default_icon_name(config::APP_ID);
            if let Some(display) = gtk::gdk::Display::default() {
                gtk::IconTheme::for_display(&display)
                    .add_resource_path(&format!("{}/icons", config::RESOURCE_PATH));
            }
        }

        fn activate(&self) {
            self.obj().window(None).present();
        }

        fn open(&self, files: &[gio::File], _hint: &str) {
            let window = self.obj().window(None);
            for path in files.iter().filter_map(gio::File::path) {
                window.remember(path);
            }
            window.present();
        }
    }

    impl GtkApplicationImpl for Application {}
    impl AdwApplicationImpl for Application {}
}

glib::wrapper! {
    pub struct Application(ObjectSubclass<imp::Application>)
        @extends adw::Application, gtk::Application, gio::Application,
        @implements gio::ActionGroup, gio::ActionMap;
}

impl Application {
    pub fn new() -> Self {
        glib::Object::builder()
            .property("application-id", config::APP_ID)
            .property("flags", gio::ApplicationFlags::HANDLES_OPEN)
            .property("resource-base-path", config::RESOURCE_PATH)
            .build()
    }

    /// The single manager window, created on first use.
    fn window(&self, initial: Option<PathBuf>) -> BallWindow {
        if let Some(window) = self.active_window().and_downcast::<BallWindow>() {
            return window;
        }
        let build = self
            .imp()
            .build
            .borrow()
            .clone()
            .unwrap_or_else(|| config::DEFAULT_BUILD.clone());
        BallWindow::new(self, build, initial)
    }
}

impl Default for Application {
    fn default() -> Self {
        Self::new()
    }
}
