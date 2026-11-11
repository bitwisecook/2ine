// Public domain. No rights reserved. Wholly AI-authored.
//! Dependency advice shown above Applications and Install.
//!
//! Lines come from the stored dependency audit. Missing OS/2 component groups
//! link to Components; a stale audit offers a rescan and a current one opens
//! its log from the ball's `logs` folder.

use std::path::{Path, PathBuf};

use gtk::glib;
use gtk::prelude::*;
use serde_json::Value;

use crate::dialogs::form;
use crate::engine;
use crate::window::BallWindow;

/// The audit log to offer, when it is a file inside the ball's `logs` folder.
pub fn audit_log(ball: &Path, audit: &Value) -> Option<PathBuf> {
    let log = audit["logPath"].as_str().filter(|path| !path.is_empty())?;
    let log = Path::new(log).canonicalize().ok()?;
    let logs = ball.join("logs").canonicalize().ok()?;
    (log.starts_with(&logs) && log.is_file()).then_some(log)
}

/// Build the advice box for the selected ball; it is hidden when there is no advice.
pub fn dependency_advice_box(window: &BallWindow) -> gtk::Box {
    let container = gtk::Box::builder()
        .orientation(gtk::Orientation::Vertical)
        .spacing(4)
        .build();
    let audit = window
        .snapshot()
        .and_then(|snapshot| snapshot.dependency_audit.clone())
        .unwrap_or(Value::Null);
    let lines = engine::dependency_advice(&audit);
    container.set_visible(!lines.is_empty());
    let stale = audit["stale"].as_bool() == Some(true);
    for (index, text) in lines.iter().enumerate() {
        let label = gtk::Label::builder()
            .label(text)
            .xalign(0.0)
            .wrap(true)
            .selectable(true)
            .build();
        label.add_css_class("dim-label");
        label.add_css_class("caption");
        container.append(&label);
        if index > 0 || stale {
            continue;
        }
        for group in audit["componentGroups"].as_array().into_iter().flatten() {
            let (Some(id), Some(title)) = (group["id"].as_str(), group["title"].as_str()) else {
                continue;
            };
            let link = gtk::LinkButton::with_label(&format!("2ine-component:{id}"), title);
            link.set_tooltip_text(Some(&format!(
                "Show and highlight {title} in OS/2 Components"
            )));
            link.add_css_class("caption");
            link.set_halign(gtk::Align::Start);
            let id = id.to_owned();
            link.connect_activate_link(glib::clone!(
                #[weak]
                window,
                #[upgrade_or]
                glib::Propagation::Stop,
                move |_| {
                    window.show_component_media(&id, false);
                    glib::Propagation::Stop
                }
            ));
            container.append(&link);
        }
    }
    if lines.is_empty() {
        return container;
    }
    if stale {
        let check = form::button("Rescan Dependencies", None);
        check.set_halign(gtk::Align::Start);
        check.set_sensitive(!window.busy());
        check.connect_clicked(glib::clone!(
            #[weak]
            window,
            move |_| window.audit_dependencies()
        ));
        container.append(&check);
    } else if let Some(ball) = window.ball()
        && let Some(log) = audit_log(&ball, &audit)
    {
        let open = form::button("Open Dependency Audit", None);
        open.set_halign(gtk::Align::Start);
        open.connect_clicked(glib::clone!(
            #[weak]
            window,
            move |_| window.open_path(&log)
        ));
        container.append(&open);
    }
    container
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn audit_logs_must_stay_inside_the_ball() {
        let ball = tempfile::tempdir().unwrap();
        std::fs::create_dir(ball.path().join("logs")).unwrap();
        let inside = ball.path().join("logs/audit.log");
        std::fs::write(&inside, "x").unwrap();
        let outside = ball.path().join("other.log");
        std::fs::write(&outside, "x").unwrap();
        assert!(audit_log(ball.path(), &json!({"logPath": inside})).is_some());
        assert!(audit_log(ball.path(), &json!({"logPath": outside})).is_none());
        assert!(audit_log(ball.path(), &json!({})).is_none());
    }
}
