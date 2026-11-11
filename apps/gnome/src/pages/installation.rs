// Public domain. No rights reserved. Wholly AI-authored.
//! Install: start an application installer, review a launch waiting for OS/2
//! support, and reach installation media and installed launchers.

use std::cell::RefCell;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, glib};

use crate::pages::applications::advice;
use crate::window::BallWindow;
use crate::window::launch::PendingLaunch;
use crate::window::program_ops::ProgramAction;

mod imp {
    use super::{BallWindow, CompositeTemplate, ProgramAction, RefCell, glib};
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/installation-page.ui")]
    pub struct InstallationPage {
        #[template_child]
        pub pending_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub components_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub check_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub continue_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub advice_slot: TemplateChild<adw::Bin>,
        pub window: glib::WeakRef<BallWindow>,
        /// The component group the support button shows.
        pub category: RefCell<Option<String>>,
        pub handlers: RefCell<Vec<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for InstallationPage {
        const NAME: &'static str = "TwoineInstallationPage";
        type Type = super::InstallationPage;
        type ParentType = adw::Bin;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[gtk::template_callbacks]
    impl InstallationPage {
        fn with_window(&self, callback: impl FnOnce(&BallWindow)) {
            if let Some(window) = self.window.upgrade() {
                callback(&window);
            }
        }

        #[template_callback]
        fn on_components(&self) {
            let category = self.category.borrow().clone();
            self.with_window(|window| {
                if let Some(category) = category {
                    window.show_component_media(&category, true);
                }
            });
        }

        #[template_callback]
        fn on_check(&self) {
            self.with_window(|window| window.refresh_pending_launch(false));
            self.obj().refresh();
        }

        #[template_callback]
        fn on_continue(&self) {
            self.with_window(BallWindow::continue_pending_launch);
            self.obj().refresh();
        }

        #[template_callback]
        fn on_cancel(&self) {
            self.with_window(BallWindow::cancel_pending_launch);
            self.obj().refresh();
        }

        #[template_callback]
        fn on_choose_installer(&self) {
            self.with_window(|window| window.program_dialog(ProgramAction::Install, None));
        }

        #[template_callback]
        fn on_manage_media(&self) {
            self.with_window(|window| window.show_page("drives"));
        }

        #[template_callback]
        fn on_applications(&self) {
            self.with_window(|window| window.show_page("apps"));
        }
    }

    impl ObjectImpl for InstallationPage {
        fn dispose(&self) {
            if let Some(window) = self.window.upgrade() {
                for handler in self.handlers.take() {
                    window.disconnect(handler);
                }
            }
        }
    }

    impl WidgetImpl for InstallationPage {
        fn map(&self) {
            self.parent_map();
            self.obj().refresh();
        }
    }

    impl BinImpl for InstallationPage {}
}

glib::wrapper! {
    pub struct InstallationPage(ObjectSubclass<imp::InstallationPage>)
        @extends adw::Bin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

pub fn page(window: &BallWindow) -> gtk::Widget {
    InstallationPage::new(window).upcast()
}

/// The support button for a pending launch: label and component group.
pub fn support_button(status: &serde_json::Value) -> Option<(&'static str, String)> {
    let categories: Vec<&str> = status["requiredCategories"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(serde_json::Value::as_str)
        .collect();
    if categories.contains(&"cmd") {
        Some(("CMD and REXX Support…", "cmd".to_owned()))
    } else {
        categories
            .first()
            .map(|category| ("Show Components…", (*category).to_owned()))
    }
}

impl InstallationPage {
    pub fn new(window: &BallWindow) -> Self {
        let page: Self = glib::Object::new();
        page.imp().window.set(Some(window));
        let refresh = |page: &Self| {
            glib::clone!(
                #[weak]
                page,
                move |_: &BallWindow| page.refresh()
            )
        };
        let handlers = vec![
            window.connect_snapshot_changed(refresh(&page)),
            window.connect_sessions_changed(refresh(&page)),
            window.connect_busy_notify(refresh(&page)),
        ];
        page.imp().handlers.replace(handlers);
        page.refresh();
        page
    }

    /// Show the selected ball's pending launch and dependency advice.
    pub fn refresh(&self) {
        let imp = self.imp();
        let Some(window) = imp.window.upgrade() else {
            return;
        };
        imp.advice_slot
            .set_child(Some(&advice::dependency_advice_box(&window)));
        let Some(pending) = window.pending_launch() else {
            imp.pending_group.set_visible(false);
            return;
        };
        self.show_pending(&window, &pending);
    }

    fn show_pending(&self, window: &BallWindow, pending: &PendingLaunch) {
        let imp = self.imp();
        let status = &pending.status;
        imp.pending_group.set_visible(true);
        imp.pending_group.set_description(Some(
            status["reason"]
                .as_str()
                .unwrap_or("Checking prerequisites…"),
        ));
        let ball = window.ball();
        let running = ball
            .as_deref()
            .is_some_and(|ball| !window.sessions().borrow().active(Some(ball)).is_empty());
        let component_active = ball
            .as_deref()
            .is_some_and(|ball| window.component_active(ball));
        let support = support_button(status);
        imp.components_button.set_visible(support.is_some());
        if let Some((label, _)) = &support {
            imp.components_button.set_label(label);
        }
        imp.components_button.set_sensitive(!running);
        imp.category.replace(support.map(|(_, category)| category));
        imp.check_button.set_sensitive(!pending.checking);
        imp.continue_button.set_sensitive(
            status["ready"].as_bool() == Some(true) && !component_active && !pending.checking,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::support_button;
    use serde_json::json;

    #[test]
    fn support_prefers_cmd_then_the_first_required_group() {
        assert_eq!(
            support_button(&json!({"requiredCategories": ["opengl", "cmd"]})),
            Some(("CMD and REXX Support…", "cmd".to_owned()))
        );
        assert_eq!(
            support_button(&json!({"requiredCategories": ["opengl"]})),
            Some(("Show Components…", "opengl".to_owned()))
        );
        assert_eq!(support_button(&json!({"ready": true})), None);
    }
}
