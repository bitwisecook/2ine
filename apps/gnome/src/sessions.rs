// Public domain. No rights reserved. Wholly AI-authored.
//! Application sessions and exact-plan leases for manager-owned startup services.
//!
//! Applications in one ball may run concurrently. An installer runs alone in
//! its ball. Startup services named by a launch plan are shared by every
//! session that requests the identical plan and are released by the last one.

use std::cell::{Cell, RefCell};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::fmt;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use serde_json::{Value, json};

pub fn canonical(ball: &Path) -> PathBuf {
    ball.canonicalize().unwrap_or_else(|_| ball.to_path_buf())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Preparing,
    NeedsComponents,
    Running,
    Stopping,
    Cancelling,
    Finishing,
    Finished,
    Cancelled,
    Failed,
}

impl State {
    pub fn is_active(self) -> bool {
        matches!(
            self,
            Self::Preparing | Self::Running | Self::Stopping | Self::Cancelling | Self::Finishing
        )
    }
}

impl fmt::Display for State {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Preparing => "preparing",
            Self::NeedsComponents => "needs components",
            Self::Running => "running",
            Self::Stopping => "stopping",
            Self::Cancelling => "cancelling",
            Self::Finishing => "finishing",
            Self::Finished => "finished",
            Self::Cancelled => "cancelled",
            Self::Failed => "failed",
        })
    }
}

/// One application or installer run owned by the manager.
#[derive(Debug)]
pub struct Session {
    pub id: String,
    pub ball: PathBuf,
    pub installer: bool,
    pub name: String,
    pub started: String,
    pub state: Cell<State>,
    pub process: RefCell<Option<gtk::gio::Subprocess>>,
    pub plan: RefCell<Value>,
    pub log: RefCell<Option<PathBuf>>,
    pub code: Cell<Option<i32>>,
    pub stop_sent: Cell<bool>,
    pub leases: RefCell<Vec<ServiceKey>>,
}

impl Session {
    pub fn is_active(&self) -> bool {
        self.state.get().is_active()
    }

    /// `Name · start time · state` for the Logs session list.
    pub fn label(&self) -> String {
        format!("{} · {} · {}", self.name, self.started, self.state.get())
    }

    pub fn supervised(&self) -> bool {
        self.plan.borrow()["operationSupervised"].as_bool() == Some(true)
    }
}

/// The sessions started by this window, in start order.
#[derive(Debug, Default)]
pub struct Sessions {
    records: Vec<Rc<Session>>,
}

impl Sessions {
    pub fn active(&self, ball: Option<&Path>) -> Vec<Rc<Session>> {
        let ball = ball.map(canonical);
        self.records
            .iter()
            .filter(|session| {
                session.is_active() && ball.as_ref().is_none_or(|ball| &session.ball == ball)
            })
            .cloned()
            .collect()
    }

    /// Applications share a ball; installers and launch preparation are exclusive.
    pub fn can_start(&self, ball: &Path, installer: bool) -> bool {
        let peers = self.active(Some(ball));
        let exclusive = peers.iter().any(|peer| {
            peer.installer
                || matches!(
                    peer.state.get(),
                    State::Preparing | State::Cancelling | State::Finishing
                )
        });
        !exclusive && (!installer || peers.is_empty())
    }

    pub fn reserve(
        &mut self,
        ball: &Path,
        installer: bool,
        name: &str,
    ) -> Result<Rc<Session>, String> {
        if !self.can_start(ball, installer) {
            return Err("Wait for this ball’s installer or launch preparation to finish.".into());
        }
        let session = Rc::new(Session {
            id: gtk::glib::uuid_string_random().replace('-', ""),
            ball: canonical(ball),
            installer,
            name: name.to_owned(),
            started: gtk::glib::DateTime::now_local()
                .and_then(|now| now.format("%X"))
                .map(Into::into)
                .unwrap_or_default(),
            state: Cell::new(State::Preparing),
            process: RefCell::default(),
            plan: RefCell::new(json!({})),
            log: RefCell::default(),
            code: Cell::default(),
            stop_sent: Cell::default(),
            leases: RefCell::default(),
        });
        self.records.push(session.clone());
        Ok(session)
    }

    pub fn get(&self, id: &str) -> Option<Rc<Session>> {
        self.records
            .iter()
            .find(|session| session.id == id)
            .cloned()
    }

