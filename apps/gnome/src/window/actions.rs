// Public domain. No rights reserved. Wholly AI-authored.
//! Window actions and keyboard accelerators.

use std::path::Path;

use adw::prelude::*;
use gtk::{gio, glib};

use super::BallWindow;
use super::program_ops::ProgramAction;
use crate::config;
use crate::navigation::SECTIONS;

type Callback = fn(&BallWindow);
type TargetCallback = fn(&BallWindow, &str);

const ACTIONS: &[(&str, Callback)] = &[
    ("new", BallWindow::new_ball),
    ("open", |window| window.open_ball()),
    ("rename-ball", |window| {
        window.with_ball(BallWindow::rename_ball);
    }),
    ("trash-ball", |window| {
        window.with_ball(BallWindow::trash_ball);
    }),
    ("forget-ball", |window| {
        window.with_ball(BallWindow::forget_missing_ball);
    }),
    ("import", |window| window.import_ball(None)),
    ("export", |window| window.export_ball(false)),
    ("export-images", |window| window.export_ball(true)),
    ("refresh", |window| window.refresh(None)),
    ("runtime", BallWindow::runtime_dialog),
    ("save", BallWindow::save_config),
    ("undo", BallWindow::undo),
    ("redo", BallWindow::redo),
    ("stop", |window| window.stop_session(false)),
    ("force-stop", |window| window.stop_session(true)),
    ("add-drive", |window| window.map_dialog(None)),
    ("create-disk", |window| {
        window.create_image_dialog(false, None);
    }),
    ("create-floppy", |window| {
        window.create_image_dialog(true, None);
    }),
    ("install", |window| {
        window.show_page("application-installation");
    }),
    ("run", |window| {
        window.program_dialog(ProgramAction::Run, None);
    }),
    ("restore-libraries", BallWindow::audit_dependencies),
    ("about", BallWindow::about),
    ("preferences", BallWindow::preferences_dialog),
    ("shortcuts", BallWindow::shortcuts_dialog),
    ("help", |window| window.open_uri(config::HELP_URL)),
    ("close", |window| window.close()),
    ("find", BallWindow::find),
    ("os2-files", |window| window.show_page("components")),
    ("add-app", |window| {
        window.program_dialog(ProgramAction::Save, None);
    }),
    ("launch-selected", BallWindow::launch_selected_application),
    ("edit-selected", BallWindow::edit_selected_application),
    ("remove-selected", BallWindow::remove_selected_application),
    ("audio", BallWindow::audio_dialog),
    ("import-files", BallWindow::import_files),
    ("copy", |window| {
        if let Some(ball) = window.ball() {
            window.clipboard().set_text(&ball.to_string_lossy());
        }
    }),
    ("reveal", |window| {
        if let Some(ball) = window.ball() {
            window.open_path(&ball);
        }
    }),
];

const TARGET_ACTIONS: &[(&str, TargetCallback)] = &[
    ("rename-ball-at", |window, path| {
        window.rename_ball(Path::new(path));
    }),
    ("trash-ball-at", |window, path| {
        window.trash_ball(Path::new(path));
    }),
    ("forget-ball-at", |window, path| {
        window.forget_missing_ball(Path::new(path));
    }),
    ("reveal-ball-at", |window, path| {
        window.open_path(Path::new(path));
    }),
    ("copy-ball-at", |window, path| {
        window.clipboard().set_text(path);
    }),
    ("open-file", |window, path| {
        window.open_path(Path::new(path));
    }),
    ("launch-application", |window, name| {
        window.launch(vec!["launch".into(), name.into()], serde_json::json!({}));
    }),
    ("edit-application", |window, name| {
        window.program_dialog(ProgramAction::Save, Some(name));
    }),
    ("copy-application", |window, name| {
        if let Some(snapshot) = window.snapshot() {
            let text = crate::pages::applications::order::copy_text(
                &snapshot.manifest.applications,
                [name],
            );
            window.clipboard().set_text(&text);
        }
    }),
    ("remove-application", |window, name| {
        window.execute(vec!["remove-app".into(), name.into()]);
    }),
    ("attach-image-at", |window, letter| {
        window.attach_image(letter);
    }),
    ("eject-at", |window, letter| {
        window.media_command(vec!["eject".into(), letter.into()]);
    }),
    ("readonly-at", |window, value| {
        if let Some((letter, mode)) = value.split_once(':') {
            window.media_command(vec!["media-readonly".into(), letter.into(), mode.into()]);
        }
    }),
    ("create-image-at", |window, letter| {
        let floppy = window
            .snapshot()
            .and_then(|snapshot| snapshot.drive(letter).map(crate::model::Drive::kind))
            == Some(crate::model::DriveKind::Floppy);
        window.create_image_dialog(floppy, Some(letter));
    }),
    ("browse-drive-at", |window, letter| {
        window.browse_drive(letter);
    }),
    ("add-device", |window, kind| window.add_device(kind)),
];

