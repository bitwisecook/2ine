// Public domain. No rights reserved. Wholly AI-authored.
//! One launcher in the Applications list.

use std::cell::{Cell, RefCell};

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, gio, glib};

use super::order::folded;
use crate::model::{Application, working_directory_summary};

mod imp {
    use super::{Cell, CompositeTemplate, RefCell, glib};
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, glib::Properties, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/applications-row.ui")]
    #[properties(wrapper_type = super::ApplicationRow)]
    pub struct ApplicationRow {
        #[template_child]
        pub icon: TemplateChild<gtk::Image>,
        #[template_child]
        pub play_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub sessions_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub remove_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub more_button: TemplateChild<gtk::MenuButton>,
        /// The launcher name in the ball manifest.
        #[property(get)]
        pub application_name: RefCell<String>,
        /// The launcher's OS/2 program path.
        #[property(get)]
        pub program: RefCell<String>,
        /// Whether real artwork has loaded for the launcher.
        #[property(get, set)]
        pub has_icon: Cell<bool>,
        /// Folded name, program and working folders for searching.
        #[property(get)]
        pub search_text: RefCell<String>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ApplicationRow {
        const NAME: &'static str = "TwoineApplicationRow";
        type Type = super::ApplicationRow;
        type ParentType = adw::ActionRow;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[glib::derived_properties]
    impl ObjectImpl for ApplicationRow {}
    impl WidgetImpl for ApplicationRow {}
    impl ListBoxRowImpl for ApplicationRow {}
    impl PreferencesRowImpl for ApplicationRow {}
    impl ActionRowImpl for ApplicationRow {}
}

glib::wrapper! {
    pub struct ApplicationRow(ObjectSubclass<imp::ApplicationRow>)
        @extends adw::ActionRow, adw::PreferencesRow, gtk::ListBoxRow, gtk::Widget,
        @implements gtk::Accessible, gtk::Actionable, gtk::Buildable, gtk::ConstraintTarget;
}

/// Per-row menu entries: title and window action taking the launcher name.
const MENU: [(&str, &str); 4] = [
    ("Launch", "win.launch-application"),
    ("Edit Application…", "win.edit-application"),
    ("Copy", "applications.copy"),
    ("Remove Launcher", "win.remove-application"),
];

impl ApplicationRow {
    pub fn new(name: &str, application: &Application) -> Self {
        let row: Self = glib::Object::new();
        let imp = row.imp();
        let summary = working_directory_summary(application);
        name.clone_into(&mut imp.application_name.borrow_mut());
        application
            .program
            .clone_into(&mut imp.program.borrow_mut());
        imp.search_text
            .replace(folded(&format!("{name} {} {summary}", application.program)));
        row.set_title(name);
        row.set_subtitle(&format!("{}\n{summary}", application.program));
        let target = name.to_variant();
        let launch = format!("Launch {name}");
        imp.play_button.set_action_target_value(Some(&target));
        imp.play_button.set_tooltip_text(Some(&launch));
        imp.play_button
            .update_property(&[gtk::accessible::Property::Label(&launch)]);
        imp.remove_button.set_action_target_value(Some(&target));
        imp.remove_button
            .update_property(&[gtk::accessible::Property::Label(&format!(
                "Remove launcher for {name}"
            ))]);
        imp.sessions_button
            .set_tooltip_text(Some(&format!("View sessions for {name}")));
        imp.more_button.set_menu_model(Some(&row.menu()));
        imp.more_button
            .update_property(&[gtk::accessible::Property::Label(&format!(
                "Actions for {name}"
            ))]);
        row
    }

    /// Launch, edit, copy and remove actions for this launcher.
    pub fn menu(&self) -> gio::Menu {
        let menu = gio::Menu::new();
        let target = self.application_name().to_variant();
        for (title, action) in MENU {
            let item = gio::MenuItem::new(Some(title), None);
            item.set_action_and_target_value(Some(action), Some(&target));
            menu.append_item(&item);
        }
        menu
    }

    pub fn icon(&self) -> gtk::Image {
        self.imp().icon.clone()
    }

    pub fn sessions_button(&self) -> gtk::Button {
        self.imp().sessions_button.clone()
    }

    /// Show how many of the launcher's sessions are active.
    pub fn set_session_count(&self, count: usize) {
        let button = &self.imp().sessions_button;
        button.set_visible(count > 0);
        button.set_label(&format!(
            "{count} session{}",
            if count == 1 { "" } else { "s" }
        ));
    }
}