    pub fn for_ball(&self, ball: &Path) -> Vec<Rc<Session>> {
        let ball = canonical(ball);
        self.records
            .iter()
            .filter(|session| session.ball == ball)
            .cloned()
            .collect()
    }
}

/// Runtime desktop identity variables for a manager-owned guest process.
pub fn desktop_environment(
    plan: &Value,
    session: Option<&str>,
    service: bool,
) -> BTreeMap<String, String> {
    let mut values = BTreeMap::new();
    if service {
        values.insert("TWOINE_DESKTOP_AUXILIARY".into(), "1".into());
        return values;
    }
    if let Some(session) = session {
        values.insert("TWOINE_DESKTOP_SESSION".into(), session.into());
    }
    if let Some(name) = plan["desktopIdentity"]["name"]
        .as_str()
        .filter(|name| !name.is_empty())
    {
        values.insert("TWOINE_DESKTOP_NAME".into(), name.into());
    }
    values
}

pub const SIGKILL: i32 = 9;
pub const SIGTERM: i32 = 15;
/// Asks a supervised runtime to stop its guest operation immediately.
#[cfg(target_os = "linux")]
pub const SIGUSR2: i32 = 12;
#[cfg(not(target_os = "linux"))]
pub const SIGUSR2: i32 = 31;

/// The most output shown from a session or audit log.
pub const LOG_TAIL: u64 = 64 * 1024;

/// The last [`LOG_TAIL`] bytes of a log as text, or empty when it is absent.
pub fn read_log(path: &Path) -> String {
    use std::io::{Read as _, Seek as _, SeekFrom};
    let Ok(mut file) = std::fs::File::open(path) else {
        return String::new();
    };
    let size = file
        .metadata()
        .map(|metadata| metadata.len())
        .unwrap_or_default();
    let mut bytes = Vec::new();
    if file
        .seek(SeekFrom::Start(size.saturating_sub(LOG_TAIL)))
        .is_err()
        || file.read_to_end(&mut bytes).is_err()
    {
        return String::new();
    }
    String::from_utf8_lossy(&bytes).replace('\0', "")
}

/// A one-line summary of the ball's last installer receipt.
pub fn read_receipt(ball: &Path) -> String {
    let text = match std::fs::read(ball.join("logs/last-install.json")) {
        Ok(text) => text,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            return "No installer receipt yet".into();
        }
        Err(_) => return "The last installer receipt could not be read".into(),
    };
    let receipt: Value = serde_json::from_slice(&text).unwrap_or_default();
    match (
        receipt["exit_code"].as_i64(),
        receipt["runtime_id"].as_str(),
    ) {
        (Some(code), Some(runtime)) => {
            let runtime: String = runtime.chars().take(16).collect();
            format!("Last installer: exit {code} · runtime {runtime}")
        }
        _ => "The last installer receipt could not be read".into(),
    }
}

/// The session log for a launch started at `stamp` (UTC `%Y%m%dT%H%M%S.%fZ`).
pub fn session_log(ball: &Path, stamp: &str) -> PathBuf {
    ball.join("logs").join(format!("gnome-{stamp}.log"))
}

/// The log of the `index`th startup service beside a session log.
pub fn service_log(log: &Path, index: usize) -> PathBuf {
    log.with_extension(format!("startup-{index}.log"))
}

/// The installation medium drive for `install` arguments naming a drive path.
pub fn media_drive(arguments: &[String]) -> Option<String> {
    if arguments.first().map(String::as_str) != Some("install") {
        return None;
    }
    let at = if arguments.get(1).is_some_and(|argument| argument == "--cwd") {
        3
    } else {
        1
    };
    let program = arguments.get(at)?;
    let mut characters = program.chars();
    let letter = characters.next()?;
    (characters.next() == Some(':')).then(|| letter.to_uppercase().collect())
}

/// The Logs heading for a session.
pub fn session_title(session: &Session) -> String {
    let state = session.state.get();
    match session.code.get() {
        _ if state == State::Preparing => "Preparing application…".into(),
        Some(code) if !session.is_active() => format!("{} · exit {code}", session.name),
        _ => format!("{} · {state}", session.name),
    }
}

/// Explain a death the worker could not report itself, or None for a normal end.
/// `code` is the exit status, or the negated signal number.
pub fn abnormal_exit_message(
    name: &str,
    code: Option<i32>,
    stop_sent: bool,
    log: &str,
) -> Option<String> {
    let code = code.filter(|&code| code < 0 && !stop_sent)?;
    let cause = if code == -SIGKILL {
        "The system or another program killed it, for example because memory ran out."
    } else {
        "2ine could not record what happened."
    };
    Some(format!(
        "{name} was terminated by signal {}. {cause}\n\nThe session log is at {log}.",
        -code
    ))
}

