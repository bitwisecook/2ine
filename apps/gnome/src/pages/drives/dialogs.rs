// Public domain. No rights reserved. Wholly AI-authored.
//! Drive dialogs: Add Drive / Drive Options, Create Image and Device Settings.
//!
//! Each dialog runs its engine commands on the serialized worker (allowed
//! while applications run, as media changes are), keeps errors in the dialog
//! and records one Undo step that restores the drive's previous record.

use adw::prelude::*;
use gtk::glib;
use serde_json::{Value, json};

use super::helpers::{
    DISK_FILESYSTEMS, DRIVE_LETTERS, FLOPPY_CAPACITIES, PREFERRED_LETTERS, capacity_label,
    disk_capacities, drive_description, drive_source, first_free, image_letters, replacement_label,
};
use crate::dialogs::{FormDialog, files, form};
use crate::engine::{self, Error};
use crate::model::{Drive, DriveKind, Snapshot, display_path};
use crate::window::history::{HistoryItem, drive_inverse};
use crate::window::{BallWindow, Work};

/// Source kinds offered by Add Drive, in menu order.
const SOURCES: [DriveKind; 4] = [
    DriveKind::Folder,
    DriveKind::Cdrom,
    DriveKind::Floppy,
    DriveKind::Disk,
];

fn string_list(items: &[String]) -> gtk::StringList {
    let items: Vec<&str> = items.iter().map(String::as_str).collect();
    gtk::StringList::new(&items)
}

fn index(value: u32) -> usize {
    usize::try_from(value).unwrap_or(0)
}

fn position(value: usize) -> u32 {
    u32::try_from(value).unwrap_or(0)
}

fn preferences_page(dialog: &FormDialog) -> adw::PreferencesPage {
    let page = adw::PreferencesPage::builder().vexpand(true).build();
    dialog.append(&page);
    page
}

/// Send a ball command built from `base` (a request carrying ball and build).
fn command(base: &Value, arguments: &[&str]) -> Result<Value, Error> {
    let mut message = base.clone();
    message["arguments"] = json!(arguments);
    engine::request(&message)
}

/// Apply a drive change's snapshot, record its Undo step and close the dialog.
fn finish(
    window: &BallWindow,
    dialog: &FormDialog,
    letter: &str,
    inverse: Value,
    snapshot: Snapshot,
    label: &str,
) {
    if let Some(page) = window.drives_page() {
        page.set_selected_drive(letter);
    }
    let record = snapshot.drive_records.get(letter).cloned();
    window.apply_snapshot(snapshot);
    if let Some(ball) = window.ball()
        && let Some(record) = record
    {
        window.add_history(HistoryItem {
            ball,
            undo: vec![inverse],
            redo: vec![
                json!({"action": "command", "arguments": ["restore-drive", letter, record]}),
            ],
            label: label.to_owned(),
        });
    }
    dialog.close();
}

