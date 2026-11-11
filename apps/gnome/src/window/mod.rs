// Public domain. No rights reserved. Wholly AI-authored.
//! The manager window: ball sidebar, section stack and the shared services
//! that sections use to talk to the engine.
//!
//! Sections never call the engine for ball changes directly. They use
//! [`BallWindow::execute`] for undoable ball commands, [`BallWindow::run`] for
//! other serialized work and [`BallWindow::api`] for requests scoped to the
//! selected ball. Sections observe `snapshot-changed` to refresh themselves.

mod actions;
mod ball_row;
mod balls;
mod component_ops;
mod drive_ops;
pub mod history;
mod imp;
pub mod launch;
mod manage;
pub mod program_ops;
mod sections;
mod settings_ops;

use std::cell::RefCell;
use std::fmt::Display;
use std::future::Future;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};
use serde_json::{Map, Value, json};

use crate::engine::{self, Error};
use crate::model::Snapshot;
use crate::navigation;
use crate::preferences::Preferences;
use crate::sessions::{self, Sessions};
use history::HistoryItem;

glib::wrapper! {
    pub struct BallWindow(ObjectSubclass<imp::BallWindow>)
        @extends adw::ApplicationWindow, gtk::ApplicationWindow, gtk::Window, gtk::Widget,
        @implements gio::ActionGroup, gio::ActionMap, gtk::Accessible, gtk::Buildable,
                    gtk::ConstraintTarget, gtk::Native, gtk::Root, gtk::ShortcutManager;
}

/// Engine actions that only read state and remain available while an
/// application or component job owns the ball.
const READ_ONLY_ACTIONS: &[&str] = &[
    "snapshot",
    "config-sys",
    "guest-environment",
    "parse-arguments",
    "format-arguments",
    "image-preview",
    "application-details",
    "launch-prerequisites",
    "ball-navigation-state",
    "browse",
    "browse-media",
    "configuration-read",
    "configuration-file-read",
    "configuration-files",
    "runtime-features",
    "os2-component-state",
    "application-icon",
    "open-os2-source",
    "validate-os2-media",
    "plan-os2-extraction",
];

/// Options for [`BallWindow::run`].
#[derive(Debug, Clone, Copy, Default)]
pub struct Work {
    /// Read-only work that neither waits for nor blocks ball operations.
    pub unlocked: bool,
    /// Permit the work while the selected ball has running applications.
    pub allow_running: bool,
}

impl BallWindow {
    pub fn new(
        application: &impl IsA<gtk::Application>,
        build: PathBuf,
        initial: Option<PathBuf>,
    ) -> Self {
        let window: Self = glib::Object::builder()
            .property("application", application)
            .build();
        let imp = window.imp();
        let build = build.canonicalize().unwrap_or(build);
        imp.default_build
            .set(build.clone())
            .expect("default build set once");
        imp.build.replace(build);
        window.setup_actions();
        window.setup_balls();
        window.startup(initial);
        window
    }

    // ----- State --------------------------------------------------------------------------

    /// The selected ball package.
    pub fn ball(&self) -> Option<PathBuf> {
        self.imp().ball.borrow().clone()
    }

    /// The selected ball's latest snapshot.
    pub fn snapshot(&self) -> Option<Rc<Snapshot>> {
        self.imp().snapshot.borrow().clone()
    }

    /// The runtime build used for the selected ball.
    pub fn build(&self) -> PathBuf {
        self.imp().build.borrow().clone()
    }

    pub fn default_build(&self) -> PathBuf {
        self.imp().default_build.get().cloned().unwrap_or_default()
    }

