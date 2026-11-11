// Public domain. No rights reserved. Wholly AI-authored.
//! Application launch, sessions and pending launches.
//!
//! A launch reserves a session, checks the launch's OS/2 prerequisites and
//! library dependencies off the main thread, asks the engine for a launch
//! plan, then spawns the runtime with its output in a session log under the
//! ball's `logs` folder. Startup services named by the plan are leased from the
//! window's [`StartupPool`](crate::sessions::StartupPool) and stopped when their
//! last session ends. Every session state change updates the `win.stop` and
//! `win.force-stop` actions and emits `sessions-changed`.
//!
//! A launch whose prerequisites are not ready is kept as the ball's
//! [`PendingLaunch`]; changes to pending launches emit `snapshot-changed` so
//! that sections presenting them refresh.

use std::collections::BTreeSet;
use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::time::{Duration, Instant};

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};
use serde_json::{Map, Value, json};

use super::sections::launch_title;
use super::{BallWindow, Work};
use crate::engine::{self, Error};
use crate::model::Snapshot;
use crate::pages::logs::LogsPage;
use crate::sessions::{self, LeaseError, ServiceKey, Session, StartupPool, State};

/// How long a launch waits for a released startup service to finish closing.
const SERVICE_CLOSING_WAIT: Duration = Duration::from_secs(10);
const SERVICE_RETRY: Duration = Duration::from_millis(100);
/// Grace periods before a released startup service is interrupted, then killed.
const SERVICE_TERM_GRACE: Duration = Duration::from_secs(2);
const SERVICE_INTERRUPT_GRACE: Duration = Duration::from_secs(5);

/// A launch waiting for OS/2 components or configuration.
#[derive(Debug, Clone)]
pub struct PendingLaunch {
    pub arguments: Vec<String>,
    pub fields: Value,
    pub status: Value,
    pub auto_resume: bool,
    pub checking: bool,
}

/// The outcome of preparing a launch off the main thread.
#[derive(Debug)]
enum Prepared {
    /// OS/2 components or configuration are missing; the status explains which.
    Prerequisites(Value),
    /// The program's libraries need OS/2 components that are not installed.
    Dependencies(Value),
    /// A launch plan and the session log path.
    Plan(Value, PathBuf),
}

/// What a finished session reads back from its ball.
struct Finished {
    snapshot: Snapshot,
    receipt_error: Option<String>,
    summary: String,
}

impl BallWindow {
    /// Start `run`, `install` or `launch` arguments as an application session.
    /// `fields` carries `workingDirectories`, `environment` and similar launch options.
    pub fn launch(&self, arguments: Vec<String>, fields: Value) -> bool {
        let Some(ball) = self.ball() else {
            return false;
        };
        if self.operation_busy() || self.component_active(&ball) {
            return false;
        }
        self.imp().banner.set_revealed(false);
        let Some(action) = arguments.first().cloned() else {
            return false;
        };
        if !matches!(action.as_str(), "run" | "install" | "launch") {
            self.execute(arguments);
            return true;
        }
        let build = self.build();
        let peers = !self.sessions().borrow().active(Some(&ball)).is_empty();
        let reserved = self.sessions().borrow_mut().reserve(
            &ball,
            action == "install",
            &launch_title(&arguments),
        );
        let session = match reserved {
            Ok(session) => session,
            Err(error) => {
                self.toast(error);
                return false;
            }
        };
        self.select_session(Some(session.id.clone()));
        self.sessions_updated();
        self.show_page("activity");
        let original = fields.clone();
        let mut fields = object(fields);
        let resolve = !peers
            && fields
                .get("resolve")
                .and_then(Value::as_bool)
                .unwrap_or(true);
        fields.insert("resolve".into(), json!(resolve));
        let window = self.clone();
        glib::spawn_future_local(async move {
            let (path, build, request) = (ball.clone(), build.clone(), arguments.clone());
            let original_fields = original.clone();
            let outcome = engine::spawn(move || {
                prepare_checked(&path, &build, &request, &original_fields, &fields)
            })
            .await;
            window
                .launch_prepared(&ball, &arguments, original, &session, outcome)
                .await;
        });
        true
    }

