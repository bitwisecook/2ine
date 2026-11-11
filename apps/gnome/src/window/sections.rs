// Public domain. No rights reserved. Wholly AI-authored.
//! The selected ball's sections, header media menu and pending-launch banner.

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};

use super::BallWindow;
use crate::media_menu;
use crate::model::{DriveKind, ball_name};
use crate::pages;

impl BallWindow {
    /// Replace the section pages for a newly selected ball.
    pub(super) fn build_sections(&self) {
        let imp = self.imp();
        let stack = &imp.stack;
        let previous = stack.visible_child_name();
        while let Some(child) = stack.first_child() {
            stack.remove(&child);
        }
        self.add_page("drives", &pages::drives::page(self));
        self.add_page("components", &pages::components::page(self));
        self.add_page("application-installation", &pages::installation::page(self));
        self.add_page("apps", &pages::applications::page(self));
        self.add_page("settings", &pages::settings::page(self));
        self.add_page("activity", &pages::logs::page(self));
        if let Some(previous) = previous {
            self.show_page(&previous);
        }
        imp.view_switcher.set_stack(Some(stack));
        imp.switcher_bar.set_stack(Some(stack));
        imp.title_stack.set_visible(true);
        imp.content.set_visible_child_name("ball");
        if let Some(ball) = self.ball() {
            imp.window_title.set_title(&ball_name(&ball));
            imp.detail.set_title(&ball_name(&ball));
        }
        stack.connect_visible_child_name_notify(glib::clone!(
            #[weak(rename_to = window)]
            self,
            move |_| window.update_pending_banner()
        ));
        self.update_media_menu();
        self.update_pending_banner();
        self.emit_by_name::<()>("snapshot-changed", &[]);
    }

    /// Hide section switching while no ball workspace is shown.
    pub(super) fn detach_sections(&self) {
        let imp = self.imp();
        imp.view_switcher.set_stack(None::<&adw::ViewStack>);
        imp.switcher_bar.set_stack(None::<&adw::ViewStack>);
        imp.title_stack.set_visible(false);
        imp.media_menu.set_visible(false);
        imp.pending_banner.set_revealed(false);
        imp.detail.set_title("2ine");
    }

    pub(super) fn connect_section_updates(&self) {
        self.connect_snapshot_changed(|window| {
            window.update_media_menu();
            window.update_pending_banner();
            window.update_busy();
        });
    }

    fn update_media_menu(&self) {
        let Some(snapshot) = self.snapshot() else {
            return;
        };
        let menu = gio::Menu::new();
        for (heading, entries) in media_menu::sections(&snapshot) {
            let section = gio::Menu::new();
            for entry in entries {
                let item = gio::MenuItem::new(Some(&entry.label), None);
                if let Some(action) = entry.action {
                    item.set_action_and_target_value(
                        Some(&format!("win.{action}")),
                        Some(&entry.target.to_variant()),
                    );
                }
                section.append_item(&item);
            }
            menu.append_section(heading.as_deref(), &section);
        }
        let button = &self.imp().media_menu;
        button.set_menu_model(Some(&menu));
        button.set_visible(true);
    }

    /// Add a removable device of `kind` at its next conventional letter.
    pub(super) fn add_device(&self, kind: &str) {
        let Some(snapshot) = self.snapshot() else {
            return;
        };
        let kind = match kind {
            "floppy" => DriveKind::Floppy,
            "disk" => DriveKind::Disk,
            _ => DriveKind::Cdrom,
        };
        if let Some(letter) = media_menu::available_letter(&snapshot, kind) {
            self.media_command(vec![
                "device".into(),
                letter.to_string(),
                kind.as_str().into(),
            ]);
        }
    }

    /// Show the selected ball's pending launch outside the Install section, which
    /// presents the same launch with its full controls.
    pub fn update_pending_banner(&self) {
        let banner = &self.imp().pending_banner;
        let pending = self.pending_launch();
        let on_install = self.visible_page().as_deref() == Some("application-installation");
        let Some(pending) = pending.filter(|_| !on_install) else {
            banner.set_revealed(false);
            return;
        };
        let title = launch_title(&pending.arguments);
        let ready = pending.status["ready"].as_bool() == Some(true);
        let components = pending.status["requiredCategories"]
            .as_array()
            .is_some_and(|categories| !categories.is_empty());
        banner.set_title(&if ready {
            format!("Ready to continue {title}")
        } else if components {
            format!("{title} is waiting for OS/2 components")
        } else {
            format!("Review the configuration for {title}")
        });
        banner.set_button_label(Some(if ready {
            "Continue"
        } else if components {
            "Show Components"
        } else {
            "Review"
        }));
        banner.set_revealed(true);
    }

    pub(super) fn pending_banner_clicked(&self) {
        let Some(pending) = self.pending_launch() else {
            return;
        };
        if pending.status["ready"].as_bool() == Some(true) {
            self.continue_pending_launch();
        } else if let Some(category) = pending.status["requiredCategories"][0].as_str() {
            self.show_component_media(category, true);
        } else {
            self.show_page("application-installation");
        }
    }
}

/// The program name shown for `run`, `install` or `launch` arguments.
pub fn launch_title(arguments: &[String]) -> String {
    let at = if arguments.get(1).is_some_and(|argument| argument == "--cwd") {
        3
    } else {
        1
    };
    let title = arguments
        .get(at)
        .cloned()
        .unwrap_or_else(|| "Application".into());
    if arguments.first().is_some_and(|action| action != "launch") {
        title
            .replace('\\', "/")
            .rsplit('/')
            .next()
            .unwrap_or_default()
            .to_owned()
    } else {
        title
    }
}

#[cfg(test)]
mod tests {
    use super::launch_title;

    #[test]
    fn launch_titles_use_program_names() {
        let arguments =
            |values: &[&str]| values.iter().map(ToString::to_string).collect::<Vec<_>>();
        assert_eq!(
            launch_title(&arguments(&["install", "D:\\SETUP\\INSTALL.EXE"])),
            "INSTALL.EXE"
        );
        assert_eq!(
            launch_title(&arguments(&["run", "--cwd", "C:\\", "C:\\X.EXE", "a"])),
            "X.EXE"
        );
        assert_eq!(
            launch_title(&arguments(&["launch", "Word Pro"])),
            "Word Pro"
        );
    }
}
