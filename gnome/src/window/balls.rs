// Public domain. No rights reserved. Wholly AI-authored.
//! The ball sidebar: discovery, selection, and per-ball row menus.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gdk, gio, glib};
use serde_json::{Value, json};

use super::ball_row::BallRow;
use super::{BallWindow, Work};
use crate::config::{self, APP_ID};
use crate::engine::{self, Error};
use crate::model::{Snapshot, ball_name, display_path};
use crate::navigation;
use crate::preferences::Preferences;
use crate::sessions::State;

impl BallWindow {
    pub(super) fn setup_balls(&self) {
        self.imp().ball_list.connect_row_selected(glib::clone!(
            #[weak(rename_to = window)]
            self,
            move |_, row| {
                if let Some(path) = row.and_then(row_path)
                    && window.ball().as_deref() != Some(path.as_path())
                {
                    window.select_ball(&path, None);
                }
            }
        ));
        self.imp()
            .pending_banner
            .connect_button_clicked(glib::clone!(
                #[weak(rename_to = window)]
                self,
                move |_| window.pending_banner_clicked()
            ));
        self.imp()
            .banner
            .connect_button_clicked(|banner| banner.set_revealed(false));
        self.connect_sessions_changed(|window| {
            window.update_ball_rows();
            window.update_busy();
        });
        let drop = gtk::DropTarget::new(gdk::FileList::static_type(), gdk::DragAction::COPY);
        drop.connect_drop(glib::clone!(
            #[weak(rename_to = window)]
            self,
            #[upgrade_or]
            false,
            move |_, value, _, _| {
                let Ok(files) = value.get::<gdk::FileList>() else {
                    return false;
                };
                for path in files.files().iter().filter_map(gio::File::path) {
                    window.remember(path);
                }
                true
            }
        ));
        self.add_controller(drop);
    }

    pub(super) fn startup(&self, initial: Option<PathBuf>) {
        let window = self.clone();
        glib::spawn_future_local(async move {
            let path = config::preferences_path();
            let (preferences, discovered) = window
                .run(Work::default(), move || {
                    (Preferences::load(&path), discover())
                })
                .await;
            // Balls opened before startup finished (for example from the command
            // line) are kept and take precedence over the remembered selection.
            let opened = window.imp().external.borrow().clone();
            let target = initial
                .map(|path| path.canonicalize().unwrap_or(path))
                .or_else(|| opened.last().cloned())
                .or_else(|| preferences.selected.clone());
            let mut external = preferences.ball_paths.clone();
            for path in opened.into_iter().chain(target.clone()) {
                if !external.contains(&path) && !discovered.contains(&path) {
                    external.push(path);
                }
            }
            window.imp().external.replace(external);
            window.imp().started.set(true);
            window.imp().preferences.replace(preferences);
            window.refresh(target);
        });
    }

    /// Re-read the ball list and select `target` or the current ball.
    pub fn refresh(&self, target: Option<PathBuf>) {
        let target = target.or_else(|| self.ball());
        let external = self.imp().external.borrow().clone();
        let window = self.clone();
        glib::spawn_future_local(async move {
            let (paths, missing) = window
                .run(Work::default(), move || {
                    let mut paths: BTreeSet<PathBuf> = discover().into_iter().collect();
                    paths.extend(external);
                    let missing = paths
                        .iter()
                        .filter(|path| !path.exists())
                        .cloned()
                        .collect();
                    (paths, missing)
                })
                .await;
            window.imp().missing.replace(missing);
            window.populate(&paths.into_iter().collect::<Vec<_>>(), target.as_deref());
        });
    }

    fn populate(&self, paths: &[PathBuf], target: Option<&Path>) {
        let list = &self.imp().ball_list;
        list.remove_all();
        let mut chosen = None;
        for path in paths {
            let row = self.ball_row(path);
            list.append(&row);
            if Some(path.as_path()) == target {
                chosen = Some(row);
            }
        }
        if paths.is_empty() {
            self.imp().ball.replace(None);
            self.imp().snapshot.replace(None);
            self.detach_sections();
            self.imp().content.set_visible_child_name("empty");
            self.update_busy();
            return;
        }
        let row = chosen
            .map(Cast::upcast::<gtk::ListBoxRow>)
            .or_else(|| list.row_at_index(0));
        // Re-selecting the current ball after a refresh keeps its sections in place.
        if let Some(row) = row {
            if row_path(&row).as_deref() == self.ball().as_deref() {
                list.select_row(Some(&row));
                if let Some(ball) = self.ball() {
                    self.select_ball(&ball, None);
                }
            } else {
                list.select_row(Some(&row));
            }
        }
    }