    async fn launch_prepared(
        &self,
        ball: &Path,
        arguments: &[String],
        fields: Value,
        session: &Rc<Session>,
        outcome: Result<Prepared, Error>,
    ) {
        let current = self.ball().as_deref() == Some(ball);
        match outcome {
            Err(error) => {
                session.state.set(State::Failed);
                self.sessions_updated();
                if current {
                    self.report_error(&error);
                } else {
                    self.toast(format!("Could not start {}: {error}", session.name));
                }
            }
            Ok(_) if session.stop_sent.get() => {
                session.state.set(State::Cancelled);
                self.sessions_updated();
            }
            Ok(Prepared::Prerequisites(status)) => {
                session.state.set(State::NeedsComponents);
                let needs_cmd = status["requiredCategories"]
                    .as_array()
                    .is_some_and(|categories| categories.iter().any(|category| category == "cmd"));
                self.imp().pending_launches.borrow_mut().insert(
                    ball.to_path_buf(),
                    PendingLaunch {
                        arguments: arguments.to_vec(),
                        fields,
                        status,
                        auto_resume: true,
                        checking: false,
                    },
                );
                self.sessions_updated();
                if current {
                    self.pending_changed();
                    if needs_cmd {
                        self.show_component_media("cmd", true);
                    } else {
                        self.show_page("application-installation");
                    }
                }
            }
            Ok(Prepared::Dependencies(audit)) => {
                session.state.set(State::Cancelled);
                self.sessions_updated();
                if current {
                    self.request_dependency_components(&audit);
                }
            }
            Ok(Prepared::Plan(plan, log)) => {
                let audit = &plan["dependencyAudit"];
                if audit["complete"].as_bool() == Some(false) {
                    let advice = engine::dependency_advice(audit);
                    self.toast(if advice.is_empty() {
                        audit["summary"]
                            .as_str()
                            .unwrap_or("Library check incomplete.")
                            .to_owned()
                    } else {
                        advice.join(" ")
                    });
                }
                self.start_session(ball.to_path_buf(), plan, log, session.clone())
                    .await;
            }
        }
    }

    /// Open the first missing OS/2 component group named by a launch audit.
    fn request_dependency_components(&self, audit: &Value) {
        let groups = audit["componentGroups"]
            .as_array()
            .map(Vec::as_slice)
            .unwrap_or_default();
        let titles = groups
            .iter()
            .filter_map(|group| group["title"].as_str().or_else(|| group["id"].as_str()))
            .collect::<Vec<_>>()
            .join(", ");
        let request = if titles.is_empty() {
            "Additional OS/2 components are required.".to_owned()
        } else {
            format!("Required OS/2 components: {titles}.")
        };
        self.toast(format!(
            "{request} Install them, then start the application again."
        ));
        if let Some(group) = groups.first().and_then(|group| group["id"].as_str()) {
            self.show_component_media(group, false);
        }
    }

    /// Lease the plan's startup services, spawn the program and await its exit.
    async fn start_session(
        &self,
        ball: PathBuf,
        mut plan: Value,
        log: PathBuf,
        session: Rc<Session>,
    ) -> bool {
        if plan.get("gnomeReceivingBuild").is_none() {
            plan["gnomeReceivingBuild"] = json!(self.build());
        }
        session.plan.replace(plan.clone());
        session.log.replace(Some(log.clone()));
        let startup = plan["startup"].as_array().cloned().unwrap_or_default();
        let deadline = Instant::now() + SERVICE_CLOSING_WAIT;
        let error = loop {
            let (leases, error) = self.imp().startup_pool.borrow_mut().acquire(
                &ball,
                &startup,
                &session.id,
                |service, index| {
                    spawn_process(service, &sessions::service_log(&log, index), None, true)
                        .map_err(|error| error.message().to_owned())
                },
                |process| process.identifier().is_some(),
            );
            session.leases.replace(leases);
            if error == Some(LeaseError::Closing)
                && !session.stop_sent.get()
                && Instant::now() < deadline
            {
                self.release_leases(&session);
                session.state.set(State::Preparing);
                glib::timeout_future(SERVICE_RETRY).await;
                continue;
            }
            break error;
        };
        let spawned = match error {
            Some(error) => Err(error.to_string()),
            None => spawn_process(&plan, &log, Some(&session.id), false)
                .map_err(|error| error.message().to_owned()),
        };
        let process = match spawned {
            Ok(process) => process,
            Err(error) => {
                self.release_leases(&session);
                session.state.set(State::Failed);
                self.sessions_updated();
                self.report_error(error);
                return false;
            }
        };
        session.process.replace(Some(process.clone()));
        session.state.set(State::Running);
        if self.ball().as_deref() == Some(ball.as_path()) {
            self.select_session(Some(session.id.clone()));
            self.show_page("activity");
        }
        self.sessions_updated();
        let window = self.clone();
        glib::spawn_future_local(async move {
            // The exit status is read from the process whether or not waiting reported an error.
            let _ = process.wait_future().await;
            window.completed(&session, &process).await;
        });
        true
    }

