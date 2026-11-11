// Public domain. No rights reserved. Wholly AI-authored.
//! Folder contents read on explicit expansion in the file tree.
//!
//! Unread folders show a single placeholder child so they can be disclosed;
//! contents are read once per folder when the user expands it, and a failed
//! read is shown inline until the folder is expanded again.

use std::collections::{HashMap, HashSet};
use std::rc::Rc;

use gtk::glib;

use super::entry::Entry;

#[derive(Debug, Default)]
pub struct FolderChildren {
    entries: HashMap<String, Vec<Rc<Entry>>>,
    pending: HashSet<String>,
    errors: HashMap<String, String>,
}

fn key(path: &str) -> String {
    glib::casefold(path).into()
}

impl FolderChildren {
    /// The known children of a folder, or a loading or error placeholder.
    pub fn children(&self, entry: &Entry) -> Vec<Rc<Entry>> {
        if !entry.directory || entry.placeholder {
            return Vec::new();
        }
        let key = key(&entry.path);
        if let Some(entries) = self.entries.get(&key) {
            return entries.clone();
        }
        vec![Rc::new(Entry::placeholder(
            &entry.path,
            self.errors.get(&key).map(String::as_str),
        ))]
    }

    /// Whether a read of `path` is in progress.
    pub fn is_pending(&self, path: &str) -> bool {
        self.pending.contains(&key(path))
    }

    /// Start reading a folder; false when it is already read or being read.
    pub fn begin(&mut self, path: &str) -> bool {
        let key = key(path);
        if self.pending.contains(&key) || self.entries.contains_key(&key) {
            return false;
        }
        self.errors.remove(&key);
        self.pending.insert(key);
        true
    }

    pub fn complete(&mut self, path: &str, entries: Vec<Rc<Entry>>) {
        let key = key(path);
        self.pending.remove(&key);
        self.errors.remove(&key);
        self.entries.insert(key, entries);
    }

    pub fn fail(&mut self, path: &str, error: &str) {
        let key = key(path);
        self.pending.remove(&key);
        self.errors.insert(key, error.to_owned());
    }

    /// Forget every read folder, for example after the ball's drives change.
    pub fn clear(&mut self) {
        self.entries.clear();
        self.errors.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn folder(path: &str) -> Entry {
        Entry {
            name: "OS2".into(),
            path: path.into(),
            directory: true,
            ..Entry::default()
        }
    }

    #[test]
    fn unknown_folder_offers_disclosure_without_reading_or_eager_recursion() {
        let folders = FolderChildren::default();
        let children = folders.children(&folder(r"C:\OS2"));
        assert!(children[0].placeholder);
        assert!(folders.pending.is_empty());
        assert!(folders.children(&children[0]).is_empty());
        assert!(
            folders
                .children(&Entry {
                    directory: false,
                    ..folder(r"C:\OS2")
                })
                .is_empty()
        );
    }

    #[test]
    fn only_explicit_expansion_reads_once_and_retains_real_child_identity() {
        let mut folders = FolderChildren::default();
        assert!(folders.begin(r"C:\OS2"));
        assert!(!folders.begin(r"c:\os2"));
        assert!(folders.is_pending(r"C:\os2"));
        let child = Rc::new(folder(r"C:\OS2\DLL"));
        folders.complete(r"C:\OS2", vec![child.clone()]);
        assert!(Rc::ptr_eq(&folders.children(&folder(r"c:\os2"))[0], &child));
        assert!(!folders.begin(r"C:\OS2"));
        assert!(folders.pending.is_empty());
        assert!(folders.children(&child)[0].placeholder);
    }

    #[test]
    fn failure_displays_inline_and_retry_requires_another_expansion() {
        let mut folders = FolderChildren::default();
        folders.begin(r"D:\MEDIA");
        folders.fail(r"D:\MEDIA", "No medium");
        let media = folder(r"D:\MEDIA");
        assert!(folders.children(&media)[0].error);
        assert_eq!(folders.children(&media)[0].name, "No medium");
        assert!(folders.pending.is_empty());
        assert!(folders.begin(r"D:\MEDIA"));
        folders.complete(r"D:\MEDIA", Vec::new());
        assert!(folders.children(&media).is_empty());
    }
}
