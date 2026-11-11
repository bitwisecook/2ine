// Public domain. No rights reserved. Wholly AI-authored.
//! Settings operations reachable from window actions and other sections:
//! CONFIG.SYS saving and reloading, runtime selection, sound banks, OpenGL
//! component installation and the unsaved-draft close guard.

use std::cell::RefCell;
use std::collections::HashMap;
use std::path::PathBuf;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::glib;
use serde_json::json;

use super::{BallWindow, Work};
use crate::dialogs::files;
use crate::engine::{self, Error};
use crate::model::Snapshot;
use crate::pages::settings::{self, AudioDialog, SettingsPage};

/// Unsaved OS/2 file drafts: (ball, guest path) → (loaded revision, edited text).
pub type FileDrafts = HashMap<(PathBuf, String), (String, String)>;

impl BallWindow {
    /// Unsaved CONFIG.SYS drafts: ball → (text loaded from disk, edited text).
    pub fn config_drafts(&self) -> &RefCell<HashMap<PathBuf, (String, String)>> {
        &self.imp().config_drafts
    }

    /// Unsaved OS/2 file editor drafts keyed by ball and guest path.
    pub fn file_drafts(&self) -> &RefCell<FileDrafts> {
        &self.imp().file_drafts
    }

    /// Unapplied settings drafts keyed by ball and setting name.
    pub fn setting_drafts(&self) -> &RefCell<HashMap<(PathBuf, String), String>> {
        &self.imp().setting_drafts
    }

    fn settings_page(&self) -> Option<SettingsPage> {
        self.page("settings").and_downcast::<SettingsPage>()
    }

    /// Show Settings at a section: `overview`, `general`, `display`,
    /// `environment`, `printing`, `files` or `registry`.
    pub fn show_settings(&self, section: &str) {
        self.show_page("settings");
        if let Some(page) = self.settings_page() {
            page.set_visible_section(section);
        }
    }

    /// Show and focus the setting a component refers to: `printing`,
    /// `graphics.opengl` or `graphics`.
    pub fn show_component_setting(&self, token: &str) {
        let Some(page) = self.settings_page() else {
            return;
        };
        let Some((section, target)) = page.setting_target(token) else {
            return;
        };
        self.show_settings(section);
        self.set_focus_visible(true);
        glib::idle_add_local_once(move || {
            target.grab_focus();
        });
    }

    /// Save the selected ball's CONFIG.SYS draft, checking for external changes.
    pub fn save_config(&self) {
        let Some(ball) = self.ball() else { return };
        if self.ball_locked(&ball) {
            self.toast(
                "Wait for this ball’s applications or component job before saving CONFIG.SYS.",
            );
            return;
        }
        let Some((base, text)) = self.config_drafts().borrow().get(&ball).cloned() else {
            return;
        };
        let request =
            json!({"action": "save-config-sys", "ball": ball, "expected": base, "text": text});
        let window = self.clone();
        glib::spawn_future_local(async move {
            let options = Work {
                allow_running: true,
                ..Work::default()
            };
            let result = window
                .run(options, move || engine::request_as::<Snapshot>(&request))
                .await;
            match result {
                Ok(snapshot) => {
                    window.config_drafts().borrow_mut().remove(&ball);
                    if window.ball().as_deref() == Some(ball.as_path()) {
                        window.apply_snapshot(snapshot);
                    }
                    window.toast("CONFIG.SYS saved");
                }
                Err(error) => window.report_error(error),
            }
        });
    }

    /// Replace the selected ball's CONFIG.SYS editor with the file on disk,
    /// confirming before an unsaved draft is discarded.
    pub fn reload_config(&self) {
        let Some(ball) = self.ball() else { return };
        let window = self.clone();
        glib::spawn_future_local(async move {
            if window.config_drafts().borrow().contains_key(&ball)
                && !settings::confirm_discard(
                    &window,
                    "Discard CONFIG.SYS changes?",
                    "Your unsaved edits will be replaced with the file on disk.",
                )
                .await
            {
                return;
            }
            let options = Work {
                allow_running: true,
                ..Work::default()
            };
            let path = ball.clone();
            let result = window
                .run(options, move || {
                    engine::request_as::<Snapshot>(&json!({"action": "snapshot", "ball": path}))
                })
                .await;
            match result {
                Ok(snapshot) => {
                    window.config_drafts().borrow_mut().remove(&ball);
                    if window.ball().as_deref() == Some(ball.as_path()) {
                        window.apply_snapshot(snapshot);
                    }
                }
                Err(error) => window.report_error(error),
            }
        });
    }