    fn ball_row(&self, path: &Path) -> BallRow {
        let row = BallRow::new(path.to_path_buf());
        row.set_title(&ball_name(path));
        row.set_tooltip_text(Some(&display_path(path)));
        row.add_prefix(&gtk::Image::from_icon_name(&format!("{APP_ID}-symbolic")));

        let drag = gtk::DragSource::new();
        drag.set_actions(gdk::DragAction::COPY);
        let file = gio::File::for_path(path);
        drag.connect_prepare(move |_, _, _| {
            Some(gdk::ContentProvider::for_value(
                &gdk::FileList::from_array(std::slice::from_ref(&file)).to_value(),
            ))
        });
        row.add_controller(drag);

        let menu = self.ball_menu(path);
        let more = gtk::MenuButton::builder()
            .icon_name("view-more-symbolic")
            .menu_model(&menu)
            .tooltip_text("Ball Actions")
            .valign(gtk::Align::Center)
            .css_classes(["flat"])
            .build();
        more.update_property(&[gtk::accessible::Property::Label(&format!(
            "Actions for {}",
            ball_name(path)
        ))]);
        row.add_suffix(&more);
        let click = gtk::GestureClick::builder()
            .button(gdk::BUTTON_SECONDARY)
            .build();
        click.connect_pressed(glib::clone!(
            #[weak]
            row,
            #[weak(rename_to = window)]
            self,
            move |_, _, x, y| {
                if window.busy() {
                    return;
                }
                let popover = gtk::PopoverMenu::from_model(Some(&menu));
                popover.set_parent(&row);
                #[expect(
                    clippy::cast_possible_truncation,
                    reason = "pointer coordinates fit in i32"
                )]
                let point = gdk::Rectangle::new(x as i32, y as i32, 1, 1);
                popover.set_pointing_to(Some(&point));
                popover.connect_closed(WidgetExt::unparent);
                popover.popup();
            }
        ));
        row.add_controller(click);
        self.update_ball_row(&row, path);
        row
    }

    fn ball_menu(&self, path: &Path) -> gio::Menu {
        let menu = gio::Menu::new();
        let target = path.to_string_lossy().to_variant();
        let section = |entries: &[(&str, &str)]| {
            let section = gio::Menu::new();
            for (label, action) in entries {
                let item = gio::MenuItem::new(Some(label), None);
                item.set_action_and_target_value(Some(&format!("win.{action}")), Some(&target));
                section.append_item(&item);
            }
            section
        };
        if self.imp().missing.borrow().contains(path) {
            menu.append_section(
                None,
                &section(&[("Remove Missing Ball from List", "forget-ball-at")]),
            );
        } else {
            menu.append_section(
                None,
                &section(&[
                    ("Open Folder", "reveal-ball-at"),
                    ("Copy Path", "copy-ball-at"),
                ]),
            );
            menu.append_section(
                None,
                &section(&[
                    ("Rename Ball…", "rename-ball-at"),
                    ("Move Ball to Trash…", "trash-ball-at"),
                ]),
            );
        }
        menu
    }

    /// Show a ball's availability or running applications beneath its name.
    fn update_ball_row(&self, row: &BallRow, path: &Path) {
        let running = self
            .sessions()
            .borrow()
            .active(Some(path))
            .iter()
            .filter(|session| session.state.get() == State::Running)
            .count();
        let subtitle = if self.imp().missing.borrow().contains(path) {
            "Not Found".to_owned()
        } else if running > 0 {
            format!("{running} Running")
        } else {
            String::new()
        };
        row.set_subtitle(&subtitle);
        row.set_component_active(self.component_active(path));
    }

    pub fn update_ball_rows(&self) {
        let mut child = self.imp().ball_list.first_child();
        while let Some(widget) = child {
            if let Some(row) = widget.downcast_ref::<BallRow>() {
                self.update_ball_row(row, &row.path());
            }
            child = widget.next_sibling();
        }
    }

    /// Load and show a ball, optionally using an already-read snapshot.
    pub fn select_ball(&self, path: &Path, snapshot: Option<Snapshot>) {
        let path = path.to_path_buf();
        let previous = self.ball();
        let newly_created = self.imp().new_balls.borrow().contains(&path);
        let landing = newly_created || previous.as_deref() != Some(path.as_path());
        let window = self.clone();
        glib::spawn_future_local(async move {
            let ball = path.clone();
            let loaded = window
                .run(
                    Work::default(),
                    move || -> Result<(Snapshot, Option<Value>), Error> {
                        let snapshot = match snapshot {
                            Some(snapshot) => snapshot,
                            None => {
                                engine::request_as(&json!({"action": "snapshot", "ball": ball}))?
                            }
                        };
                        let navigation = (landing && !newly_created)
                            .then(|| {
                                engine::request(
                                    &json!({"action": "ball-navigation-state", "ball": ball}),
                                )
                                .ok()
                            })
                            .flatten();
                        Ok((snapshot, navigation))
                    },
                )
                .await;
            match loaded {
                Ok((snapshot, navigation)) => {
                    if landing {
                        let section =
                            navigation::initial_section(navigation.as_ref(), newly_created);
                        window.imp().next_section.replace(Some(section.to_owned()));
                        window.imp().new_balls.borrow_mut().remove(&path);
                    }
                    let changed = previous.as_deref() != Some(path.as_path());
                    window.imp().ball.replace(Some(path.clone()));
                    window
                        .imp()
                        .build
                        .replace(window.effective_runtime(&snapshot).await);
                    if changed {
                        window
                            .imp()
                            .snapshot
                            .replace(Some(std::rc::Rc::new(snapshot)));
                        window.build_sections();
                    } else {
                        window.apply_snapshot(snapshot);
                    }
                    if let Some(section) = window.imp().next_section.take() {
                        window.show_page(&section);
                    }
                    window.persist_preferences();
                    window.update_busy();
                    window.imp().split.set_show_content(true);
                }
                Err(error) => window.show_unavailable(&path, &error),
            }
        });
    }

    /// The receiving runtime for a ball's saved runtime selection.
    async fn effective_runtime(&self, snapshot: &Snapshot) -> PathBuf {
        let default = self.default_build();
        let request = json!({
            "action": "effective-runtime",
            "build": default,
            "path": snapshot.manifest.runtime.build,
        });
        engine::spawn(move || engine::request(&request))
            .await
            .ok()
            .and_then(|value| value.as_str().map(PathBuf::from))
            .unwrap_or(default)
    }

    fn show_unavailable(&self, path: &Path, error: &Error) {
        let imp = self.imp();
        imp.ball.replace(Some(path.to_path_buf()));
        imp.snapshot.replace(None);
        self.detach_sections();
        let missing = imp.missing.borrow().contains(path);
        imp.detail.set_title(&ball_name(path));
        imp.unavailable_page.set_title(if missing {
            "Ball Not Found"
        } else {
            "Could Not Open Ball"
        });
        imp.unavailable_page
            .set_description(Some(&glib::markup_escape_text(&error.to_string())));
        imp.forget_button.set_visible(missing);
        imp.content.set_visible_child_name("unavailable");
        imp.split.set_show_content(true);
        self.update_busy();
    }

    /// Open a ball package (or import a ZIP) and add it to the sidebar.
    pub fn remember(&self, path: PathBuf) {
        let path = path.canonicalize().unwrap_or(path);
        if path
            .extension()
            .is_some_and(|extension| extension.eq_ignore_ascii_case("zip"))
        {
            self.import_ball(Some(path));
            return;
        }
        if !self.imp().external.borrow().contains(&path) {
            self.imp().external.borrow_mut().push(path.clone());
        }
        // Before startup completes, startup selects the most recently opened ball.
        if self.imp().started.get() {
            self.refresh(Some(path));
        }
    }

    /// Forget a ball in the sidebar list without changing its files.
    pub fn forget_ball(&self, path: &Path) {
        self.imp()
            .external
            .borrow_mut()
            .retain(|known| known != path);
        if self.ball().as_deref() == Some(path) {
            self.imp().ball.replace(None);
            self.imp().snapshot.replace(None);
        }
        self.persist_preferences();
        self.refresh(None);
    }
}

/// Balls in the default data directory.
fn discover() -> Vec<PathBuf> {
    engine::request_as::<Vec<PathBuf>>(&json!({"action": "discover"})).unwrap_or_default()
}

fn row_path(row: &gtk::ListBoxRow) -> Option<PathBuf> {
    row.downcast_ref::<BallRow>().map(BallRow::path)
}