    /// Record a session's exit, release its services and refresh the ball.
    async fn completed(&self, session: &Rc<Session>, process: &gio::Subprocess) {
        let code = if process.has_exited() {
            process.exit_status()
        } else {
            -process.term_sig()
        };
        session.code.set(Some(code));
        session.state.set(State::Finishing);
        let log = session.log.borrow().clone().unwrap_or_default();
        if let Some(message) = sessions::abnormal_exit_message(
            &session.name,
            Some(code),
            session.stop_sent.get(),
            &log.to_string_lossy(),
        ) {
            let dialog = adw::AlertDialog::new(
                Some(&format!("{} stopped unexpectedly", session.name)),
                Some(&message),
            );
            dialog.add_response("close", "Close");
            dialog.present(Some(self));
        }
        self.release_leases(session);
        self.sessions_updated();
        let ball = session.ball.clone();
        let plan = session.plan.borrow().clone();
        let (path, receipt_plan) = (ball.clone(), plan.clone());
        let result = self
            .run(
                Work {
                    unlocked: true,
                    ..Work::default()
                },
                move || finish(&path, &receipt_plan, code),
            )
            .await;
        session.state.set(State::Finished);
        let current = self
            .ball()
            .is_some_and(|selected| sessions::canonical(&selected) == ball);
        match result {
            Ok(finished) => {
                if current {
                    self.apply_snapshot(finished.snapshot);
                    if let Some(page) = self.logs_page() {
                        page.set_summary(&finished.summary);
                    }
                    if let Some(error) = finished.receipt_error {
                        self.report_error(format!("Installer receipt could not be saved: {error}"));
                    }
                }
                self.sessions_updated();
                self.toast(format!("{} finished: exit {code}", session.name));
                if current && let Some(letter) = plan["gnomeMediaDrive"].as_str() {
                    self.install_completed(&ball, letter, code);
                }
                let audit = &plan["dependencyAudit"];
                if current && audit["scope"] == "launch" && engine::needs_dependency_media(audit) {
                    self.request_dependency_components(audit);
                }
            }
            Err(error) => {
                self.sessions_updated();
                self.toast(format!("Could not refresh {}: {error}", session.name));
            }
        }
    }

    /// Offer to keep or eject the installation medium once its installer exits.
    fn install_completed(&self, ball: &Path, letter: &str, code: i32) {
        let Some(snapshot) = self.snapshot() else {
            return;
        };
        let Some(device) = snapshot.drive(letter).and_then(crate::model::Drive::device) else {
            return;
        };
        if device.image.is_none() {
            return;
        }
        let result = if code >= 0 {
            format!("exit code {code}")
        } else {
            format!("signal {}", -code)
        };
        let dialog = adw::AlertDialog::new(
            Some("Installer Finished"),
            Some(&format!(
                "The installer ended with {result}. Keep the installation medium in {letter}: for later \
                 setup steps, or eject it. The drive letter is kept either way."
            )),
        );
        dialog.add_response("keep", "Keep Attached");
        dialog.add_response("eject", &format!("Eject {letter}:"));
        dialog.set_default_response(Some("keep"));
        dialog.set_close_response("keep");
        dialog.set_response_enabled("eject", !device.locked);
        let (window, ball, letter) = (self.clone(), ball.to_path_buf(), letter.to_owned());
        glib::spawn_future_local(async move {
            let choice = dialog.choose_future(Some(&window)).await;
            if choice == "eject"
                && window
                    .ball()
                    .is_some_and(|selected| sessions::canonical(&selected) == ball)
            {
                window.execute(vec!["eject".into(), letter]);
            }
        });
    }

