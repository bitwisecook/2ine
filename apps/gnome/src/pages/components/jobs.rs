// Public domain. No rights reserved. Wholly AI-authored.
//! Component file state, status labels and desired-state reconciliation.
//!
//! The engine reports the actual state of each planned file. The pane keeps
//! the user's desired selection in [`DesiredComponents`] and reconciles it
//! with background `import-os2-support` and `remove-os2-components` jobs, one
//! batch at a time. Only unchanged files that 2ine installed and still owns
//! are ever removed; modified, unowned and protected files stay checked.

use std::collections::{BTreeSet, HashMap, HashSet};
use std::rc::Rc;
use std::sync::mpsc;
use std::sync::{Arc, LazyLock, Mutex};
use std::thread;

use serde::Serialize;
use serde_json::{Map, Value, json};

use crate::engine::Error;

/// A planned file's media source, shared between the tree and the queue.
pub type Source = Rc<str>;

/// Per-source edit revisions captured when a job starts.
pub type Batch = HashMap<Source, u64>;

/// The ownership state of one installed destination.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FileState {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub managed: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
    pub present: bool,
    pub removable: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub actual_path: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub installed_source: Option<String>,
}

/// Project an `os2-component-state` file record. Only present files with the
/// explicit unchanged `managed` status may be removed.
pub fn file_state(record: &Value) -> FileState {
    let present = record["present"].as_bool() == Some(true);
    let status = record["status"].as_str().unwrap_or_default();
    let text = |key: &str| {
        record[key]
            .as_str()
            .filter(|value| !value.is_empty())
            .map(str::to_owned)
    };
    FileState {
        managed: record
            .get("managed")
            .map(|value| value.as_bool() == Some(true)),
        status: record["status"].as_str().map(str::to_owned),
        reason: record["reason"].as_str().map(str::to_owned),
        present,
        removable: record["managed"].as_bool() == Some(true) && present && status == "managed",
        actual_path: text("path"),
        installed_source: text("source"),
    }
}

/// The combined state of every destination selected from one source.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SourceState {
    pub present: bool,
    pub managed: bool,
    pub status: String,
    pub removable: bool,
    pub removable_count: usize,
    pub actual_files: Vec<FileState>,
    pub actual_paths: Vec<String>,
    pub actual_path: Option<String>,
    pub installed_source: Option<String>,
    pub reason: String,
}

impl SourceState {
    /// Merge this state into a planned file or tree node, keeping its plan fields.
    pub fn apply(&self, node: &mut Map<String, Value>) {
        if let Ok(Value::Object(fields)) = serde_json::to_value(self) {
            node.extend(fields);
        }
    }
}

fn severity(status: &str) -> u8 {
    match status {
        "missing" => 0,
        "managed" => 1,
        "unowned" => 2,
        "modified" => 3,
        "protected" => 4,
        _ => 5,
    }
}

/// Group state records by the source they were requested for.
///
/// A missing alternate source never borrows another source's ownership of
/// the same destination, and records without `selectedSource` are ignored.
pub fn source_states(records: &[Value]) -> HashMap<String, SourceState> {
    let mut grouped: Vec<(String, Vec<(String, FileState)>)> = Vec::new();
    for record in records {
        let Some(source) = record["selectedSource"]
            .as_str()
            .filter(|source| !source.is_empty())
        else {
            continue;
        };
        let key = record["path"].as_str().unwrap_or_default().to_lowercase();
        let index = grouped
            .iter()
            .position(|(known, _)| known == source)
            .unwrap_or_else(|| {
                grouped.push((source.to_owned(), Vec::new()));
                grouped.len() - 1
            });
        let files = &mut grouped[index].1;
        let state = file_state(record);
        match files.iter_mut().find(|(path, _)| *path == key) {
            Some(existing) => existing.1 = state,
            None => files.push((key, state)),
        }
    }
    grouped
        .into_iter()
        .map(|(source, files)| {
            let files: Vec<FileState> = files.into_iter().map(|(_, state)| state).collect();
            let mut status = String::new();
            for file in &files {
                let candidate = file.status.as_deref().unwrap_or("protected");
                if status.is_empty() || severity(candidate) > severity(&status) {
                    candidate.clone_into(&mut status);
                }
            }
            let present: Vec<&FileState> = files.iter().filter(|file| file.present).collect();
            let removable_count = files.iter().filter(|file| file.removable).count();
            let actual_paths: Vec<String> = present
                .iter()
                .filter_map(|file| file.actual_path.clone())
                .collect();
            let mut reasons: Vec<&str> = Vec::new();
            for reason in files
                .iter()
                .filter_map(|file| file.reason.as_deref())
                .filter(|reason| !reason.is_empty())
            {
                if !reasons.contains(&reason) {
                    reasons.push(reason);
                }
            }
            let state = SourceState {
                present: !present.is_empty(),
                managed: !present.is_empty() && status == "managed",
                removable: removable_count > 0,
                removable_count,
                actual_path: actual_paths.first().cloned(),
                installed_source: present
                    .iter()
                    .find_map(|file| file.installed_source.clone()),
                reason: reasons.join("\n"),
                actual_paths,
                status,
                actual_files: files,
            };
            (source, state)
        })
        .collect()
}

