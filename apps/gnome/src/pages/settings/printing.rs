// Public domain. No rights reserved. Wholly AI-authored.
//! Printing: the default printer queue stored in the guest profiles.
//!
//! The preference is `PM_SPOOLER`/`QUEUE` in the user profile, falling back to
//! the system profile and then the runtime default (Save as PDF). Reading the
//! page never writes defaults; choosing a printer writes only the user profile.

// List positions are small indexes into GTK models.
#![allow(clippy::cast_possible_truncation)]

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use adw::prelude::*;
use gtk::glib;
use serde_json::json;

use super::profile::{Entry, ProfileSnapshot};
use super::registry_values::{hex_bytes, latin1, to_hex};
use super::{Context, flat_button, write};

/// The stored default printer entry of a profile.
pub fn preference(profile: &ProfileSnapshot) -> Option<&Entry> {
    profile.entries.iter().find(|entry| {
        entry.application.eq_ignore_ascii_case("PM_SPOOLER")
            && entry.key.eq_ignore_ascii_case("QUEUE")
    })
}

/// What the printer row shows for the user and system profiles.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PrinterState {
    /// Queues for each choice; None for a stored queue 2ine does not provide.
    pub choices: Vec<Option<&'static str>>,
    pub labels: Vec<String>,
    pub selected: u32,
    /// Whether the user profile overrides the inherited default.
    pub overridden: bool,
    pub title: &'static str,
    pub subtitle: String,
}

pub fn printer_state(user: &ProfileSnapshot, system: &ProfileSnapshot) -> PrinterState {
    let user = preference(user);
    let entry = user.or_else(|| preference(system));
    let stored = entry.map_or_else(
        || "2INEPDF;".to_owned(),
        |entry| {
            let data = hex_bytes(&entry.data).unwrap_or_default();
            latin1(data.split(|&byte| byte == 0).next().unwrap_or_default())
        },
    );
    let name = stored
        .split(';')
        .next()
        .unwrap_or_default()
        .trim()
        .to_owned();
    let mut choices = vec![Some("2INEPDF"), Some("2INEPCL")];
    let mut labels = vec!["Save as PDF".to_owned(), "PCL 5 Printer".to_owned()];
    let upper = name.to_uppercase();
    let selected = choices
        .iter()
        .position(|choice| *choice == Some(upper.as_str()));
    if selected.is_none() {
        choices.push(None);
        labels.push(format!(
            "Stored Queue: {}",
            if name.is_empty() { "(none)" } else { &name }
        ));
    }
    PrinterState {
        choices,
        labels,
        selected: selected.map_or(2, |index| index as u32),
        overridden: user.is_some(),
        title: if user.is_some() {
            "User Profile"
        } else if entry.is_some() {
            "System Profile"
        } else {
            "Runtime Default"
        },
        subtitle: if entry.is_some() {
            format!("Stored value: {stored}. Reading this page does not write defaults.")
        } else {
            "Save as PDF. No stored preference.".into()
        },
    }
}

/// The stored data for a queue: its name, a separator and a terminating NUL.
pub fn queue_data(queue: &str) -> String {
    let mut data: Vec<u8> = queue
        .chars()
        .filter_map(|character| u8::try_from(u32::from(character)).ok())
        .collect();
    data.extend(b";\0");
    to_hex(&data)
}

pub struct PrintingPage {
    pub page: adw::PreferencesPage,
    pub queue: adw::ComboRow,
    context: Rc<Context>,
    reset: gtk::Button,
    status: adw::ActionRow,
    profiles: RefCell<Option<(ProfileSnapshot, ProfileSnapshot)>>,
    choices: RefCell<Vec<Option<&'static str>>>,
    loading: Cell<bool>,
    updating: Cell<bool>,
}

