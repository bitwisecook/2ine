// Public domain. No rights reserved. Wholly AI-authored.
//! The Logs section: application sessions, their output, dependency audits and
//! the last installer receipt.
//!
//! The page lists this ball's sessions after "Activity and Audits", shows the
//! selected session's heading and the tail of its log, and offers Stop and
//! Force Stop while the window's `win.stop` and `win.force-stop` actions are
//! enabled. While an application in the ball runs, the page polls its log and
//! the ball's CONFIG.SYS and guest environment, applying changed values as a
//! new window snapshot.

use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::time::Duration;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};
use serde_json::json;

use crate::engine;
use crate::model::Snapshot;
use crate::sessions::{self, Session};
use crate::window::BallWindow;

const LOG_INTERVAL: Duration = Duration::from_millis(250);
const CONFIG_INTERVAL: Duration = Duration::from_secs(1);
const ACTIVITY_LABEL: &str = "Activity and Audits";

mod imp {
    use std::cell::{Cell, RefCell};
    use std::path::PathBuf;

    use adw::subclass::prelude::*;
    use gtk::prelude::ObjectExt;
    use gtk::{CompositeTemplate, glib};

    use crate::window::BallWindow;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/logs-page.ui")]
    pub struct LogsPage {
        #[template_child]
        pub status: TemplateChild<gtk::Label>,
        #[template_child]
        pub stop_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub force_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub more_button: TemplateChild<gtk::MenuButton>,
        #[template_child]
        pub session_selector: TemplateChild<gtk::DropDown>,
        #[template_child]
        pub summary: TemplateChild<gtk::Label>,
        #[template_child]
        pub log_stack: TemplateChild<gtk::Stack>,
        #[template_child]
        pub log_view: TemplateChild<gtk::TextView>,

