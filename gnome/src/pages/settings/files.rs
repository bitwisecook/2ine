// Public domain. No rights reserved. Wholly AI-authored.
//! Config Files: CONFIG.SYS, OS/2 profiles and other configuration files
//! found in the ball's private C: drive.
//!
//! Profiles open in the binary-safe profile editor. Other text files open in
//! a text editor whose unsaved draft is kept in the window, with the file
//! revision it was loaded from, until it is saved or discarded.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::PathBuf;
use std::rc::Rc;

use adw::prelude::*;
use gtk::glib;
use serde::Deserialize;
use serde_json::json;

use super::profile::{ProfilePage, Source};
use super::registry_values::to_latin1;
use super::table::{Column, Options, Table, Tree};
use super::{Context, confirm_discard, flat_button, write};
use crate::window::BallWindow;

/// A configuration file found by `configuration-files`.
#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct FileRecord {
    pub path: String,
    pub label: String,
    pub role: String,
    pub kind: String,
    pub editable: bool,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
struct Listing {
    files: Vec<FileRecord>,
    truncated: bool,
}

/// A file read by `configuration-file-read`.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct FileSnapshot {
    pub path: String,
    pub revision: String,
    pub editable: bool,
    pub reason: String,
    pub text: Option<String>,
    pub data: Option<String>,
}

thread_local! {
    /// The configuration file last shown for each ball.
    static FILE_PATHS: RefCell<HashMap<PathBuf, String>> = RefCell::default();
}

/// The unsaved draft of a file: the revision it was loaded from and its text.
pub fn file_draft(window: &BallWindow, key: &(PathBuf, String)) -> Option<(String, String)> {
    window.file_drafts().borrow().get(key).cloned()
}

fn set_file_draft(window: &BallWindow, key: (PathBuf, String), draft: Option<(String, String)>) {
    match draft {
        Some(draft) => window.file_drafts().borrow_mut().insert(key, draft),
        None => window.file_drafts().borrow_mut().remove(&key),
    };
}

/// Discovered files other than the registry, which has its own section.
fn editable_records(files: Vec<FileRecord>) -> Vec<FileRecord> {
    files
        .into_iter()
        .filter(|record| record.role != "registry")
        .collect()
}

pub struct FilesPage {
    pub widget: gtk::ScrolledWindow,
    context: Rc<Context>,
    table: Rc<Table<FileRecord>>,
    notice: adw::Banner,
    content: gtk::Box,
    config: gtk::Widget,
    records: RefCell<Vec<FileRecord>>,
    current: RefCell<Option<FileRecord>>,
    profile: RefCell<Option<Rc<ProfilePage>>>,
    updating: Cell<bool>,
    serial: Cell<u64>,
    file_path: RefCell<String>,
}

impl FilesPage {
    pub fn new(context: &Rc<Context>, config: &gtk::Widget) -> Rc<Self> {
        let table = Table::new(
            vec![
                Column::new("OS/2 File", |record: &FileRecord| record.label.clone())
                    .width(240, true)
                    .sorted(|record| glib::casefold(&record.label).into()),
                Column::new("Path", |record: &FileRecord| record.path.clone())
                    .width(320, true)
                    .sorted(|record| glib::casefold(&record.path).into()),
            ],
            Options {
                title: "OS/2 configuration files",
                search_hint: "Search Configuration Files",
                identity: Rc::new(|record: &FileRecord| record.path.clone()),
                searchable: Rc::new(|record: &FileRecord| {
                    format!("{} {}", record.label, record.path)
                }),
                tooltip: None,
                single_click: false,
                height: 180,
            },
        );
        let body = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(12)
            .vexpand(true)
            .build();
        let group = adw::PreferencesGroup::builder()
            .description(
                "CONFIG.SYS controls startup settings. OS2.INI and OS2SYS.INI hold user and system profile values; \
                 installers may use additional INI files. Registry keys have their own Settings tab.",
            )
            .build();
        group.add(&table.widget);
        let find = flat_button("Find Files", None);
        group.set_header_suffix(Some(&find));
        body.append(&group);
        let notice = adw::Banner::builder().use_markup(false).build();
        body.append(&notice);
        let editors = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(12)
            .vexpand(true)
            .build();
        body.append(&editors);
        let widget = gtk::ScrolledWindow::builder()
            .hscrollbar_policy(gtk::PolicyType::Never)
            .child(&body)
            .build();
        let file_path = FILE_PATHS
            .with_borrow(|paths| paths.get(&context.ball).cloned())
            .unwrap_or_else(|| "C:\\CONFIG.SYS".into());
        let page = Rc::new(Self {
            widget,
            context: context.clone(),
            table,
            notice,
            content: editors,
            config: config.clone(),
            records: RefCell::default(),
            current: RefCell::default(),
            profile: RefCell::default(),
            updating: Cell::new(false),
            serial: Cell::new(0),
            file_path: RefCell::new(file_path),
        });
        let weak = Rc::downgrade(&page);
        find.connect_clicked(move |_| {
            if let Some(page) = weak.upgrade() {
                page.discover();
            }
        });
        let weak = Rc::downgrade(&page);
        page.table.connect_selected(move |record| {
            if let Some(page) = weak.upgrade() {
                page.selected(record.cloned());
            }
        });
        let weak = Rc::downgrade(&page);
        page.widget.connect_map(move |_| {
            if let Some(page) = weak.upgrade()
                && page.records.borrow().is_empty()
            {
                page.discover();
            }
        });
        page
    }