    /// Release a session's startup services, stopping those it was the last to use.
    fn release_leases(&self, session: &Session) {
        let ball = session.ball.clone();
        let supervised: BTreeSet<ServiceKey> = session.plan.borrow()["startup"]
            .as_array()
            .into_iter()
            .flatten()
            .filter(|service| service["operationSupervised"].as_bool() == Some(true))
            .map(|service| StartupPool::<gio::Subprocess>::key(&ball, service))
            .collect();
        let leases = session.leases.take();
        self.imp()
            .startup_pool
            .borrow_mut()
            .release(&leases, &session.id, |key, process| {
                stop_owned_service(process, supervised.contains(key));
            });
    }

    /// Stop (or force stop) the session selected in Logs, after confirmation.
    pub fn stop_session(&self, force: bool) {
        let Some(session) = self
            .selected_session()
            .and_then(|id| self.sessions().borrow().get(&id))
        else {
            return;
        };
        if !stoppable(&session) {
            return;
        }
        let dialog = adw::AlertDialog::new(
            Some(if force {
                "Force Stop Application?"
            } else {
                "Stop Application?"
            }),
            Some(&format!(
                "Unsaved work in {} may be lost. Other application sessions keep running.",
                session.name
            )),
        );
        dialog.add_response("cancel", "Cancel");
        dialog.add_response(
            "stop",
            if force {
                "Force Stop"
            } else {
                "Stop Application"
            },
        );
        dialog.set_response_appearance("stop", adw::ResponseAppearance::Destructive);
        dialog.set_close_response("cancel");
        let window = self.clone();
        glib::spawn_future_local(async move {
            if dialog.choose_future(Some(&window)).await != "stop" || !stoppable(&session) {
                return;
            }
            let process = session.process.borrow().clone();
            match process {
                None => session.state.set(State::Cancelling),
                Some(process) => {
                    if force && session.supervised() {
                        process.send_signal(sessions::SIGUSR2);
                    } else if force {
                        process.force_exit();
                    } else {
                        process.send_signal(sessions::SIGTERM);
                    }
                    session.state.set(State::Stopping);
                }
            }
            session.stop_sent.set(true);
            window.sessions_updated();
        });
    }

    /// The session shown in Logs; None shows Activity and Audits.
    pub fn selected_session(&self) -> Option<String> {
        self.imp().selected_session.borrow().clone()
    }

    /// Show a session (or Activity and Audits) in Logs and update the stop actions.
    pub fn select_session(&self, id: Option<String>) {
        self.imp().selected_session.replace(id);
        self.update_stop_actions();
    }

    /// The last dependency audit: heading, output and log file.
    pub fn audit_activity(&self) -> (String, String, Option<PathBuf>) {
        self.imp().audit_activity.borrow().clone()
    }

    /// Enable Stop before a stop request and Force Stop after one, for the selected session.
    pub fn update_stop_actions(&self) {
        let session = self
            .selected_session()
            .and_then(|id| self.sessions().borrow().get(&id));
        let stoppable = session.as_deref().is_some_and(stoppable);
        let sent = session.is_some_and(|session| session.stop_sent.get());
        for (name, enabled) in [
            ("stop", stoppable && !sent),
            ("force-stop", stoppable && sent),
        ] {
            if let Some(action) = self.lookup_action(name).and_downcast::<gio::SimpleAction>() {
                action.set_enabled(enabled);
            }
        }
    }

    /// Update session-dependent actions and notify observers of a session change.
    pub fn sessions_updated(&self) {
        self.update_stop_actions();
        self.emit_sessions_changed();
    }