/// A startup service identity: its ball and guest program (or exact command).
pub type ServiceKey = (PathBuf, String);

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum LeaseError {
    /// A previous holder's service is still shutting down; retry shortly.
    #[error("Startup service cleanup is still finishing. Retry the launch shortly.")]
    Closing,
    #[error("A launch requests conflicting settings for one startup service.")]
    Conflicting,
    #[error(
        "Startup service settings differ from a running application. Close the affected ball’s applications before changing service settings."
    )]
    Differs,
    #[error(
        "A shared startup service exited. Close the affected ball’s running applications before restarting it."
    )]
    Exited,
    #[error("{0}")]
    Spawn(String),
}

#[derive(Debug)]
struct Lease<P> {
    process: P,
    users: BTreeSet<String>,
    fingerprint: String,
}

/// A copy of `value` with object keys in sorted order at every level.
fn sorted_keys(value: &Value) -> Value {
    match value {
        Value::Object(map) => {
            let mut entries: Vec<_> = map.iter().collect();
            entries.sort_by(|left, right| left.0.cmp(right.0));
            Value::Object(
                entries
                    .into_iter()
                    .map(|(key, value)| (key.clone(), sorted_keys(value)))
                    .collect(),
            )
        }
        Value::Array(items) => Value::Array(items.iter().map(sorted_keys).collect()),
        other => other.clone(),
    }
}

/// Shared startup services, keyed by ball and program.
#[derive(Debug)]
pub struct StartupPool<P> {
    records: HashMap<ServiceKey, Lease<P>>,
    closing: HashMap<ServiceKey, P>,
}

impl<P> Default for StartupPool<P> {
    fn default() -> Self {
        Self {
            records: HashMap::new(),
            closing: HashMap::new(),
        }
    }
}

impl<P: Clone> StartupPool<P> {
    pub fn key(ball: &Path, plan: &Value) -> ServiceKey {
        let identity = match plan["program"].as_str() {
            Some(program) => program.replace('/', "\\").to_uppercase(),
            None => plan["command"].to_string(),
        };
        (canonical(ball), identity)
    }

    fn fingerprint(plan: &Value) -> String {
        let mut unset: Vec<String> =
            serde_json::from_value(plan["unsetEnvironment"].clone()).unwrap_or_default();
        unset.sort();
        json!({
            "command": plan["command"],
            "cwd": plan["cwd"],
            "environment": sorted_keys(&plan["environment"]),
            "unsetEnvironment": unset,
        })
        .to_string()
    }

    /// Lease every service in `plans` for `session`, spawning those not running.
    /// On failure, leases taken so far are returned with the error; release them.
    pub fn acquire(
        &mut self,
        ball: &Path,
        plans: &[Value],
        session: &str,
        mut spawn: impl FnMut(&Value, usize) -> Result<P, String>,
        alive: impl Fn(&P) -> bool,
    ) -> (Vec<ServiceKey>, Option<LeaseError>) {
        let mut leases = Vec::new();
        self.closing.retain(|_, process| alive(process));
        for (index, plan) in plans.iter().enumerate() {
            let key = Self::key(ball, plan);
            let fingerprint = Self::fingerprint(plan);
            if leases.contains(&key) {
                if self.records[&key].fingerprint != fingerprint {
                    return (leases, Some(LeaseError::Conflicting));
                }
                continue;
            }
            if self.closing.contains_key(&key) {
                return (leases, Some(LeaseError::Closing));
            }
            if let Some(record) = self.records.get(&key) {
                if record.fingerprint != fingerprint {
                    return (leases, Some(LeaseError::Differs));
                }
                if !alive(&record.process) && !record.users.is_empty() {
                    return (leases, Some(LeaseError::Exited));
                }
            }
            if self
                .records
                .get(&key)
                .is_none_or(|record| !alive(&record.process))
            {
                match spawn(plan, index) {
                    Ok(process) => {
                        self.records.insert(
                            key.clone(),
                            Lease {
                                process,
                                users: BTreeSet::new(),
                                fingerprint,
                            },
                        );
                    }
                    Err(error) => return (leases, Some(LeaseError::Spawn(error))),
                }
            }
            self.records
                .get_mut(&key)
                .expect("leased record")
                .users
                .insert(session.to_owned());
            leases.push(key);
        }
        (leases, None)
    }