/// `{source, path}` pairs for `os2-component-state` and removal requests.
///
/// Files supplied by 2ine are skipped; distinct sources at one planned path
/// are kept apart.
pub fn state_selection(files: &[Value], source_ids: Option<&HashSet<Source>>) -> Vec<Value> {
    let mut seen = HashSet::new();
    let mut result = Vec::new();
    for file in files {
        let Some(source) = file["source"].as_str().filter(|source| !source.is_empty()) else {
            continue;
        };
        if file["provided"].as_bool() == Some(true)
            || source_ids.is_some_and(|ids| !ids.contains(source))
        {
            continue;
        }
        let path = file["path"].as_str().unwrap_or_default();
        if seen.insert((source.to_owned(), path.to_owned())) {
            result.push(json!({"source": source, "path": path}));
        }
    }
    result
}

/// The tooltip for a tree row: its planned path, actual destinations and reason.
pub fn file_tooltip(node: &Map<String, Value>) -> String {
    let text = |key: &str| {
        node.get(key)
            .and_then(Value::as_str)
            .filter(|value| !value.is_empty())
    };
    let path = text("path").unwrap_or_default();
    let mut tooltip = text("path")
        .or_else(|| text("detail"))
        .or_else(|| text("title"))
        .unwrap_or_default()
        .to_owned();
    let actual: Vec<&str> = node
        .get("actualPaths")
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(Value::as_str)
        .collect();
    if !actual.is_empty() && (actual.len() > 1 || actual[0].to_lowercase() != path.to_lowercase()) {
        tooltip.push_str("\nInstalled at:\n");
        tooltip.push_str(&actual.join("\n"));
    }
    if let Some(reason) = text("reason") {
        tooltip.push('\n');
        tooltip.push_str(reason);
    }
    tooltip
}

/// The Status column text for a file row.
pub fn state_label(record: &Map<String, Value>, active: bool, queued: bool) -> &'static str {
    if active {
        return "Updating…";
    }
    if queued {
        return "Queued…";
    }
    let present = record.get("present").and_then(Value::as_bool) == Some(true);
    match record.get("status").and_then(Value::as_str) {
        Some("protected") => {
            return if present {
                "Protected · kept"
            } else {
                "Protected"
            };
        }
        Some("modified") => return "Modified · kept",
        Some("unowned") => return "Existing · not managed",
        _ => {}
    }
    let installed = record
        .get("installedSource")
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty());
    if let Some(installed) = installed
        && record.get("source").and_then(Value::as_str) != Some(installed)
    {
        return "Another version at destination";
    }
    if present {
        "Installed"
    } else if record.get("preferred").and_then(Value::as_bool) == Some(false) {
        "Alternative version"
    } else {
        "To add"
    }
}

/// The Status column text for a group: ticked sources and installed files.
pub fn group_label<'a>(
    sources: &[Source],
    selected: &HashSet<Source>,
    files: impl IntoIterator<Item = &'a Map<String, Value>>,
) -> String {
    let flag =
        |file: &Map<String, Value>, key: &str| file.get(key).and_then(Value::as_bool) == Some(true);
    let installed = files
        .into_iter()
        .filter(|file| flag(file, "present") && !flag(file, "disabled") && !flag(file, "provided"))
        .count();
    let unique: HashSet<&Source> = sources.iter().collect();
    let ticked = unique
        .iter()
        .filter(|source| selected.contains(**source))
        .count();
    format!("{ticked}/{} selected · {installed} installed", unique.len())
}

