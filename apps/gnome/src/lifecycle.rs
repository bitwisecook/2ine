// Public domain. No rights reserved. Wholly AI-authored.
//! Package-only ball targets for rename, Trash and removal.

use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};

use gtk::{gio, prelude::*};

use crate::engine::{Error, Result};

/// A package's filesystem identity, used to detect replacement while a dialog is open.
pub type Identity = (u64, u64);

/// Validate a `.2ineball` package directory (never a symbolic link) and
/// optionally confirm it is still the same directory.
pub fn package(path: &Path, expected: Option<Identity>) -> Result<PathBuf> {
    let path = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()?.join(path)
    };
    let metadata = path.symlink_metadata()?;
    if metadata.file_type().is_symlink() || !metadata.is_dir() {
        return Err(Error::message(
            "Choose a ball package directory, not a symbolic link.",
        ));
    }
    let path = path.canonicalize()?;
    let home = crate::engine::home()
        .canonicalize()
        .unwrap_or_else(|_| crate::engine::home());
    let suffix = path
        .extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("2ineball"));
    if path == home || path.parent().is_none() || !suffix {
        return Err(Error::message("Choose a .2ineball package."));
    }
    if !path
        .join("ball.json")
        .symlink_metadata()
        .is_ok_and(|metadata| metadata.is_file())
    {
        return Err(Error::message(
            "The selected directory is not a ball package.",
        ));
    }
    if let Some(expected) = expected
        && identity(&path)? != expected
    {
        return Err(Error::message(
            "The ball changed on disk. Select it again before continuing.",
        ));
    }
    Ok(path)
}

pub fn identity(path: &Path) -> Result<Identity> {
    let metadata = path.symlink_metadata()?;
    Ok((metadata.dev(), metadata.ino()))
}

/// The renamed package path for a user-entered name.
pub fn rename_destination(path: &Path, name: &str) -> Result<PathBuf> {
    let source = package(path, None)?;
    let name = name.trim();
    let name = if name.to_lowercase().ends_with(".2ineball") {
        &name[..name.len() - 9]
    } else {
        name
    };
    if name.is_empty()
        || name == "."
        || name == ".."
        || name
            .chars()
            .any(|c| matches!(c, '/' | '\\') || (c as u32) < 32 || c as u32 == 127)
    {
        return Err(Error::message(
            "Choose a ball name without slashes or control characters.",
        ));
    }
    let destination = source.with_file_name(format!("{name}.2ineball"));
    if destination != source && destination.symlink_metadata().is_ok() {
        return Err(Error::message(
            "A file or ball with that name already exists.",
        ));
    }
    Ok(destination)
}

/// Validate a new ball name and return its package path inside `location`.
pub fn new_ball_path(location: &Path, name: &str) -> Result<PathBuf> {
    let name = name.trim();
    if name.is_empty() || name == "." || name == ".." || name.contains(['/', '\\', '\r', '\n']) {
        return Err(Error::message("Choose a ball name without slashes."));
    }
    let name = if name.ends_with(".2ineball") {
        name.to_owned()
    } else {
        format!("{name}.2ineball")
    };
    Ok(location.join(name))
}

pub const ICON_NAME: &str = "application-x-2ine-ball";

/// Assign the themed ball icon in this machine's GVFS metadata, without
/// writing into the ball or replacing a custom icon. Returns whether the icon applies.
pub fn decorate(path: &Path) -> bool {
    let suffix = path
        .extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("2ineball"));
    if !suffix || !path.join("ball.json").is_file() {
        return false;
    }
    let file = gio::File::for_path(path.canonicalize().unwrap_or_else(|_| path.to_path_buf()));
    let flags = gio::FileQueryInfoFlags::NOFOLLOW_SYMLINKS;
    let Ok(info) = file.query_info(
        "metadata::custom-icon,metadata::custom-icon-name",
        flags,
        gio::Cancellable::NONE,
    ) else {
        return false;
    };
    let current = info.attribute_string("metadata::custom-icon-name");
    if info.attribute_string("metadata::custom-icon").is_some() || current.is_some() {
        return current.as_deref() == Some(ICON_NAME);
    }
    file.set_attribute_string(
        "metadata::custom-icon-name",
        ICON_NAME,
        flags,
        gio::Cancellable::NONE,
    )
    .is_ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ball(root: &Path, name: &str) -> PathBuf {
        let path = root.join(name);
        std::fs::create_dir(&path).unwrap();
        std::fs::write(path.join("ball.json"), "{}").unwrap();
        path
    }

    #[test]
    fn packages_require_the_suffix_a_manifest_and_no_symlink() {
        let root = tempfile::tempdir().unwrap();
        let good = ball(root.path(), "Office.2ineball");
        assert!(package(&good, None).is_ok());
        let plain = ball(root.path(), "Plain");
        assert!(package(&plain, None).is_err());
        let link = root.path().join("Link.2ineball");
        std::os::unix::fs::symlink(&good, &link).unwrap();
        assert!(package(&link, None).is_err());
        let identity = identity(&good).unwrap();
        assert!(package(&good, Some((identity.0, identity.1 + 1))).is_err());
    }

    #[test]
    fn rename_rejects_separators_and_existing_names() {
        let root = tempfile::tempdir().unwrap();
        let good = ball(root.path(), "Office.2ineball");
        ball(root.path(), "Taken.2ineball");
        assert_eq!(
            rename_destination(&good, " Suite.2ineball ").unwrap(),
            root.path().canonicalize().unwrap().join("Suite.2ineball")
        );
        assert!(rename_destination(&good, "a/b").is_err());
        assert!(rename_destination(&good, "Taken").is_err());
        assert!(new_ball_path(root.path(), "..").is_err());
        assert_eq!(
            new_ball_path(root.path(), "New").unwrap(),
            root.path().join("New.2ineball")
        );
    }
}