const ACCELERATORS: &[(&str, &str)] = &[
    ("new", "<Control>n"),
    ("open", "<Control>o"),
    ("refresh", "<Control>r"),
    ("save", "<Control>s"),
    ("undo", "<Control>z"),
    ("redo", "<Control><Shift>z"),
    ("stop", "<Control>period"),
    ("install", "<Control>i"),
    ("run", "<Control><Shift>r"),
    ("preferences", "<Control>comma"),
    ("shortcuts", "<Control>question"),
    ("help", "F1"),
    ("find", "<Control>f"),
    ("close", "<Control>w"),
    ("export", "<Control><Alt>e"),
    ("add-drive", "<Control><Shift>d"),
    ("import-files", "<Control><Shift>i"),
    ("add-app", "<Control><Shift>a"),
    ("launch-selected", "<Control>Return"),
    ("edit-selected", "<Control><Shift>e"),
];

impl BallWindow {
    pub(super) fn setup_actions(&self) {
        let entries = ACTIONS.iter().map(|&(name, callback)| {
            gio::ActionEntry::builder(name)
                .activate(move |window: &Self, _, _| callback(window))
                .build()
        });
        let targeted = TARGET_ACTIONS.iter().map(|&(name, callback)| {
            gio::ActionEntry::builder(name)
                .parameter_type(Some(glib::VariantTy::STRING))
                .activate(move |window: &Self, _, value| {
                    if let Some(value) = value.and_then(glib::Variant::str) {
                        callback(window, value);
                    }
                })
                .build()
        });
        let sections = SECTIONS.iter().enumerate().map(|(index, section)| {
            let name = section.name;
            gio::ActionEntry::builder(&format!("page{}", index + 1))
                .activate(move |window: &Self, _, _| window.show_page(name))
                .build()
        });
        self.add_action_entries(entries.chain(targeted).chain(sections));
        if let Some(application) = self.application() {
            for (name, accelerator) in ACCELERATORS {
                application.set_accels_for_action(&format!("win.{name}"), &[accelerator]);
            }
            for index in 1..=SECTIONS.len() {
                application.set_accels_for_action(
                    &format!("win.page{index}"),
                    &[&format!("<Alt>{index}")],
                );
            }
        }
        self.connect_section_updates();
    }

    fn with_ball(&self, callback: fn(&Self, &Path)) {
        if let Some(ball) = self.ball() {
            callback(self, &ball);
        }
    }

    /// Focus the search entry of the visible section or dialog.
    fn find(&self) {
        let root: gtk::Widget = match self.visible_dialog() {
            Some(dialog) => dialog.upcast(),
            None => match self.imp_stack_visible() {
                Some(child) => child,
                None => return,
            },
        };
        if let Some(entry) = first_descendant::<gtk::SearchEntry>(&root) {
            entry.grab_focus();
        }
    }

    fn imp_stack_visible(&self) -> Option<gtk::Widget> {
        let name = self.visible_page()?;
        self.page(&name)
    }
}

/// The first mapped descendant of type `T`, depth first.
pub fn first_descendant<T: IsA<gtk::Widget>>(root: &gtk::Widget) -> Option<T> {
    let mut child = root.first_child();
    while let Some(widget) = child {
        if widget.is_mapped() {
            if let Ok(found) = widget.clone().downcast::<T>() {
                return Some(found);
            }
            if let Some(found) = first_descendant::<T>(&widget) {
                return Some(found);
            }
        }
        child = widget.next_sibling();
    }
    None
}