pub fn selection_summary(ticked: usize, pending: usize) -> String {
    format!(
        "{ticked} ticked · {pending} {} pending",
        if pending == 1 { "change" } else { "changes" }
    )
}

/// A component group recommended because registered applications require it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Recommendation {
    pub id: String,
    pub title: String,
    pub required_by: Vec<String>,
}

/// Groups from the stored dependency audit that name the programs needing them.
pub fn recommended_groups(audit: Option<&Value>) -> Vec<Recommendation> {
    let groups = audit
        .and_then(|audit| audit["componentGroups"].as_array())
        .map(Vec::as_slice)
        .unwrap_or_default();
    groups
        .iter()
        .filter_map(|group| {
            let id = group["id"].as_str().filter(|id| !id.is_empty())?;
            let required_by: Vec<String> = group["requiredBy"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(Value::as_str)
                .map(|path| {
                    path.replace('/', "\\")
                        .rsplit('\\')
                        .next()
                        .unwrap_or_default()
                        .to_owned()
                })
                .collect();
            if required_by.is_empty() {
                return None;
            }
            let title = group["title"]
                .as_str()
                .filter(|title| !title.is_empty())
                .unwrap_or(id);
            Some(Recommendation {
                id: id.to_owned(),
                title: title.to_owned(),
                required_by,
            })
        })
        .collect()
}

/// The next reconciliation step.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Step {
    Install,
    Remove,
}

/// The user's desired component selection reconciled against actual state.
///
/// Each source records the revision of its last edit so that a failed or
/// partially applied batch snaps back to actual state only for sources the
/// user did not change while the batch ran.
#[derive(Debug, Clone, Default)]
pub struct DesiredComponents {
    pub allowed: HashSet<Source>,
    pub desired: BTreeSet<Source>,
    pub actual: HashMap<Source, SourceState>,
    pub revisions: HashMap<Source, u64>,
    pub revision: u64,
    pub initialized: bool,
}

impl DesiredComponents {
    pub fn new(allowed: impl IntoIterator<Item = Source>, selected: &HashSet<Source>) -> Self {
        let allowed: HashSet<Source> = allowed.into_iter().collect();
        let desired = selected
            .iter()
            .filter(|source| allowed.contains(*source))
            .cloned()
            .collect();
        Self {
            allowed,
            desired,
            ..Self::default()
        }
    }

    /// Record a user edit; returns protected sources that were held at their actual state.
    pub fn edit(&mut self, selected: &HashSet<Source>) -> BTreeSet<Source> {
        let selected: BTreeSet<Source> = selected
            .iter()
            .filter(|source| self.allowed.contains(*source))
            .cloned()
            .collect();
        self.revision += 1;
        for source in self.desired.symmetric_difference(&selected) {
            self.revisions.insert(source.clone(), self.revision);
        }
        self.desired = selected;
        self.clamp_protected()
    }

    /// Observe new actual state. Once initialised, files installed or removed
    /// outside the queue are followed unless the user has pending intent for them.
    pub fn update_actual(&mut self, records: &HashMap<String, SourceState>) {
        let previous = self.present();
        let mut protected_intent = self.pending();
        protected_intent.extend(self.revisions.keys().cloned());
        for (source, record) in records {
            if let Some(known) = self.allowed.get(source.as_str()) {
                self.actual.insert(known.clone(), record.clone());
            }
        }
        if self.initialized {
            let actual = self.present();
            for source in actual.difference(&previous) {
                if !protected_intent.contains(source) {
                    self.desired.insert(source.clone());
                }
            }
            for source in previous.difference(&actual) {
                if !protected_intent.contains(source) {
                    self.desired.remove(source);
                }
            }
        }
    }

    pub fn present(&self) -> BTreeSet<Source> {
        self.actual
            .iter()
            .filter(|(_, record)| record.present)
            .map(|(source, _)| source.clone())
            .collect()
    }

    /// Adopt the first actual state. A ball with component history keeps exactly
    /// what is installed unless the user expressed intent first.
    pub fn initialize(&mut self, has_history: bool, explicit: bool) {
        if has_history && !explicit {
            self.desired = self.present();
        } else {
            for source in self.present() {
                if !self.revisions.contains_key(&source) {
                    self.desired.insert(source);
                }
            }
        }
        self.initialized = true;
        self.clamp_protected();
    }

