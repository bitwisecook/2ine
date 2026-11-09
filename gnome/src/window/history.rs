// Public domain. No rights reserved. Wholly AI-authored.
//! Undo/redo records for ball changes.
//!
//! Each forward request is paired with engine requests that restore the
//! previous state, computed from the snapshot taken before the change. History
//! applies to the captured ball, not whichever ball is selected later.

use std::path::PathBuf;

use serde_json::{Value, json};

use crate::model::{Drive, Snapshot};

#[derive(Debug, Clone)]
pub struct HistoryItem {
    pub ball: PathBuf,
    pub undo: Vec<Value>,
    pub redo: Vec<Value>,
    pub label: String,
}

fn command(arguments: &[&str]) -> Value {
    json!({"action": "command", "arguments": arguments})
}

/// The request restoring a drive letter to its recorded state.
pub fn drive_inverse(snapshot: &Snapshot, letter: &str) -> Value {
    match snapshot.drive_records.get(letter) {
        Some(record) => command(&["restore-drive", letter, record]),
        None => command(&["unmap", letter]),
    }
}

/// The request reversing a ball `command`, or None when it cannot be undone.
pub fn inverse(snapshot: &Snapshot, arguments: &[String]) -> Option<Value> {
    let action = arguments.first()?.as_str();
    let argument = |index: usize| arguments.get(index).map_or("", String::as_str);
    match action {
        "map" | "unmap" | "map-image" | "device" | "eject" | "restore-drive" | "create-floppy"
        | "create-disk" => {
            let letter = argument(1).trim_end_matches(':').to_uppercase();
            Some(drive_inverse(snapshot, &letter))
        }
        "device-lock" | "media-readonly" => {
            let device = snapshot.drive(argument(1)).and_then(Drive::device);
            let enabled = device.is_some_and(|device| {
                if action == "device-lock" {
                    device.locked
                } else {
                    device.readonly
                }
            });
            Some(command(&[
                action,
                argument(1),
                if enabled { "on" } else { "off" },
            ]))
        }
        "remove-app" => snapshot
            .application_records
            .get(argument(1))
            .map(|record| command(&["restore-app", argument(1), record])),
        "add-app" => {
            let name = argument(if argument(1) == "--cwd" { 3 } else { 1 });
            Some(match snapshot.application_records.get(name) {
                Some(record) => command(&["restore-app", name, record]),
                None => command(&["remove-app", name]),
            })
        }
        "config" | "unset" => {
            let (group, key) = argument(1).split_once('.')?;
            let old = if group == "env" {
                snapshot
                    .manifest
                    .environment
                    .get(key)
                    .map(|value| Value::String(value.clone()))
            } else if group == "system" {
                snapshot.manifest.system.get(key).cloned()
            } else if group == "runtime" {
                match key {
                    "build" => snapshot.manifest.runtime.build.clone().map(Value::String),
                    "backend" => snapshot.manifest.runtime.backend.clone().map(Value::String),
                    _ => None,
                }
            } else {
                None
            };
            Some(match old {
                Some(Value::String(text)) => command(&["config", argument(1), &text]),
                Some(Value::Bool(flag)) => command(&["config", argument(1), &flag.to_string()]),
                Some(other) => command(&["config", argument(1), &other.to_string()]),
                None => command(&["unset", argument(1)]),
            })
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn snapshot() -> Snapshot {
        serde_json::from_value(json!({
            "manifest": {
                "drives": {"C": "drive_c", "A": {"device": "floppy", "locked": true}},
                "environment": {"PATH": "C:\\OS2"},
                "system": {"dive": false}
            },
            "driveRecords": {"C": "{\"c\":1}"},
            "applicationRecords": {"Word": "{\"w\":1}"}
        }))
        .unwrap()
    }

    fn arguments(values: &[&str]) -> Vec<String> {
        values.iter().map(ToString::to_string).collect()
    }

    #[test]
    fn drive_changes_restore_records_or_unmap() {
        let snapshot = snapshot();
        assert_eq!(
            inverse(&snapshot, &arguments(&["map", "c:", "/x"])),
            Some(command(&["restore-drive", "C", "{\"c\":1}"]))
        );
        assert_eq!(
            inverse(&snapshot, &arguments(&["device", "D", "cdrom"])),
            Some(command(&["unmap", "D"]))
        );
        assert_eq!(
            inverse(&snapshot, &arguments(&["device-lock", "A", "off"])),
            Some(command(&["device-lock", "A", "on"]))
        );
    }

    #[test]
    fn settings_restore_previous_values_or_inheritance() {
        let snapshot = snapshot();
        assert_eq!(
            inverse(&snapshot, &arguments(&["config", "env.PATH", "x"])),
            Some(command(&["config", "env.PATH", "C:\\OS2"]))
        );
        assert_eq!(
            inverse(&snapshot, &arguments(&["unset", "system.dive"])),
            Some(command(&["config", "system.dive", "false"]))
        );
        assert_eq!(
            inverse(&snapshot, &arguments(&["config", "system.pcm", "true"])),
            Some(command(&["unset", "system.pcm"]))
        );
    }

    #[test]
    fn launchers_restore_records() {
        let snapshot = snapshot();
        assert_eq!(
            inverse(
                &snapshot,
                &arguments(&["add-app", "--cwd", "C:\\", "New", "C:\\X.EXE"])
            ),
            Some(command(&["remove-app", "New"]))
        );
        assert_eq!(
            inverse(&snapshot, &arguments(&["remove-app", "Word"])),
            Some(command(&["restore-app", "Word", "{\"w\":1}"]))
        );
        assert_eq!(
            inverse(&snapshot, &arguments(&["remove-app", "Gone"])),
            None
        );
    }
}