/// Add Drive (no letter) or Drive Options for an existing letter.
pub fn media_dialog(window: &BallWindow, letter: Option<&str>) {
    let Some(snapshot) = window.snapshot() else {
        return;
    };
    let dialog = FormDialog::new(
        window,
        if letter.is_some() {
            "Drive Options"
        } else {
            "Add Drive"
        },
        "Apply",
    )
    .with_size(620, 480);
    let page = preferences_page(&dialog);
    let group = form::group("Drive", None);
    page.add(&group);

    let letters: Vec<String> = letter.map_or_else(
        || DRIVE_LETTERS.chars().map(String::from).collect(),
        |letter| vec![letter.to_owned()],
    );
    let labels: Vec<String> = letters
        .iter()
        .map(|choice| {
            if letter.is_some() {
                format!("{choice}:")
            } else {
                replacement_label(&snapshot, choice)
            }
        })
        .collect();
    let letter_row = adw::ComboRow::builder()
        .title("Drive Letter")
        .model(&string_list(&labels))
        .subtitle_lines(2)
        .build();
    letter_row.set_selected(first_free(&letters, &snapshot, PREFERRED_LETTERS));
    letter_row.set_sensitive(letter.is_none());
    group.add(&letter_row);

    let mapping = letter.and_then(|letter| snapshot.drive(letter));
    let kind = mapping.map_or(DriveKind::Folder, Drive::kind);
    let kind_row = adw::ComboRow::builder()
        .title("Source")
        .model(&gtk::StringList::new(&[
            "Folder",
            "CD Image",
            "Floppy Image",
            "Disk Image",
        ]))
        .build();
    kind_row.set_selected(position(
        SOURCES.iter().position(|known| *known == kind).unwrap_or(0),
    ));
    group.add(&kind_row);

    let initial = match mapping {
        Some(Drive::Folder(folder)) => folder.clone(),
        Some(Drive::Device(device)) => device.image.clone().unwrap_or_default(),
        None => String::new(),
    };
    let source = adw::EntryRow::builder()
        .title("Folder or Image")
        .text(&initial)
        .build();
    let choose = form::button("Choose Source", Some("folder-open-symbolic"));
    choose.add_css_class("flat");
    source.add_suffix(&choose);
    group.add(&source);
    let notice = adw::ActionRow::builder()
        .title("Keep a Drive Allocated")
        .subtitle(
            "Leave the image path empty to create an ejected device. Changing mappings preserves the previous files.",
        )
        .subtitle_lines(0)
        .use_markup(false)
        .build();
    group.add(&notice);

    let kind_changed = glib::clone!(
        #[weak]
        source,
        #[weak]
        kind_row,
        move || source.set_title(if kind_row.selected() == 0 {
            "Host Folder"
        } else {
            "Image File (optional)"
        })
    );
    kind_changed();
    kind_row.connect_selected_notify(move |_| kind_changed());

    let original = letter.is_some();
    let letter_changed = glib::clone!(
        #[weak]
        window,
        #[weak]
        letter_row,
        #[strong]
        letters,
        move || {
            if original {
                return;
            }
            let Some(snapshot) = window.snapshot() else {
                return;
            };
            let Some(letter) = letters.get(index(letter_row.selected())) else {
                return;
            };
            if snapshot.drive(letter).is_none() {
                letter_row.set_subtitle("");
                return;
            }
            let source =
                drive_source(&snapshot, letter).map(|source| format!(" {}", display_path(source)));
            letter_row.set_subtitle(&format!(
                "Replaces {}{}. Its files are kept.",
                drive_description(&snapshot, letter),
                source.unwrap_or_default()
            ));
        }
    );
    letter_changed();
    letter_row.connect_selected_notify(move |_| letter_changed());

    choose.connect_clicked(glib::clone!(
        #[weak]
        window,
        #[weak]
        source,
        #[weak]
        kind_row,
        move |_| {
            glib::spawn_future_local(async move {
                let path = if kind_row.selected() == 0 {
                    files::select_folder(&window, "Choose Folder", None).await
                } else {
                    files::open_file(&window, "Choose Image", None).await
                };
                if let Some(path) = path {
                    source.set_text(&path.to_string_lossy());
                }
            });
        }
    ));
    dialog.accept_button().set_sensitive(!letters.is_empty());

    dialog.connect_submit(glib::clone!(
        #[weak]
        window,
        #[weak]
        letter_row,
        #[weak]
        kind_row,
        #[weak]
        source,
        move |dialog| {
            let Some(snapshot) = window.snapshot() else {
                return;
            };
            let Some(letter) = letters.get(index(letter_row.selected())).cloned() else {
                return;
            };
            let text = source.text().trim().to_owned();
            let kind = SOURCES
                .get(index(kind_row.selected()))
                .copied()
                .unwrap_or(DriveKind::Folder);
            if kind == DriveKind::Folder && text.is_empty() {
                dialog.show_error("Choose a host folder.");
                return;
            }
            let inverse = drive_inverse(&snapshot, &letter);
            let existing = snapshot.drive(&letter).cloned();
            let previous = snapshot.drive_records.get(&letter).cloned();
            let replacing = existing
                .as_ref()
                .is_some_and(|drive| drive.kind() != kind || kind == DriveKind::Folder);
            let base = window.message("command", json!({}));
            let read = window.message("snapshot", json!({}));
            let job_letter = letter.clone();
            let job = move || -> Result<Snapshot, Error> {
                let letter = job_letter.as_str();
                let value = if kind == DriveKind::Folder {
                    command(&base, &["map", letter, &text])?
                } else {
                    let current = if replacing {
                        command(
                            &base,
                            &[
                                "restore-drive",
                                letter,
                                &json!({"device": kind.as_str()}).to_string(),
                            ],
                        )?
                    } else if existing.is_none() {
                        command(&base, &["device", letter, kind.as_str()])?
                    } else if text.is_empty()
                        && existing
                            .as_ref()
                            .and_then(Drive::device)
                            .is_some_and(|device| device.image.is_some())
                    {
                        return Ok(serde_json::from_value(command(&base, &["eject", letter])?)?);
                    } else {
                        engine::request(&read)?
                    };
                    if text.is_empty() {
                        current
                    } else {
                        match command(&base, &["map-image", letter, &text]) {
                            Ok(value) => value,
                            Err(error) => {
                                // Leave the letter as it was before this dialog's change.
                                let _ = match &previous {
                                    Some(record) => {
                                        command(&base, &["restore-drive", letter, record])
                                    }
                                    None => command(&base, &["unmap", letter]),
                                };
                                return Err(error);
                            }
                        }
                    }
                };
                Ok(serde_json::from_value(value)?)
            };
            let dialog = dialog.clone();
            glib::spawn_future_local(async move {
                let options = Work {
                    allow_running: true,
                    ..Work::default()
                };
                if let Some(snapshot) = dialog.run(&window, options, job).await {
                    finish(&window, &dialog, &letter, inverse, snapshot, "Change Drive");
                }
            });
        }
    ));
    dialog.present(Some(window));
}