    /// Present files that cannot be removed, and protected destinations.
    pub fn protected(&self) -> HashSet<Source> {
        self.actual
            .iter()
            .filter(|(_, record)| {
                record.status == "protected"
                    || (record.present && (record.status != "managed" || !record.removable))
            })
            .map(|(source, _)| source.clone())
            .collect()
    }

    /// Hold protected sources at their actual presence; returns those changed.
    pub fn clamp_protected(&mut self) -> BTreeSet<Source> {
        let mut changed = BTreeSet::new();
        for source in self.protected() {
            let present = self
                .actual
                .get(&source)
                .is_some_and(|record| record.present);
            if self.desired.contains(&source) != present {
                if present {
                    self.desired.insert(source.clone());
                } else {
                    self.desired.remove(&source);
                }
                changed.insert(source);
            }
        }
        changed
    }

    pub fn pending(&self) -> BTreeSet<Source> {
        self.desired
            .symmetric_difference(&self.present())
            .cloned()
            .collect()
    }

    /// Removals of owned unchanged files come before installs.
    pub fn next_step(&mut self) -> Option<(Step, BTreeSet<Source>)> {
        self.clamp_protected();
        let present = self.present();
        let remove: BTreeSet<Source> = present
            .difference(&self.desired)
            .filter(|source| {
                self.actual
                    .get(*source)
                    .is_some_and(|record| record.status == "managed" && record.removable)
            })
            .cloned()
            .collect();
        if !remove.is_empty() {
            return Some((Step::Remove, remove));
        }
        let install: BTreeSet<Source> = self.desired.difference(&present).cloned().collect();
        (!install.is_empty()).then_some((Step::Install, install))
    }

    pub fn batch<'a>(&self, attempted: impl IntoIterator<Item = &'a Source>) -> Batch {
        attempted
            .into_iter()
            .map(|source| {
                (
                    source.clone(),
                    self.revisions.get(source).copied().unwrap_or_default(),
                )
            })
            .collect()
    }

    /// Snap unedited sources of a failed batch back to actual state.
    pub fn failed(&mut self, batch: &Batch) {
        for (source, revision) in batch {
            if self.revisions.get(source).copied().unwrap_or_default() != *revision {
                continue;
            }
            if self.actual.get(source).is_some_and(|record| record.present) {
                self.desired.insert(source.clone());
            } else {
                self.desired.remove(source);
            }
        }
        self.clamp_protected();
    }

    /// Finish a batch: unedited sources whose intent was not met are returned
    /// and then snapped to actual state so they are never retried forever.
    pub fn settle(&mut self, batch: &Batch) -> BTreeSet<Source> {
        let unfinished = batch
            .iter()
            .filter(|(source, revision)| {
                self.revisions.get(*source).copied().unwrap_or_default() == **revision
                    && self.desired.contains(*source)
                        != self
                            .actual
                            .get(*source)
                            .is_some_and(|record| record.present)
            })
            .map(|(source, _)| source.clone())
            .collect();
        self.failed(batch);
        unfinished
    }

    pub fn cancel_queued(&mut self) {
        self.desired = self.present();
    }
}

type Task = Box<dyn FnOnce() + Send>;

/// A fixed set of worker threads for component jobs.
#[derive(Debug)]
pub struct Executor {
    tasks: Mutex<mpsc::Sender<Task>>,
}

impl Executor {
    fn new(name: &str, workers: usize) -> Self {
        let (tasks, receiver) = mpsc::channel::<Task>();
        let receiver = Arc::new(Mutex::new(receiver));
        for index in 0..workers {
            let receiver = receiver.clone();
            let spawned = thread::Builder::new()
                .name(format!("{name}-{index}"))
                .spawn(move || {
                    loop {
                        let task = match receiver.lock() {
                            Ok(receiver) => receiver.recv(),
                            Err(_) => break,
                        };
                        match task {
                            Ok(task) => task(),
                            Err(_) => break,
                        }
                    }
                });
            if let Err(error) = spawned {
                eprintln!("2ine: could not start component worker: {error}");
            }
        }
        Self {
            tasks: Mutex::new(tasks),
        }
    }

