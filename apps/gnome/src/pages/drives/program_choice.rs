// Public domain. No rights reserved. Wholly AI-authored.
//! Installer discovery on guest media and an editable automatic program choice.
//!
//! Installers at a drive's root win over nested ones; within a depth, known
//! installer names, then preferred extensions, then English folders and a
//! natural path order decide.

#![allow(dead_code, reason = "shared with the installer program editor")]

use std::collections::{HashSet, VecDeque};

use gtk::glib;

use super::entry::Entry;

const STEMS: [&str; 5] = ["install", "setup", "inst", "os2inst", "autorun"];
const EXTENSIONS: [&str; 4] = ["exe", "cmd", "bat", "com"];
/// Folders read while searching media for installers.
const SCAN_LIMIT: usize = 96;
/// Installer suggestions returned.
const CANDIDATE_LIMIT: usize = 32;

/// A natural-order path component: text sorts before numbers.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
enum Part {
    Text(String),
    Number(u128),
}

/// The ordering key of an installer candidate; smaller is better.
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
    for character in path.chars() {
        let digit = character.is_ascii_digit();
        if digit != digits && !current.is_empty() {
            parts.push(part(&current, digits));
            current.clear();
        }
        digits = digit;
        current.push(character);
    }
    if !current.is_empty() {
        parts.push(part(&current, digits));
    }
    parts
}

fn part(text: &str, digits: bool) -> Part {
    if digits {
        Part::Number(text.parse().unwrap_or(u128::MAX))
    } else {
        Part::Text(glib::casefold(text).into())
    }
}

/// Rank a program found `depth` folders below a drive root; None when it is not a candidate.
///
/// Any program at the root qualifies; nested programs need a known installer name.
pub fn candidate_rank(path: &str, depth: usize) -> Option<Rank> {
    let normalized = path.replace('/', "\\");
    let name: String = glib::casefold(normalized.rsplit('\\').next().unwrap_or_default()).into();
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

/// A program path chosen automatically from candidates until the user edits it.
#[derive(Debug, Clone, Default)]
pub struct ProgramChoice {
    pub text: String,
    pub manual: bool,
    pub candidates: Vec<String>,
}

impl ProgramChoice {
    /// A choice that starts from `text`; a non-empty text counts as the user's choice.
    pub fn new(text: &str) -> Self {
        Self {
            text: text.to_owned(),
            manual: !text.is_empty(),
            candidates: Vec::new(),
        }
    }

    pub fn edit(&mut self, text: &str) {
        text.clone_into(&mut self.text);
        self.manual = true;
    }

    /// Replace the candidates (case-insensitively unique) and return the current choice.
    pub fn update(&mut self, candidates: impl IntoIterator<Item = String>) -> &str {
        let mut seen = HashSet::new();
        self.candidates = candidates
            .into_iter()
            .filter(|candidate| seen.insert(glib::casefold(candidate)))
            .collect();
        if !self.manual {
            self.text = self.candidates.first().cloned().unwrap_or_default();
        }
        &self.text
    }
}

/// Search a drive breadth-first for installers, reading at most 96 folders.
///
/// `browse` lists a guest folder; folders that cannot be read are skipped.
pub fn installer_candidates<E>(
    mut browse: impl FnMut(&str) -> Result<Vec<Entry>, E>,
    letter: &str,
) -> Vec<String> {
    let mut pending = VecDeque::from([(format!("{letter}:\\"), 0_usize)]);
    let mut found = Vec::new();
    let mut scanned = 0;
    while scanned < SCAN_LIMIT
        && let Some((path, depth)) = pending.pop_front()
    {
        scanned += 1;
        let Ok(entries) = browse(&path) else { continue };
        for entry in entries {
            let name: String = glib::casefold(&entry.name).into();
            if entry.directory {
                if depth < 2
                    && (depth == 0
                        || name == "english"
                        || name == "os2"
                        || name.contains("install")
                        || name.contains("setup"))
                {
                    pending.push_back((entry.path, depth + 1));
                }
            } else if let Some(rank) = candidate_rank(&entry.path, depth) {
                found.push((rank, entry.path));
            }
        }
    }
    found.sort_by(|first, second| first.0.cmp(&second.0).then_with(|| first.1.cmp(&second.1)));
    found
        .into_iter()
        .take(CANDIDATE_LIMIT)
        .map(|(_, path)| path)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn root_installer_wins_over_nested_even_with_less_preferred_extension() {
        let candidates = [
            (r"D:\ENGLISH\INSTALL.EXE", 1),
            (r"D:\INSTALL.CMD", 0),
            (r"D:\SETUP.EXE", 0),
            (r"D:\OTHER.EXE", 0),
        ];
        let mut ranked: Vec<_> = candidates
            .iter()
            .filter_map(|&(path, depth)| candidate_rank(path, depth).map(|rank| (rank, path)))
            .collect();
        ranked.sort();
        assert_eq!(
            ranked.into_iter().map(|(_, path)| path).collect::<Vec<_>>(),
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
        assert!(candidate_rank(r"D:\DOCS\OTHER.EXE", 1).is_none());
    }

    #[test]
    fn unreadable_nested_folder_keeps_a_valid_root_suggestion() {
        let browse = |path: &str| -> Result<Vec<Entry>, String> {
            if path == "D:\\" {
                Ok(vec![
                    Entry {
                        name: "INSTALL.CMD".into(),
                        path: r"D:\INSTALL.CMD".into(),
                        ..Entry::default()
                    },
                    Entry {
                        name: "ENGLISH".into(),
                        path: r"D:\ENGLISH".into(),
                        directory: true,
                        ..Entry::default()
                    },
                ])
            } else {
                Err("Cannot read folder".into())
            }
        };
        assert_eq!(installer_candidates(browse, "D"), [r"D:\INSTALL.CMD"]);
    }

    #[test]
    fn auto_choice_clears_on_eject_and_uses_new_media_default() {
        let mut choice = ProgramChoice::default();
        assert_eq!(
            choice.update([r"D:\INSTALL.CMD".into(), r"d:\install.cmd".into()]),
            r"D:\INSTALL.CMD"
        );
        assert_eq!(choice.candidates.len(), 1);
        assert_eq!(choice.update([]), "");
        assert_eq!(choice.update([r"E:\SETUP.EXE".into()]), r"E:\SETUP.EXE");
    }

    #[test]
    fn typing_or_browsing_keeps_manual_choice_when_scan_finishes_or_media_changes() {
        let mut choice = ProgramChoice::default();
        choice.update([r"D:\INSTALL.EXE".into()]);
        choice.edit(r"C:\CUSTOM\MYSETUP.CMD");
        assert_eq!(
            choice.update([r"E:\SETUP.EXE".into()]),
            r"C:\CUSTOM\MYSETUP.CMD"
        );
        assert_eq!(choice.update([]), r"C:\CUSTOM\MYSETUP.CMD");
        let mut explicit = ProgramChoice::new(r"C:\CHOSEN.EXE");
        assert_eq!(
            explicit.update([r"D:\INSTALL.EXE".into()]),
            r"C:\CHOSEN.EXE"
        );
    }
}
