// Public domain. No rights reserved. Wholly AI-authored.
//! Ball package management, application preferences, shortcuts and About.

use std::path::{Path, PathBuf};

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};
use serde_json::{Value, json};

use super::{BallWindow, Work};
use crate::config::{APP_ID, VERSION};
use crate::dialogs::{FormDialog, files, form};
use crate::engine::{self, Error};
use crate::lifecycle;
use crate::model::{Snapshot, ball_name, display_path};
use crate::navigation::SECTIONS;

impl BallWindow {
    /// Where new balls are created unless the user chooses another folder.
    pub fn default_ball_folder(&self) -> PathBuf {
        self.preferences()
            .ball_folder
            .clone()
            .unwrap_or_else(engine::ball_home)
    }

    pub(super) fn new_ball(&self) {
        let dialog = FormDialog::new(self, "New Ball", "Create").with_size(520, 300);
        let page = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::new();
        let name = adw::EntryRow::builder().title("Ball Name").build();
        let location = adw::ActionRow::builder()
            .title("Location")
            .subtitle(display_path(self.default_ball_folder()))
            .activatable(true)
            .build();
        location.add_suffix(&gtk::Image::from_icon_name("folder-open-symbolic"));
        let folder = std::rc::Rc::new(std::cell::RefCell::new(self.default_ball_folder()));
        location.connect_activated(glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[strong]
            folder,
            move |row| {
                let row = row.clone();
                let folder = folder.clone();
                glib::spawn_future_local(async move {
                    let initial = folder.borrow().clone();
                    if let Some(chosen) =
                        files::select_folder(&window, "Ball Location", Some(&initial)).await
                    {
                        row.set_subtitle(&display_path(&chosen));
                        folder.replace(chosen);
                    }
                });
            }
        ));
        group.add(&name);
        group.add(&location);
        page.add(&group);
        dialog.append(&page);
        name.connect_entry_activated(glib::clone!(
            #[weak]
            dialog,
            move |_| dialog.emit_by_name::<()>("submit", &[])
        ));
        dialog.connect_submit(glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[weak]
            name,
            move |dialog| {
                let path = match lifecycle::new_ball_path(&folder.borrow(), &name.text()) {
                    Ok(path) => path,
                    Err(error) => return dialog.show_error(error),
                };
                let dialog = dialog.clone();
                glib::spawn_future_local(async move {
                    let target = path.clone();
                    let created = dialog
                        .run(&window, Work::default(), move || -> Result<(), Error> {
                            if target.symlink_metadata().is_ok() {
                                return Err(Error::message("A ball with that name already exists."));
                            }
                            engine::request(&json!({"action": "command", "ball": target, "arguments": ["init"]}))?;
                            lifecycle::decorate(&target);
                            Ok(())
                        })
                        .await;
                    if created.is_some() {
                        let path = path.canonicalize().unwrap_or(path);
                        window.imp().new_balls.borrow_mut().insert(path.clone());
                        window.imp().external.borrow_mut().push(path.clone());
                        dialog.close();
                        window.refresh(Some(path));
                    }
                });
            }
        ));
        dialog.present(Some(self));
    }

    pub(super) fn open_ball(&self) {
        let window = self.clone();
        glib::spawn_future_local(async move {
            if let Some(path) = files::select_folder(&window, "Open Ball", None).await {
                window.remember(path);
            }
        });
    }

    fn ball_available_for_management(&self, path: &Path) -> bool {
        !self.operation_busy() && !self.ball_locked(path)
    }

    pub(super) fn rename_ball(&self, path: &Path) {
        if !self.ball_available_for_management(path) {
            return;
        }
        let (source, expected) = match lifecycle::package(path, None)
            .and_then(|source| Ok((lifecycle::identity(&source)?, source)))
        {
            Ok((expected, source)) => (source, expected),
            Err(error) => return self.report_error(error),
        };
        let dialog = FormDialog::new(self, "Rename Ball", "Rename").with_size(480, 220);
        let page = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::new();
        let name = adw::EntryRow::builder()
            .title("Ball Name")
            .text(ball_name(&source))
            .build();
        group.add(&name);
        page.add(&group);
        dialog.append(&page);
        name.connect_entry_activated(glib::clone!(
            #[weak]
            dialog,
            move |_| dialog.emit_by_name::<()>("submit", &[])
        ));
        dialog.connect_submit(glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[weak]
            name,
            move |dialog| {
                if !window.ball_available_for_management(&source) {
                    return;
                }
                let destination = match lifecycle::rename_destination(&source, &name.text()) {
                    Ok(destination) => destination,
                    Err(error) => return dialog.show_error(error),
                };
                if destination == source {
                    dialog.close();
                    return;
                }
                let text = name.text().to_string();
                let source = source.clone();
                let dialog = dialog.clone();
                glib::spawn_future_local(async move {
                    let from = source.clone();
                    let renamed = dialog
                        .run(&window, Work::default(), move || -> Result<Value, Error> {
                            lifecycle::package(&from, Some(expected))?;
                            engine::request(
                                &json!({"action": "rename-ball", "ball": from, "text": text}),
                            )
                        })
                        .await;
                    if let Some(result) = renamed {
                        let target = result["path"].as_str().map_or(destination, PathBuf::from);
                        window.update_references(&source, Some(&target));
                        dialog.close();
                        window.persist_preferences();
                        window.refresh(window.ball());
                        window.toast(format!("Renamed ball to {}", ball_name(&target)));
                    }
                });
            }
        ));
        dialog.present(Some(self));
    }

    pub(super) fn trash_ball(&self, path: &Path) {
        if !self.ball_available_for_management(path) {
            return;
        }
        let (source, expected) = match lifecycle::package(path, None)
            .and_then(|source| Ok((lifecycle::identity(&source)?, source)))
        {
            Ok((expected, source)) => (source, expected),
            Err(error) => return self.report_error(error),
        };
        let name = source
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned();
        let dialog = adw::AlertDialog::new(
            Some(&format!("Move “{name}” to the Trash?")),
            Some(
                "This moves the ball package and everything stored inside it to the Trash. Unsaved drafts for this ball will be discarded.",
            ),
        );
        dialog.add_responses(&[("cancel", "Cancel"), ("trash", "Move to Trash")]);
        dialog.set_response_appearance("trash", adw::ResponseAppearance::Destructive);
        dialog.set_default_response(Some("cancel"));
        dialog.set_close_response("cancel");
        let window = self.clone();
        glib::spawn_future_local(async move {
            if dialog.choose_future(Some(&window)).await == "trash" {
                window.remove_ball_package(source, expected, false);
            }
        });
    }

    fn remove_ball_package(&self, source: PathBuf, expected: lifecycle::Identity, permanent: bool) {
        if !self.ball_available_for_management(&source) {
            return;
        }
        let window = self.clone();
        glib::spawn_future_local(async move {
            let target = source.clone();
            let result = window
                .run(Work::default(), move || -> Result<bool, Error> {
                    let path = lifecycle::package(&target, Some(expected))?;
                    if permanent {
                        std::fs::remove_dir_all(&path)?;
                        return Ok(true);
                    }
                    match gio::File::for_path(&path).trash(gio::Cancellable::NONE) {
                        Ok(()) => Ok(true),
                        Err(error) if error.matches(gio::IOErrorEnum::NotSupported) => Ok(false),
                        Err(error) => Err(Error::message(error.message())),
                    }
                })
                .await;
            match result {
                Ok(true) => {
                    window.update_references(&source, None);
                    window.persist_preferences();
                    window.refresh(window.ball());
                    window.toast(if permanent {
                        "Deleted ball"
                    } else {
                        "Moved ball to Trash"
                    });
                }
                Ok(false) => window.confirm_permanent_delete(source, expected),
                Err(error) => window.report_error(error),
            }
        });
    }

    fn confirm_permanent_delete(&self, source: PathBuf, expected: lifecycle::Identity) {
        let name = source
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned();
        let dialog = adw::AlertDialog::new(
            Some(&format!("Permanently delete “{name}”?")),
            Some(
                "Trash is not supported at this location. Permanently deleting this ball and its contents cannot be undone.",
            ),
        );
        dialog.add_responses(&[("cancel", "Cancel"), ("delete", "Delete Permanently")]);
        dialog.set_response_appearance("delete", adw::ResponseAppearance::Destructive);
        dialog.set_default_response(Some("cancel"));
        dialog.set_close_response("cancel");
        let window = self.clone();
        glib::spawn_future_local(async move {
            if dialog.choose_future(Some(&window)).await == "delete" {
                window.remove_ball_package(source, expected, true);
            }
        });
    }

    /// Remove a ball that no longer exists from the sidebar list.
    pub(super) fn forget_missing_ball(&self, path: &Path) {
        if self.busy() {
            return;
        }
        if path.symlink_metadata().is_ok() {
            self.report_error("This ball is available. Open it or move it to the Trash instead.");
            return;
        }
        self.update_references(path, None);
        self.imp().missing.borrow_mut().remove(path);
        self.persist_preferences();
        self.refresh(None);
        self.toast("Removed the missing ball from the list.");
    }

    /// Move or drop machine-local references after a ball is renamed or removed.
    fn update_references(&self, source: &Path, destination: Option<&Path>) {
        let imp = self.imp();
        let source = source
            .canonicalize()
            .unwrap_or_else(|_| source.to_path_buf());
        let matches =
            |path: &Path| path.canonicalize().unwrap_or_else(|_| path.to_path_buf()) == source;
        {
            let mut external = imp.external.borrow_mut();
            let mut updated: Vec<PathBuf> = external
                .iter()
                .filter_map(|path| {
                    if matches(path) {
                        destination.map(Path::to_path_buf)
                    } else {
                        Some(path.clone())
                    }
                })
                .collect();
            updated.sort();
            updated.dedup();
            *external = updated;
        }
        let rekey = |path: &PathBuf| {
            if matches(path) {
                destination.map(Path::to_path_buf)
            } else {
                Some(path.clone())
            }
        };
        let drafts = std::mem::take(&mut *imp.config_drafts.borrow_mut());
        *imp.config_drafts.borrow_mut() = drafts
            .into_iter()
            .filter_map(|(path, value)| Some((rekey(&path)?, value)))
            .collect();
        let drafts = std::mem::take(&mut *imp.file_drafts.borrow_mut());
        *imp.file_drafts.borrow_mut() = drafts
            .into_iter()
            .filter_map(|((path, key), value)| Some(((rekey(&path)?, key), value)))
            .collect();
        let drafts = std::mem::take(&mut *imp.setting_drafts.borrow_mut());
        *imp.setting_drafts.borrow_mut() = drafts
            .into_iter()
            .filter_map(|((path, key), value)| Some(((rekey(&path)?, key), value)))
            .collect();
        imp.undo.borrow_mut().retain(|item| !matches(&item.ball));
        imp.redo.borrow_mut().retain(|item| !matches(&item.ball));
        let selected = imp.ball.borrow().as_deref().is_some_and(matches);
        if selected {
            imp.ball.replace(destination.map(Path::to_path_buf));
            if destination.is_none() {
                imp.snapshot.replace(None);
            }
        }
    }

    pub(super) fn import_ball(&self, source: Option<PathBuf>) {
        if self.busy() {
            return;
        }
        let window = self.clone();
        glib::spawn_future_local(async move {
            let source = if let Some(source) = source {
                source
            } else {
                let filters = files::filters("ZIP Archives", &["*.zip", "*.ZIP"]);
                let Some(source) =
                    files::open_file(&window, "Import Ball from ZIP", Some(filters.upcast_ref()))
                        .await
                else {
                    return;
                };
                source
            };
            let stem = source
                .file_stem()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned();
            let Some(destination) = files::save_file(
                &window,
                "Import into a New Ball",
                &format!("{stem}.2ineball"),
            )
            .await
            else {
                return;
            };
            let target = destination.clone();
            let result = window
                .run(Work::default(), move || -> Result<Snapshot, Error> {
                    let snapshot = engine::request_as(
                        &json!({"action": "import-ball", "ball": target, "path": source}),
                    )?;
                    lifecycle::decorate(&target);
                    Ok(snapshot)
                })
                .await;
            match result {
                Ok(snapshot) => {
                    if !snapshot.unavailable_drives.is_empty() {
                        window.imp().next_section.replace(Some("drives".into()));
                    }
                    let destination = destination.canonicalize().unwrap_or(destination);
                    if !window.imp().external.borrow().contains(&destination) {
                        window.imp().external.borrow_mut().push(destination.clone());
                    }
                    window.persist_preferences();
                    window.refresh(Some(destination));
                }
                Err(error) => window.report_error(error),
            }
        });
    }

    pub(super) fn export_ball(&self, include_images: bool) {
        let Some(ball) = self.ball() else { return };
        if self.operation_busy() || self.ball_locked(&ball) {
            return;
        }
        let imp = self.imp();
        if imp.config_drafts.borrow().contains_key(&ball)
            || imp
                .file_drafts
                .borrow()
                .keys()
                .any(|(path, _)| *path == ball)
        {
            self.report_error("Save the OS/2 file drafts before exporting this ball.");
            self.show_page("config");
            return;
        }
        let dialog =
            FormDialog::new(self, "Export Ball", "Choose Destination…").with_size(560, 380);
        let page = adw::PreferencesPage::new();
        let group = form::group(
            "Portable ZIP Archive",
            Some(
                "The archive keeps installed applications, settings, profiles and icons. An imported ball uses the receiving computer's runtime.",
            ),
        );
        let images = adw::SwitchRow::builder()
            .title("Include Disk Images")
            .subtitle(
                "Include attached media and image files stored in the ball. Archives may be large.",
            )
            .active(include_images)
            .build();
        group.add(&images);
        group.add(
            &adw::ActionRow::builder()
                .title("External Folders")
                .subtitle("External folder mappings are retained as reconnect hints. Their files are not copied into the archive.")
                .subtitle_lines(0)
                .build(),
        );
        page.add(&group);
        dialog.append(&page);
        let build = self.build();
        dialog.connect_submit(glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[weak]
            images,
            move |dialog| {
                let include = images.is_active();
                let (ball, build, dialog) = (ball.clone(), build.clone(), dialog.clone());
                glib::spawn_future_local(async move {
                    let name = format!("{}.zip", ball_name(&ball));
                    let Some(path) = files::save_file(&window, "Export Ball as ZIP", &name).await
                    else {
                        return;
                    };
                    let exported = dialog
                        .run(&window, Work::default(), move || {
                            engine::request(&json!({
                                "action": "export-ball",
                                "ball": ball,
                                "build": build,
                                "path": path,
                                "includeImages": include,
                                "exporter": {"name": "2ine for GNOME", "version": VERSION},
                            }))
                        })
                        .await;
                    if let Some(result) = exported {
                        window.toast(format!(
                            "Ball exported to {}",
                            result.as_str().unwrap_or_default()
                        ));
                        dialog.close();
                    }
                });
            }
        ));
        dialog.present(Some(self));
    }

    pub(super) fn preferences_dialog(&self) {
        let dialog = adw::PreferencesDialog::builder()
            .title("Preferences")
            .content_width(560)
            .content_height(450)
            .build();
        let page = adw::PreferencesPage::new();
        dialog.add(&page);

        let balls = adw::PreferencesGroup::builder().title("New Balls").build();
        let folder = adw::ActionRow::builder()
            .title("Default Folder")
            .activatable(true)
            .use_markup(false)
            .build();
        folder.add_suffix(&gtk::Image::from_icon_name("folder-open-symbolic"));
        let reset = adw::ActionRow::builder()
            .title("Use Default Folder")
            .activatable(true)
            .build();
        reset.add_suffix(&gtk::Image::from_icon_name("edit-undo-symbolic"));
        balls.add(&folder);
        balls.add(&reset);
        page.add(&balls);
        let update_folder = glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[weak]
            folder,
            #[weak]
            reset,
            move || {
                folder.set_subtitle(&display_path(window.default_ball_folder()));
                reset.set_sensitive(window.preferences().ball_folder.is_some());
            }
        );
        update_folder();
        folder.connect_activated(glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[strong]
            update_folder,
            move |_| {
                let update_folder = update_folder.clone();
                glib::spawn_future_local(async move {
                    let initial = window.default_ball_folder();
                    if let Some(chosen) =
                        files::select_folder(&window, "Default Ball Folder", Some(&initial)).await
                    {
                        window.update_preferences(|preferences| {
                            preferences.ball_folder = Some(chosen.canonicalize().unwrap_or(chosen));
                        });
                        update_folder();
                    }
                });
            }
        ));
        reset.connect_activated(glib::clone!(
            #[weak(rename_to = window)]
            self,
            move |_| {
                window.update_preferences(|preferences| preferences.ball_folder = None);
                update_folder();
            }
        ));

        let logs = form::group(
            "Logs",
            Some(
                "Text size applies to application output. Other text follows your desktop preferences.",
            ),
        );
        let desktop = adw::SwitchRow::builder()
            .title("Use Desktop Text Size")
            .active(!self.preferences().custom_log_size)
            .build();
        let size = adw::SpinRow::with_range(10.0, 24.0, 1.0);
        size.set_title("Log Text Size (pt)");
        size.set_value(f64::from(self.preferences().log_font_size));
        size.set_sensitive(!desktop.is_active());
        logs.add(&desktop);
        logs.add(&size);
        page.add(&logs);
        let changed = glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[weak]
            desktop,
            #[weak]
            size,
            move || {
                size.set_sensitive(!desktop.is_active());
                #[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
                let points = size.value() as u32;
                window.update_preferences(|preferences| {
                    preferences.custom_log_size = !desktop.is_active();
                    preferences.log_font_size = points;
                });
                window.emit_by_name::<()>("preferences-changed", &[]);
            }
        );
        desktop.connect_active_notify(glib::clone!(
            #[strong]
            changed,
            move |_| changed()
        ));
        size.connect_value_notify(move |_| changed());

        let media = form::group(
            "OS/2 Installation Media",
            Some(
                "The last verified media is reused by new balls. Each ball’s remembered source takes priority.",
            ),
        );
        let remembered = adw::ActionRow::builder()
            .title("Last Verified OS/2 Media")
            .use_markup(false)
            .build();
        let forget = adw::ActionRow::builder()
            .title("Forget OS/2 Media")
            .activatable(true)
            .build();
        media.add(&remembered);
        media.add(&forget);
        page.add(&media);
        let update_media = glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[weak]
            remembered,
            #[weak]
            forget,
            move || {
                let sources = window.preferences().last_os2_media_sources.clone();
                remembered.set_subtitle(&if sources.is_empty() {
                    "No verified media remembered".to_owned()
                } else {
                    sources.join("\n")
                });
                forget.set_sensitive(!sources.is_empty());
            }
        );
        update_media();
        forget.connect_activated(glib::clone!(
            #[weak(rename_to = window)]
            self,
            move |_| {
                window.update_preferences(|preferences| {
                    preferences.last_os2_media_sources.clear();
                    preferences.last_os2_media_files.clear();
                });
                update_media();
            }
        ));
        dialog.present(Some(self));
    }

    pub(super) fn shortcuts_dialog(&self) {
        let dialog = adw::ShortcutsDialog::new();
        let navigation: Vec<(String, String)> =
            std::iter::once(("Find in Current View".to_owned(), "find".to_owned()))
                .chain(SECTIONS.iter().enumerate().map(|(index, section)| {
                    (section.title.to_owned(), format!("page{}", index + 1))
                }))
                .chain([
                    ("Preferences".to_owned(), "preferences".to_owned()),
                    ("Keyboard Shortcuts".to_owned(), "shortcuts".to_owned()),
                    ("Close Window".to_owned(), "close".to_owned()),
                ])
                .collect();
        let groups: [(&str, Vec<(String, String)>); 3] = [
            (
                "Balls",
                [
                    ("New Ball", "new"),
                    ("Open Ball", "open"),
                    ("Refresh", "refresh"),
                    ("Save CONFIG.SYS", "save"),
                    ("Undo Ball Change", "undo"),
                    ("Redo Ball Change", "redo"),
                    ("Export Ball as ZIP", "export"),
                    ("Add Drive", "add-drive"),
                    ("Import Files", "import-files"),
                ]
                .map(|(label, action)| (label.to_owned(), action.to_owned()))
                .to_vec(),
            ),
            (
                "Applications",
                [
                    ("Install Software", "install"),
                    ("Run Program", "run"),
                    ("Add Application", "add-app"),
                    ("Launch Selected Application", "launch-selected"),
                    ("Edit Selected Application", "edit-selected"),
                    ("Stop Operation", "stop"),
                ]
                .map(|(label, action)| (label.to_owned(), action.to_owned()))
                .to_vec(),
            ),
            ("Navigation", navigation),
        ];
        for (title, actions) in groups {
            let section = adw::ShortcutsSection::new(Some(title));
            for (label, action) in actions {
                section.add(adw::ShortcutsItem::from_action(
                    &label,
                    &format!("win.{action}"),
                ));
            }
            dialog.add(section);
        }
        dialog.present(Some(self));
    }

    pub(super) fn about(&self) {
        let window = self.clone();
        let build = self.build();
        glib::spawn_future_local(async move {
            let catalogue = engine::spawn(move || {
                engine::request(&json!({"action": "project-sources", "build": build}))
            })
            .await
            .unwrap_or_default();
            let dialog = adw::AboutDialog::builder()
                .application_name("2ine")
                .application_icon(APP_ID)
                .developer_name("Ryan C. Gordon and 2ine Contributors")
                .developers([
                    "Ryan C. Gordon (original 2ine) https://icculus.org/",
                    "2ine Contributors https://github.com/bitwisecook/2ine",
                ])
                .copyright("Original 2ine © 2016–2022 Ryan C. Gordon")
                .version(VERSION)
                .website("https://github.com/bitwisecook/2ine")
                .license_type(gtk::License::Custom)
                .license("Original 2ine: zlib-style license. The Unicorn-linked runtime is distributed under GPLv2-compatible terms. Bundled components retain their own licenses; see Legal for sources and credits.")
                .comments("Created by Ryan C. Gordon (icculus). This fork builds on his original OS/2 compatibility layer.")
                .build();
            if let Some(link) = catalogue["catalogue"].as_str() {
                dialog.add_link("All Project Sources, Versions and Downloads", link);
            }
            dialog.add_link(
                "Original 2ine by Ryan C. Gordon",
                "https://github.com/icculus/2ine",
            );
            dialog.add_credit_section(
                Some("Running Frontend"),
                &[
                    &format!(
                        "GTK {}.{}.{} https://www.gtk.org/",
                        gtk::major_version(),
                        gtk::minor_version(),
                        gtk::micro_version()
                    ),
                    &format!(
                        "libadwaita {}.{}.{} https://gitlab.gnome.org/GNOME/libadwaita",
                        adw::major_version(),
                        adw::minor_version(),
                        adw::micro_version()
                    ),
                    "gtk-rs https://gtk-rs.org/",
                ],
            );
            for item in catalogue["projects"].as_array().into_iter().flatten() {
                let text = |key: &str| item[key].as_str().unwrap_or_default().to_owned();
                let links: Vec<String> = [
                    ("homepage", "Project"),
                    ("source", "Source"),
                    ("download", "Download"),
                ]
                .into_iter()
                .filter(|(key, _)| item[*key].is_string())
                .map(|(key, label)| {
                    format!(
                        "<a href=\"{}\">{label}</a>",
                        glib::markup_escape_text(&text(key))
                    )
                })
                .collect();
                let role = item["role"].as_str().unwrap_or("Component");
                let body = format!(
                    "{}\n{}\n{}",
                    glib::markup_escape_text(role),
                    glib::markup_escape_text(&text("license")),
                    links.join(" · ")
                );
                let authors: Vec<&str> = item["authors"]
                    .as_array()
                    .into_iter()
                    .flatten()
                    .filter_map(Value::as_str)
                    .collect();
                dialog.add_legal_section(
                    &format!("{} — {}", text("name"), text("version")),
                    Some(&authors.join("; ")),
                    gtk::License::Custom,
                    Some(&body),
                );
            }
            dialog.present(Some(&window));
        });
    }
}