    /// Run `job` on a worker and await its result on the main loop.
    pub async fn run<T: Send + 'static>(
        &self,
        job: impl FnOnce() -> Result<T, Error> + Send + 'static,
    ) -> Result<T, Error> {
        let (sender, receiver) = async_channel::bounded(1);
        let task: Task = Box::new(move || {
            let _ = sender.send_blocking(job());
        });
        let queued = self.tasks.lock().map_err(|_| stopped())?.send(task).is_ok();
        if !queued {
            return Err(stopped());
        }
        receiver.recv().await.unwrap_or_else(|_| Err(stopped()))
    }
}

fn stopped() -> Error {
    Error::message("The OS/2 component worker stopped unexpectedly.")
}

/// Component imports, removals and media reads: up to four at once across balls.
pub static JOBS: LazyLock<Executor> = LazyLock::new(|| Executor::new("os2-components", 4));
/// Installed-status reads, progress polls and cancellation, one at a time.
pub static STATUS: LazyLock<Executor> = LazyLock::new(|| Executor::new("os2-component-status", 1));

#[cfg(test)]
mod tests {
    use super::*;

    fn set(values: &[&str]) -> HashSet<Source> {
        values.iter().map(|value| Source::from(*value)).collect()
    }

    fn ordered(values: &[&str]) -> BTreeSet<Source> {
        values.iter().map(|value| Source::from(*value)).collect()
    }

    fn map(value: &Value) -> Map<String, Value> {
        value.as_object().cloned().unwrap_or_default()
    }

    fn actual(status: &str, present: bool) -> SourceState {
        SourceState {
            present,
            status: status.into(),
            managed: status == "managed",
            removable: status == "managed" && present,
            ..SourceState::default()
        }
    }

    fn records(values: &[(&str, SourceState)]) -> HashMap<String, SourceState> {
        values
            .iter()
            .map(|(source, state)| ((*source).to_owned(), state.clone()))
            .collect()
    }

    #[test]
    fn modified_unowned_and_protected_payloads_are_never_offered_for_removal() {
        for status in [
            "modified",
            "protected",
            "unowned",
            "symlink",
            "not-regular",
            "unsafe",
        ] {
            assert!(
                !file_state(&json!({"present": true, "managed": true, "status": status})).removable
            );
        }
        assert!(!file_state(&json!({"present": true, "managed": false})).removable);
        assert!(!file_state(&json!({"present": false, "managed": true})).removable);
    }

    #[test]
    fn only_explicit_unchanged_managed_status_is_removable() {
        assert!(
            file_state(&json!({"present": true, "managed": true, "status": "managed"})).removable
        );
        assert!(
            !file_state(
                &json!({"present": true, "managed": true, "status": "unknown-future-status"})
            )
            .removable
        );
        assert!(!file_state(&json!({"present": true, "managed": true})).removable);
    }

    #[test]
    fn live_ownership_provenance_preserves_the_current_plan_source() {
        let mut planned = map(
            &json!({"source": "current.zip!/os2.iso!/file", "category": "help", "id": "current-source"}),
        );
        let state = source_states(&[json!({
            "selectedSource": "current.zip!/os2.iso!/file",
            "path": "C:\\OS2\\DLL\\HELPMGR.DLL", "present": true, "managed": true,
            "status": "managed", "source": "older.iso!/file", "category": "full"
        })]);
        state["current.zip!/os2.iso!/file"].apply(&mut planned);
        assert_eq!(planned["source"], "current.zip!/os2.iso!/file");
        assert_eq!(planned["category"], "help");
        assert_eq!(planned["id"], "current-source");
        assert_eq!(planned["installedSource"], "older.iso!/file");
        assert_eq!(
            file_state(&json!({"source": "older.iso!/file"}))
                .installed_source
                .as_deref(),
            Some("older.iso!/file")
        );
    }

    #[test]
    fn retained_variant_uses_actual_destination_without_rewriting_plan() {
        let mut planned = map(
            &json!({"source": "alternate", "id": "alternate", "category": "help", "path": r"C:\OS2\FILE.HLP"}),
        );
        let actual = r"C:\OS2\INSTALL\MEDIA\hash\FILE.HLP";
        let state = source_states(&[json!({
            "selectedSource": "alternate", "plannedPath": r"C:\OS2\FILE.HLP", "path": actual,
            "present": true, "managed": true, "status": "managed"
        })]);
        state["alternate"].apply(&mut planned);
        assert_eq!(planned["path"], r"C:\OS2\FILE.HLP");
        assert_eq!(planned["source"], "alternate");
        assert_eq!(planned["id"], "alternate");
        assert_eq!(planned["category"], "help");
        assert_eq!(planned["actualPath"], actual);
        assert_eq!(planned["removableCount"], 1);
        planned.insert("title".into(), json!("Help"));
        assert!(file_tooltip(&planned).contains(actual));
    }

