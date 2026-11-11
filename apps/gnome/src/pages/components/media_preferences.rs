// Public domain. No rights reserved. Wholly AI-authored.
//! Verified OS/2 media remembered in host preferences, independent of balls.
//!
//! A ball's own `os2Support.mediaSources` takes priority over the media last
//! verified in any ball. Only media that `validate-os2-media` reports as a
//! complete installation set is remembered; the physical files behind its
//! opaque normalised sources seed the next file chooser.

use std::path::PathBuf;

use serde_json::Value;

use crate::model::Snapshot;
use crate::preferences::Preferences;

/// Non-empty, NUL-free strings from a JSON array, deduplicated in order.
pub fn sources(value: &Value) -> Vec<String> {
    let strings = value
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(Value::as_str);
    unique(strings)
}

/// Deduplicate stored sources, dropping empty and NUL-containing entries.
pub fn clean(values: &[String]) -> Vec<String> {
    unique(values.iter().map(String::as_str))
}

fn unique<'a>(values: impl Iterator<Item = &'a str>) -> Vec<String> {
    let mut result: Vec<String> = Vec::new();
    for value in values {
        if !value.is_empty() && !value.contains('\0') && !result.iter().any(|known| known == value)
        {
            result.push(value.to_owned());
        }
    }
    result
}

/// The media to reuse for a ball: its own sources, otherwise the last verified media.
pub fn preferred(snapshot: Option<&Snapshot>, preferences: &Preferences) -> Vec<String> {
    let local = snapshot
        .map(|snapshot| sources(&snapshot.os2_support["mediaSources"]))
        .unwrap_or_default();
    if local.is_empty() {
        clean(&preferences.last_os2_media_sources)
    } else {
        local
    }
}

/// The normalised sources of a complete validation result.
pub fn verified(result: &Value) -> Vec<String> {
    if result["complete"].as_bool() == Some(true) {
        sources(&result["sources"])
    } else {
        Vec::new()
    }
}

/// Remember verified media; incomplete or unverified results never replace it.
pub fn remember(preferences: &mut Preferences, result: &Value) -> bool {
    let selected = verified(result);
    if selected.is_empty() {
        return false;
    }
    preferences.last_os2_media_sources = selected;
    preferences.last_os2_media_files = sources(&result["physicalSources"]);
    true
}

/// Forget the remembered media. Returns whether anything was stored.
pub fn forget(preferences: &mut Preferences) -> bool {
    let changed = !preferences.last_os2_media_sources.is_empty()
        || !preferences.last_os2_media_files.is_empty();
    preferences.last_os2_media_sources.clear();
    preferences.last_os2_media_files.clear();
    changed
}

/// The first remembered physical file or folder that still exists.
pub fn picker_path(preferences: &Preferences) -> Option<PathBuf> {
    clean(&preferences.last_os2_media_files)
        .into_iter()
        .map(PathBuf::from)
        .find(|path| path.is_file() || path.is_dir())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn snapshot(value: &Value) -> Snapshot {
        serde_json::from_value(value.clone()).unwrap()
    }

    #[test]
    fn new_ball_reuses_global_but_existing_ball_keeps_its_source() {
        let preferences = Preferences {
            last_os2_media_sources: vec!["/media/server.zip!/disk.iso!/os2image".into()],
            ..Preferences::default()
        };
        assert_eq!(
            preferred(Some(&snapshot(&json!({}))), &preferences),
            preferences.last_os2_media_sources
        );
        assert_eq!(
            preferred(None, &preferences),
            preferences.last_os2_media_sources
        );
        let local =
            snapshot(&json!({"os2Support": {"mediaSources": ["/media/warp.iso!/os2image"]}}));
        assert_eq!(
            preferred(Some(&local), &preferences),
            ["/media/warp.iso!/os2image"]
        );
        assert_eq!(
            preferences.last_os2_media_sources,
            ["/media/server.zip!/disk.iso!/os2image"]
        );
    }

    #[test]
    fn validation_retains_opaque_normalized_paths_and_physical_picker_paths() {
        let mut preferences = Preferences::default();
        preferences
            .other
            .insert("otherPreference".into(), json!(true));
        let sources = [
            "/media/server.zip!/WSEB_CP2.iso!/OS2IMAGE",
            "/media/server.zip!/WSEB_CP2.iso!/CID/SERVER/MPTS/MPTN",
        ];
        assert!(remember(
            &mut preferences,
            &json!({"complete": true, "sources": sources, "physicalSources": ["/media/server.zip"]})
        ));
        assert_eq!(preferences.last_os2_media_sources, sources);
        assert_eq!(preferences.last_os2_media_files, ["/media/server.zip"]);
        assert_eq!(preferences.other["otherPreference"], true);
    }

    #[test]
    fn incomplete_mixed_or_invalid_media_never_overwrites_success() {
        let mut preferences = Preferences {
            last_os2_media_sources: vec!["valid".into()],
            last_os2_media_files: vec!["physical".into()],
            ..Preferences::default()
        };
        for result in [
            json!({"complete": false, "sources": ["invalid"], "media": [{"status": "verified-os2-media"}]}),
            json!({"sources": ["unchecked"]}),
            json!({"complete": true, "sources": []}),
        ] {
            assert!(!remember(&mut preferences, &result));
            assert_eq!(preferences.last_os2_media_sources, ["valid"]);
            assert_eq!(preferences.last_os2_media_files, ["physical"]);
        }
    }

    #[test]
    fn picker_uses_existing_physical_sources_without_parsing_virtual_paths() {
        let directory = tempfile::tempdir().unwrap();
        let archive = directory.path().join("disk.zip");
        std::fs::write(&archive, b"").unwrap();
        let mut preferences = Preferences {
            last_os2_media_files: vec![
                "/does/not/exist".into(),
                archive.to_string_lossy().into_owned(),
            ],
            ..Preferences::default()
        };
        assert_eq!(picker_path(&preferences), Some(archive.clone()));
        preferences.last_os2_media_files = vec![directory.path().to_string_lossy().into_owned()];
        assert_eq!(picker_path(&preferences).as_deref(), Some(directory.path()));
        let virtual_only = Preferences {
            last_os2_media_sources: vec![format!("{}!/disk.iso", archive.display())],
            ..Preferences::default()
        };
        assert_eq!(picker_path(&virtual_only), None);
    }

    #[test]
    fn forget_removes_only_global_media_and_keeps_chooser_possible() {
        let mut preferences = Preferences {
            last_os2_media_sources: vec!["missing".into()],
            last_os2_media_files: vec!["missing".into()],
            ball_paths: vec!["ball".into()],
            ..Preferences::default()
        };
        assert!(forget(&mut preferences));
        assert!(
            preferences.last_os2_media_sources.is_empty()
                && preferences.last_os2_media_files.is_empty()
        );
        assert_eq!(preferences.ball_paths, [PathBuf::from("ball")]);
        assert!(!forget(&mut preferences));
        assert_eq!(preferred(None, &preferences), Vec::<String>::new());
    }

    #[test]
    fn malformed_values_are_filtered_and_duplicate_sources_are_stable() {
        assert_eq!(
            sources(&json!(["disk", "disk", null, 10, "", "a\0b", "other"])),
            ["disk", "other"]
        );
        assert_eq!(sources(&json!("disk")), Vec::<String>::new());
        assert!(remember(
            &mut Preferences::default(),
            &json!({"complete": true, "sources": ["disk"]})
        ));
    }
}
