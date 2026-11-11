// Public domain. No rights reserved. Wholly AI-authored.
//! Drive presentation helpers: availability, backing source, descriptions,
//! letter choices and image sizes, all derived from a ball snapshot.

use crate::model::{Drive, DriveKind, Snapshot};

/// Letters offered for new drives; `C:` always holds the ball's system drive.
pub const DRIVE_LETTERS: &str = "ABDEFGHIJKLMNOPQRSTUVWXYZ";
/// Preferred order for a new folder, CD or disk: `D:` onwards, then the floppy letters.
pub const PREFERRED_LETTERS: &str = "DEFGHIJKLMNOPQRSTUVWXYZAB";

/// Fixed disk image sizes in MiB.
pub const DISK_CAPACITIES: [u32; 14] = [
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536,
];
/// Disk image filesystems: engine key and title.
pub const DISK_FILESYSTEMS: [(&str, &str); 3] =
    [("fat32", "FAT32"), ("exfat", "exFAT"), ("hpfs", "HPFS")];
/// Floppy image sizes in KiB.
pub const FLOPPY_CAPACITIES: [u32; 5] = [360, 720, 1200, 1440, 2880];

/// Why a mapped drive cannot be browsed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DriveState {
    /// A folder mapping whose host folder is missing.
    Disconnected,
    /// A removable device without an available image.
    Ejected,
}

/// The availability of a mapped drive letter; None when it can be browsed.
pub fn drive_state(snapshot: &Snapshot, letter: &str) -> Option<DriveState> {
    let unavailable = snapshot
        .unavailable_drives
        .get(letter)
        .is_some_and(|path| !path.is_empty());
    match snapshot.drive(letter)? {
        Drive::Folder(_) => unavailable.then_some(DriveState::Disconnected),
        Drive::Device(device) => (unavailable || device.image.as_deref().is_none_or(str::is_empty))
            .then_some(DriveState::Ejected),
    }
}

/// The host folder or image path that backs a drive, if any.
pub fn drive_source(snapshot: &Snapshot, letter: &str) -> Option<String> {
    match snapshot.drive(letter)? {
        Drive::Folder(mapping) => Some(
            snapshot
                .unavailable_drives
                .get(letter)
                .filter(|path| !path.is_empty())
                .cloned()
                .or_else(|| {
                    snapshot
                        .roots
                        .get(letter)
                        .map(|root| root.to_string_lossy().into_owned())
                })
                .unwrap_or_else(|| mapping.clone()),
        ),
        Drive::Device(device) => snapshot
            .image_sources
            .get(letter)
            .and_then(|source| source.as_str())
            .filter(|source| !source.is_empty())
            .map(str::to_owned)
            .or_else(|| device.image.clone().filter(|image| !image.is_empty())),
    }
}

/// Describe a drive's kind and access state, for example "CD · Read Only".
pub fn drive_description(snapshot: &Snapshot, letter: &str) -> String {
    let state = drive_state(snapshot, letter);
    match snapshot.drive(letter) {
        None | Some(Drive::Folder(_)) => if state.is_some() {
            "Disconnected Folder"
        } else {
            "Folder"
        }
        .into(),
        Some(Drive::Device(device)) => {
            let access = if state.is_some() {
                "Ejected"
            } else if device.device == DriveKind::Cdrom || device.readonly {
                "Read Only"
            } else {
                "Writable"
            };
            let locked = if device.locked { " · Locked" } else { "" };
            format!("{} · {access}{locked}", device.device.title())
        }
    }
}

pub fn drive_icon(snapshot: &Snapshot, letter: &str) -> &'static str {
    snapshot
        .drive(letter)
        .map_or(DriveKind::Folder, Drive::kind)
        .icon()
}

/// Whether files can be copied onto a drive: it is available and not a CD or protected image.
pub fn drive_writable(snapshot: &Snapshot, letter: &str) -> bool {
    drive_state(snapshot, letter).is_none() && media_writable(snapshot, letter)
}

/// Whether a drive's medium accepts writes, ignoring availability.
pub fn media_writable(snapshot: &Snapshot, letter: &str) -> bool {
    match snapshot.drive(letter) {
        Some(Drive::Device(device)) => device.device != DriveKind::Cdrom && !device.readonly,
        _ => true,
    }
}

/// Label a drive-letter choice, noting the mapping that choosing it replaces.
pub fn replacement_label(snapshot: &Snapshot, letter: &str) -> String {
    match snapshot.drive(letter) {
        None => format!("{letter}:"),
        Some(Drive::Folder(_)) => format!("{letter}: — Replaces Folder"),
        Some(Drive::Device(device)) => {
            let kind = device.device.title();
            if device
                .image
                .as_deref()
                .is_some_and(|image| !image.is_empty())
            {
                format!("{letter}: — Replaces {kind} Image")
            } else {
                format!("{letter}: — Empty {kind} Drive")
            }
        }
    }
}

/// The index of the first unmapped letter in a preferred order, or 0.
pub fn first_free(letters: &[String], snapshot: &Snapshot, order: &str) -> u32 {
    order
        .chars()
        .map(String::from)
        .find(|letter| letters.contains(letter) && snapshot.drive(letter).is_none())
        .and_then(|letter| letters.iter().position(|known| *known == letter))
        .and_then(|index| u32::try_from(index).ok())
        .unwrap_or(0)
}

