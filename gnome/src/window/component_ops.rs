// Public domain. No rights reserved. Wholly AI-authored.
//! OS/2 component installation entry points.
//!
//! Other sections open the Components section through these methods: to
//! choose or reuse OS/2 media, to tick a component group needed by a launch
//! or a setting, or to show a group without ticking it. Each ball's pane and
//! job state live in `pages::components`, which reports job activity back
//! through [`BallWindow::set_component_active`].

use std::collections::BTreeSet;

use adw::prelude::*;
use gtk::{gio, glib};
use serde_json::Value;

use super::BallWindow;
use crate::pages::components::{self, ComponentsPane, media_preferences};

impl BallWindow {
    /// The selected ball's component pane.
    pub fn component_pane(&self) -> Option<ComponentsPane> {
        self.ball().map(|ball| components::pane(self, &ball))
    }

    /// Show Components focused on a component group (`cmd`, `opengl`, `printers`, …).
    /// `selecting` ticks the group for installation.
    pub fn show_component_media(&self, category: &str, selecting: bool) {
        let Some(ball) = self.ball() else { return };
        if self.operation_busy()
            || (selecting && !self.sessions().borrow().active(Some(&ball)).is_empty())
        {
            return;
        }
        self.show_page("components");
        let pane = components::pane(self, &ball);
        pane.set_focus(Some(category), !selecting, selecting);
        if pane.has_plan() {
            if self.component_active(&ball) {
                if selecting {
                    pane.select_group(category);
                } else {
                    pane.focus_group(category);
                }
            } else {
                pane.resume_components(category, selecting);
            }
            return;
        }
        let sources = media_preferences::preferred(self.snapshot().as_deref(), &self.preferences());
        if sources.is_empty() {
            self.choose_os2_support(&[category], true, selecting);
        } else {
            self.present_os2_media(sources, &[category], selecting);
        }
    }

    /// Open OS/2 media for the selected ball: its remembered media unless
    /// `force_picker`, otherwise media chosen in a file dialog.
    pub fn choose_os2_support(&self, categories: &[&str], force_picker: bool, selecting: bool) {
        let Some(ball) = self.ball() else { return };
        if self.operation_busy() || !self.sessions().borrow().active(Some(&ball)).is_empty() {
            return;
        }
        self.show_page("components");
        let remembered =
            media_preferences::preferred(self.snapshot().as_deref(), &self.preferences());
        if !remembered.is_empty() && !force_picker {
            self.present_os2_media(remembered, categories, selecting);
            return;
        }
        let dialog = self.os2_media_dialog("Choose OS/2 Installation ISO or Floppy Images", false);
        dialog.set_accept_label(Some("Browse Media"));
        let categories: Vec<String> = categories.iter().map(ToString::to_string).collect();
        let window = self.clone();
        glib::spawn_future_local(async move {
            match dialog.open_multiple_future(Some(&window)).await {
                Ok(files) => {
                    let sources: Vec<String> = files
                        .iter::<gio::File>()
                        .filter_map(|file| file.ok()?.path())
                        .map(|path| path.to_string_lossy().into_owned())
                        .collect();
                    if !sources.is_empty() && window.ball().as_deref() == Some(ball.as_path()) {
                        let categories: Vec<&str> = categories.iter().map(String::as_str).collect();
                        window.present_os2_media(sources, &categories, selecting);
                    }
                }
                Err(error) if !error.matches(gtk::DialogError::Dismissed) => {
                    window.report_error(error);
                }
                Err(_) => {}
            }
        });
    }

    /// Show Components and open `sources`. Selecting ticks `categories`
    /// (CMD and REXX support by default); otherwise nothing is ticked and a
    /// single category is only shown.
    pub fn present_os2_media(&self, sources: Vec<String>, categories: &[&str], selecting: bool) {
        let Some(pane) = self.component_pane() else {
            return;
        };
        self.show_page("components");
        let ticked: BTreeSet<String> = if !selecting {
            BTreeSet::new()
        } else if categories.is_empty() {
            BTreeSet::from(["cmd".to_owned()])
        } else {
            categories.iter().map(ToString::to_string).collect()
        };
        let focus = match categories {
            [category] => Some(*category),
            _ => None,
        };
        pane.set_categories(ticked);
        pane.set_focus(focus, !selecting, selecting);
        pane.open_sources(sources);
    }

    /// Show a missing component group without ticking anything.
    pub fn focus_missing_component(&self, category: &str) {
        let Some(pane) = self.component_pane() else {
            return;
        };
        pane.set_focus(Some(category), true, false);
        if pane.has_tree() {
            pane.focus_group(category);
        } else {
            pane.set_categories(BTreeSet::new());
        }
        self.show_page("components");
    }

    /// Remember media that validated as a complete OS/2 installation set.
    pub fn remember_os2_media(&self, validation: &Value) {
        let mut preferences = self.preferences().clone();
        if media_preferences::remember(&mut preferences, validation) {
            self.update_preferences(|stored| *stored = preferences);
        }
    }

    /// Forget the remembered OS/2 media.
    pub fn forget_os2_media(&self) {
        let mut preferences = self.preferences().clone();
        if media_preferences::forget(&mut preferences) {
            self.update_preferences(|stored| *stored = preferences);
        }
    }

    /// A file dialog for OS/2 media starting at the last verified physical media.
    pub fn os2_media_dialog(&self, title: &str, folder: bool) -> gtk::FileDialog {
        let dialog = gtk::FileDialog::builder().title(title).modal(true).build();
        if let Some(mut path) = media_preferences::picker_path(&self.preferences()) {
            if folder
                && path.is_file()
                && let Some(parent) = path.parent()
            {
                path = parent.to_path_buf();
            }
            if path.is_dir() {
                dialog.set_initial_folder(Some(&gio::File::for_path(&path)));
            } else {
                dialog.set_initial_file(Some(&gio::File::for_path(&path)));
            }
        }
        dialog
    }

    /// Whether any ball in this window has a running component job or queued changes.
    pub fn any_component_active(&self) -> bool {
        components::panes(self)
            .iter()
            .any(ComponentsPane::component_busy)
    }

    /// Show the component spinner on sidebar rows of balls with component activity.
    pub fn update_component_indicators(&self) {
        self.update_ball_rows();
    }
}
