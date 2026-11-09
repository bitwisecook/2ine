// Public domain. No rights reserved. Wholly AI-authored.
//! Host application preferences stored as JSON beside other 2ine settings.
//!
//! Unknown keys are retained so that other clients and newer versions keep
//! their values when this client saves.

use std::fs;
use std::io::Write as _;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Preferences {
    #[serde(default, alias = "balls")]
    pub ball_paths: Vec<PathBuf>,
    #[serde(default)]
    pub selected: Option<PathBuf>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ball_folder: Option<PathBuf>,
    #[serde(default = "default_log_size")]
    pub log_font_size: u32,
    #[serde(default)]
    pub custom_log_size: bool,
    #[serde(
        default,
        rename = "lastOS2MediaSources",
        skip_serializing_if = "Vec::is_empty"
    )]
    pub last_os2_media_sources: Vec<String>,
    #[serde(
        default,
        rename = "lastOS2MediaFiles",
        skip_serializing_if = "Vec::is_empty"
    )]
    pub last_os2_media_files: Vec<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub application_sort: Option<String>,
    #[serde(flatten)]
    pub other: Map<String, Value>,
}

const fn default_log_size() -> u32 {
    12
}

impl Preferences {
    /// Read preferences, falling back to the legacy file name and to defaults
    /// for missing or malformed content.
    pub fn load(path: &Path) -> Self {
        let legacy = path.with_file_name("balls-gnome.json");
        let source = if path.exists() {
            path
        } else {
            legacy.as_path()
        };
        let mut preferences: Self = fs::read(source)
            .ok()
            .and_then(|bytes| serde_json::from_slice(&bytes).ok())
            .unwrap_or_default();
        preferences.log_font_size = preferences.log_font_size.clamp(10, 24);
        preferences
    }

    /// Atomically replace the preference file.
    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        let directory = path.parent().unwrap_or(Path::new("."));
        fs::create_dir_all(directory)?;
        let mut temporary = tempfile_in(directory, path)?;
        temporary.1.write_all(&serde_json::to_vec_pretty(self)?)?;
        temporary.1.write_all(b"\n")?;
        temporary.1.sync_all()?;
        fs::rename(&temporary.0, path)
    }
}

fn tempfile_in(directory: &Path, path: &Path) -> std::io::Result<(PathBuf, fs::File)> {
    let name = path
        .file_name()
        .map_or_else(String::new, |name| name.to_string_lossy().into_owned());
    for attempt in 0..100 {
        let candidate = directory.join(format!(".{name}.{}.{attempt}", std::process::id()));
        match fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&candidate)
        {
            Ok(file) => return Ok((candidate, file)),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {}
            Err(error) => return Err(error),
        }
    }
    Err(std::io::Error::other(
        "cannot create a temporary preference file",
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn legacy_keys_load_and_unknown_keys_survive() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("ball-gnome.json");
        fs::write(
            &path,
            r#"{"balls": ["/a"], "logFontSize": 99, "future": 1}"#,
        )
        .unwrap();
        let preferences = Preferences::load(&path);
        assert_eq!(preferences.ball_paths, [PathBuf::from("/a")]);
        assert_eq!(preferences.log_font_size, 24);
        preferences.save(&path).unwrap();
        let saved: Value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
        assert_eq!(saved["future"], 1);
        assert_eq!(saved["ballPaths"][0], "/a");
    }
}