impl PrintingPage {
    pub fn new(context: &Rc<Context>) -> Rc<Self> {
        let page = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::builder()
            .title("Printing")
            .description(
                "PDF is the runtime default and opens the system save dialog for a name and location. PCL 5 \
                 output can be saved for a compatible printer. Printing is provided by 2ine; original OS/2 \
                 printer drivers cannot currently be selected. Changes apply next launch.",
            )
            .build();
        page.add(&group);
        let queue = adw::ComboRow::builder()
            .title("Default Printer")
            .use_markup(false)
            .sensitive(false)
            .build();
        group.add(&queue);
        let install = adw::ButtonRow::builder()
            .title("Install OS/2 System Component…")
            .build();
        group.add(&install);
        let reset = flat_button("Use Inherited Default", None);
        reset.set_sensitive(false);
        group.set_header_suffix(Some(&reset));
        let status = adw::ActionRow::builder()
            .title("Reading OS/2 printer preferences…")
            .use_markup(false)
            .build();
        let reload = flat_button("Reload", None);
        status.add_suffix(&reload);
        group.add(&status);
        let output = adw::PreferencesGroup::builder().title("Output").build();
        page.add(&output);
        for (title, value) in [("Resolution", "300 DPI"), ("Page", "A4")] {
            let row = adw::ActionRow::builder().title(title).build();
            let label = gtk::Label::new(Some(value));
            label.add_css_class("dim-label");
            row.add_suffix(&label);
            output.add(&row);
        }
        install.connect_activated(glib::clone!(
            #[strong]
            context,
            move |_| {
                if let Some(window) = context.current() {
                    window.show_component_media("printers", true);
                }
            }
        ));
        let printing = Rc::new(Self {
            page,
            queue,
            context: context.clone(),
            reset,
            status,
            profiles: RefCell::default(),
            choices: RefCell::default(),
            loading: Cell::new(false),
            updating: Cell::new(false),
        });
        let weak = Rc::downgrade(&printing);
        reload.connect_clicked(move |_| {
            if let Some(printing) = weak.upgrade() {
                printing.load();
            }
        });
        let weak = Rc::downgrade(&printing);
        printing.page.connect_map(move |_| {
            if let Some(printing) = weak.upgrade() {
                printing.load();
            }
        });
        let weak = Rc::downgrade(&printing);
        printing.queue.connect_selected_notify(move |row| {
            if let Some(printing) = weak.upgrade() {
                printing.selected(row.selected());
            }
        });
        let weak = Rc::downgrade(&printing);
        printing.reset.connect_clicked(move |_| {
            if let Some(printing) = weak.upgrade() {
                printing.write(None);
            }
        });
        printing
    }