    /// Release a session's leases, stopping services that have no remaining users.
    pub fn release(
        &mut self,
        leases: &[ServiceKey],
        session: &str,
        mut stop: impl FnMut(&ServiceKey, &P),
    ) {
        for key in leases {
            let Some(record) = self.records.get_mut(key) else {
                continue;
            };
            record.users.remove(session);
            if record.users.is_empty()
                && let Some(record) = self.records.remove(key)
            {
                stop(key, &record.process);
                self.closing.insert(key.clone(), record.process);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_an_unrequested_signal_death_needs_a_dialog() {
        assert_eq!(abnormal_exit_message("App", Some(0), false, "log"), None);
        assert_eq!(abnormal_exit_message("App", Some(1), false, "log"), None);
        assert_eq!(abnormal_exit_message("App", Some(-15), true, "log"), None);
        let killed =
            abnormal_exit_message("App", Some(-9), false, "/ball/logs/session.log").unwrap();
        assert!(
            killed.contains("signal 9")
                && killed.contains("memory")
                && killed.contains("/ball/logs")
        );
        assert!(
            abnormal_exit_message("App", Some(-11), false, "log")
                .unwrap()
                .contains("could not record")
        );
    }

    #[test]
    fn applications_share_a_ball_while_installers_are_exclusive() {
        let mut sessions = Sessions::default();
        let first = sessions
            .reserve(Path::new("/tmp/a"), false, "First")
            .unwrap();
        assert!(!sessions.can_start(Path::new("/tmp/a"), false));
        first.state.set(State::Running);
        let second = sessions
            .reserve(Path::new("/tmp/a"), false, "Second")
            .unwrap();
        assert_ne!(first.id, second.id);
        assert!(!sessions.can_start(Path::new("/tmp/a"), true));
        assert!(sessions.can_start(Path::new("/tmp/b"), true));
    }

    #[test]
    fn desktop_identity_names_sessions_and_marks_services_auxiliary() {
        let plan = json!({"desktopIdentity": {"name": "Word Pro"}});
        let values = desktop_environment(&plan, Some("abc"), false);
        assert_eq!(values["TWOINE_DESKTOP_SESSION"], "abc");
        assert_eq!(values["TWOINE_DESKTOP_NAME"], "Word Pro");
        assert_eq!(
            desktop_environment(&plan, Some("abc"), true)["TWOINE_DESKTOP_AUXILIARY"],
            "1"
        );
    }

    #[test]
    fn services_are_shared_then_closed_by_the_last_holder() {
        let mut pool = StartupPool::<u32>::default();
        let plan = json!({"program": "c:/svc.exe", "command": ["x"]});
        let mut spawned = 0;
        let (leases, error) = pool.acquire(
            Path::new("/b"),
            std::slice::from_ref(&plan),
            "one",
            |_, _| {
                spawned += 1;
                Ok(7)
            },
            |_| true,
        );
        assert!(error.is_none());
        let (more, error) = pool.acquire(
            Path::new("/b"),
            std::slice::from_ref(&plan),
            "two",
            |_, _| Ok(8),
            |_| true,
        );
        assert!(error.is_none());
        assert_eq!(spawned, 1);
        let mut stopped = Vec::new();
        pool.release(&leases, "one", |_, process| stopped.push(*process));
        assert_eq!(stopped.len(), 0);
        pool.release(&more, "two", |_, process| stopped.push(*process));
        assert_eq!(stopped, [7]);
        let (_, error) = pool.acquire(Path::new("/b"), &[plan], "three", |_, _| Ok(9), |_| true);
        assert_eq!(error, Some(LeaseError::Closing));
    }

    #[test]
    fn completion_and_stop_state_are_session_specific() {
        let mut sessions = Sessions::default();
        let first = sessions
            .reserve(Path::new("/tmp/a"), false, "First")
            .unwrap();
        first.state.set(State::Running);
        let second = sessions
            .reserve(Path::new("/tmp/a"), false, "Second")
            .unwrap();
        second.state.set(State::Running);
        first.state.set(State::Stopping);
        first.stop_sent.set(true);
        assert!(!second.stop_sent.get());
        first.state.set(State::Finished);
        first.code.set(Some(-15));
        let active = sessions.active(Some(Path::new("/tmp/a")));
        assert_eq!(active.len(), 1);
        assert!(Rc::ptr_eq(&active[0], &second));
        assert_eq!(second.code.get(), None);
        second.state.set(State::Finished);
        assert!(sessions.can_start(Path::new("/tmp/a"), true));
    }

    fn service() -> Value {
        json!({
            "program": "C:\\OS2\\SOMDD.EXE",
            "command": ["loader", "SOMDD.EXE"],
            "cwd": "/tmp",
            "environment": {"A": "1", "B": "2"},
            "unsetEnvironment": ["Z", "Y"],
        })
    }

    /// A pool of fake services whose liveness the test controls.
    #[derive(Default)]
    struct Fixture {
        pool: StartupPool<usize>,
        alive: Vec<bool>,
    }

    impl Fixture {
        fn acquire(
            &mut self,
            session: &str,
            plans: &[Value],
            ball: &str,
        ) -> (Vec<ServiceKey>, Option<LeaseError>) {
            let alive = self.alive.clone();
            let spawned = &mut self.alive;
            self.pool.acquire(
                Path::new(ball),
                plans,
                session,
                |_, _| {
                    spawned.push(true);
                    Ok(spawned.len() - 1)
                },
                |&child| alive.get(child).copied().unwrap_or(true),
            )
        }
    }

    #[test]
    fn service_stays_until_the_last_app_exits_and_other_balls_do_not_share() {
        let mut fixture = Fixture::default();
        let (first, _) = fixture.acquire("one", &[service()], "/tmp/a");
        let (second, _) = fixture.acquire("two", &[service()], "/tmp/a");
        assert_eq!(fixture.alive.len(), 1);
        let mut stopped = Vec::new();
        fixture
            .pool
            .release(&first, "one", |_, &child| stopped.push(child));
        assert_eq!(stopped.len(), 0);
        let (other, _) = fixture.acquire("other", &[service()], "/tmp/b");
        assert_eq!(fixture.alive.len(), 2);
        fixture
            .pool
            .release(&second, "two", |_, &child| stopped.push(child));
        assert_eq!(stopped, [0]);
        fixture
            .pool
            .release(&other, "other", |_, &child| stopped.push(child));
        assert_eq!(stopped.len(), 2);
    }

    #[test]
    fn same_logical_service_rejects_settings_changes_and_reuses_identical_plan() {
        let mut fixture = Fixture::default();
        fixture.acquire("one", &[service()], "/tmp/a");
        for (field, value) in [
            ("cwd", json!("/other")),
            ("command", json!(["loader", "OTHER.EXE"])),
            ("environment", json!({"A": "changed"})),
        ] {
            let mut plan = service();
            plan[field] = value;
            let (leases, error) = fixture.acquire(field, &[plan], "/tmp/a");
            assert_eq!(leases.len(), 0);
            assert_eq!(error, Some(LeaseError::Differs));
        }
        assert_eq!(fixture.alive.len(), 1);
        let mut same = service();
        same["environment"] = json!({"B": "2", "A": "1"});
        same["unsetEnvironment"] = json!(["Y", "Z"]);
        assert_eq!(fixture.acquire("same", &[same], "/tmp/a").1, None);
        assert_eq!(fixture.alive.len(), 1);
        let mut another = service();
        another["program"] = json!("C:\\OS2\\OTHER.EXE");
        another["command"] = json!(["loader", "OTHER.EXE"]);
        assert_eq!(fixture.acquire("another", &[another], "/tmp/a").1, None);
        assert_eq!(fixture.alive.len(), 2);
    }

    #[test]
    fn last_service_cleanup_blocks_relaunch_until_owned_child_has_exited() {
        let mut fixture = Fixture::default();
        let (lease, _) = fixture.acquire("one", &[service()], "/tmp/a");
        fixture.pool.release(&lease, "one", |_, _| {});
        let (leases, error) = fixture.acquire("new", &[service()], "/tmp/a");
        assert_eq!(leases.len(), 0);
        assert_eq!(error, Some(LeaseError::Closing));
        fixture.alive[0] = false;
        assert_eq!(fixture.acquire("fresh", &[service()], "/tmp/a").1, None);
        assert_eq!(fixture.alive.len(), 2);
    }

    #[test]
    fn dead_shared_service_rejects_new_launch_until_older_leases_finish() {
        let mut fixture = Fixture::default();
        let (first, _) = fixture.acquire("one", &[service()], "/tmp/a");
        fixture.alive[0] = false;
        let (second, error) = fixture.acquire("two", &[service()], "/tmp/a");
        assert_eq!(second.len(), 0);
        assert!(
            error
                .as_ref()
                .is_some_and(|error| error.to_string().contains("Close"))
        );
        assert_eq!(fixture.alive.len(), 1);
        fixture.pool.release(&first, "one", |_, _| {});
        assert_eq!(fixture.acquire("fresh", &[service()], "/tmp/a").1, None);
        assert_eq!(fixture.alive.len(), 2);
    }

    #[test]
    fn partial_startup_failure_releases_only_this_sessions_owned_leases() {
        let mut fixture = Fixture::default();
        let (shared, _) = fixture.acquire("existing", &[service()], "/tmp/a");
        let mut bad = service();
        bad["program"] = json!("C:\\OS2\\BAD.EXE");
        bad["cwd"] = json!("/bad");
        let (leases, error) = fixture.pool.acquire(
            Path::new("/tmp/a"),
            &[service(), bad],
            "new",
            |_, index| {
                if index > 0 {
                    Err("startup failure".into())
                } else {
                    Ok(9)
                }
            },
            |_| true,
        );
        assert_eq!(error, Some(LeaseError::Spawn("startup failure".into())));
        let mut stopped = Vec::new();
        fixture
            .pool
            .release(&leases, "new", |_, &child| stopped.push(child));
        assert_eq!(stopped.len(), 0);
        fixture
            .pool
            .release(&shared, "existing", |_, &child| stopped.push(child));
        assert_eq!(stopped.len(), 1);
    }

    #[test]
    fn logs_are_tailed_and_receipts_summarised() {
        let directory = tempfile::tempdir().unwrap();
        let log = directory.path().join("session.log");
        assert_eq!(read_log(&log), "");
        let mut text = vec![b'a'; usize::try_from(LOG_TAIL).unwrap()];
        text.splice(0..0, b"head\0".iter().copied());
        text.extend_from_slice(b"tail\0\xff");
        std::fs::write(&log, &text).unwrap();
        let tail = read_log(&log);
        assert!(!tail.contains("head") && tail.ends_with("tail\u{fffd}") && !tail.contains('\0'));
        assert_eq!(read_receipt(directory.path()), "No installer receipt yet");
        std::fs::create_dir(directory.path().join("logs")).unwrap();
        let receipt = directory.path().join("logs/last-install.json");
        std::fs::write(
            &receipt,
            r#"{"exit_code": 3, "runtime_id": "0123456789abcdef0123"}"#,
        )
        .unwrap();
        assert_eq!(
            read_receipt(directory.path()),
            "Last installer: exit 3 · runtime 0123456789abcdef"
        );
        std::fs::write(&receipt, "{").unwrap();
        assert_eq!(
            read_receipt(directory.path()),
            "The last installer receipt could not be read"
        );
    }

    #[test]
    fn launch_paths_and_media_drives() {
        let log = session_log(Path::new("/b"), "20260101T000000.123456Z");
        assert_eq!(log, Path::new("/b/logs/gnome-20260101T000000.123456Z.log"));
        assert_eq!(
            service_log(&log, 2),
            Path::new("/b/logs/gnome-20260101T000000.123456Z.startup-2.log")
        );
        let arguments =
            |values: &[&str]| values.iter().map(ToString::to_string).collect::<Vec<_>>();
        assert_eq!(
            media_drive(&arguments(&["install", "d:\\SETUP.EXE"])).as_deref(),
            Some("D")
        );
        assert_eq!(
            media_drive(&arguments(&["install", "--cwd", "C:\\", "E:\\X.EXE"])).as_deref(),
            Some("E")
        );
        assert_eq!(media_drive(&arguments(&["install", "SETUP.EXE"])), None);
        assert_eq!(media_drive(&arguments(&["run", "D:\\X.EXE"])), None);
    }

    #[test]
    fn session_titles_follow_state() {
        let mut sessions = Sessions::default();
        let session = sessions.reserve(Path::new("/tmp/t"), false, "App").unwrap();
        assert_eq!(session_title(&session), "Preparing application…");
        session.state.set(State::Running);
        assert_eq!(session_title(&session), "App · running");
        session.state.set(State::Finished);
        session.code.set(Some(0));
        assert_eq!(session_title(&session), "App · exit 0");
        session.code.set(None);
        session.state.set(State::Cancelled);
        assert_eq!(session_title(&session), "App · cancelled");
    }
}
