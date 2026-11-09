// Public domain. No rights reserved. Wholly AI-authored.
//! Stable launcher ordering, search folding and clipboard text.
//!
//! Ordering is independent of when icons finish loading: only the
//! icons-first mode looks at icon availability, and every mode ends with the
//! folded and exact names so equal titles keep a deterministic order.

use std::collections::BTreeMap;

use gtk::glib;

use crate::model::{Application, working_directory_summary};

/// The launcher sort orders offered in the Applications sort menu.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum SortMode {
    /// Launchers with real icons first, then by name.
    #[default]
    Icons,
    Name,
    Program,
}

impl SortMode {
    pub const ALL: [Self; 3] = [Self::Icons, Self::Name, Self::Program];

    /// Parse a saved preference; unknown or missing values use icons first.
    pub fn parse(value: Option<&str>) -> Self {
        match value {
            Some("name") => Self::Name,
            Some("program") => Self::Program,
            _ => Self::Icons,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Icons => "icons",
            Self::Name => "name",
            Self::Program => "program",
        }
    }

    pub fn title(self) -> &'static str {
        match self {
            Self::Icons => "Icons First",
            Self::Name => "Name",
            Self::Program => "Program",
        }
    }
}

/// The comparison key for a launcher row in `mode`.
pub fn key(
    name: &str,
    program: &str,
    has_icon: bool,
    mode: SortMode,
) -> (bool, String, String, String) {
    let title = folded(name);
    match mode {
        SortMode::Icons => (!has_icon, String::new(), title, name.to_owned()),
        SortMode::Program => (false, folded(program), title, name.to_owned()),
        SortMode::Name => (false, String::new(), title, name.to_owned()),
    }
}

/// Case- and accent-insensitive text for searching and sorting.
pub fn folded(value: &str) -> String {
    let lowered = glib::casefold(value);
    glib::normalize(&lowered, glib::NormalizeMode::All)
        .chars()
        .filter(|&c| !combining(c))
        .collect()
}

fn combining(c: char) -> bool {
    matches!(
        u32::from(c),
        0x0300..=0x036F | 0x1AB0..=0x1AFF | 0x1DC0..=0x1DFF | 0x20D0..=0x20FF | 0xFE20..=0xFE2F
    )
}

/// Whether every word of `query` occurs in the folded `text`.
pub fn matches(text: &str, query: &str) -> bool {
    folded(query)
        .split_whitespace()
        .all(|word| text.contains(word))
}

/// Tab-separated name, program and working-folder lines for the named launchers.
pub fn copy_text<'a>(
    applications: &BTreeMap<String, Application>,
    names: impl IntoIterator<Item = &'a str>,
) -> String {
    names
        .into_iter()
        .filter_map(|name| {
            let application = applications.get(name)?;
            Some(format!(
                "{name}\t{}\t{}",
                application.program,
                working_directory_summary(application)
            ))
        })
        .collect::<Vec<_>>()
        .join("\n")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rows(mode: SortMode, available: &[&str]) -> Vec<&'static str> {
        let entries = [
            ("Zulu", "A.EXE"),
            ("Café", "C.EXE"),
            ("Alpha License", "Z.EXE"),
        ];
        let mut names: Vec<_> = entries.to_vec();
        names.sort_by_key(|(name, program)| key(name, program, available.contains(name), mode));
        names.into_iter().map(|(name, _)| name).collect()
    }

    #[test]
    fn icons_arriving_late_move_only_the_real_icon_entries_first() {
        assert_eq!(
            rows(SortMode::Icons, &[]),
            ["Alpha License", "Café", "Zulu"]
        );
        assert_eq!(
            rows(SortMode::Icons, &["Zulu"]),
            ["Zulu", "Alpha License", "Café"]
        );
        assert_eq!(
            rows(SortMode::Icons, &["Café", "Zulu"]),
            ["Café", "Zulu", "Alpha License"]
        );
    }

    #[test]
    fn explicit_name_or_program_order_survives_late_icon_results() {
        for mode in [SortMode::Name, SortMode::Program] {
            assert_eq!(rows(mode, &[]), rows(mode, &["Zulu", "Café"]));
        }
        assert_eq!(
            rows(SortMode::Program, &[]),
            ["Zulu", "Café", "Alpha License"]
        );
    }

    #[test]
    fn unknown_saved_mode_uses_icon_first_and_duplicate_titles_are_stable() {
        assert_eq!(SortMode::parse(Some("unknown")), SortMode::Icons);
        assert_eq!(SortMode::parse(None), SortMode::Icons);
        assert!(key("App", "a", true, SortMode::Icons) < key("app", "b", true, SortMode::Icons));
        for mode in SortMode::ALL {
            assert_eq!(SortMode::parse(Some(mode.as_str())), mode);
        }
    }

    #[test]
    fn search_folds_case_and_accents() {
        assert_eq!(folded("Café NOTES"), "cafe notes");
        assert!(matches(&folded("Café Notes C:\\NOTES.EXE"), "cafe  notes"));
        assert!(!matches(&folded("Calculator"), "cafe"));
        assert!(matches("anything", "   "));
    }

    #[test]
    fn copy_text_lists_known_launchers() {
        let mut applications = BTreeMap::new();
        applications.insert(
            "Edit".to_owned(),
            Application {
                program: "C:\\E.EXE".into(),
                cwd: Some("d:\\work".into()),
                ..Application::default()
            },
        );
        assert_eq!(
            copy_text(&applications, ["Edit", "Missing"]),
            "Edit\tC:\\E.EXE\td:\\work"
        );
        assert_eq!(copy_text(&applications, []), "");
    }
}
