// Public domain. No rights reserved. Wholly AI-authored.
//! Application launchers and the program editor.

use adw::prelude::*;
use adw::subclass::prelude::*;
use serde_json::json;

use super::BallWindow;
use crate::pages::applications::{ApplicationsPage, editor};

/// What the program editor creates.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProgramAction {
    /// Save or edit an application launcher.
    Save,
    /// Run a program once.
    Run,
    /// Run an installer.
    Install,
}

impl ProgramAction {
    /// The engine command that starts the program, for `Run` and `Install`.
    pub fn command(self) -> &'static str {
        match self {
            Self::Save => "add-app",
            Self::Run => "run",
            Self::Install => "install",
        }
    }
}

impl BallWindow {
    /// Open the program editor; `name` edits an existing launcher.
    pub fn program_dialog(&self, action: ProgramAction, name: Option<&str>) {
        self.program_dialog_for(action, name, "");
    }

    /// Open the program editor with `program` filled in for a new launcher, run or install.
    ///
    /// Nothing opens while an operation or component job is active, while the
    /// ball's applications run (when editing launchers), or while another
    /// launch or installer prevents starting a program.
    pub fn program_dialog_for(&self, action: ProgramAction, name: Option<&str>, program: &str) {
        let Some(ball) = self.ball() else { return };
        if self.snapshot().is_none() || self.operation_busy() || self.component_active(&ball) {
            return;
        }
        let sessions = self.sessions().borrow();
        let blocked = match action {
            ProgramAction::Save => !sessions.active(Some(&ball)).is_empty(),
            ProgramAction::Run => !sessions.can_start(&ball, false),
            ProgramAction::Install => !sessions.can_start(&ball, true),
        };
        drop(sessions);
        if !blocked {
            editor::open(self, action, name, program);
        }
    }

    /// The Applications section for the selected ball.
    fn applications_page(&self) -> Option<ApplicationsPage> {
        self.page("apps").and_then(|page| page.downcast().ok())
    }

    fn selected_application(&self) -> Option<String> {
        self.applications_page()?
            .selected_name()
            .filter(|_| !self.operation_busy())
    }

    /// Launch, edit or remove the launcher selected in Applications.
    pub fn launch_selected_application(&self) {
        if let Some(name) = self.selected_application() {
            self.launch(vec!["launch".into(), name], json!({}));
        }
    }

    pub fn edit_selected_application(&self) {
        if let Some(name) = self.selected_application() {
            self.program_dialog(ProgramAction::Save, Some(&name));
        }
    }

    pub fn remove_selected_application(&self) {
        if let Some(page) = self.applications_page() {
            page.remove_selected();
        }
    }

    /// Show the newest active session of a launcher in Logs.
    pub fn show_application_sessions(&self, name: &str) {
        let Some(ball) = self.ball() else { return };
        let session = self
            .sessions()
            .borrow()
            .active(Some(&ball))
            .into_iter()
            .rfind(|session| session.name == name);
        if let Some(session) = session {
            self.imp()
                .selected_session
                .replace(Some(session.id.clone()));
            self.emit_sessions_changed();
            self.show_page("activity");
        }
    }
}
