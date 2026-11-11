// Public domain. No rights reserved. Wholly AI-authored.
//! Settings: a searchable overview of six ball settings sections with native
//! back navigation.
//!
//! General, Graphics & Sound, Environment and Printing edit `ball.json` and the
//! guest profiles; Config Files edits CONFIG.SYS and other OS/2 files; Registry
//! edits the Open32 registry. The CONFIG.SYS editor follows the snapshot's
//! CONFIG.SYS text, which the launch code refreshes while an application runs.

mod audio;
mod config_sys;
mod environment;
pub mod files;
mod general;
mod graphics;
mod printing;
mod profile;
mod registry;
pub mod registry_values;
mod table;

use std::cell::{OnceCell, RefCell};
use std::path::PathBuf;
use std::rc::{Rc, Weak};

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, glib};
use serde_json::{Value, json};

pub use audio::AudioDialog;
pub use config_sys::ConfigSysEditor;

use crate::engine::{self, Error};
use crate::window::{BallWindow, Work};

/// Settings sections: tag, title, icon and description.
pub const SECTIONS: [(&str, &str, &str, &str); 6] = [
    (
        "general",
        "General",
        "applications-system-symbolic",
        "Runtime and diagnostics",
    ),
    (
        "display",
        "Graphics & Sound",
        "video-display-symbolic",
        "Display, video, audio and MIDI",
    ),
    (
        "environment",
        "Environment",
        "utilities-terminal-symbolic",
        "Inherited values and overrides",
    ),
    (
        "printing",
        "Printing",
        "printer-symbolic",
        "Default printer and output",
    ),
    (
        "files",
        "Config Files",
        "document-properties-symbolic",
        "CONFIG.SYS and OS/2 profiles",
    ),
    (
        "registry",
        "Registry",
        "view-list-symbolic",
        "Keys and typed values",
    ),
];

thread_local! {
    /// The settings section shown last; a newly selected ball opens the same section.
    static LAST_SECTION: RefCell<String> = RefCell::new("overview".into());
}

/// Whether every word of `query` occurs in a section's title, icon or description.
pub fn section_matches(name: &str, query: &str) -> bool {
    let Some((_, title, icon, description)) = SECTIONS.iter().find(|section| section.0 == name)
    else {
        return false;
    };
    let text = glib::casefold(format!("{title} {icon} {description}")).to_string();
    glib::casefold(query.trim())
        .split_whitespace()
        .all(|word| text.contains(word))
}

/// State shared by the sections of one ball's settings.
pub struct Context {
    pub window: glib::WeakRef<BallWindow>,
    pub ball: PathBuf,
    /// Profile editors that read the user, system or registry profiles.
    pub profile_pages: RefCell<Vec<Weak<profile::ProfilePage>>>,
}

impl Context {
    pub fn window(&self) -> Option<BallWindow> {
        self.window.upgrade()
    }

    /// The window, while it still shows this context's ball.
    pub fn current(&self) -> Option<BallWindow> {
        self.window()
            .filter(|window| window.ball().as_deref() == Some(self.ball.as_path()))
    }

    /// Forget loaded user-profile values so their editors reread them when shown.
    pub fn invalidate_profiles(&self, area: &str) {
        for page in self.profile_pages.borrow().iter().filter_map(Weak::upgrade) {
            if page.area() == area {
                page.invalidate();
            }
        }
    }

    pub fn refresh_editability(&self) {
        for page in self.profile_pages.borrow().iter().filter_map(Weak::upgrade) {
            page.refresh_editability();
        }
    }
}

/// Run a ball-changing request on the serialized worker.
///
/// Refused while another operation runs or while an application or component
/// job owns the ball.
pub async fn write(window: &BallWindow, action: &str, fields: Value) -> Result<Value, Error> {
    let message = window.message(action, fields);
    if let Some(ball) = message["ball"].as_str()
        && window.ball_locked(std::path::Path::new(ball))
    {
        return Err(Error::message(
            "An application or component job is active in this ball. Finish it before editing.",
        ));
    }
    if window.operation_busy() {
        return Err(Error::message("Wait for the current operation to finish."));
    }
    window
        .run(Work::default(), move || engine::request(&message))
        .await
}

/// Set (`Some`) or clear (`None`) a `system.*` ball setting with Undo.
pub fn save_system(window: &BallWindow, key: &str, value: Option<&str>) {
    let arguments: Vec<String> = match value {
        Some(value) => vec!["config".into(), format!("system.{key}"), value.into()],
        None => vec!["unset".into(), format!("system.{key}")],
    };
    let Some(ball) = window.ball() else { return };
    let Some(handle) = window.commit(
        json!({"action": "command", "arguments": arguments}),
        None,
        "Update Ball",
        false,
    ) else {
        return;
    };
    if key == "beep_volume" {
        let window = window.downgrade();
        glib::spawn_future_local(async move {
            if let Ok(Some(_)) = handle.await
                && let Some(window) = window.upgrade()
            {
                window
                    .setting_drafts()
                    .borrow_mut()
                    .remove(&(ball, "beep_volume".to_owned()));
            }
        });
    }
}

