// Public domain. No rights reserved. Wholly AI-authored.
//! A dialog that keeps its inputs and shows errors while work completes.
//!
//! Callers add content with [`FormDialog::append`] (usually an
//! `AdwPreferencesPage` or `AdwViewStack`), handle `submit`, and run their
//! work through [`FormDialog::run`], which disables the form while it waits
//! and reports failures in the dialog rather than closing it.

use std::cell::RefCell;
use std::fmt::Display;
use std::path::PathBuf;
use std::sync::OnceLock;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::glib::subclass::Signal;
use gtk::{CompositeTemplate, glib};

use crate::window::{BallWindow, Work};

mod imp {
    use super::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/form-dialog.ui")]
    pub struct FormDialog {
        #[template_child]
        pub cancel_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub accept_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub spinner: TemplateChild<adw::Spinner>,
        #[template_child]
        pub error_banner: TemplateChild<adw::Banner>,
        #[template_child]
        pub body: TemplateChild<gtk::Box>,
        /// The ball selected when the dialog opened; work refuses to run after it changes.
        pub ball: RefCell<Option<PathBuf>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FormDialog {
        const NAME: &'static str = "TwoineFormDialog";
        type Type = super::FormDialog;
        type ParentType = adw::Dialog;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[gtk::template_callbacks]
    impl FormDialog {
        #[template_callback]
        fn on_cancel(&self) {
            self.obj().close();
        }

        #[template_callback]
        fn on_accept(&self) {
            self.obj().emit_by_name::<()>("submit", &[]);
        }
    }

    impl ObjectImpl for FormDialog {
        fn signals() -> &'static [Signal] {
            static SIGNALS: OnceLock<Vec<Signal>> = OnceLock::new();
            SIGNALS.get_or_init(|| vec![Signal::builder("submit").build()])
        }
    }

    impl WidgetImpl for FormDialog {}
    impl AdwDialogImpl for FormDialog {}
}

glib::wrapper! {
    pub struct FormDialog(ObjectSubclass<imp::FormDialog>)
        @extends adw::Dialog, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::ShortcutManager;
}

impl FormDialog {
    pub fn new(window: &BallWindow, title: &str, accept: &str) -> Self {
        let dialog: Self = glib::Object::builder().property("title", title).build();
        dialog.imp().accept_button.set_label(accept);
        dialog.imp().ball.replace(window.ball());
        dialog.set_default_widget(Some(&*dialog.imp().accept_button));
        dialog
    }

    #[must_use]
    pub fn with_size(self, width: i32, height: i32) -> Self {
        self.set_content_width(width);
        self.set_content_height(height);
        self
    }

    pub fn append(&self, child: &impl IsA<gtk::Widget>) {
        self.imp().body.append(child);
    }

    pub fn accept_button(&self) -> gtk::Button {
        self.imp().accept_button.clone()
    }

    pub fn connect_submit<F: Fn(&Self) + 'static>(&self, callback: F) -> glib::SignalHandlerId {
        self.connect_local("submit", false, move |values| {
            callback(&values[0].get::<Self>().expect("dialog"));
            None
        })
    }

    pub fn show_error(&self, error: impl Display) {
        self.set_pending(false);
        let banner = &self.imp().error_banner;
        banner.set_title(&error.to_string());
        banner.set_revealed(true);
    }

    pub fn clear_error(&self) {
        self.imp().error_banner.set_revealed(false);
    }

    pub fn error_visible(&self) -> bool {
        self.imp().error_banner.is_revealed()
    }

    pub fn error_text(&self) -> String {
        self.imp().error_banner.title().into()
    }

    pub fn set_pending(&self, active: bool) {
        let imp = self.imp();
        self.set_can_close(!active);
        imp.accept_button.set_sensitive(!active);
        imp.body.set_sensitive(!active);
        imp.spinner.set_visible(active);
    }

    /// Run serialized ball work for this dialog. Errors stay in the dialog.
    pub async fn run<T: Send + 'static, E: Display + Send + 'static>(
        &self,
        window: &BallWindow,
        options: Work,
        job: impl FnOnce() -> Result<T, E> + Send + 'static,
    ) -> Option<T> {
        if *self.imp().ball.borrow() != window.ball() {
            self.show_error("The selected ball changed. Close this dialog and open it again.");
            return None;
        }
        self.clear_error();
        self.set_pending(true);
        let result = window.run(options, job).await;
        self.set_pending(false);
        match result {
            Ok(value) => Some(value),
            Err(error) => {
                self.show_error(error);
                None
            }
        }
    }
}

/// An `AdwPreferencesGroup` with a title and optional description.
pub fn group(title: &str, description: Option<&str>) -> adw::PreferencesGroup {
    let group = adw::PreferencesGroup::builder().title(title).build();
    if let Some(description) = description {
        group.set_description(Some(description));
    }
    group
}

/// A labelled button that keeps its accessible name; icon buttons show the label as a tooltip.
pub fn button(label: &str, icon: Option<&str>) -> gtk::Button {
    let button = match icon {
        Some(icon) => gtk::Button::builder()
            .icon_name(icon)
            .tooltip_text(label)
            .build(),
        None => gtk::Button::with_label(label),
    };
    button.set_valign(gtk::Align::Center);
    button.update_property(&[gtk::accessible::Property::Label(label)]);
    button
}
