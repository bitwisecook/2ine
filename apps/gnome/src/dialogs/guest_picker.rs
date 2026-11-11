// Public domain. No rights reserved. Wholly AI-authored.
//! Choose an OS/2 program or folder by browsing the ball's mapped drives,
//! including members of attached images.

use std::rc::Rc;

use adw::prelude::*;

use crate::dialogs::FormDialog;
use crate::pages::drives::browser::{GuestBrowser, Options};
use crate::window::BallWindow;

/// What the guest picker selects.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PickerMode {
    /// An OS/2 executable or command file.
    Program,
    /// A folder, for working directories.
    Folder,
}

impl BallWindow {
    /// Browse the ball's mapped drives (including image members) and return the
    /// chosen OS/2 path, or None when cancelled.
    pub async fn pick_guest_path(
        &self,
        title: &str,
        mode: PickerMode,
        initial: Option<String>,
    ) -> Option<String> {
        pick(self, title, mode, initial, None).await
    }
}

/// Show the guest picker, optionally restricted to one drive letter.
///
/// `initial` may name a folder or a file, whose folder is shown. Without it
/// the picker opens the ball's current drive.
pub async fn pick(
    window: &BallWindow,
    title: &str,
    mode: PickerMode,
    initial: Option<String>,
    required_drive: Option<String>,
) -> Option<String> {
    let snapshot = window.snapshot()?;
    let path = initial
        .filter(|path| !path.trim().is_empty())
        .unwrap_or_else(|| {
            let letter = required_drive
                .clone()
                .unwrap_or_else(|| snapshot.current_drive.clone());
            if letter.is_empty() {
                "C:\\".into()
            } else {
                format!("{letter}:\\")
            }
        });
    let dialog = FormDialog::new(window, title, "Choose").with_size(680, 620);
    dialog.accept_button().set_visible(false);
    let (sender, receiver) = async_channel::bounded::<Option<String>>(1);
    let chosen = sender.clone();
    let weak = dialog.downgrade();
    let choose: Rc<dyn Fn(String)> = Rc::new(move |value| {
        let _ = chosen.try_send(Some(value));
        if let Some(dialog) = weak.upgrade() {
            dialog.close();
        }
    });
    let browser = GuestBrowser::new(
        window,
        Options {
            path,
            directories_only: mode == PickerMode::Folder,
            required_drive,
            choose: Some(choose),
            allow_import: false,
            show_drive_picker: true,
        },
    );
    browser.set_margin_start(18);
    browser.set_margin_end(18);
    browser.set_margin_bottom(18);
    dialog.append(&browser);
    dialog.connect_closed(move |_| {
        let _ = sender.try_send(None);
    });
    let parent = window
        .visible_dialog()
        .map_or_else(|| window.clone().upcast::<gtk::Widget>(), Cast::upcast);
    dialog.present(Some(&parent));
    receiver.recv().await.ok().flatten()
}
