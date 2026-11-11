// Public domain. No rights reserved. Wholly AI-authored.
//! The CONFIG.SYS editor shown in Config Files.
//!
//! Edits are kept as a per-ball draft of (text loaded from disk, edited text)
//! in the window, so they survive switching balls. A draft whose loaded text no
//! longer matches the file on disk is a conflict: it is preserved, but cannot be
//! saved until it is reloaded.

use std::cell::{Cell, OnceCell};
use std::path::PathBuf;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, glib};

use crate::window::BallWindow;

const CONFLICT: &str =
    "CONFIG.SYS changed on disk. Your unsaved draft is preserved. Reload before saving.";

/// The draft to keep after an edit: None when `text` equals the loaded base.
pub fn draft_after_edit(
    previous: Option<&(String, String)>,
    on_disk: &str,
    text: &str,
) -> Option<(String, String)> {
    let base = previous.map_or(on_disk, |(base, _)| base.as_str());
    (text != base).then(|| (base.to_owned(), text.to_owned()))
}

/// Whether a draft was loaded from text that no longer matches the file on disk.
pub fn conflicted(draft: Option<&(String, String)>, on_disk: &str) -> bool {
    draft.is_some_and(|(base, _)| base != on_disk)
}

#[allow(clippy::wildcard_imports)]
mod imp {
    use super::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/settings-config-sys.ui")]
    pub struct ConfigSysEditor {
        #[template_child]
        pub conflict: TemplateChild<adw::Banner>,
        #[template_child]
        pub editor: TemplateChild<gtk::TextView>,
        #[template_child]
        pub save_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub reload_button: TemplateChild<gtk::Button>,
        pub window: glib::WeakRef<BallWindow>,
        pub ball: OnceCell<PathBuf>,
        pub syncing: Cell<bool>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ConfigSysEditor {
        const NAME: &'static str = "TwoineConfigSysEditor";
        type Type = super::ConfigSysEditor;
        type ParentType = gtk::Box;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[gtk::template_callbacks]
    impl ConfigSysEditor {
        #[template_callback]
        fn on_save(&self) {
            if let Some(window) = self.window.upgrade() {
                window.save_config();
            }
        }

        #[template_callback]
        fn on_reload(&self) {
            if let Some(window) = self.window.upgrade() {
                window.reload_config();
            }
        }
    }

    impl ObjectImpl for ConfigSysEditor {}
    impl WidgetImpl for ConfigSysEditor {}
    impl BoxImpl for ConfigSysEditor {}
}

glib::wrapper! {
    pub struct ConfigSysEditor(ObjectSubclass<imp::ConfigSysEditor>)
        @extends gtk::Box, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::Orientable;
}

impl ConfigSysEditor {
    pub fn new(window: &BallWindow) -> Self {
        let editor: Self = glib::Object::new();
        let imp = editor.imp();
        imp.window.set(Some(window));
        let ball = window.ball().unwrap_or_default();
        let text = window
            .config_drafts()
            .borrow()
            .get(&ball)
            .map(|(_, draft)| draft.clone())
            .or_else(|| {
                window
                    .snapshot()
                    .map(|snapshot| snapshot.config_sys.clone())
            })
            .unwrap_or_default();
        imp.ball.set(ball).ok();
        let buffer = imp.editor.buffer();
        buffer.set_enable_undo(true);
        buffer.begin_irreversible_action();
        buffer.set_text(&text);
        buffer.end_irreversible_action();
        buffer.connect_changed(glib::clone!(
            #[weak]
            editor,
            move |_| editor.changed()
        ));
        editor.update_state();
        editor
    }

    fn text(&self) -> String {
        let buffer = self.imp().editor.buffer();
        buffer
            .text(&buffer.start_iter(), &buffer.end_iter(), true)
            .into()
    }

    fn ball(&self) -> PathBuf {
        self.imp().ball.get().cloned().unwrap_or_default()
    }

    fn changed(&self) {
        let Some(window) = self.imp().window.upgrade() else {
            return;
        };
        if self.imp().syncing.get() || window.ball().as_deref() != Some(self.ball().as_path()) {
            return;
        }
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        let ball = self.ball();
        let text = self.text();
        {
            let mut drafts = window.config_drafts().borrow_mut();
            match draft_after_edit(drafts.get(&ball), &snapshot.config_sys, &text) {
                Some(draft) => drafts.insert(ball, draft),
                None => drafts.remove(&ball),
            };
        }
        self.update_state();
    }

    /// Show the file on disk unless a draft exists, then update the conflict state.
    pub fn sync(&self) {
        let Some(window) = self.imp().window.upgrade() else {
            return;
        };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        let ball = self.ball();
        if !window.config_drafts().borrow().contains_key(&ball)
            && self.text() != snapshot.config_sys
        {
            let buffer = self.imp().editor.buffer();
            self.imp().syncing.set(true);
            buffer.begin_irreversible_action();
            buffer.set_text(&snapshot.config_sys);
            buffer.end_irreversible_action();
            self.imp().syncing.set(false);
        }
        self.update_state();
    }

    fn update_state(&self) {
        let imp = self.imp();
        let Some(window) = imp.window.upgrade() else {
            return;
        };
        let on_disk = window
            .snapshot()
            .map(|snapshot| snapshot.config_sys.clone())
            .unwrap_or_default();
        let drafts = window.config_drafts().borrow();
        let draft = drafts.get(&self.ball());
        let conflict = conflicted(draft, &on_disk);
        imp.save_button.set_sensitive(draft.is_some() && !conflict);
        imp.conflict.set_title(if conflict { CONFLICT } else { "" });
        imp.conflict.set_revealed(conflict);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn drafts_keep_their_loaded_base_and_vanish_when_reverted() {
        let first = draft_after_edit(None, "A", "B").unwrap();
        assert_eq!(first, ("A".into(), "B".into()));
        let second = draft_after_edit(Some(&first), "changed", "C").unwrap();
        assert_eq!(second, ("A".into(), "C".into()));
        assert!(draft_after_edit(Some(&second), "changed", "A").is_none());
        assert!(conflicted(Some(&second), "changed"));
        assert!(!conflicted(Some(&second), "A"));
        assert!(!conflicted(None, "A"));
    }
}
