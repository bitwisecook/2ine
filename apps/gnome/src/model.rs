// Public domain. No rights reserved. Wholly AI-authored.
//! Read-only typed views of engine snapshots.
//!
//! The engine owns `ball.json`; these types only describe what the client
//! displays. Unknown fields are ignored, and loosely structured reports stay as
//! JSON values so that additive engine fields never break the client.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::Deserialize;
use serde_json::Value;

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Snapshot {
    pub manifest: Manifest,
    pub os2_support: Value,
    pub dependency_audit: Option<Value>,
    pub application_registration: Option<Value>,
    pub application_records: BTreeMap<String, String>,
    pub drive_records: BTreeMap<String, String>,
    pub image_sources: BTreeMap<String, Value>,
    pub roots: BTreeMap<String, PathBuf>,
    pub unavailable_drives: BTreeMap<String, String>,
    pub working_directories: BTreeMap<String, String>,
    pub current_drive: String,
    pub environment_defaults: BTreeMap<String, String>,
    pub config_sys: String,
}

impl Snapshot {
    /// The drive mapping for a letter such as `"D"`.
    pub fn drive(&self, letter: &str) -> Option<&Drive> {
        self.manifest.drives.get(letter)
    }

    /// Removable devices (CD, floppy and disk), sorted by letter.
    pub fn devices(&self) -> impl Iterator<Item = (&str, &Device)> {
        self.manifest
            .drives
            .iter()
            .filter_map(|(letter, drive)| match drive {
                Drive::Device(device) => Some((letter.as_str(), device)),
                Drive::Folder(_) => None,
            })
    }

    /// The image attached to a device, unless it is missing on this host.
    pub fn attached_image(&self, letter: &str) -> Option<&str> {
        if self.unavailable_drives.contains_key(letter) {
            return None;
        }
        match self.drive(letter)? {
            Drive::Device(device) => device.image.as_deref(),
            Drive::Folder(_) => None,
        }
    }

    pub fn system(&self, key: &str) -> Option<&Value> {
        self.manifest.system.get(key)
    }

    /// A `system.*` setting as the engine's boolean text forms.
    pub fn system_flag(&self, key: &str, default: bool) -> bool {
        match self.system(key) {
            Some(Value::Bool(value)) => *value,
            Some(Value::String(text)) => {
                matches!(
                    text.to_lowercase().as_str(),
                    "1" | "yes" | "y" | "true" | "t" | "on"
                )
            }
            Some(Value::Number(number)) => number.as_f64().is_some_and(|value| value != 0.0),
            _ => default,
        }
    }

    pub fn system_text(&self, key: &str) -> Option<String> {
        self.system(key).map(|value| match value {
            Value::String(text) => text.clone(),
            other => other.to_string(),
        })
    }
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct Manifest {
    pub version: u32,
    pub drives: BTreeMap<String, Drive>,
    pub environment: BTreeMap<String, String>,
    pub system: BTreeMap<String, Value>,
    pub runtime: Runtime,
    pub applications: BTreeMap<String, Application>,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct Runtime {
    pub build: Option<String>,
    pub backend: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(untagged)]
pub enum Drive {
    Folder(String),
    Device(Device),
}

impl Drive {
    pub fn kind(&self) -> DriveKind {
        match self {
            Self::Folder(_) => DriveKind::Folder,
            Self::Device(device) => device.device,
        }
    }

    pub fn device(&self) -> Option<&Device> {
        match self {
            Self::Device(device) => Some(device),
            Self::Folder(_) => None,
        }
    }
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct Device {
    pub device: DriveKind,
    pub image: Option<String>,
    pub readonly: bool,
    pub locked: bool,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum DriveKind {
    Folder,
    #[default]
    Cdrom,
    Floppy,
    Disk,
}

impl DriveKind {
    pub const REMOVABLE: [Self; 3] = [Self::Cdrom, Self::Floppy, Self::Disk];

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Folder => "folder",
            Self::Cdrom => "cdrom",
            Self::Floppy => "floppy",
            Self::Disk => "disk",
        }
    }

    pub fn title(self) -> &'static str {
        match self {
            Self::Folder => "Folder",
            Self::Cdrom => "CD",
            Self::Floppy => "Floppy",
            Self::Disk => "Disk",
        }
    }

    pub fn icon(self) -> &'static str {
        match self {
            Self::Folder => "folder-symbolic",
            Self::Cdrom => "media-optical-symbolic",
            Self::Floppy => "media-floppy-symbolic",
            Self::Disk => "drive-harddisk-symbolic",
        }
    }
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Application {
    pub program: String,
    pub arguments: Vec<String>,
    pub cwd: Option<String>,
    pub working_directories: BTreeMap<String, String>,
    pub environment: BTreeMap<String, Option<String>>,
    pub icon: Option<String>,
    #[serde(flatten)]
    pub extra: BTreeMap<String, Value>,
}

/// Summarise a launcher's working folders: per-drive overrides plus its initial folder.
pub fn working_directory_summary(application: &Application) -> String {
    let mut directories = application.working_directories.clone();
    if let Some(cwd) = application.cwd.as_deref()
        && let Some(drive) = cwd.chars().next()
    {
        directories.insert(drive.to_uppercase().collect(), cwd.to_owned());
    }
    if directories.is_empty() {
        return "Per-drive defaults".into();
    }
    directories.into_values().collect::<Vec<_>>().join("; ")
}

/// Abbreviate the home directory to `~` for display.
pub fn display_path(path: impl AsRef<Path>) -> String {
    let path = path.as_ref();
    let home = crate::engine::home();
    match path.strip_prefix(&home) {
        Ok(rest) if rest.as_os_str().is_empty() => "~".into(),
        Ok(rest) => format!("~/{}", rest.display()),
        Err(_) => path.display().to_string(),
    }
}

/// The display name of a ball package.
pub fn ball_name(path: &Path) -> String {
    let name = path
        .file_name()
        .map_or_else(String::new, |name| name.to_string_lossy().into_owned());
    name.strip_suffix(".2ineball")
        .map_or_else(|| name.clone(), str::to_owned)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn drives_decode_folders_and_devices() {
        let snapshot: Snapshot = serde_json::from_value(json!({
            "manifest": {"drives": {"C": "drive_c", "D": {"device": "cdrom", "image": "/x.iso"}}},
            "unavailableDrives": {}
        }))
        .unwrap();
        assert_eq!(snapshot.drive("C").unwrap().kind(), DriveKind::Folder);
        assert_eq!(snapshot.attached_image("D"), Some("/x.iso"));
        assert_eq!(snapshot.devices().count(), 1);
    }

    #[test]
    fn flags_accept_engine_text_forms() {
        let snapshot: Snapshot = serde_json::from_value(
            json!({"manifest": {"system": {"a": "on", "b": "0", "c": true}}}),
        )
        .unwrap();
        assert!(snapshot.system_flag("a", false));
        assert!(!snapshot.system_flag("b", true));
        assert!(snapshot.system_flag("c", false));
        assert!(snapshot.system_flag("missing", true));
    }

    #[test]
    fn ball_names_drop_the_package_suffix() {
        assert_eq!(ball_name(Path::new("/x/Office.2ineball")), "Office");
        assert_eq!(ball_name(Path::new("/x/Plain")), "Plain");
    }
}
