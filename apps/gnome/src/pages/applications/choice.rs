// Public domain. No rights reserved. Wholly AI-authored.
//! Installer suggestions for attached media and the editable program choice.
//!
//! Suggestions rank root programs before nested ones, then conventional
//! installer names, preferred extensions, English folders and natural path
//! order. A program the user typed or browsed to is never replaced by a later
//! scan; an automatic choice follows the media.

use std::collections::{HashSet, VecDeque};

use serde_json::Value;

use crate::model::{DriveKind, Snapshot};

const STEMS: [&str; 5] = ["install", "setup", "inst", "os2inst", "autorun"];
const EXTENSIONS: [&str; 4] = ["exe", "cmd", "bat", "com"];
/// Folders browsed while looking for installers on one medium.
const SCAN_LIMIT: usize = 96;
/// Suggestions offered for one medium.
const SUGGESTION_LIMIT: usize = 32;

/// One run of a path in natural order: text sorts before numbers.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
enum Part {
    Text(String),
    /// Significant digit count, then the digits without leading zeros.
    Number(usize, String),
}

/// The ordering of one installer suggestion.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct Rank {
    depth: usize,
    stem: usize,
    extension: usize,
    not_english: bool,
    path: Vec<Part>,
}

fn natural(path: &str) -> Vec<Part> {
    let mut parts = Vec::new();
    let mut current = String::new();
    let mut digits = false;
    let mut flush = |current: &mut String, digits: bool| {
        if current.is_empty() {
            return;
        }
        let text = std::mem::take(current);
        parts.push(if digits {
            let trimmed = text.trim_start_matches('0').to_owned();
            Part::Number(trimmed.len(), trimmed)
        } else {
            Part::Text(text.to_lowercase())
        });
    };
    for c in path.chars() {
        let is_digit = c.is_ascii_digit();
        if is_digit != digits {
            flush(&mut current, digits);
            digits = is_digit;
        }
        current.push(c);
    }
    flush(&mut current, digits);
    parts
}

/// Rank an executable found `depth` folders below the medium root, or None
/// when it is not a plausible installer.
pub fn candidate_rank(path: &str, depth: usize) -> Option<Rank> {
    let name = path
        .replace('/', "\\")
        .rsplit('\\')
        .next()
        .unwrap_or_default()
        .to_lowercase();
    let (stem, extension) = name.rsplit_once('.')?;
    let extension = EXTENSIONS.iter().position(|known| *known == extension)?;
    let stem = STEMS.iter().position(|known| *known == stem);
    if depth != 0 && stem.is_none() {
        return None;
    }
    Some(Rank {
        depth,
        stem: stem.unwrap_or(99),
        extension,
        not_english: !path.to_uppercase().contains("\\ENGLISH\\"),
        path: natural(path),
    })
}

/// Browse a drive breadth-first for installers. `browse` lists one guest folder
/// as the engine's `browse` result; unreadable folders are skipped.
pub fn installer_candidates<E>(
    mut browse: impl FnMut(&str) -> Result<Value, E>,
    letter: &str,
) -> Vec<String> {
    let mut pending = VecDeque::from([(format!("{letter}:\\"), 0_usize)]);
    let mut found = Vec::new();
    let mut scanned = 0;
    while scanned < SCAN_LIMIT
        && let Some((path, depth)) = pending.pop_front()
    {
        scanned += 1;
        let Ok(result) = browse(&path) else { continue };
        for entry in result["entries"].as_array().into_iter().flatten() {
            let (Some(name), Some(entry_path)) = (entry["name"].as_str(), entry["path"].as_str())
            else {
                continue;
            };
            let name = name.to_lowercase();
            if entry["directory"].as_bool() == Some(true) {
                if depth < 2
                    && (depth == 0
                        || name == "english"
                        || name == "os2"
                        || name.contains("install")
                        || name.contains("setup"))
                {
                    pending.push_back((entry_path.to_owned(), depth + 1));
                }
            } else if let Some(rank) = candidate_rank(entry_path, depth) {
                found.push((rank, entry_path.to_owned()));
            }
        }
    }
    found.sort();
    found
        .into_iter()
        .take(SUGGESTION_LIMIT)
        .map(|(_, path)| path)
        .collect()
}

/// The program field of the installer editor: suggestions fill it until the
/// user types or browses.
#[derive(Debug, Clone, Default)]
pub struct ProgramChoice {
    pub text: String,
    manual: bool,
    pub candidates: Vec<String>,
}

impl ProgramChoice {
    pub fn new(text: &str) -> Self {
        Self {
            text: text.to_owned(),
            manual: !text.is_empty(),
            candidates: Vec::new(),
        }
    }

    /// Record a user edit; later suggestions keep it.
    pub fn edit(&mut self, text: &str) {
        text.clone_into(&mut self.text);
        self.manual = true;
    }

    /// Replace the suggestions, dropping case-insensitive duplicates, and
    /// return the program text to show.
    pub fn update(&mut self, candidates: &[String]) -> String {
        let mut seen = HashSet::new();
        self.candidates = candidates
            .iter()
            .filter(|candidate| seen.insert(candidate.to_lowercase()))
            .cloned()
            .collect();
        if !self.manual {
            self.text = self.candidates.first().cloned().unwrap_or_default();
        }
        self.text.clone()
    }
}