    #[test]
    fn missing_alternate_cannot_borrow_another_sources_canonical_ownership() {
        let path = r"C:\OS2\FILE.HLP";
        let result = source_states(&[
            json!({"selectedSource": "main", "path": path, "present": true, "managed": true, "status": "managed"}),
            json!({"selectedSource": "alternate", "path": path, "present": false, "managed": false, "status": "missing"}),
        ]);
        assert!(result["main"].present && result["main"].removable);
        assert!(!result["alternate"].present && !result["alternate"].removable);
        assert_eq!(result["alternate"].actual_paths, Vec::<String>::new());
        assert!(
            source_states(&[
                json!({"path": path, "managed": true, "present": true, "status": "managed"})
            ])
            .is_empty()
        );
    }

    #[test]
    fn multiple_destinations_keep_modified_records_protected_individually() {
        let result = &source_states(&[
            json!({"selectedSource": "one", "path": r"C:\OS2\FILE.HLP", "present": true, "managed": true, "status": "managed"}),
            json!({"selectedSource": "one", "path": r"C:\OS2\BACKUP\FILE.HLP", "present": true,
                   "managed": false, "status": "modified", "reason": "User changes"}),
        ])["one"];
        assert_eq!(result.removable_count, 1);
        assert_eq!(result.actual_files.len(), 2);
        assert_eq!(result.status, "modified");
        assert!(!result.managed);
        assert_eq!(
            result
                .actual_files
                .iter()
                .map(|file| file.removable)
                .collect::<Vec<_>>(),
            [true, false]
        );
        assert_eq!(result.reason, "User changes");
    }

    #[test]
    fn source_selection_keeps_distinct_sources_at_same_planned_path() {
        let files = [
            json!({"source": "main", "path": "C:\\FILE"}),
            json!({"source": "alternate", "path": "C:\\FILE"}),
            json!({"source": "main", "path": "C:\\FILE"}),
            json!({"source": "provided", "path": "C:\\OTHER", "provided": true}),
        ];
        assert_eq!(
            state_selection(&files, None),
            [
                json!({"source": "main", "path": "C:\\FILE"}),
                json!({"source": "alternate", "path": "C:\\FILE"})
            ]
        );
        assert_eq!(
            state_selection(&files, Some(&set(&["alternate"]))),
            [json!({"source": "alternate", "path": "C:\\FILE"})]
        );
    }

    #[test]
    fn file_status_matches_macos_component_tree() {
        let label = |value: Value| state_label(&map(&value), false, false);
        assert_eq!(
            label(json!({"present": true, "status": "managed"})),
            "Installed"
        );
        assert_eq!(
            label(json!({"present": true, "status": "unowned"})),
            "Existing · not managed"
        );
        assert_eq!(
            label(json!({"present": true, "status": "modified"})),
            "Modified · kept"
        );
        assert_eq!(
            label(json!({"present": true, "status": "protected"})),
            "Protected · kept"
        );
        assert_eq!(
            label(json!({"present": false, "status": "protected"})),
            "Protected"
        );
        assert_eq!(label(json!({"present": false})), "To add");
        assert_eq!(
            label(json!({"present": false, "preferred": false})),
            "Alternative version"
        );
        assert_eq!(
            label(
                json!({"source": "a", "installedSource": "b", "present": true, "status": "managed"})
            ),
            "Another version at destination"
        );
        assert_eq!(
            label(json!({"source": "a", "installedSource": "a", "present": true})),
            "Installed"
        );
        assert_eq!(
            label(json!({"source": "a", "installedSource": null, "present": true})),
            "Installed"
        );
    }

    #[test]
    fn activity_overrides_file_state() {
        let record = map(&json!({"present": true, "status": "managed"}));
        assert_eq!(state_label(&record, true, true), "Updating…");
        assert_eq!(state_label(&record, false, true), "Queued…");
    }