    /// Choose an external runtime build for the selected ball.
    pub fn runtime_dialog(&self) {
        if self.ball().is_none() {
            return;
        }
        let window = self.clone();
        glib::spawn_future_local(async move {
            if let Some(path) =
                files::select_folder(&window, "Choose 2ine Runtime Build", None).await
            {
                window.set_runtime_build(Some(path));
            }
        });
    }

    /// Select an external runtime build, or with None return to the packaged runtime. Undoable.
    pub fn set_runtime_build(&self, build: Option<PathBuf>) {
        let Some(ball) = self.ball() else { return };
        let (message, label) = match build {
            Some(build) => (
                json!({"action": "runtime-build", "build": build}),
                "Change Runtime",
            ),
            None => (json!({"action": "runtime-build"}), "Reset Runtime"),
        };
        let Some(handle) = self.commit(message, None, label, false) else {
            return;
        };
        let window = self.clone();
        glib::spawn_future_local(async move {
            if let Ok(Some(snapshot)) = handle.await
                && window.ball().as_deref() == Some(ball.as_path())
            {
                window.select_ball(&ball, Some(snapshot));
            }
        });
    }

    /// Sound banks and MIDI channel mapping.
    pub fn audio_dialog(&self) {
        if self.ball().is_some() && self.snapshot().is_some() && !self.busy() {
            AudioDialog::present(self);
        }
    }

    /// Select the original OS/2 OpenGL renderer, then show its component for installation.
    pub fn install_opengl_component(&self) {
        let Some(ball) = self.ball() else { return };
        if self.busy() || self.component_active(&ball) {
            return;
        }
        let original = self
            .snapshot()
            .and_then(|snapshot| snapshot.system_text("opengl"))
            .is_some_and(|provider| provider == "original");
        if original {
            self.show_component_media("opengl", true);
            return;
        }
        let message =
            json!({"action": "command", "arguments": ["config", "system.opengl", "original"]});
        let Some(handle) = self.commit(message, None, "Update Ball", false) else {
            return;
        };
        let window = self.clone();
        glib::spawn_future_local(async move {
            if let Ok(Some(_)) = handle.await {
                window.show_component_media("opengl", true);
            }
        });
    }

    /// Guard closing the window while OS/2 file or CONFIG.SYS drafts are unsaved.
    ///
    /// Returns true when there is nothing to save and closing may proceed.
    /// Otherwise it asks whether to discard OS/2 file drafts, or whether to save
    /// or discard CONFIG.SYS drafts, returns false, and closes the window itself
    /// once the drafts have been saved or discarded.
    pub fn confirm_close_with_drafts(&self) -> bool {
        let files = !self.file_drafts().borrow().is_empty();
        let configs = !self.config_drafts().borrow().is_empty();
        if !files && !configs {
            return true;
        }
        let window = self.clone();
        glib::spawn_future_local(async move {
            if files {
                if settings::confirm_discard(
                    &window,
                    "Discard unsaved OS/2 file changes?",
                    "Return to Config Files to save them before closing.",
                )
                .await
                {
                    window.file_drafts().borrow_mut().clear();
                    window.close();
                }
                return;
            }
            let dialog = adw::AlertDialog::builder()
                .heading("Save CONFIG.SYS changes?")
                .body("A ball has unsaved edits.")
                .default_response("save")
                .close_response("cancel")
                .build();
            dialog.add_response("cancel", "Cancel");
            dialog.add_response("discard", "Discard Changes");
            dialog.add_response("save", "Save Changes");
            dialog.set_response_appearance("save", adw::ResponseAppearance::Suggested);
            dialog.set_response_appearance("discard", adw::ResponseAppearance::Destructive);
            match dialog.choose_future(Some(&window)).await.as_str() {
                "discard" => {
                    window.config_drafts().borrow_mut().clear();
                    window.close();
                }
                "save" => window.save_all_configs().await,
                _ => {}
            }
        });
        false
    }

    /// Save every CONFIG.SYS draft, closing the window when all succeed.
    async fn save_all_configs(&self) {
        let drafts: Vec<(PathBuf, (String, String))> = self
            .config_drafts()
            .borrow()
            .iter()
            .map(|(ball, draft)| (ball.clone(), draft.clone()))
            .collect();
        let (saved, error) = self
            .run(Work::default(), move || {
                let mut saved = Vec::new();
                for (ball, (base, text)) in drafts {
                    let request = json!({"action": "save-config-sys", "ball": ball, "expected": base, "text": text});
                    if let Err(error) = engine::request(&request) {
                        return (saved, Some(error));
                    }
                    saved.push(ball);
                }
                (saved, None::<Error>)
            })
            .await;
        for ball in saved {
            self.config_drafts().borrow_mut().remove(&ball);
        }
        match error {
            Some(error) => self.report_error(error),
            None => self.close(),
        }
    }
}