    pub fn refresh_editability(&self, busy: bool) {
        let config = self
            .current
            .borrow()
            .as_ref()
            .is_some_and(|record| record.role == "config");
        self.content.set_sensitive(!busy || config);
    }

    fn error(&self, error: &dyn std::fmt::Display) {
        if self.context.current().is_some() {
            self.notice.set_title(&error.to_string());
            self.notice.set_revealed(true);
        }
    }

    /// List the ball's configuration files and reopen the last one shown.
    pub fn discover(self: &Rc<Self>) {
        let Some(window) = self.context.current() else {
            return;
        };
        let weak = Rc::downgrade(self);
        glib::spawn_future_local(async move {
            let result = window
                .api("configuration-files", json!({}))
                .await
                .and_then(|value| Ok(serde_json::from_value::<Listing>(value)?));
            let Some(page) = weak.upgrade() else { return };
            if page.context.current().is_none() {
                return;
            }
            match result {
                Ok(listing) => page.listed(listing),
                Err(error) => page.error(&error),
            }
        });
    }

    fn listed(self: &Rc<Self>, listing: Listing) {
        let records = editable_records(listing.files);
        self.updating.set(true);
        self.table
            .set_rows(records.iter().cloned().map(Tree::leaf).collect());
        let wanted = self.file_path.borrow().to_uppercase();
        let selected = records
            .iter()
            .find(|record| record.path.to_uppercase() == wanted)
            .or(records.first());
        if let Some(selected) = selected {
            self.table.focus(&selected.path);
        }
        self.records.replace(records);
        self.updating.set(false);
        self.notice.set_title(if listing.truncated {
            "File discovery reached its limit. Additional files were not listed."
        } else {
            ""
        });
        self.notice.set_revealed(listing.truncated);
        self.selected(self.table.selected());
    }

    fn selected(self: &Rc<Self>, record: Option<FileRecord>) {
        if self.updating.get() {
            return;
        }
        let Some(window) = self.context.current() else {
            return;
        };
        let Some(record) = record else { return };
        self.file_path.replace(record.path.clone());
        FILE_PATHS
            .with_borrow_mut(|paths| paths.insert(self.context.ball.clone(), record.path.clone()));
        self.current.replace(Some(record.clone()));
        self.refresh_editability(window.busy());
        self.serial.set(self.serial.get() + 1);
        let serial = self.serial.get();
        if let Some(previous) = self.profile.take() {
            self.context
                .profile_pages
                .borrow_mut()
                .retain(|page| !std::ptr::eq(page.as_ptr(), Rc::as_ptr(&previous)));
        }
        while let Some(child) = self.content.first_child() {
            self.content.remove(&child);
        }
        if record.role == "config" {
            if let Some(parent) = self.config.parent().and_downcast::<gtk::Box>() {
                parent.remove(&self.config);
            }
            self.content.append(&self.config);
            return;
        }
        if record.kind == "profile" && record.editable {
            let profile = ProfilePage::new(
                &self.context,
                Source::File {
                    path: record.path,
                    role: record.role,
                },
            );
            self.context
                .profile_pages
                .borrow_mut()
                .push(Rc::downgrade(&profile));
            self.content.append(&profile.widget);
            self.profile.replace(Some(profile));
            return;
        }
        let weak = Rc::downgrade(self);
        glib::spawn_future_local(async move {
            let result = window
                .api("configuration-file-read", json!({"path": record.path}))
                .await
                .and_then(|value| Ok(serde_json::from_value::<FileSnapshot>(value)?));
            let Some(page) = weak.upgrade() else { return };
            if page.context.current().is_none() || serial != page.serial.get() {
                return;
            }
            match result {
                Ok(snapshot) => page.show_text(&window, snapshot),
                Err(error) => page.error(&error),
            }
        });
    }

    fn reselect(self: &Rc<Self>) {
        self.selected(self.table.selected());
    }