/// Create Floppy Image or Create Disk Image, optionally for an existing device letter.
pub fn create_image_dialog(window: &BallWindow, floppy: bool, letter: Option<&str>) {
    let Some(snapshot) = window.snapshot() else {
        return;
    };
    let title = format!(
        "{}{}",
        if floppy {
            "Create Floppy Image"
        } else {
            "Create Disk Image"
        },
        letter
            .map(|letter| format!(" for {letter}:"))
            .unwrap_or_default()
    );
    let dialog = FormDialog::new(window, &title, "Create").with_size(620, 480);
    let page = preferences_page(&dialog);
    let group = form::group(
        "New Image",
        Some(
            "The image is formatted and attached to the drive. Use Device Settings to make it read-only or lock it \
             in place. Existing files are never replaced.",
        ),
    );
    page.add(&group);
    let letters = image_letters(&snapshot, floppy, letter);
    let labels: Vec<String> = letters
        .iter()
        .map(|choice| replacement_label(&snapshot, choice))
        .collect();
    let letter_row = adw::ComboRow::builder()
        .title("Drive Letter")
        .model(&string_list(&labels))
        .build();
    letter_row.set_sensitive(letter.is_none());
    letter_row.set_selected(first_free(
        &letters,
        &snapshot,
        if floppy {
            DRIVE_LETTERS
        } else {
            PREFERRED_LETTERS
        },
    ));
    group.add(&letter_row);

    let path = adw::EntryRow::builder().title("New Image File").build();
    let save = form::button("Choose Image Destination", Some("document-save-symbolic"));
    save.add_css_class("flat");
    save.connect_clicked(glib::clone!(
        #[weak]
        window,
        #[weak]
        path,
        move |_| {
            glib::spawn_future_local(async move {
                let initial = if floppy { "Floppy.img" } else { "Disk.img" };
                if let Some(chosen) = files::save_file(&window, "Create Image", initial).await {
                    path.set_text(&chosen.to_string_lossy());
                }
            });
        }
    ));
    path.add_suffix(&save);
    group.add(&path);

    let capacity = adw::ComboRow::builder().title("Capacity").build();
    let filesystem = adw::ComboRow::builder()
        .title("Filesystem")
        .model(&gtk::StringList::new(
            &DISK_FILESYSTEMS.map(|(_, title)| title),
        ))
        .build();
    let capacities = std::rc::Rc::new(std::cell::RefCell::new(Vec::<u32>::new()));
    if floppy {
        capacities.replace(FLOPPY_CAPACITIES.to_vec());
        let labels: Vec<String> = FLOPPY_CAPACITIES
            .iter()
            .map(|kib| format!("{kib} KiB"))
            .collect();
        capacity.set_model(Some(&string_list(&labels)));
        capacity.set_selected(position(
            FLOPPY_CAPACITIES
                .iter()
                .position(|&kib| kib == 1440)
                .unwrap_or(0),
        ));
        group.add(&capacity);
    } else {
        let filesystem_changed = glib::clone!(
            #[weak]
            capacity,
            #[weak]
            filesystem,
            #[strong]
            capacities,
            move |preferred: Option<u32>| {
                let preferred = preferred
                    .or_else(|| capacities.borrow().get(index(capacity.selected())).copied());
                let (key, _) =
                    DISK_FILESYSTEMS[index(filesystem.selected()).min(DISK_FILESYSTEMS.len() - 1)];
                let sizes = disk_capacities(key);
                let labels: Vec<String> = sizes.iter().map(|&mib| capacity_label(mib)).collect();
                capacity.set_model(Some(&string_list(&labels)));
                let selected = preferred
                    .and_then(|preferred| sizes.iter().position(|&mib| mib == preferred))
                    .or_else(|| sizes.iter().position(|&mib| mib == 64))
                    .unwrap_or(0);
                capacity.set_selected(position(selected));
                capacities.replace(sizes);
            }
        );
        filesystem_changed(Some(512));
        filesystem.connect_selected_notify(move |_| filesystem_changed(None));
        group.add(&filesystem);
        group.add(&capacity);
    }
    dialog.accept_button().set_sensitive(!letters.is_empty());

    dialog.connect_submit(glib::clone!(
        #[weak]
        window,
        #[weak]
        letter_row,
        #[weak]
        path,
        #[weak]
        capacity,
        #[weak]
        filesystem,
        move |dialog| {
            let destination = path.text().trim().to_owned();
            if destination.is_empty() {
                dialog.show_error("Choose a destination for the new image.");
                return;
            }
            let Some(snapshot) = window.snapshot() else {
                return;
            };
            let Some(letter) = letters.get(index(letter_row.selected())).cloned() else {
                return;
            };
            let Some(size) = capacities
                .borrow()
                .get(index(capacity.selected()))
                .map(ToString::to_string)
            else {
                return;
            };
            let arguments: Vec<String> = if floppy {
                vec!["create-floppy".into(), letter.clone(), destination, size]
            } else {
                let (key, _) =
                    DISK_FILESYSTEMS[index(filesystem.selected()).min(DISK_FILESYSTEMS.len() - 1)];
                vec![
                    "create-disk".into(),
                    letter.clone(),
                    destination,
                    key.into(),
                    size,
                ]
            };
            let inverse = drive_inverse(&snapshot, &letter);
            let message = window.message("command", json!({"arguments": arguments}));
            let dialog = dialog.clone();
            glib::spawn_future_local(async move {
                let options = Work {
                    allow_running: true,
                    ..Work::default()
                };
                let job = move || engine::request_as::<Snapshot>(&message);
                if let Some(snapshot) = dialog.run(&window, options, job).await {
                    finish(
                        &window,
                        &dialog,
                        &letter,
                        inverse,
                        snapshot,
                        "Create Image Drive",
                    );
                }
            });
        }
    ));
    dialog.present(Some(window));
}

