// Public domain. No rights reserved. Wholly AI-authored.
//! The Components section: original OS/2 system components for a ball.
//!
//! Sections are rebuilt whenever another ball is selected, but a ball's
//! component job must keep running and keep its tree, queue and progress.
//! Each ball's [`ComponentsPane`] therefore lives in a per-thread registry
//! keyed by ball path for as long as its window exists; `page` reparents the
//! selected ball's pane into the new section stack.

mod jobs;
mod media_list;
pub mod media_preferences;
mod model;
mod pane;
mod tree;

use std::cell::RefCell;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

use adw::prelude::*;

pub use pane::ComponentsPane;

use crate::window::BallWindow;

thread_local! {
    /// Component panes by ball, created on first use and kept across section rebuilds.
    static PANES: RefCell<HashMap<PathBuf, ComponentsPane>> = RefCell::default();
}

/// The pane for `ball` if one exists in `window`.
pub fn existing(window: &BallWindow, ball: &Path) -> Option<ComponentsPane> {
    PANES.with_borrow_mut(|panes| {
        panes.retain(|_, pane| pane.window().is_some());
        panes
            .get(ball)
            .filter(|pane| pane.window().as_ref() == Some(window))
            .cloned()
    })
}

/// The pane for `ball`, created when the ball is first shown.
pub fn pane(window: &BallWindow, ball: &Path) -> ComponentsPane {
    if let Some(pane) = existing(window, ball) {
        return pane;
    }
    let pane = ComponentsPane::new(window, ball.to_path_buf());
    PANES.with_borrow_mut(|panes| panes.insert(ball.to_path_buf(), pane.clone()));
    pane
}

/// Every pane of a window, for window-wide checks such as closing.
pub fn panes(window: &BallWindow) -> Vec<ComponentsPane> {
    PANES.with_borrow(|panes| {
        panes
            .values()
            .filter(|pane| pane.window().as_ref() == Some(window))
            .cloned()
            .collect()
    })
}

pub fn page(window: &BallWindow) -> gtk::Widget {
    let Some(ball) = window.ball() else {
        return adw::StatusPage::builder()
            .icon_name("applications-system-symbolic")
            .title("No Ball Selected")
            .build()
            .upcast();
    };
    let pane = pane(window, &ball);
    pane.set_build(window.build());
    if let Some(parent) = pane.parent() {
        if let Some(stack) = parent.downcast_ref::<adw::ViewStack>() {
            stack.remove(&pane);
        } else if let Some(stack) = parent.downcast_ref::<gtk::Stack>() {
            stack.remove(&pane);
        } else {
            pane.unparent();
        }
    }
    pane.upcast()
}