    fn logs_page(&self) -> Option<LogsPage> {
        self.page("activity").and_downcast::<LogsPage>()
    }

    /// Refresh sections that present the pending launch.
    fn pending_changed(&self) {
        self.update_pending_banner();
        self.emit_by_name::<()>("snapshot-changed", &[]);
    }

    /// The selected ball's pending launch.
    pub fn pending_launch(&self) -> Option<PendingLaunch> {
        let ball = self.ball()?;
        self.imp().pending_launches.borrow().get(&ball).cloned()
    }

    pub fn continue_pending_launch(&self) {
        let Some(ball) = self.ball() else { return };
        if self.component_active(&ball) {
            return;
        }
        let Some(pending) = self.imp().pending_launches.borrow_mut().remove(&ball) else {
            return;
        };
        if pending.status["ready"].as_bool() != Some(true) {
            self.imp()
                .pending_launches
                .borrow_mut()
                .insert(ball, pending);
            return;
        }
        if !self.launch(pending.arguments.clone(), pending.fields.clone()) {
            self.imp()
                .pending_launches
                .borrow_mut()
                .insert(ball, pending);
        }
        self.pending_changed();
    }

    pub fn cancel_pending_launch(&self) {
        if let Some(ball) = self.ball() {
            self.imp().pending_launches.borrow_mut().remove(&ball);
        }
        self.pending_changed();
    }

    /// Re-check a pending launch's prerequisites, continuing it when ready if allowed.
    pub fn refresh_pending_launch(&self, auto: bool) {
        let Some(ball) = self.ball() else { return };
        let request = {
            let mut pending = self.imp().pending_launches.borrow_mut();
            let Some(pending) = pending.get_mut(&ball).filter(|pending| !pending.checking) else {
                return;
            };
            pending.checking = true;
            let mut request = object(pending.fields.clone());
            request.insert("action".into(), json!("launch-prerequisites"));
            request.insert("ball".into(), json!(ball));
            request.insert("arguments".into(), json!(pending.arguments));
            Value::Object(request)
        };
        let window = self.clone();
        glib::spawn_future_local(async move {
            let result = engine::spawn(move || engine::request(&request)).await;
            let current = window.ball().as_deref() == Some(ball.as_path());
            let resume = {
                let mut launches = window.imp().pending_launches.borrow_mut();
                let Some(pending) = launches.get_mut(&ball).filter(|pending| pending.checking)
                else {
                    return;
                };
                pending.checking = false;
                match result {
                    Ok(status) => {
                        pending.status = status;
                        if !current {
                            pending.auto_resume = false;
                        }
                    }
                    Err(error) => {
                        pending.auto_resume = false;
                        pending.status = json!({"ready": false, "reason": error.to_string()});
                    }
                }
                auto && pending.status["ready"].as_bool() == Some(true) && pending.auto_resume
            };
            if resume
                && current
                && !window.operation_busy()
                && !window.component_active(&ball)
                && window.sessions().borrow().active(None).is_empty()
            {
                window.continue_pending_launch();
            } else if current {
                window.pending_changed();
            }
        });
    }

