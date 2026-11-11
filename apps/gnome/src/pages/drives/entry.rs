// Public domain. No rights reserved. Wholly AI-authored.
//! Guest folder listings returned by the engine's `browse` action.

use std::cmp::Ordering;

use gtk::glib;
use serde::{Deserialize, Deserializer};
use serde_json::Value;

/// One file or folder in a guest listing, or a placeholder row for a folder
/// whose contents have not been read.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
#[expect(clippy::struct_excessive_bools, reason = "independent engine flags")]
pub struct Entry {
    pub name: String,
    /// The canonical OS/2 path, for example `C:\OS2\CMD.EXE`.
    pub path: String,
    pub directory: bool,
    #[serde(deserialize_with = "unsigned")]
    pub size: Option<u64>,
    /// Modification time in seconds since the Unix epoch.
    #[serde(deserialize_with = "signed")]
    pub modified: Option<i64>,
    /// An archive or image member container.
    pub container: bool,
    /// The host file behind the entry, when it is not inside an image.
    pub host_path: Option<String>,
    #[serde(skip)]
    pub placeholder: bool,
    /// A placeholder describing a folder that could not be read.
    #[serde(skip)]
    pub error: bool,
}

/// The result of `browse`: the canonical folder path, its parent and entries.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Listing {
    pub path: String,
    pub parent: Option<String>,
    pub host_path: Option<String>,
    pub entries: Vec<Entry>,
}

impl Listing {
    /// The drive letter of the listed folder, for example `"C"`.
    pub fn letter(&self) -> String {
        letter(&self.path).unwrap_or_default()
    }
}

/// The upper-case drive letter of an OS/2 path such as `d:\setup`, if present.
pub fn letter(path: &str) -> Option<String> {
    let mut characters = path.chars();
    let first = characters.next()?;
    (first.is_ascii_alphabetic() && characters.next() == Some(':'))
        .then(|| first.to_ascii_uppercase().to_string())
}

impl Entry {
    /// The row shown inside a folder whose contents are loading or failed to load.
    pub fn placeholder(folder: &str, error: Option<&str>) -> Self {
        Self {
            name: error.map_or_else(|| "Loading…".into(), str::to_owned),
            path: format!("loading:{folder}"),
            placeholder: true,
            error: error.is_some(),
            ..Self::default()
        }
    }

    pub fn icon(&self) -> &'static str {
        if self.directory {
            "folder-symbolic"
        } else if self.container {
            "package-x-generic-symbolic"
        } else {
            "text-x-generic-symbolic"
        }
    }

    /// Whether the name has an OS/2 program or command file extension.
    pub fn is_program(&self) -> bool {
        self.name.rsplit_once('.').is_some_and(|(_, extension)| {
            matches!(
                extension.to_ascii_lowercase().as_str(),
                "exe" | "com" | "cmd" | "bat"
            )
        })
    }
}

#[expect(
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    reason = "engine numbers are whole non-negative values"
)]
fn unsigned<'de, D: Deserializer<'de>>(deserializer: D) -> Result<Option<u64>, D::Error> {
    Ok(match Value::deserialize(deserializer)? {
        Value::Number(number) => number.as_u64().or_else(|| {
            number
                .as_f64()
                .filter(|value| value.is_finite() && *value >= 0.0)
                .map(|value| value as u64)
        }),
        _ => None,
    })
}

#[expect(
    clippy::cast_possible_truncation,
    reason = "engine timestamps are whole seconds"
)]
fn signed<'de, D: Deserializer<'de>>(deserializer: D) -> Result<Option<i64>, D::Error> {
    Ok(match Value::deserialize(deserializer)? {
        Value::Number(number) => number.as_i64().or_else(|| {
            number
                .as_f64()
                .filter(|value| value.is_finite())
                .map(|value| value as i64)
        }),
        _ => None,
    })
}

/// The Size column: a human-readable size, or a dash for folders and unknown sizes.
pub fn file_size(entry: &Entry) -> String {
    match entry.size {
        Some(size) if !entry.directory => glib::format_size(size).into(),
        _ => "—".into(),
    }
}

/// The Date Modified column in the local time zone and locale date format.
pub fn file_date(entry: &Entry) -> String {
    entry
        .modified
        .and_then(|stamp| glib::DateTime::from_unix_local(stamp).ok())
        .and_then(|date| date.format("%x %H:%M").ok())
        .map_or_else(|| "—".into(), Into::into)
}

/// A part of a file name compared for natural ordering.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
enum Part {
    Text(String),
    Number(u128, usize),
}

/// A case-insensitive natural sort key: `FILE2` sorts before `FILE10`.
fn filename_key(name: &str) -> Vec<Part> {
    let folded = glib::casefold(name).to_string();
    let mut parts = Vec::new();
    let mut current = String::new();
    let mut digits = false;
    let mut flush = |current: &mut String, digits: bool| {
        if current.is_empty() {
            return;
        }
        let part = if digits {
            Part::Number(current.parse().unwrap_or(u128::MAX), current.len())
        } else {
            Part::Text(current.clone())
        };
        parts.push(part);
        current.clear();
    };
    for character in folded.chars() {
        let digit = character.is_ascii_digit();
        if digit != digits {
            flush(&mut current, digits);
            digits = digit;
        }
        current.push(character);
    }
    flush(&mut current, digits);
    parts
}

/// Compare file names the way file managers do: case-insensitive with numbers in value order.
pub fn compare_names(first: &str, second: &str) -> Ordering {
    filename_key(first)
        .cmp(&filename_key(second))
        .then_with(|| first.cmp(second))
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::*;

    #[test]
    fn listings_decode_engine_numbers_and_host_paths() {
        let listing: Listing = serde_json::from_value(json!({
            "path": "C:\\OS2", "parent": "C:\\", "hostPath": "/b/drive_c/OS2",
            "entries": [
                {"name": "CMD.EXE", "path": "C:\\OS2\\CMD.EXE", "hostPath": "/b/drive_c/OS2/CMD.EXE",
                 "directory": false, "size": 1024.0, "modified": 1_700_000_000},
                {"name": "DLL", "path": "C:\\OS2\\DLL", "directory": true},
            ],
        }))
        .expect("listing");
        assert_eq!(listing.letter(), "C");
        assert_eq!(listing.entries[0].size, Some(1024));
        assert!(listing.entries[0].is_program());
        assert_eq!(file_size(&listing.entries[1]), "—");
        assert_eq!(file_date(&listing.entries[1]), "—");
        assert_ne!(file_date(&listing.entries[0]), "—");
        assert_eq!(listing.entries[1].icon(), "folder-symbolic");
    }

    #[test]
    fn drive_letters_require_a_colon() {
        assert_eq!(letter("d:\\setup").as_deref(), Some("D"));
        assert_eq!(letter("D:").as_deref(), Some("D"));
        assert_eq!(letter("loading:C:\\X"), None);
        assert_eq!(letter("D"), None);
    }

    #[test]
    fn names_sort_naturally_and_case_insensitively() {
        let mut names = vec!["file10.txt", "File2.txt", "alpha", "BETA"];
        names.sort_by(|a, b| compare_names(a, b));
        assert_eq!(names, ["alpha", "BETA", "File2.txt", "file10.txt"]);
    }
}
