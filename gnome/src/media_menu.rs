// Public domain. No rights reserved. Wholly AI-authored.
//! Header-bar removable media menu entries derived from a ball snapshot.

use std::path::Path;

use crate::model::{DriveKind, Snapshot};

/// One menu entry: label, window action and its string target. Entries without
/// an action are informational and shown insensitive.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub label: String,
    pub action: Option<&'static str>,
    pub target: String,
}

impl Entry {
    fn info(label: String) -> Self {
        Self {
            label,
            action: None,
            target: String::new(),
        }
    }

    fn action(label: String, action: &'static str, target: impl Into<String>) -> Self {
        Self {
            label,
            action: Some(action),
            target: target.into(),
        }
    }
}

/// The next unallocated drive letter conventionally used for a device kind.
pub fn available_letter(snapshot: &Snapshot, kind: DriveKind) -> Option<char> {
    let order = if kind == DriveKind::Floppy {
        "ABDEFGHIJKLMNOPQRSTUVWXYZ"
    } else {
        "DEFGHIJKLMNOPQRSTUVWXYZAB"
    };
    order
        .chars()
        .find(|letter| !snapshot.manifest.drives.contains_key(&letter.to_string()))
}

fn file_name(path: &str) -> String {
    Path::new(path).file_name().map_or_else(
        || path.to_owned(),
        |name| name.to_string_lossy().into_owned(),
    )
}

/// Menu sections: an optional heading and its entries.
pub fn sections(snapshot: &Snapshot) -> Vec<(Option<String>, Vec<Entry>)> {
    let mut result = Vec::new();
    for (letter, device) in snapshot.devices() {
        let kind = device.device;
        let title = kind.title();
        let image = snapshot.attached_image(letter);
        let mut state = match snapshot.unavailable_drives.get(letter) {
            Some(missing) => format!("Ejected — {}", file_name(missing)),
            None => image.map_or_else(|| "Ejected".into(), file_name),
        };
        if device.locked {
            state.push_str(" · Locked in Settings");
        }
        let mut entries = vec![Entry::info(state)];
        if !device.locked {
            let verb = if image.is_some() {
                "Replace Image in"
            } else {
                "Attach Image to"
            };
            entries.push(Entry::action(
                format!("{verb} {letter}:…"),
                "attach-image-at",
                letter,
            ));
            if image.is_some() {
                entries.push(Entry::action(
                    format!("Eject {letter}:"),
                    "eject-at",
                    letter,
                ));
            }
            if kind != DriveKind::Cdrom {
                let (label, value) = if device.readonly {
                    ("Make Writable", "off")
                } else {
                    ("Make Read Only", "on")
                };
                entries.push(Entry::action(
                    label.into(),
                    "readonly-at",
                    format!("{letter}:{value}"),
                ));
                entries.push(Entry::action(
                    format!("Create {title} Image for {letter}:…"),
                    "create-image-at",
                    letter,
                ));
            }
        }
        if image.is_some() {
            entries.push(Entry::action(
                format!("Browse {letter}:"),
                "browse-drive-at",
                letter,
            ));
        }
        result.push((Some(format!("{letter}: — {title} Drive")), entries));
    }
    let adds: Vec<Entry> = DriveKind::REMOVABLE
        .into_iter()
        .filter(|&kind| available_letter(snapshot, kind).is_some())
        .map(|kind| {
            Entry::action(
                format!("Add {} Drive", kind.title()),
                "add-device",
                kind.as_str(),
            )
        })
        .collect();
    if !adds.is_empty() {
        result.push((None, adds));
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn snapshot(drives: &serde_json::Value, unavailable: &serde_json::Value) -> Snapshot {
        serde_json::from_value(
            json!({"manifest": {"drives": drives}, "unavailableDrives": unavailable}),
        )
        .unwrap()
    }

    #[test]
    fn letters_follow_device_conventions() {
        let snapshot = snapshot(
            &json!({"C": "c", "A": {"device": "floppy"}, "D": {"device": "cdrom"}}),
            &json!({}),
        );
        assert_eq!(available_letter(&snapshot, DriveKind::Floppy), Some('B'));
        assert_eq!(available_letter(&snapshot, DriveKind::Cdrom), Some('E'));
    }

    #[test]
    fn attached_floppy_offers_eject_protection_and_browse() {
        let result = sections(&snapshot(
            &json!({"A": {"device": "floppy", "image": "/m/disk1.img"}}),
            &json!({}),
        ));
        let (heading, entries) = &result[0];
        assert_eq!(heading.as_deref(), Some("A: — Floppy Drive"));
        assert_eq!(entries[0].label, "disk1.img");
        let actions: Vec<_> = entries.iter().map(|entry| entry.action).collect();
        assert_eq!(
            actions,
            [
                None,
                Some("attach-image-at"),
                Some("eject-at"),
                Some("readonly-at"),
                Some("create-image-at"),
                Some("browse-drive-at")
            ]
        );
        assert_eq!(entries[3].target, "A:on");
    }

    #[test]
    fn locked_or_ejected_cd_hides_media_changes() {
        let locked = sections(&snapshot(
            &json!({"D": {"device": "cdrom", "image": "/m/cd.iso", "locked": true}}),
            &json!({}),
        ));
        assert_eq!(
            locked[0]
                .1
                .iter()
                .map(|entry| entry.action)
                .collect::<Vec<_>>(),
            [None, Some("browse-drive-at")]
        );
        assert!(locked[0].1[0].label.contains("Locked"));
        let ejected = sections(&snapshot(
            &json!({"D": {"device": "cdrom", "image": "/gone.iso"}}),
            &json!({"D": "/gone.iso"}),
        ));
        assert_eq!(ejected[0].1[0].label, "Ejected — gone.iso");
        assert_eq!(
            ejected[0]
                .1
                .iter()
                .map(|entry| entry.action)
                .collect::<Vec<_>>(),
            [None, Some("attach-image-at")]
        );
    }

    #[test]
    fn add_entries_follow_free_letters() {
        let result = sections(&snapshot(&json!({}), &json!({})));
        let targets: Vec<_> = result
            .last()
            .unwrap()
            .1
            .iter()
            .map(|entry| entry.target.as_str())
            .collect();
        assert_eq!(targets, ["cdrom", "floppy", "disk"]);
    }
}