    pub fn preferences(&self) -> std::cell::Ref<'_, Preferences> {
        self.imp().preferences.borrow()
    }

    /// Change host preferences and persist them in the background.
    pub fn update_preferences(&self, change: impl FnOnce(&mut Preferences)) {
        change(&mut self.imp().preferences.borrow_mut());
        self.persist_preferences();
    }

    pub fn connect_snapshot_changed<F: Fn(&Self) + 'static>(
        &self,
        callback: F,
    ) -> glib::SignalHandlerId {
        self.connect_local("snapshot-changed", false, move |values| {
            callback(&values[0].get::<Self>().expect("window"));
            None
        })
    }

    pub fn connect_sessions_changed<F: Fn(&Self) + 'static>(
        &self,
        callback: F,
    ) -> glib::SignalHandlerId {
        self.connect_local("sessions-changed", false, move |values| {
            callback(&values[0].get::<Self>().expect("window"));
            None
        })
    }

    pub fn emit_sessions_changed(&self) {
        self.emit_by_name::<()>("sessions-changed", &[]);
    }

    /// Install a new snapshot for the selected ball and notify sections.
    pub fn apply_snapshot(&self, snapshot: Snapshot) {
        self.imp().snapshot.replace(Some(Rc::new(snapshot)));
        self.imp().banner.set_revealed(false);
        self.emit_by_name::<()>("snapshot-changed", &[]);
    }

    /// Whether an application session or component job owns `ball`.
    pub fn ball_locked(&self, ball: &Path) -> bool {
        self.ball_has_activity(ball)
    }

    fn ball_has_activity(&self, ball: &Path) -> bool {
        let imp = self.imp();
        !imp.sessions.borrow().active(Some(ball)).is_empty()
            || imp
                .component_balls
                .borrow()
                .contains(&sessions::canonical(ball))
    }

    /// Application sessions started from this window.
    pub fn sessions(&self) -> &RefCell<Sessions> {
        &self.imp().sessions
    }

    /// Mark a ball as owned by a running OS/2 component job.
    pub fn set_component_active(&self, ball: &Path, active: bool) {
        let ball = sessions::canonical(ball);
        let changed = if active {
            self.imp().component_balls.borrow_mut().insert(ball)
        } else {
            self.imp().component_balls.borrow_mut().remove(&ball)
        };
        if changed {
            self.update_busy();
            self.update_ball_rows();
        }
    }

    pub fn component_active(&self, ball: &Path) -> bool {
        self.imp()
            .component_balls
            .borrow()
            .contains(&sessions::canonical(ball))
    }

    // ----- Feedback -----------------------------------------------------------------------

    pub fn toast(&self, message: impl Display) {
        self.imp()
            .overlay
            .add_toast(adw::Toast::new(&message.to_string()));
    }

    fn toast_with_undo(&self, item: &Rc<HistoryItem>) {
        let toast = adw::Toast::builder()
            .title(&item.label)
            .button_label("Undo")
            .build();
        let item = Rc::downgrade(item);
        toast.connect_button_clicked(glib::clone!(
            #[weak(rename_to = window)]
            self,
            move |_| {
                if let Some(item) = item.upgrade() {
                    window.undo_item(&item);
                }
            }
        ));
        self.imp().overlay.add_toast(toast);
    }

    /// Show an operation error in the window banner.
    pub fn report_error(&self, error: impl Display) {
        let banner = &self.imp().banner;
        banner.set_title(&error.to_string());
        banner.set_button_label(Some("Dismiss"));
        banner.set_revealed(true);
    }

    /// Open a file or folder with the desktop's default handler.
    pub fn open_path(&self, path: &Path) {
        gtk::FileLauncher::new(Some(&gio::File::for_path(path))).launch(
            Some(self),
            gio::Cancellable::NONE,
            glib::clone!(
                #[weak(rename_to = window)]
                self,
                move |result| {
                    if let Err(error) = result
                        && !error.matches(gtk::DialogError::Dismissed)
                    {
                        window.toast(error.message());
                    }
                }
            ),
        );
    }

    /// Show a file or folder selected in its containing folder.
    pub fn reveal_path(&self, path: &Path) {
        gtk::FileLauncher::new(Some(&gio::File::for_path(path))).open_containing_folder(
            Some(self),
            gio::Cancellable::NONE,
            |_| {},
        );
    }

    pub fn open_uri(&self, uri: &str) {
        gtk::UriLauncher::new(uri).launch(Some(self), gio::Cancellable::NONE, |_| {});
    }

    // ----- Navigation ---------------------------------------------------------------------

    /// Show a section by name: `drives`, `components`, `application-installation`,
    /// `apps`, `settings`, `config` (Settings → Config Files) or `activity`.
    pub fn show_page(&self, name: &str) {
        if name == "config" {
            self.show_settings("files");
            return;
        }
        let stack = &self.imp().stack;
        if stack.child_by_name(name).is_some() {
            stack.set_visible_child_name(name);
        } else {
            self.imp().next_section.replace(Some(name.to_owned()));
        }
    }

    pub fn visible_page(&self) -> Option<String> {
        self.imp().stack.visible_child_name().map(Into::into)
    }

    /// Add a section widget to the ball's view stack using its standard title and icon.
    pub fn add_page(&self, name: &str, widget: &impl IsA<gtk::Widget>) {
        let section = navigation::section(name).expect("known section");
        self.imp()
            .stack
            .add_titled_with_icon(widget, Some(name), section.title, section.icon);
    }

    pub fn page(&self, name: &str) -> Option<gtk::Widget> {
        self.imp().stack.child_by_name(name)
    }

    // ----- Engine access ------------------------------------------------------------------

    /// Run blocking work off the main thread, then deliver its result on the main loop.
    ///
    /// Locked work is serialized with other ball operations on one worker
    /// thread and marks the window busy while queued or running; actions that
    /// would compete are disabled meanwhile. Unlocked work runs on the GIO pool.
    pub async fn run<T: Send + 'static>(
        &self,
        options: Work,
        job: impl FnOnce() -> T + Send + 'static,
    ) -> T {
        let imp = self.imp();
        if options.unlocked {
            return engine::spawn(job).await;
        }
        imp.pending_operations.set(imp.pending_operations.get() + 1);
        self.update_busy();
        let worker = imp.worker.get().expect("worker").clone();
        let result = worker.run(job).await;
        imp.pending_operations.set(imp.pending_operations.get() - 1);
        self.update_busy();
        result
    }

    /// Spawn a future on the main loop, reporting errors in the banner.
    pub fn spawn(&self, future: impl Future<Output = Result<(), Error>> + 'static) {
        let window = self.downgrade();
        glib::spawn_future_local(async move {
            if let Err(error) = future.await
                && let Some(window) = window.upgrade()
            {
                window.report_error(error);
            }
        });
    }

    /// Build a request for the selected ball, adding its path and runtime build.
    pub fn message(&self, action: &str, fields: Value) -> Value {
        let mut object = match fields {
            Value::Object(map) => map,
            _ => Map::new(),
        };
        object.insert("action".into(), json!(action));
        if !object.contains_key("ball")
            && let Some(ball) = self.ball()
        {
            object.insert("ball".into(), json!(ball));
        }
        object.entry("build").or_insert_with(|| json!(self.build()));
        Value::Object(object)
    }

    /// A request against the selected ball, run as unlocked work.
    ///
    /// Mutating actions are refused while an application or component job owns the ball.
    pub async fn api(&self, action: &str, fields: Value) -> Result<Value, Error> {
        let message = self.message(action, fields);
        let ball = message["ball"].as_str().map(PathBuf::from);
        if !READ_ONLY_ACTIONS.contains(&action)
            && ball.as_deref().is_some_and(|ball| self.ball_locked(ball))
        {
            return Err(Error::message(
                "An application or component job is active in this ball. Finish it before editing.",
            ));
        }
        engine::spawn(move || engine::request(&message)).await
    }

    /// Read a fresh snapshot for a ball.
    pub async fn read_snapshot(&self, ball: PathBuf) -> Result<Snapshot, Error> {
        engine::spawn(move || engine::request_as(&json!({"action": "snapshot", "ball": ball})))
            .await
    }

    // ----- Ball changes and history -------------------------------------------------------

    /// Run an undoable ball command such as `["map", "D", "/media"]`.
    #[expect(
        clippy::needless_pass_by_value,
        reason = "callers build owned argument vectors"
    )]
    pub fn execute(&self, arguments: Vec<String>) {
        self.commit(
            json!({"action": "command", "arguments": arguments}),
            None,
            "Update Ball",
            false,
        );
    }

    /// Change removable media; permitted while the ball's applications are running.
    #[expect(
        clippy::needless_pass_by_value,
        reason = "callers build owned argument vectors"
    )]
    pub fn media_command(&self, arguments: Vec<String>) {
        self.commit(
            json!({"action": "command", "arguments": arguments}),
            None,
            "Change Media",
            true,
        );
    }

    /// Run a ball-changing engine request with an inverse for Undo.
    ///
    /// When `inverse` is None it is derived from the current snapshot for
    /// `command` and `runtime-build` requests. Resolves to the new snapshot.
    pub fn commit(
        &self,
        message: Value,
        inverse: Option<Value>,
        label: &str,
        allow_running: bool,
    ) -> Option<glib::JoinHandle<Option<Snapshot>>> {
        let ball = self.ball()?;
        let snapshot = self.snapshot()?;
        if self.ball_locked(&ball) && !allow_running {
            self.toast("Wait for this ball’s applications or component job before changing it.");
            return None;
        }
        let inverse = inverse.or_else(|| match message["action"].as_str() {
            Some("command") => {
                let arguments: Vec<String> =
                    serde_json::from_value(message["arguments"].clone()).ok()?;
                history::inverse(&snapshot, &arguments)
            }
            Some("runtime-build") => Some(match &snapshot.manifest.runtime.build {
                Some(build) => json!({"action": "restore-runtime", "build": build}),
                None => json!({"action": "restore-runtime"}),
            }),
            _ => None,
        });
        let request = self.prepared(&ball, &message);
        let label = label.to_owned();
        let window = self.clone();
        Some(glib::spawn_future_local(async move {
            let options = Work {
                allow_running,
                ..Work::default()
            };
            let path = ball.clone();
            let result = window
                .run(options, move || {
                    engine::request(&request)?;
                    engine::request_as::<Snapshot>(&json!({"action": "snapshot", "ball": path}))
                })
                .await;
            match result {
                Ok(snapshot) => {
                    if window.ball().as_deref() == Some(ball.as_path()) {
                        window.apply_snapshot(snapshot.clone());
                    }
                    if let Some(inverse) = inverse {
                        window.add_history(HistoryItem {
                            ball,
                            undo: vec![inverse],
                            redo: vec![message],
                            label,
                        });
                    }
                    Some(snapshot)
                }
                Err(error) => {
                    window.report_error(&error);
                    None
                }
            }
        }))
    }

    /// Add ball and build to a request. Omitting build resets runtime selection.
    fn prepared(&self, ball: &Path, message: &Value) -> Value {
        let mut request = message.clone();
        request["ball"] = json!(ball);
        let resets = matches!(
            message["action"].as_str(),
            Some("runtime-build" | "restore-runtime")
        );
        if !(resets && message.get("build").is_none()) {
            request["build"] = json!(self.build());
        }
        request
    }

    pub fn add_history(&self, item: HistoryItem) {
        let item = Rc::new(item);
        self.imp().undo.borrow_mut().push(item.clone());
        self.imp().redo.borrow_mut().clear();
        self.update_history_actions();
        self.toast_with_undo(&item);
    }

    fn update_history_actions(&self) {
        let busy = self.busy();
        self.set_action_enabled("undo", !busy && !self.imp().undo.borrow().is_empty());
        self.set_action_enabled("redo", !busy && !self.imp().redo.borrow().is_empty());
    }

    /// Undo text editing in a focused editor, otherwise the last ball change.
    pub fn undo(&self) {
        if self.text_history(true) {
            return;
        }
        let item = self.imp().undo.borrow().last().cloned();
        if let Some(item) = item {
            self.undo_item(&item);
        }
    }

    pub fn redo(&self) {
        if self.text_history(false) {
            return;
        }
        let item = self.imp().redo.borrow().last().cloned();
        if let Some(item) = item {
            self.history_operation(&item, false);
        }
    }

    fn undo_item(&self, item: &Rc<HistoryItem>) {
        if !self.busy()
            && self
                .imp()
                .undo
                .borrow()
                .iter()
                .any(|known| Rc::ptr_eq(known, item))
        {
            self.history_operation(item, true);
        }
    }

    /// Route Undo/Redo to a focused text editor's own history.
    fn text_history(&self, undo: bool) -> bool {
        let Some(focus) = GtkWindowExt::focus(self) else {
            return false;
        };
        if let Some(view) = focus.downcast_ref::<gtk::TextView>() {
            let buffer = view.buffer();
            if undo && buffer.can_undo() {
                buffer.undo();
            } else if !undo && buffer.can_redo() {
                buffer.redo();
            }
            return true;
        }
        if focus.is::<gtk::Text>() || focus.ancestor(gtk::Editable::static_type()).is_some() {
            let _ = focus.activate_action(if undo { "text.undo" } else { "text.redo" }, None);
            return true;
        }
        false
    }

    fn history_operation(&self, item: &Rc<HistoryItem>, reverse: bool) {
        if self.ball_locked(&item.ball) {
            self.toast(
                "Wait for this ball’s applications or component job before undoing or redoing.",
            );
            return;
        }
        let messages: Vec<Value> = (if reverse { &item.undo } else { &item.redo })
            .iter()
            .map(|message| self.prepared(&item.ball, message))
            .collect();
        let item = item.clone();
        let window = self.clone();
        glib::spawn_future_local(async move {
            let ball = item.ball.clone();
            let result = window
                .run(Work::default(), move || -> Result<Snapshot, Error> {
                    for message in &messages {
                        engine::request(message)?;
                    }
                    engine::request_as(&json!({"action": "snapshot", "ball": ball}))
                })
                .await;
            match result {
                Ok(snapshot) => {
                    let imp = window.imp();
                    let (source, target) = if reverse {
                        (&imp.undo, &imp.redo)
                    } else {
                        (&imp.redo, &imp.undo)
                    };
                    source
                        .borrow_mut()
                        .retain(|known| !Rc::ptr_eq(known, &item));
                    target.borrow_mut().push(item.clone());
                    window.select_ball(&item.ball, Some(snapshot));
                    window.toast(format!(
                        "{} {}",
                        if reverse { "Undid" } else { "Redid" },
                        item.label
                    ));
                }
                Err(error) => window.report_error(error),
            }
        });
    }

    // ----- Busy state ---------------------------------------------------------------------

    /// Enable or disable a `win.*` action by name (without the `win.` prefix).
    pub fn set_action_enabled(&self, name: &str, enabled: bool) {
        if let Some(action) = self.lookup_action(name).and_downcast::<gio::SimpleAction>() {
            action.set_enabled(enabled);
        }
    }

    /// Recompute busy state and the sensitivity of competing actions.
    pub fn update_busy(&self) {
        let imp = self.imp();
        let operation = imp.pending_operations.get() > 0;
        let ball_active = self
            .ball()
            .is_some_and(|ball| self.ball_has_activity(&ball));
        if imp.operation_busy.replace(operation) != operation {
            self.notify_operation_busy();
        }
        let busy = operation || ball_active;
        if imp.busy.replace(busy) != busy {
            self.notify_busy();
        }
        imp.sidebar.set_sensitive(!operation);
        imp.spinner.set_visible(operation);
        imp.media_menu.set_sensitive(!operation);
        let has_ball = self.snapshot().is_some();
        for name in [
            "rename-ball",
            "trash-ball",
            "export",
            "runtime",
            "add-drive",
            "create-disk",
            "create-floppy",
            "restore-libraries",
            "save",
        ] {
            self.set_action_enabled(name, has_ball && !busy);
        }
        for name in ["new", "open", "import"] {
            self.set_action_enabled(name, !operation);
        }
        self.update_history_actions();
    }

    // ----- Preferences --------------------------------------------------------------------

    pub fn persist_preferences(&self) {
        let imp = self.imp();
        let mut preferences = imp.preferences.borrow().clone();
        preferences.ball_paths.clone_from(&imp.external.borrow());
        preferences.selected = self.ball();
        let path = crate::config::preferences_path();
        glib::spawn_future_local(async move {
            if let Err(error) = engine::spawn(move || preferences.save(&path)).await {
                glib::g_warning!("2ine", "Could not save preferences: {error}");
            }
        });
    }

    fn close_requested(&self) -> glib::Propagation {
        if self.imp().operation_busy.get() {
            self.toast("Wait for the running operation to finish.");
            return glib::Propagation::Stop;
        }
        if !self.imp().component_balls.borrow().is_empty() {
            self.toast(
                "A component job is running. Finish or cancel it in Components before closing.",
            );
            return glib::Propagation::Stop;
        }
        if !self.sessions().borrow().active(None).is_empty() {
            self.toast(
                "Close or stop the running application sessions before closing the manager.",
            );
            return glib::Propagation::Stop;
        }
        if !self.confirm_close_with_drafts() {
            return glib::Propagation::Stop;
        }
        glib::Propagation::Proceed
    }
}