/// A labelled flat button; icon buttons show the label as a tooltip.
pub fn flat_button(label: &str, icon: Option<&str>) -> gtk::Button {
    let button = crate::dialogs::form::button(label, icon);
    button.add_css_class("flat");
    button
}

/// Ask before discarding unsaved edits. Resolves true when the user confirms.
pub async fn confirm_discard(window: &BallWindow, heading: &str, body: &str) -> bool {
    let dialog = adw::AlertDialog::builder()
        .heading(heading)
        .body(body)
        .close_response("cancel")
        .build();
    dialog.add_response("cancel", "Cancel");
    dialog.add_response("discard", "Discard Changes");
    dialog.set_response_appearance("discard", adw::ResponseAppearance::Destructive);
    dialog.choose_future(Some(window)).await == "discard"
}

#[allow(clippy::wildcard_imports)]
mod imp {
    use super::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/settings-page.ui")]
    pub struct SettingsPage {
        #[template_child]
        pub view: TemplateChild<adw::NavigationView>,
        #[template_child]
        pub overview: TemplateChild<adw::NavigationPage>,
        #[template_child]
        pub search: TemplateChild<gtk::SearchEntry>,
        #[template_child]
        pub sections: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub empty: TemplateChild<adw::PreferencesGroup>,

        pub context: OnceCell<Rc<Context>>,
        pub rows: RefCell<Vec<(&'static str, adw::ActionRow)>>,
        pub general: OnceCell<Rc<general::GeneralPage>>,
        pub graphics: OnceCell<Rc<graphics::GraphicsPage>>,
        pub environment: OnceCell<Rc<environment::EnvironmentPage>>,
        pub printing: OnceCell<Rc<printing::PrintingPage>>,
        pub files: OnceCell<Rc<files::FilesPage>>,
        pub registry: OnceCell<Rc<profile::ProfilePage>>,
        pub config: OnceCell<ConfigSysEditor>,
        pub handlers: RefCell<Vec<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for SettingsPage {
        const NAME: &'static str = "TwoineSettingsPage";
        type Type = super::SettingsPage;
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
    impl SettingsPage {
        #[template_callback]
        fn on_search_changed(&self) {
            let query = self.search.text();
            let mut count = 0;
            for (name, row) in self.rows.borrow().iter() {
                let visible = section_matches(name, &query);
                row.set_visible(visible);
                count += usize::from(visible);
            }
            self.sections.set_visible(count > 0);
            self.empty.set_visible(count == 0);
        }
    }

    impl ObjectImpl for SettingsPage {
        fn dispose(&self) {
            if let Some(window) = self.context.get().and_then(|context| context.window()) {
                for handler in self.handlers.take() {
                    window.disconnect(handler);
                }
            }
        }
    }

    impl WidgetImpl for SettingsPage {}
    impl BinImpl for SettingsPage {}
}

glib::wrapper! {
    pub struct SettingsPage(ObjectSubclass<imp::SettingsPage>)
        @extends adw::Bin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

/// The Settings section for the selected ball.
pub fn page(window: &BallWindow) -> gtk::Widget {
    SettingsPage::new(window).upcast()
}

impl SettingsPage {
    pub fn new(window: &BallWindow) -> Self {
        let page: Self = glib::Object::new();
        let imp = page.imp();
        let context = Rc::new(Context {
            window: window.downgrade(),
            ball: window.ball().unwrap_or_default(),
            profile_pages: RefCell::default(),
        });
        imp.context.set(context.clone()).ok();
        let config = ConfigSysEditor::new(window);
        imp.config.set(config.clone()).ok();

        let general = general::GeneralPage::new(&context);
        page.add_section("general", &general.page);
        imp.general.set(general).ok();
        let graphics = graphics::GraphicsPage::new(&context);
        page.add_section("display", &graphics.page);
        imp.graphics.set(graphics).ok();
        let environment = environment::EnvironmentPage::new(&context);
        page.add_section("environment", &environment.page);
        imp.environment.set(environment).ok();
        let printing = printing::PrintingPage::new(&context);
        page.add_section("printing", &printing.page);
        imp.printing.set(printing).ok();
        let files = files::FilesPage::new(&context, config.upcast_ref());
        page.add_section("files", &files.widget);
        imp.files.set(files).ok();
        let registry = profile::ProfilePage::new(&context, profile::Source::Registry);
        context
            .profile_pages
            .borrow_mut()
            .push(Rc::downgrade(&registry));
        page.add_section("registry", &registry.widget);
        imp.registry.set(registry).ok();

        let last = LAST_SECTION.with_borrow(Clone::clone);
        page.set_visible_section(&last);
        imp.view.connect_visible_page_notify(|view| {
            if let Some(tag) = view.visible_page().and_then(|page| page.tag()) {
                LAST_SECTION.with_borrow_mut(|last| *last = tag.into());
            }
        });

        let weak = page.downgrade();
        let handlers = vec![
            window.connect_snapshot_changed(glib::clone!(
                #[weak(rename_to = page)]
                page,
                move |_| page.snapshot_changed()
            )),
            window.connect_busy_notify(move |_| {
                if let Some(page) = weak.upgrade() {
                    page.busy_changed();
                }
            }),
            window.connect_operation_busy_notify(glib::clone!(
                #[weak(rename_to = page)]
                page,
                move |_| page.busy_changed()
            )),
        ];
        imp.handlers.replace(handlers);
        page.busy_changed();
        page
    }

    fn context(&self) -> Rc<Context> {
        self.imp().context.get().expect("settings context").clone()
    }

    fn add_section(&self, name: &'static str, widget: &impl IsA<gtk::Widget>) {
        let imp = self.imp();
        let Some(&(_, title, icon, description)) =
            SECTIONS.iter().find(|section| section.0 == name)
        else {
            return;
        };
        let content: gtk::Widget = if widget.is::<adw::PreferencesPage>() {
            widget.clone().upcast()
        } else {
            widget.set_margin_top(12);
            widget.set_margin_bottom(12);
            widget.set_margin_start(12);
            widget.set_margin_end(12);
            widget.clone().upcast()
        };
        let toolbar = adw::ToolbarView::builder().content(&content).build();
        toolbar.add_top_bar(
            &adw::HeaderBar::builder()
                .show_start_title_buttons(false)
                .show_end_title_buttons(false)
                .build(),
        );
        imp.view.add(
            &adw::NavigationPage::builder()
                .title(title)
                .tag(name)
                .child(&toolbar)
                .build(),
        );
        let row = adw::ActionRow::builder()
            .title(title)
            .subtitle(description)
            .activatable(true)
            .use_markup(false)
            .build();
        row.add_prefix(&gtk::Image::from_icon_name(icon));
        row.add_suffix(&gtk::Image::from_icon_name("go-next-symbolic"));
        row.connect_activated(glib::clone!(
            #[weak(rename_to = page)]
            self,
            move |_| page.set_visible_section(name)
        ));
        imp.sections.add(&row);
        imp.rows.borrow_mut().push((name, row));
    }

    /// The visible settings section tag; `overview` for the section list.
    pub fn visible_section(&self) -> String {
        self.imp()
            .view
            .visible_page()
            .and_then(|page| page.tag())
            .map_or_else(|| "overview".into(), Into::into)
    }

    /// Show a settings section by tag (`general`, `display`, `environment`,
    /// `printing`, `files`, `registry`) or the `overview` list.
    pub fn set_visible_section(&self, name: &str) {
        let imp = self.imp();
        if name == self.visible_section() {
            return;
        }
        if name == "overview" {
            imp.view.pop_to_page(&*imp.overview);
        } else if imp.view.find_page(name).is_some() {
            if self.visible_section() != "overview" {
                imp.view.pop_to_page(&*imp.overview);
            }
            imp.view.push_by_tag(name);
        }
    }

    /// The control a component setting token refers to: `printing`,
    /// `graphics.opengl` or `graphics`.
    pub fn setting_target(&self, token: &str) -> Option<(&'static str, gtk::Widget)> {
        let imp = self.imp();
        match token {
            "printing" => Some(("printing", imp.printing.get()?.queue.clone().upcast())),
            "graphics.opengl" => Some(("display", imp.graphics.get()?.opengl.clone().upcast())),
            "graphics" => Some(("display", imp.graphics.get()?.video_row().upcast())),
            _ => None,
        }
    }

    pub fn config_editor(&self) -> Option<ConfigSysEditor> {
        self.imp().config.get().cloned()
    }

    fn snapshot_changed(&self) {
        let context = self.context();
        let Some(window) = context.current() else {
            return;
        };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        let imp = self.imp();
        if let Some(general) = imp.general.get() {
            general.refresh(&window, &snapshot);
        }
        if let Some(graphics) = imp.graphics.get() {
            graphics.refresh(&window, &snapshot);
        }
        if let Some(environment) = imp.environment.get() {
            environment.refresh(&snapshot);
        }
        if let Some(config) = imp.config.get() {
            config.sync();
        }
    }

    fn busy_changed(&self) {
        let context = self.context();
        let Some(window) = context.window() else {
            return;
        };
        self.set_sensitive(!window.operation_busy());
        context.refresh_editability();
        if let Some(files) = self.imp().files.get() {
            files.refresh_editability(window.busy());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::section_matches;

    #[test]
    fn section_search_uses_titles_and_descriptions() {
        assert!(section_matches("display", "midi"));
        assert!(section_matches("files", "config sys"));
        assert!(section_matches("registry", ""));
        assert!(!section_matches("printing", "registry"));
    }
}