    fn show_text(self: &Rc<Self>, window: &BallWindow, snapshot: FileSnapshot) {
        let key = (self.context.ball.clone(), snapshot.path.clone());
        let text = snapshot
            .text
            .clone()
            .or_else(|| snapshot.data.clone())
            .unwrap_or_default();
        let draft = file_draft(window, &key);
        let editor = gtk::TextView::builder()
            .monospace(true)
            .vexpand(true)
            .editable(snapshot.editable)
            .build();
        editor.update_property(&[gtk::accessible::Property::Label(&snapshot.path)]);
        let buffer = editor.buffer();
        buffer.set_enable_undo(true);
        buffer.begin_irreversible_action();
        buffer.set_text(draft.as_ref().map_or(&text, |(_, draft)| draft));
        buffer.end_irreversible_action();
        let scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .min_content_height(280)
            .has_frame(true)
            .child(&editor)
            .build();
        self.content.append(
            &gtk::Label::builder()
                .label(format!("{} · {}", snapshot.path, snapshot.reason))
                .wrap(true)
                .xalign(0.0)
                .selectable(true)
                .build(),
        );
        self.content.append(&scroll);
        if !snapshot.editable {
            return;
        }
        let revision = draft.as_ref().map_or_else(
            || snapshot.revision.clone(),
            |(revision, _)| revision.clone(),
        );
        if draft.is_some() && revision != snapshot.revision {
            self.notice.set_title(
                "This file changed on disk. Your draft is preserved; reload before saving.",
            );
            self.notice.set_revealed(true);
        }
        let current = |buffer: &gtk::TextBuffer| -> String {
            buffer
                .text(&buffer.start_iter(), &buffer.end_iter(), true)
                .into()
        };
        buffer.connect_changed(glib::clone!(
            #[weak]
            window,
            #[strong]
            key,
            #[strong]
            revision,
            move |buffer| {
                let value = current(buffer);
                let draft = (value != text).then(|| (revision.clone(), value));
                set_file_draft(&window, key.clone(), draft);
            }
        ));
        let controls = adw::WrapBox::builder()
            .child_spacing(12)
            .line_spacing(6)
            .build();
        let save = gtk::Button::with_label("Save File");
        let reload = gtk::Button::with_label("Reload from Disk");
        controls.append(&save);
        controls.append(&reload);
        self.content.append(&controls);
        let weak = Rc::downgrade(self);
        let path = snapshot.path;
        save.connect_clicked(glib::clone!(
            #[strong]
            key,
            #[weak]
            buffer,
            move |_| {
                let Some(page) = weak.upgrade() else { return };
                let Some(window) = page.context.current() else {
                    return;
                };
                if window.busy() {
                    return;
                }
                let value = current(&buffer);
                if to_latin1(&value).is_err() {
                    page.error(&"OS/2 text files require Latin-1 bytes.");
                    return;
                }
                let fields = json!({"path": path, "expected": revision, "text": value});
                let key = key.clone();
                glib::spawn_future_local(async move {
                    let result = write(&window, "configuration-file-write", fields).await;
                    match result {
                        Ok(_) => {
                            set_file_draft(&window, key, None);
                            if page.context.current().is_some() {
                                page.notice.set_revealed(false);
                                page.reselect();
                            }
                        }
                        Err(error) => page.error(&error),
                    }
                });
            }
        ));
        let weak = Rc::downgrade(self);
        reload.connect_clicked(move |_| {
            let Some(page) = weak.upgrade() else { return };
            let Some(window) = page.context.current() else {
                return;
            };
            let key = key.clone();
            glib::spawn_future_local(async move {
                if window.file_drafts().borrow().contains_key(&key)
                    && !confirm_discard(
                        &window,
                        "Discard this file's changes?",
                        "Your draft will be replaced with the current file on disk.",
                    )
                    .await
                {
                    return;
                }
                set_file_draft(&window, key, None);
                page.notice.set_revealed(false);
                page.reselect();
            });
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registry_files_are_left_to_the_registry_section() {
        let listing: Listing = serde_json::from_value(json!({
            "files": [
                {"path": "C:\\CONFIG.SYS", "label": "CONFIG.SYS", "role": "config", "kind": "text", "editable": true},
                {"path": "C:\\OS2\\REGISTRY.INI", "label": "Registry", "role": "registry", "kind": "profile", "editable": true},
                {"path": "C:\\OS2\\OS2.INI", "label": "OS2.INI", "role": "user", "kind": "profile", "editable": true}
            ],
            "truncated": false
        }))
        .unwrap();
        let records = editable_records(listing.files);
        let roles: Vec<_> = records.iter().map(|record| record.role.as_str()).collect();
        assert_eq!(roles, ["config", "user"]);
    }
}