    #[test]
    fn aggregate_state_reports_installed_source_of_present_files() {
        let states = source_states(&[
            json!({"selectedSource": "a", "path": "C:\\X", "present": false, "status": "missing"}),
            json!({"selectedSource": "a", "path": "C:\\Y", "present": true, "status": "managed", "managed": true, "source": "b"}),
        ]);
        assert_eq!(states["a"].installed_source.as_deref(), Some("b"));
    }

    #[test]
    fn group_status_counts_selected_and_installed_files() {
        let files = [
            map(&json!({"kind": "file", "present": true})),
            map(&json!({"kind": "file", "present": true, "disabled": true})),
            map(&json!({"kind": "file", "present": true, "provided": true})),
            map(&json!({"kind": "file", "present": false})),
        ];
        let sources: Vec<Source> = ["a", "b", "c"].into_iter().map(Source::from).collect();
        assert_eq!(
            group_label(&sources, &set(&["a", "z"]), &files),
            "1/3 selected · 1 installed"
        );
    }

    #[test]
    fn selection_summary_pluralises_changes() {
        assert_eq!(selection_summary(3, 1), "3 ticked · 1 change pending");
        assert_eq!(selection_summary(0, 2), "0 ticked · 2 changes pending");
    }

    #[test]
    fn recommended_groups_use_audited_requirers() {
        let audit = json!({"componentGroups": [
            {"id": "rexx", "title": "REXX", "requiredBy": ["C:\\APPS\\TOOL.EXE", "D:/X/Y.CMD"]},
            {"id": "fonts", "title": "Fonts", "requiredBy": []},
        ]});
        assert_eq!(
            recommended_groups(Some(&audit)),
            [Recommendation {
                id: "rexx".into(),
                title: "REXX".into(),
                required_by: vec!["TOOL.EXE".into(), "Y.CMD".into()]
            }]
        );
        assert_eq!(recommended_groups(None), []);
    }

    #[test]
    fn default_cmd_selection_immediately_produces_real_install_delta() {
        let mut queue = DesiredComponents::new(set(&["cmd", "font"]), &set(&["cmd"]));
        assert_eq!(queue.next_step(), Some((Step::Install, ordered(&["cmd"]))));
        queue.update_actual(&records(&[("cmd", actual("managed", true))]));
        assert_eq!(queue.next_step(), None);
    }

    #[test]
    fn restart_after_prior_operation_does_not_reinstall_unchecked_cmd_default() {
        let mut queue = DesiredComponents::new(set(&["cmd"]), &set(&["cmd"]));
        queue.initialize(true, false);
        assert_eq!(queue.next_step(), None);
        let mut explicit = DesiredComponents::new(set(&["cmd"]), &set(&["cmd"]));
        explicit.initialize(true, true);
        assert_eq!(
            explicit.next_step(),
            Some((Step::Install, ordered(&["cmd"])))
        );
        let mut first = DesiredComponents::new(set(&["cmd"]), &set(&["cmd"]));
        first.initialize(false, false);
        assert_eq!(first.next_step(), Some((Step::Install, ordered(&["cmd"]))));
    }

    #[test]
    fn external_installed_presence_is_observed_without_automatic_uninstall() {
        let mut queue = DesiredComponents::new(set(&["font"]), &HashSet::new());
        queue.initialize(true, false);
        queue.update_actual(&records(&[("font", actual("managed", true))]));
        assert_eq!(queue.desired, ordered(&["font"]));
        assert_eq!(queue.next_step(), None);
        queue.update_actual(&records(&[("font", actual("missing", false))]));
        assert!(queue.desired.is_empty());
        assert_eq!(queue.next_step(), None);
    }

    #[test]
    fn initial_status_does_not_overwrite_an_explicit_uncheck_while_reading() {
        let mut queue = DesiredComponents::new(set(&["font"]), &set(&["font"]));
        queue.edit(&HashSet::new());
        queue.update_actual(&records(&[("font", actual("managed", true))]));
        queue.initialize(true, true);
        assert_eq!(queue.next_step(), Some((Step::Remove, ordered(&["font"]))));
    }