        pub window: glib::WeakRef<BallWindow>,
        /// The ball this page was built for.
        pub ball: RefCell<PathBuf>,
        /// Session ids in selector order; `None` is Activity and Audits.
        pub session_ids: RefCell<Vec<Option<String>>>,
        pub updating: Cell<bool>,
        /// The log file whose text is shown.
        pub shown_log: RefCell<Option<PathBuf>>,
        pub log_reading: Cell<bool>,
        pub config_polling: Cell<bool>,
        pub log_timer: RefCell<Option<glib::SourceId>>,
        pub config_timer: RefCell<Option<glib::SourceId>>,
        pub handlers: RefCell<Vec<glib::SignalHandlerId>>,
        pub font: RefCell<Option<gtk::TextTag>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for LogsPage {
        const NAME: &'static str = "TwoineLogsPage";
        type Type = super::LogsPage;
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
    impl LogsPage {
        #[template_callback]
        fn on_session_selected(&self) {
            self.obj().session_selected();
        }
    }

    impl ObjectImpl for LogsPage {
        fn dispose(&self) {
            for timer in [&self.log_timer, &self.config_timer] {
                if let Some(source) = timer.take() {
                    source.remove();
                }
            }
            if let Some(window) = self.window.upgrade() {
                for handler in self.handlers.take() {
                    window.disconnect(handler);
                }
            }
        }
    }

    impl WidgetImpl for LogsPage {}
    impl BinImpl for LogsPage {}
}

glib::wrapper! {
    /// The Logs section for one ball.
    pub struct LogsPage(ObjectSubclass<imp::LogsPage>)
        @extends adw::Bin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

pub fn page(window: &BallWindow) -> gtk::Widget {
    LogsPage::new(window).upcast()
}

impl LogsPage {
    pub fn new(window: &BallWindow) -> Self {
        let page: Self = glib::Object::new();
        let imp = page.imp();
        imp.window.set(Some(window));
        imp.ball.replace(window.ball().unwrap_or_default());
        imp.summary
            .set_label(&sessions::read_receipt(&imp.ball.borrow()));
        imp.summary.set_visible(true);
        for (button, name) in [
            (&*imp.stop_button, "stop"),
            (&*imp.force_button, "force-stop"),
        ] {
            if let Some(action) = window.lookup_action(name) {
                action
                    .bind_property("enabled", button, "visible")
                    .sync_create()
                    .build();
            }
        }
        let tag = imp.log_view.buffer().create_tag(Some("log-font"), &[]);
        imp.font.replace(tag);
        let handlers = vec![
            window.connect_sessions_changed(glib::clone!(
                #[weak]
                page,
                move |_| page.refresh()
            )),
            window.connect_snapshot_changed(glib::clone!(
                #[weak]
                page,
                move |_| page.update_menu()
            )),
            window.connect_local(
                "preferences-changed",
                false,
                glib::clone!(
                    #[weak]
                    page,
                    #[upgrade_or]
                    None,
                    move |_| {
                        page.apply_font();
                        None
                    }
                ),
            ),
        ];
        imp.handlers.replace(handlers);
        // A newly shown ball presents its latest session unless Activity was chosen here.
        let latest = window
            .sessions()
            .borrow()
            .for_ball(&imp.ball.borrow())
            .last()
            .map(|session| session.id.clone());
        let selected = window.selected_session();
        if selected.as_ref().is_none_or(|id| !page.has_session(id)) {
            window.select_session(latest);
        }
        page.apply_font();
        page.refresh();
        page
    }

    fn window(&self) -> Option<BallWindow> {
        self.imp().window.upgrade()
    }

    fn ball(&self) -> PathBuf {
        self.imp().ball.borrow().clone()
    }

    fn sessions(&self) -> Vec<Rc<Session>> {
        self.window()
            .map(|window| window.sessions().borrow().for_ball(&self.ball()))
            .unwrap_or_default()
    }

    fn has_session(&self, id: &str) -> bool {
        self.sessions().iter().any(|session| session.id == id)
    }

    /// The selected session, or None for Activity and Audits.
    fn selected(&self) -> Option<Rc<Session>> {
        let id = self.window()?.selected_session()?;
        self.sessions().into_iter().find(|session| session.id == id)
    }

    /// Show the installer receipt or audit summary beneath the session list.
    pub fn set_summary(&self, text: &str) {
        let summary = &self.imp().summary;
        summary.set_label(text);
        summary.set_visible(!text.is_empty());
    }

    /// Rebuild the session list, heading, menu and output for the current selection.
    pub fn refresh(&self) {
        let Some(window) = self.window() else { return };
        let imp = self.imp();
        let items = self.sessions();
        if let Some(id) = window.selected_session()
            && !items.iter().any(|session| session.id == id)
        {
            window.select_session(items.last().map(|session| session.id.clone()));
        }
        let selected = window.selected_session();
        let ids: Vec<Option<String>> = std::iter::once(None)
            .chain(items.iter().map(|session| Some(session.id.clone())))
            .collect();
        let labels: Vec<String> = std::iter::once(ACTIVITY_LABEL.to_owned())
            .chain(items.iter().map(|session| session.label()))
            .collect();
        imp.updating.set(true);
        let model = gtk::StringList::new(&labels.iter().map(String::as_str).collect::<Vec<_>>());
        imp.session_selector.set_model(Some(&model));
        let position = ids
            .iter()
            .position(|id| *id == selected)
            .unwrap_or_default();
        imp.session_selector
            .set_selected(u32::try_from(position).unwrap_or_default());
        imp.session_selector.set_visible(!items.is_empty());
        imp.updating.set(false);
        imp.session_ids.replace(ids);
        self.show_selection();
        self.ensure_polling();
    }

    fn session_selected(&self) {
        let imp = self.imp();
        if imp.updating.get() {
            return;
        }
        let Some(window) = self.window() else { return };
        let index = usize::try_from(imp.session_selector.selected()).unwrap_or(usize::MAX);
        let Some(id) = imp.session_ids.borrow().get(index).cloned() else {
            return;
        };
        window.select_session(id);
        self.show_selection();
        self.ensure_polling();
    }

    /// Present the selected session or the last audit.
    fn show_selection(&self) {
        let Some(window) = self.window() else { return };
        let imp = self.imp();
        if let Some(session) = self.selected() {
            imp.status.set_label(&sessions::session_title(&session));
            let log = session.log.borrow().clone();
            if *imp.shown_log.borrow() != log {
                imp.shown_log.replace(log.clone());
                self.set_text("");
            }
            if log.is_some() {
                self.load_log();
            }
        } else {
            let (title, text, log) = window.audit_activity();
            imp.status
                .set_label(if title.is_empty() { "Ready" } else { &title });
            imp.shown_log.replace(log);
            self.set_text(&text);
        }
        self.update_menu();
    }

    fn set_text(&self, text: &str) {
        let imp = self.imp();
        let buffer = imp.log_view.buffer();
        let (start, end) = buffer.bounds();
        if buffer.text(&start, &end, true) != text {
            buffer.set_text(text);
            self.tag_text();
        }
        imp.log_stack
            .set_visible_child_name(if text.is_empty() { "empty" } else { "log" });
    }

    /// Read the tail of the selected session's log in the background.
    fn load_log(&self) {
        let imp = self.imp();
        let Some(path) = imp.shown_log.borrow().clone() else {
            return;
        };
        if imp.log_reading.replace(true) {
            return;
        }
        let page = self.downgrade();
        glib::spawn_future_local(async move {
            let source = path.clone();
            let text = engine::spawn(move || sessions::read_log(&source)).await;
            let Some(page) = page.upgrade() else { return };
            page.imp().log_reading.set(false);
            if page.imp().shown_log.borrow().as_deref() == Some(path.as_path())
                && page.selected().is_some()
            {
                page.set_text(&text);
            }
        });
    }

    /// Offer the full log, audit, registrations and folders that exist.
    fn update_menu(&self) {
        let Some(window) = self.window() else { return };
        let ball = self.ball();
        let logs = ball.join("logs");
        let audit = window
            .snapshot()
            .and_then(|snapshot| snapshot.dependency_audit.clone())
            .and_then(|audit| audit["logPath"].as_str().map(PathBuf::from))
            .and_then(|path| {
                path.file_name()
                    .and_then(|name| logs_file(&ball, Path::new(name)))
            });
        let targets = [
            ("Open Full Log", self.imp().shown_log.borrow().clone()),
            ("Open Dependency Audit", audit),
            (
                "Open Application Registrations",
                logs_file(&ball, Path::new("wps-registration-latest.json")),
            ),
            ("Open Logs Folder", Some(logs)),
            ("Open Ball Folder", Some(ball.clone())),
        ];
        let menu = gio::Menu::new();
        for (label, target) in targets {
            if let Some(target) = target {
                let item = gio::MenuItem::new(Some(label), None);
                item.set_action_and_target_value(
                    Some("win.open-file"),
                    Some(&target.to_string_lossy().to_variant()),
                );
                menu.append_item(&item);
            }
        }
        let rescan = gio::Menu::new();
        rescan.append(Some("Rescan Dependencies"), Some("win.restore-libraries"));
        menu.append_section(None, &rescan);
        self.imp().more_button.set_menu_model(Some(&menu));
    }

    /// Apply the log text size from preferences.
    fn apply_font(&self) {
        let Some(window) = self.window() else { return };
        let (custom, size) = {
            let preferences = window.preferences();
            (preferences.custom_log_size, preferences.log_font_size)
        };
        if let Some(tag) = self.imp().font.borrow().as_ref() {
            tag.set_size_points(f64::from(size));
            tag.set_property("size-set", custom);
        }
        self.tag_text();
    }

    fn tag_text(&self) {
        let buffer = self.imp().log_view.buffer();
        let (start, end) = buffer.bounds();
        if let Some(tag) = self.imp().font.borrow().as_ref() {
            buffer.apply_tag(tag, &start, &end);
        }
    }

    /// Poll output and configuration while an application runs in this ball.
    fn ensure_polling(&self) {
        let imp = self.imp();
        if !self.running() {
            return;
        }
        if imp.log_timer.borrow().is_none() {
            let source = glib::timeout_add_local(
                LOG_INTERVAL,
                glib::clone!(
                    #[weak(rename_to = page)]
                    self,
                    #[upgrade_or]
                    glib::ControlFlow::Break,
                    move || page.poll_log()
                ),
            );
            imp.log_timer.replace(Some(source));
        }
        if imp.config_timer.borrow().is_none() {
            let source = glib::timeout_add_local(
                CONFIG_INTERVAL,
                glib::clone!(
                    #[weak(rename_to = page)]
                    self,
                    #[upgrade_or]
                    glib::ControlFlow::Break,
                    move || page.poll_config()
                ),
            );
            imp.config_timer.replace(Some(source));
        }
    }

    fn running(&self) -> bool {
        self.sessions().iter().any(|session| session.is_active())
    }

    fn poll_log(&self) -> glib::ControlFlow {
        if !self.running() {
            self.imp().log_timer.replace(None);
            return glib::ControlFlow::Break;
        }
        if self.selected().is_some_and(|session| session.is_active()) {
            self.load_log();
        }
        glib::ControlFlow::Continue
    }

    /// Reflect CONFIG.SYS and guest environment changes made by running programs.
    fn poll_config(&self) -> glib::ControlFlow {
        let imp = self.imp();
        if !self.running() {
            imp.config_timer.replace(None);
            return glib::ControlFlow::Break;
        }
        if imp.config_polling.replace(true) {
            return glib::ControlFlow::Continue;
        }
        let ball = self.ball();
        let page = self.downgrade();
        glib::spawn_future_local(async move {
            let path = ball.clone();
            let result = engine::spawn(move || -> engine::Result<_> {
                let text = engine::request(&json!({"action": "config-sys", "ball": path}))?;
                let environment =
                    engine::request(&json!({"action": "guest-environment", "ball": path}))?;
                Ok((text, environment))
            })
            .await;
            let Some(page) = page.upgrade() else { return };
            page.imp().config_polling.set(false);
            let Some(window) = page.window() else { return };
            if window.ball().as_deref() != Some(ball.as_path()) {
                return;
            }
            match result {
                Ok((text, environment)) => {
                    let Some(snapshot) = window.snapshot() else {
                        return;
                    };
                    if let Some(updated) = configuration_update(&snapshot, &text, &environment) {
                        window.apply_snapshot(updated);
                    }
                }
                Err(error) => glib::g_warning!("2ine", "Could not refresh CONFIG.SYS: {error}"),
            }
        });
        glib::ControlFlow::Continue
    }
}

/// A file directly inside the ball's logs folder, or None when absent.
fn logs_file(ball: &Path, name: &Path) -> Option<PathBuf> {
    let logs = ball.join("logs").canonicalize().ok()?;
    let target = logs.join(name).canonicalize().ok()?;
    (target.parent() == Some(logs.as_path()) && target.is_file()).then_some(target)
}

/// The snapshot with polled CONFIG.SYS text and guest environment, if either changed.
fn configuration_update(
    snapshot: &Snapshot,
    text: &serde_json::Value,
    environment: &serde_json::Value,
) -> Option<Snapshot> {
    let text = text.as_str().unwrap_or_default();
    let mut environment: std::collections::BTreeMap<String, String> = environment
        .as_object()
        .into_iter()
        .flatten()
        .map(|(key, value)| {
            (
                key.clone(),
                value
                    .as_str()
                    .map_or_else(|| value.to_string(), str::to_owned),
            )
        })
        .collect();
    if let Some(path) = environment.remove("OS2PATH") {
        environment.insert("PATH".into(), path);
    }
    if snapshot.config_sys == text && snapshot.environment_defaults == environment {
        return None;
    }
    let mut updated = snapshot.clone();
    text.clone_into(&mut updated.config_sys);
    updated.environment_defaults = environment;
    Some(updated)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn configuration_updates_only_when_text_or_environment_change() {
        let mut snapshot = Snapshot {
            config_sys: "A".into(),
            ..Snapshot::default()
        };
        snapshot
            .environment_defaults
            .insert("PATH".into(), "C:\\".into());
        assert!(
            configuration_update(&snapshot, &json!("A"), &json!({"OS2PATH": "C:\\"})).is_none()
        );
        let changed =
            configuration_update(&snapshot, &json!("B"), &json!({"OS2PATH": "C:\\"})).unwrap();
        assert_eq!(changed.config_sys, "B");
        let environment =
            configuration_update(&snapshot, &json!("A"), &json!({"PATH": "D:\\"})).unwrap();
        assert_eq!(environment.environment_defaults["PATH"], "D:\\");
    }

    #[test]
    fn logs_files_stay_inside_the_logs_folder() {
        let ball = tempfile::tempdir().unwrap();
        std::fs::create_dir(ball.path().join("logs")).unwrap();
        std::fs::write(ball.path().join("logs/audit.log"), "x").unwrap();
        std::fs::write(ball.path().join("ball.json"), "{}").unwrap();
        assert!(logs_file(ball.path(), Path::new("audit.log")).is_some());
        assert!(logs_file(ball.path(), Path::new("missing.log")).is_none());
        assert!(logs_file(ball.path(), Path::new("../ball.json")).is_none());
    }
}