    /// Rescan installed binaries' dependencies and show the audit in Logs.
    pub fn audit_dependencies(&self) {
        let Some(ball) = self.ball() else { return };
        if self.busy() || self.component_active(&ball) {
            return;
        }
        let build = self.build();
        self.imp()
            .audit_activity
            .replace(("Rescanning dependencies…".into(), String::new(), None));
        self.select_session(None);
        self.show_page("activity");
        self.sessions_updated();
        let window = self.clone();
        glib::spawn_future_local(async move {
            let path = ball.clone();
            let result = window
                .run(Work::default(), move || -> Result<(Value, Snapshot, String), Error> {
                    let report = engine::request(
                        &json!({"action": "audit-dependencies", "ball": path, "build": build, "resolve": false}),
                    )?;
                    let snapshot = engine::request_as(&json!({"action": "snapshot", "ball": path}))?;
                    let text = report["logPath"].as_str().map(|log| sessions::read_log(Path::new(log))).unwrap_or_default();
                    Ok((report, snapshot, text))
                })
                .await;
            if window.ball().as_deref() != Some(ball.as_path()) {
                return;
            }
            match result {
                Ok((report, snapshot, text)) => {
                    let log = report["logPath"].as_str().map(PathBuf::from);
                    window.imp().audit_activity.replace((
                        "Dependency audit finished".into(),
                        text,
                        log,
                    ));
                    window.select_session(None);
                    window.apply_snapshot(snapshot);
                    if let Some(page) = window.logs_page() {
                        page.set_summary(report["summary"].as_str().unwrap_or_default());
                    }
                    window.show_page("activity");
                }
                Err(error) => {
                    window.imp().audit_activity.replace((
                        "Operation could not be completed".into(),
                        String::new(),
                        None,
                    ));
                    window.report_error(error);
                }
            }
            window.sessions_updated();
        });
    }
}

fn stoppable(session: &Session) -> bool {
    session.is_active() && session.state.get() != State::Finishing
}

fn object(value: Value) -> Map<String, Value> {
    match value {
        Value::Object(map) => map,
        _ => Map::new(),
    }
}

/// A request for `action` with the launch's own fields taking precedence.
fn launch_request(action: &str, base: Value, fields: &Value) -> Value {
    let mut request = object(base);
    request.insert("action".into(), json!(action));
    if let Value::Object(fields) = fields {
        request.extend(
            fields
                .iter()
                .map(|(key, value)| (key.clone(), value.clone())),
        );
    }
    Value::Object(request)
}

/// Check prerequisites and launch dependencies, then prepare the launch plan.
fn prepare_checked(
    ball: &Path,
    build: &Path,
    arguments: &[String],
    original: &Value,
    fields: &Map<String, Value>,
) -> Result<Prepared, Error> {
    let fields = Value::Object(fields.clone());
    let requirements = engine::request(&launch_request(
        "launch-prerequisites",
        json!({"ball": ball, "arguments": arguments}),
        original,
    ))?;
    if requirements["ready"].as_bool() != Some(true) {
        return Ok(Prepared::Prerequisites(requirements));
    }
    let audit = engine::request(&launch_request(
        "launch-dependency-status",
        json!({"ball": ball, "build": build, "arguments": arguments}),
        &fields,
    ))?;
    if engine::needs_dependency_media(&audit)
        && audit["componentGroups"]
            .as_array()
            .is_some_and(|groups| !groups.is_empty())
    {
        return Ok(Prepared::Dependencies(audit));
    }
    let (plan, log) = prepare_launch(ball, build, arguments, &fields)?;
    Ok(Prepared::Plan(plan, log))
}

/// Ask the engine for a launch plan and choose the session log path.
fn prepare_launch(
    ball: &Path,
    build: &Path,
    arguments: &[String],
    fields: &Value,
) -> Result<(Value, PathBuf), Error> {
    let mut plan = engine::request(&launch_request(
        "prepare-launch",
        json!({"ball": ball, "build": build, "arguments": arguments}),
        fields,
    ))?;
    plan["gnomeReceivingBuild"] = json!(build);
    if let Some(letter) = sessions::media_drive(arguments) {
        plan["gnomeMediaDrive"] = json!(letter);
    }
    let stamp = glib::DateTime::now_utc()
        .and_then(|now| now.format("%Y%m%dT%H%M%S.%fZ"))
        .map_err(|error| Error::message(error.message.to_string()))?;
    let log = sessions::session_log(ball, &stamp);
    if let Some(folder) = log.parent() {
        std::fs::create_dir_all(folder)?;
    }
    Ok((plan, log))
}

/// Record an installer's receipt, then read the ball back.
fn finish(ball: &Path, plan: &Value, code: i32) -> Result<Finished, Error> {
    let receipt_error = (plan["installer"].as_bool() == Some(true))
        .then(|| {
            engine::request(&json!({
                "action": "record-install",
                "ball": ball,
                "build": plan["gnomeReceivingBuild"],
                "receipt": plan["receipt"],
                "programHost": plan["programHost"],
                "exitCode": code,
            }))
            .err()
            .map(|error| error.to_string())
        })
        .flatten();
    let snapshot = engine::request_as(&json!({"action": "snapshot", "ball": ball}))?;
    Ok(Finished {
        snapshot,
        receipt_error,
        summary: sessions::read_receipt(ball),
    })
}

