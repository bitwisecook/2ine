// Public domain. No rights reserved. Wholly AI-authored.
//! A sidebar row for one ball package.

use std::cell::RefCell;
use std::path::PathBuf;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::glib;

mod imp {
    use super::*;

    #[derive(Default, glib::Properties)]
    #[properties(wrapper_type = super::BallRow)]
    pub struct BallRow {
        /// The ball package this row represents.
        #[property(get, set)]
        pub path: RefCell<PathBuf>,
        /// Shown while an OS/2 component job runs in the ball.
        pub spinner: adw::Spinner,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for BallRow {
        const NAME: &'static str = "TwoineBallRow";
        type Type = super::BallRow;
        type ParentType = adw::ActionRow;
    }

    #[glib::derived_properties]
    impl ObjectImpl for BallRow {
        fn constructed(&self) {
            self.parent_constructed();
            self.spinner.set_visible(false);
            self.spinner
                .set_tooltip_text(Some("OS/2 component job is running"));
            self.obj().add_suffix(&self.spinner);
        }
    }
    impl WidgetImpl for BallRow {}
    impl ListBoxRowImpl for BallRow {}
    impl PreferencesRowImpl for BallRow {}
    impl ActionRowImpl for BallRow {}
}

glib::wrapper! {
    pub struct BallRow(ObjectSubclass<imp::BallRow>)
        @extends adw::ActionRow, adw::PreferencesRow, gtk::ListBoxRow, gtk::Widget,
        @implements gtk::Accessible, gtk::Actionable, gtk::Buildable, gtk::ConstraintTarget;
}

impl BallRow {
    pub fn new(path: PathBuf) -> Self {
        glib::Object::builder()
            .property("path", path)
            .property("subtitle-lines", 1)
            .property("use-markup", false)
            .build()
    }

    /// Show or hide the component job indicator.
    pub fn set_component_active(&self, active: bool) {
        self.imp().spinner.set_visible(active);
    }
}