/// Device Settings: read-only media and device lock for a removable drive.
pub fn device_settings_dialog(window: &BallWindow, letter: &str) {
    let Some(snapshot) = window.snapshot() else {
        return;
    };
    let Some(device) = snapshot.drive(letter).and_then(Drive::device).cloned() else {
        return;
    };
    let dialog =
        FormDialog::new(window, &format!("{letter}: Device Settings"), "Save").with_size(620, 400);
    let page = preferences_page(&dialog);
    let group = form::group(
        "Media Protection",
        Some("Writes update the attached image. Keep a separate backup of original media."),
    );
    page.add(&group);
    let cdrom = device.device == DriveKind::Cdrom;
    let readonly = adw::SwitchRow::builder()
        .title("Read Only")
        .subtitle("Prevent changes to image contents")
        .active(cdrom || device.readonly)
        .sensitive(!cdrom)
        .build();
    group.add(&readonly);
    let locked = adw::SwitchRow::builder()
        .title("Lock Device")
        .subtitle("Prevent ejection, replacement and protection changes")
        .active(device.locked)
        .sensitive(device.image.is_some() || device.locked)
        .build();
    group.add(&locked);

    let letter = letter.to_owned();
    dialog.connect_submit(glib::clone!(
        #[weak]
        window,
        #[weak]
        readonly,
        #[weak]
        locked,
        move |dialog| {
            let Some(snapshot) = window.snapshot() else {
                return;
            };
            let (readonly, locked) = (readonly.is_active(), locked.is_active());
            let inverse = drive_inverse(&snapshot, &letter);
            let base = window.message("command", json!({}));
            let read = window.message("snapshot", json!({}));
            let job_letter = letter.clone();
            let job = move || -> Result<Snapshot, Error> {
                let letter = job_letter.as_str();
                let mut current: Snapshot = engine::request_as(&read)?;
                let Some(old) = current.drive(letter).and_then(Drive::device).cloned() else {
                    return Err(Error::message(format!(
                        "{letter}: is no longer a removable device."
                    )));
                };
                let switch = |value: bool| if value { "on" } else { "off" };
                if old.locked && !locked {
                    current =
                        serde_json::from_value(command(&base, &["device-lock", letter, "off"])?)?;
                }
                if old.device != DriveKind::Cdrom && old.readonly != readonly {
                    current = serde_json::from_value(command(
                        &base,
                        &["media-readonly", letter, switch(readonly)],
                    )?)?;
                }
                if old.locked != locked {
                    current = serde_json::from_value(command(
                        &base,
                        &["device-lock", letter, switch(locked)],
                    )?)?;
                }
                Ok(current)
            };
            let dialog = dialog.clone();
            let letter = letter.clone();
            glib::spawn_future_local(async move {
                let options = Work {
                    allow_running: true,
                    ..Work::default()
                };
                if let Some(snapshot) = dialog.run(&window, options, job).await {
                    finish(
                        &window,
                        &dialog,
                        &letter,
                        inverse,
                        snapshot,
                        "Change Device Protection",
                    );
                }
            });
        }
    ));
    dialog.present(Some(window));
}