/// Spawn a plan's command with merged output written to `log`.
fn spawn_process(
    plan: &Value,
    log: &Path,
    session: Option<&str>,
    service: bool,
) -> Result<gio::Subprocess, glib::Error> {
    let invalid = |message: &str| glib::Error::new(gio::IOErrorEnum::InvalidArgument, message);
    let command: Vec<&str> = plan["command"]
        .as_array()
        .map(|items| items.iter().filter_map(Value::as_str).collect())
        .unwrap_or_default();
    if command.is_empty() {
        return Err(invalid("The launch plan has no command."));
    }
    let mut flags = gio::SubprocessFlags::STDERR_MERGE;
    if service {
        flags |= gio::SubprocessFlags::STDIN_PIPE;
    }
    let launcher = gio::SubprocessLauncher::new(flags);
    launcher.set_stdout_file_path(Some(log));
    if let Some(cwd) = plan["cwd"].as_str() {
        launcher.set_cwd(cwd);
    }
    for key in plan["unsetEnvironment"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(Value::as_str)
    {
        launcher.unsetenv(key);
    }
    for (key, value) in plan["environment"].as_object().into_iter().flatten() {
        if let Some(value) = value.as_str() {
            launcher.setenv(key, value, true);
        }
    }
    for (key, value) in sessions::desktop_environment(plan, session, service) {
        launcher.setenv(key, value, true);
    }
    // A runtime launched from this manager uses the manager's own dialog helper
    // unless the launch plan or the user's environment names another.
    if let Some(helper) = crate::config::DIALOG_HELPER.as_deref()
        && plan["environment"].get("TWOINE_DIALOG").is_none()
        && std::env::var_os("TWOINE_DIALOG").is_none()
    {
        launcher.setenv("TWOINE_DIALOG", helper, true);
    }
    let argv: Vec<&OsStr> = command.iter().map(OsStr::new).collect();
    let process = launcher.spawn(&argv)?;
    if service && let Some(input) = process.stdin_pipe() {
        input.close(gio::Cancellable::NONE)?;
    }
    Ok(process)
}

/// Ask a released startup service to exit, escalating after a grace period.
fn stop_owned_service(process: &gio::Subprocess, supervised: bool) {
    let alive = |process: &gio::Subprocess| process.identifier().is_some();
    if !alive(process) {
        return;
    }
    process.send_signal(sessions::SIGTERM);
    let process = process.clone();
    glib::timeout_add_local_once(SERVICE_TERM_GRACE, move || {
        if !alive(&process) {
            return;
        }
        if supervised {
            process.send_signal(sessions::SIGUSR2);
            glib::timeout_add_local_once(SERVICE_INTERRUPT_GRACE, move || {
                if alive(&process) {
                    process.force_exit();
                }
            });
        } else {
            process.force_exit();
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launch_fields_override_request_defaults() {
        let request = launch_request(
            "prepare-launch",
            json!({"ball": "/b", "arguments": ["run", "C:\\X.EXE"]}),
            &json!({"resolve": false, "workingDirectories": {"C": "\\"}}),
        );
        assert_eq!(request["action"], "prepare-launch");
        assert_eq!(request["ball"], "/b");
        assert_eq!(request["resolve"], false);
        assert_eq!(request["workingDirectories"]["C"], "\\");
    }

    #[test]
    fn stopping_requires_an_active_unfinished_session() {
        let mut sessions = sessions::Sessions::default();
        let session = sessions
            .reserve(Path::new("/tmp/stop"), false, "App")
            .unwrap();
        assert!(stoppable(&session));
        session.state.set(State::Finishing);
        assert!(!stoppable(&session));
        session.state.set(State::Finished);
        assert!(!stoppable(&session));
    }
}