    #[test]
    fn toggles_during_install_coalesce_then_remove_committed_file() {
        let mut queue = DesiredComponents::new(set(&["cmd"]), &set(&["cmd"]));
        let _ = queue.batch(&ordered(&["cmd"]));
        queue.edit(&HashSet::new());
        queue.update_actual(&records(&[("cmd", actual("managed", true))]));
        assert_eq!(queue.next_step(), Some((Step::Remove, ordered(&["cmd"]))));
        queue.edit(&set(&["cmd"]));
        assert_eq!(queue.next_step(), None);
    }

    #[test]
    fn parent_uncheck_clamps_modified_unowned_and_protected_presence() {
        let mut queue = DesiredComponents::new(
            set(&["owned", "modified", "unowned", "protected"]),
            &HashSet::new(),
        );
        queue.update_actual(&records(&[
            ("owned", actual("managed", true)),
            ("modified", actual("modified", true)),
            ("unowned", actual("unowned", true)),
            ("protected", actual("protected", true)),
        ]));
        let blocked = queue.edit(&HashSet::new());
        assert_eq!(blocked, ordered(&["modified", "unowned", "protected"]));
        assert_eq!(queue.desired, blocked);
        assert_eq!(queue.next_step(), Some((Step::Remove, ordered(&["owned"]))));
    }

    #[test]
    fn successful_zero_copy_batch_cannot_retry_preserved_other_source_forever() {
        let mut queue = DesiredComponents::new(set(&["alternate"]), &set(&["alternate"]));
        let batch = queue.batch(&ordered(&["alternate"]));
        queue.update_actual(&records(&[("alternate", actual("missing", false))]));
        assert_eq!(queue.settle(&batch), ordered(&["alternate"]));
        assert_eq!(queue.next_step(), None);
        assert!(queue.pending().is_empty());
    }

    #[test]
    fn failed_unchanged_intent_snaps_to_actual_and_never_retries() {
        let mut queue = DesiredComponents::new(set(&["cmd"]), &set(&["cmd"]));
        let batch = queue.batch(&ordered(&["cmd"]));
        queue.update_actual(&records(&[("cmd", actual("missing", false))]));
        queue.failed(&batch);
        assert!(queue.desired.is_empty());
        assert_eq!(queue.next_step(), None);
    }

    #[test]
    fn edits_during_failed_batch_survive_using_per_source_revisions() {
        let mut queue = DesiredComponents::new(set(&["cmd", "font"]), &set(&["cmd"]));
        let batch = queue.batch(&ordered(&["cmd"]));
        queue.edit(&HashSet::new());
        queue.edit(&set(&["cmd", "font"]));
        queue.update_actual(&records(&[("cmd", actual("missing", false))]));
        queue.failed(&batch);
        assert_eq!(
            queue.next_step(),
            Some((Step::Install, ordered(&["cmd", "font"])))
        );
    }

    #[test]
    fn partial_cancel_keeps_successful_files_checked_and_missing_files_off() {
        let mut queue = DesiredComponents::new(set(&["cmd", "rexx"]), &set(&["cmd", "rexx"]));
        let batch = queue.batch(&ordered(&["cmd", "rexx"]));
        queue.update_actual(&records(&[
            ("cmd", actual("managed", true)),
            ("rexx", actual("missing", false)),
        ]));
        queue.failed(&batch);
        assert_eq!(queue.desired, ordered(&["cmd"]));
        assert!(queue.pending().is_empty());
    }

    #[test]
    fn unrelated_new_selection_survives_a_failure_without_retrying_old_source() {
        let mut queue = DesiredComponents::new(set(&["cmd", "font"]), &set(&["cmd"]));
        let batch = queue.batch(&ordered(&["cmd"]));
        queue.edit(&set(&["cmd", "font"]));
        queue.failed(&batch);
        assert_eq!(queue.next_step(), Some((Step::Install, ordered(&["font"]))));
    }

    #[test]
    fn native_disabled_sources_are_never_inferred_as_removal_targets() {
        let mut queue = DesiredComponents::new(set(&["cmd"]), &HashSet::new());
        queue.update_actual(&records(&[(
            "opengl-native-disabled",
            actual("managed", true),
        )]));
        assert_eq!(queue.next_step(), None);
    }

    #[test]
    fn executor_returns_results_and_errors() {
        let context = gtk::glib::MainContext::default();
        assert_eq!(context.block_on(JOBS.run(|| Ok(4))).unwrap(), 4);
        assert!(
            context
                .block_on(STATUS.run(|| Err::<(), _>(Error::message("x"))))
                .is_err()
        );
    }
}