/// Unmapped letters and existing devices of the matching kind for a new image.
pub fn image_letters(snapshot: &Snapshot, floppy: bool, letter: Option<&str>) -> Vec<String> {
    if let Some(letter) = letter {
        return vec![letter.to_owned()];
    }
    let kind = if floppy {
        DriveKind::Floppy
    } else {
        DriveKind::Disk
    };
    DRIVE_LETTERS
        .chars()
        .map(String::from)
        .filter(|letter| match snapshot.drive(letter) {
            None => true,
            Some(Drive::Device(device)) => device.device == kind,
            Some(Drive::Folder(_)) => false,
        })
        .collect()
}

/// The fixed image sizes, in MiB, that a filesystem can be created with.
pub fn disk_capacities(filesystem: &str) -> Vec<u32> {
    DISK_CAPACITIES
        .into_iter()
        .filter(|&mib| filesystem != "fat32" || mib >= 64)
        .collect()
}

pub fn capacity_label(mib: u32) -> String {
    if mib >= 1024 {
        format!("{} GiB", mib / 1024)
    } else {
        format!("{mib} MiB")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn snapshot() -> Snapshot {
        serde_json::from_value(json!({
            "manifest": {"drives": {
                "C": "/ball/drive_c",
                "D": {"device": "cdrom", "image": "/media/os2.iso"},
                "E": "/missing",
                "F": {"device": "disk"},
                "G": {"device": "floppy", "image": "/media/a.img", "locked": true},
            }},
            "roots": {"C": "/ball/drive_c", "D": "/ball/images/D", "G": "/ball/images/G"},
            "unavailableDrives": {"E": "/missing"},
            "imageSources": {"D": "/media/os2.iso", "G": "/media/a.img"},
        }))
        .expect("snapshot")
    }

    fn letters() -> Vec<String> {
        DRIVE_LETTERS.chars().map(String::from).collect()
    }

    #[test]
    fn states_distinguish_disconnected_folders_and_ejected_devices() {
        let state = snapshot();
        assert_eq!(drive_state(&state, "C"), None);
        assert_eq!(drive_state(&state, "D"), None);
        assert_eq!(drive_state(&state, "E"), Some(DriveState::Disconnected));
        assert_eq!(drive_state(&state, "F"), Some(DriveState::Ejected));
        assert_eq!(drive_description(&state, "D"), "CD · Read Only");
        assert_eq!(drive_description(&state, "F"), "Disk · Ejected");
        assert_eq!(drive_description(&state, "G"), "Floppy · Writable · Locked");
        assert_eq!(drive_description(&state, "E"), "Disconnected Folder");
        assert_eq!(drive_source(&state, "D").as_deref(), Some("/media/os2.iso"));
        assert_eq!(drive_source(&state, "E").as_deref(), Some("/missing"));
        assert_eq!(drive_source(&state, "C").as_deref(), Some("/ball/drive_c"));
        assert_eq!(drive_source(&state, "F"), None);
        assert_eq!(drive_icon(&state, "G"), "media-floppy-symbolic");
    }

    #[test]
    fn writes_are_refused_for_cds_protected_and_unavailable_media() {
        let state = snapshot();
        assert!(drive_writable(&state, "C"));
        assert!(!drive_writable(&state, "D"));
        assert!(!drive_writable(&state, "E"));
        assert!(!drive_writable(&state, "F"));
        assert!(drive_writable(&state, "G"));
    }

    #[test]
    fn letter_choices_include_mapped_letters_as_replacements() {
        let state = snapshot();
        assert_eq!(replacement_label(&state, "H"), "H:");
        assert_eq!(replacement_label(&state, "E"), "E: — Replaces Folder");
        assert_eq!(replacement_label(&state, "D"), "D: — Replaces CD Image");
        assert_eq!(replacement_label(&state, "F"), "F: — Empty Disk Drive");
        let letters = letters();
        assert_eq!(
            letters[first_free(&letters, &state, PREFERRED_LETTERS) as usize],
            "H"
        );
        assert!(!letters.contains(&"C".to_owned()));
        assert_eq!(
            letters[first_free(&letters, &state, DRIVE_LETTERS) as usize],
            "A"
        );
    }

    #[test]
    fn image_creation_offers_matching_devices_and_fixed_sizes() {
        let state = snapshot();
        let disks = image_letters(&state, false, None);
        let floppies = image_letters(&state, true, None);
        assert!(disks.contains(&"F".to_owned()));
        assert!(!disks.contains(&"G".to_owned()));
        assert!(!disks.contains(&"D".to_owned()));
        assert!(floppies.contains(&"G".to_owned()));
        assert_eq!(image_letters(&state, false, Some("F")), ["F"]);
        assert_eq!(disk_capacities("fat32")[0], 64);
        assert_eq!(disk_capacities("hpfs")[0], 8);
        assert_eq!(disk_capacities("exfat").last(), Some(&65536));
        assert_eq!(capacity_label(512), "512 MiB");
        assert_eq!(capacity_label(2048), "2 GiB");
    }
}