/// Letters offered for installation media of `kind`: existing devices with an
/// image first, then the other devices, then the next free conventional letter.
pub fn media_letters(snapshot: &Snapshot, kind: DriveKind) -> Vec<String> {
    let drives = &snapshot.manifest.drives;
    let mut letters: Vec<(bool, &String)> = drives
        .iter()
        .filter_map(|(letter, drive)| {
            let device = drive.device().filter(|device| device.device == kind)?;
            Some((device.image.as_deref().is_none_or(str::is_empty), letter))
        })
        .collect();
    letters.sort();
    let mut letters: Vec<String> = letters
        .into_iter()
        .map(|(_, letter)| letter.clone())
        .collect();
    let order = if kind == DriveKind::Floppy {
        "ABDEFGHIJKLMNOPQRSTUVWXYZ"
    } else {
        "DEFGHIJKLMNOPQRSTUVWXYZAB"
    };
    if let Some(free) = order
        .chars()
        .map(String::from)
        .find(|letter| !drives.contains_key(letter))
    {
        letters.push(free);
    }
    letters
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn ranked(candidates: &[(&str, usize)]) -> Vec<String> {
        let mut found: Vec<_> = candidates
            .iter()
            .filter_map(|(path, depth)| Some((candidate_rank(path, *depth)?, (*path).to_owned())))
            .collect();
        found.sort();
        found.into_iter().map(|(_, path)| path).collect()
    }

    #[test]
    fn root_installer_wins_over_nested_even_with_less_preferred_extension() {
        let candidates = [
            (r"D:\ENGLISH\INSTALL.EXE", 1),
            (r"D:\INSTALL.CMD", 0),
            (r"D:\SETUP.EXE", 0),
            (r"D:\OTHER.EXE", 0),
        ];
        assert_eq!(
            ranked(&candidates),
            [
                r"D:\INSTALL.CMD",
                r"D:\SETUP.EXE",
                r"D:\OTHER.EXE",
                r"D:\ENGLISH\INSTALL.EXE"
            ]
        );
    }

    #[test]
    fn exact_depth_name_extension_then_language_priority() {
        assert!(candidate_rank(r"D:\SETUP.CMD", 0) < candidate_rank(r"D:\ENGLISH\INSTALL.EXE", 1));
        assert!(
            candidate_rank(r"D:\OTHER\INSTALL.EXE", 1)
                < candidate_rank(r"D:\ENGLISH\INSTALL.CMD", 1)
        );
        assert!(
            candidate_rank(r"D:\ENGLISH\SETUP.EXE", 1) < candidate_rank(r"D:\OTHER\SETUP.EXE", 1)
        );
        assert!(candidate_rank(r"D:\README", 0).is_none());
        assert!(candidate_rank(r"D:\SUB\OTHER.EXE", 1).is_none());
    }

    #[test]
    fn natural_order_compares_numbers_by_value() {
        assert!(candidate_rank(r"D:\DISK2\X.EXE", 0) < candidate_rank(r"D:\DISK10\X.EXE", 0));
    }

    #[test]
    fn unreadable_nested_folder_keeps_a_valid_root_suggestion() {
        let browse = |path: &str| {
            if path == "D:\\" {
                Ok(json!({"entries": [
                    {"name": "INSTALL.CMD", "path": r"D:\INSTALL.CMD", "directory": false},
                    {"name": "ENGLISH", "path": r"D:\ENGLISH", "directory": true},
                ]}))
            } else {
                Err("Cannot read folder")
            }
        };
        assert_eq!(installer_candidates(browse, "D"), [r"D:\INSTALL.CMD"]);
    }

    #[test]
    fn auto_choice_clears_on_eject_and_uses_new_media_default() {
        let mut choice = ProgramChoice::default();
        let list = |items: &[&str]| items.iter().map(ToString::to_string).collect::<Vec<_>>();
        assert_eq!(
            choice.update(&list(&[r"D:\INSTALL.CMD", r"d:\install.cmd"])),
            r"D:\INSTALL.CMD"
        );
        assert_eq!(choice.candidates.len(), 1);
        assert_eq!(choice.update(&[]), "");
        assert_eq!(choice.update(&list(&[r"E:\SETUP.EXE"])), r"E:\SETUP.EXE");
    }

    #[test]
    fn typing_or_browsing_keeps_manual_choice_when_scan_finishes_or_media_changes() {
        let list = |items: &[&str]| items.iter().map(ToString::to_string).collect::<Vec<_>>();
        let mut choice = ProgramChoice::default();
        choice.update(&list(&[r"D:\INSTALL.EXE"]));
        choice.edit(r"C:\CUSTOM\MYSETUP.CMD");
        assert_eq!(
            choice.update(&list(&[r"E:\SETUP.EXE"])),
            r"C:\CUSTOM\MYSETUP.CMD"
        );
        assert_eq!(choice.update(&[]), r"C:\CUSTOM\MYSETUP.CMD");
        let mut explicit = ProgramChoice::new(r"C:\CHOSEN.EXE");
        assert_eq!(
            explicit.update(&list(&[r"D:\INSTALL.EXE"])),
            r"C:\CHOSEN.EXE"
        );
    }

    #[test]
    fn media_letters_prefer_attached_devices_then_a_free_letter() {
        let snapshot: Snapshot = serde_json::from_value(json!({"manifest": {"drives": {
            "C": "drive_c",
            "D": {"device": "cdrom"},
            "E": {"device": "cdrom", "image": "/x.iso"},
            "A": {"device": "floppy"},
        }}}))
        .unwrap();
        assert_eq!(media_letters(&snapshot, DriveKind::Cdrom), ["E", "D", "F"]);
        assert_eq!(media_letters(&snapshot, DriveKind::Floppy), ["A", "B"]);
    }
}