    /// Read the user and system printer preferences.
    pub fn load(self: &Rc<Self>) {
        if self.loading.replace(true) {
            return;
        }
        let Some(window) = self.context.current() else {
            self.loading.set(false);
            return;
        };
        let weak = Rc::downgrade(self);
        glib::spawn_future_local(async move {
            let read = |area: &'static str| {
                let window = window.clone();
                async move {
                    let value = window
                        .api("configuration-read", json!({"name": area}))
                        .await?;
                    Ok::<ProfileSnapshot, crate::engine::Error>(serde_json::from_value(value)?)
                }
            };
            let result = match read("user").await {
                Ok(user) => read("system").await.map(|system| (user, system)),
                Err(error) => Err(error),
            };
            let Some(printing) = weak.upgrade() else {
                return;
            };
            printing.loading.set(false);
            match result {
                Ok(profiles) => {
                    if printing.context.current().is_some() {
                        printing.profiles.replace(Some(profiles));
                        printing.render();
                    }
                }
                Err(error) => printing.failed(&error),
            }
        });
    }

    fn failed(&self, error: &dyn std::fmt::Display) {
        if self.context.current().is_none() {
            return;
        }
        if self.profiles.borrow().is_some() {
            self.render();
        }
        self.status
            .set_title("Printer Preferences Could Not Be Updated");
        self.status
            .set_subtitle(&format!("{error} Reload before trying again."));
    }

    fn render(&self) {
        let Some((user, system)) = self.profiles.borrow().clone() else {
            return;
        };
        let state = printer_state(&user, &system);
        self.updating.set(true);
        self.queue.set_model(Some(&gtk::StringList::new(
            &state.labels.iter().map(String::as_str).collect::<Vec<_>>(),
        )));
        self.queue.set_selected(state.selected);
        self.queue.set_sensitive(true);
        self.updating.set(false);
        self.choices.replace(state.choices);
        self.reset.set_sensitive(state.overridden);
        self.status.set_title(state.title);
        self.status.set_subtitle(&state.subtitle);
    }

    fn selected(self: &Rc<Self>, index: u32) {
        if self.updating.get()
            || self.profiles.borrow().is_none()
            || self.context.current().is_none()
        {
            return;
        }
        let choice = self.choices.borrow().get(index as usize).copied().flatten();
        if let Some(queue) = choice {
            self.write(Some(queue));
        }
    }

    /// Store `queue` in the user profile, or remove the user preference.
    fn write(self: &Rc<Self>, queue: Option<&str>) {
        let Some(window) = self.context.current() else {
            return;
        };
        let Some((user, _)) = self.profiles.borrow().clone() else {
            return;
        };
        if !window
            .sessions()
            .borrow()
            .active(Some(&self.context.ball))
            .is_empty()
        {
            self.render();
            self.status.set_title("Stop the Guest First");
            self.status
                .set_subtitle("Stop the guest before changing its printer preference.");
            return;
        }
        if window.busy() {
            return;
        }
        let entry = preference(&user).cloned();
        let fields = json!({
            "name": "user",
            "expected": user.revision,
            "path": entry.as_ref().map_or("PM_SPOOLER", |entry| entry.application.as_str()),
            "key": entry.as_ref().map_or("QUEUE", |entry| entry.key.as_str()),
            "text": queue.map(queue_data).unwrap_or_default(),
            "remove": queue.is_none(),
        });
        let weak = Rc::downgrade(self);
        glib::spawn_future_local(async move {
            let result = write(&window, "configuration-write", fields)
                .await
                .and_then(|value| Ok(serde_json::from_value::<ProfileSnapshot>(value)?));
            let Some(printing) = weak.upgrade() else {
                return;
            };
            match result {
                Ok(profile) => {
                    if printing.context.current().is_some() {
                        if let Some(profiles) = printing.profiles.borrow_mut().as_mut() {
                            profiles.0 = profile;
                        }
                        printing.render();
                        printing.context.invalidate_profiles("user");
                    }
                }
                Err(error) => printing.failed(&error),
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn profile(entries: &[(&str, &str, &[u8])]) -> ProfileSnapshot {
        ProfileSnapshot {
            entries: entries
                .iter()
                .map(|(application, key, data)| Entry {
                    application: (*application).into(),
                    key: (*key).into(),
                    data: to_hex(data),
                })
                .collect(),
            ..ProfileSnapshot::default()
        }
    }

    #[test]
    fn missing_preferences_follow_the_runtime_default() {
        let state = printer_state(&profile(&[]), &profile(&[]));
        assert_eq!(state.selected, 0);
        assert_eq!(state.title, "Runtime Default");
        assert!(!state.overridden);
        assert_eq!(state.labels, ["Save as PDF", "PCL 5 Printer"]);
    }

    #[test]
    fn user_preferences_override_the_system_profile() {
        let system = profile(&[("PM_SPOOLER", "QUEUE", b"2INEPCL;\0")]);
        let inherited = printer_state(&profile(&[]), &system);
        assert_eq!((inherited.selected, inherited.title), (1, "System Profile"));
        let user = profile(&[("pm_spooler", "queue", b"CUSTOM;\0")]);
        let state = printer_state(&user, &system);
        assert_eq!(state.selected, 2);
        assert_eq!(state.labels[2], "Stored Queue: CUSTOM");
        assert_eq!(state.choices[2], None);
        assert!(state.overridden);
        assert_eq!(
            state.subtitle,
            "Stored value: CUSTOM;. Reading this page does not write defaults."
        );
    }

    #[test]
    fn queues_are_stored_with_a_separator_and_nul() {
        assert_eq!(queue_data("2INEPCL"), to_hex(b"2INEPCL;\0"));
    }
}
