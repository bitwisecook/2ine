// Public domain. No rights reserved. Wholly AI-authored.
//! Sound Banks and MIDI: the ball's sound-bank library and the mapping of
//! the 16 incoming MIDI channels.
//!
//! The engine's `audio-snapshot` supplies installed banks, mapping presets
//! and saved settings. Saved settings are edited in place, so fields this
//! dialog does not show are written back unchanged.

// Channel numbers, programs and bank numbers are small integers shown in GTK rows.
#![allow(clippy::cast_possible_truncation, clippy::cast_precision_loss)]

use std::cell::RefCell;
use std::path::PathBuf;
use std::rc::Rc;

use adw::prelude::*;
use gtk::{gio, glib};
use serde_json::{Value, json};

use crate::dialogs::FormDialog;
use crate::dialogs::form;
use crate::engine;
use crate::window::{BallWindow, Work};

/// Controls for one MIDI channel.
struct Channel {
    target: adw::ComboRow,
    bank: adw::ComboRow,
    program: adw::SpinRow,
    number: adw::SpinRow,
    percussion: adw::ComboRow,
    muted: adw::SwitchRow,
}

fn strings(value: &Value, key: &str) -> Vec<String> {
    value
        .as_array()
        .into_iter()
        .flatten()
        .map(|item| item[key].as_str().unwrap_or_default().to_owned())
        .collect()
}

fn integer(value: &Value) -> i64 {
    value
        .as_i64()
        .or_else(|| value.as_f64().map(|value| value as i64))
        .unwrap_or_default()
}

fn index(value: i64) -> u32 {
    u32::try_from(value.max(0)).unwrap_or_default()
}

/// Whether bank/program/type controls apply: SFZ banks define their own instruments.
pub fn bank_controls_apply(banks: &Value, channel_bank: u32, default_bank: u32) -> bool {
    let bank = if channel_bank > 0 {
        channel_bank - 1
    } else {
        default_bank
    };
    banks[bank as usize]["format"].as_str() != Some("sfz")
}

pub struct AudioDialog {
    dialog: FormDialog,
    window: glib::WeakRef<BallWindow>,
    ball: PathBuf,
    body: gtk::Box,
    data: RefCell<Value>,
    bank: RefCell<Option<adw::ComboRow>>,
    profile: RefCell<Option<adw::ComboRow>>,
    channels: RefCell<Vec<Channel>>,
}

impl AudioDialog {
    /// Present the dialog and read the ball's audio settings.
    pub fn present(window: &BallWindow) -> Rc<Self> {
        let dialog = FormDialog::new(window, "Sound Banks and MIDI", "Save").with_size(760, 720);
        let body = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .vexpand(true)
            .build();
        dialog.append(&body);
        let audio = Rc::new(Self {
            dialog,
            window: window.downgrade(),
            ball: window.ball().unwrap_or_default(),
            body,
            data: RefCell::new(Value::Null),
            bank: RefCell::default(),
            profile: RefCell::default(),
            channels: RefCell::default(),
        });
        let weak = Rc::downgrade(&audio);
        audio.dialog.connect_submit(move |_| {
            if let Some(audio) = weak.upgrade() {
                audio.submit();
            }
        });
        let owner = RefCell::new(Some(audio.clone()));
        audio.dialog.connect_closed(move |_| {
            owner.take();
        });
        audio.dialog.present(Some(window));
        audio.request("audio-snapshot", json!({}), Self::loaded);
        audio
    }

    /// Run an engine request for this ball and pass its result to `completed`.
    fn request(
        self: &Rc<Self>,
        action: &str,
        fields: Value,
        completed: impl FnOnce(&Rc<Self>, Value) + 'static,
    ) {
        let Some(window) = self.window.upgrade() else {
            return;
        };
        let message = window.message(action, fields);
        let audio = self.clone();
        glib::spawn_future_local(async move {
            if let Some(result) = audio
                .dialog
                .run(&window, Work::default(), move || engine::request(&message))
                .await
            {
                completed(&audio, result);
            }
        });
    }

    fn loaded(self: &Rc<Self>, data: Value) {
        while let Some(child) = self.body.first_child() {
            self.body.remove(&child);
        }
        let settings = data["settings"].clone();
        let bank_ids = strings(&data["banks"], "id");
        let bank_names = strings(&data["banks"], "name");
        let profile_ids = strings(&data["profiles"], "id");
        let profile_names = strings(&data["profiles"], "name");
        let page = adw::PreferencesPage::new();
        self.body.append(&page);

        let group = form::group(
            "Sound Banks",
            Some(
                "FluidR3 GM by Frank Wen is included. Import SF2, SF3, DLS, SFZ or a ZIP containing one bank. SFZ \
                 imports the complete containing folder, including samples and license files. Changes apply to \
                 the next launch.",
            ),
        );
        page.add(&group);
        let names: Vec<&str> = bank_names.iter().map(String::as_str).collect();
        let bank = adw::ComboRow::builder()
            .title("Default Bank")
            .model(&gtk::StringList::new(&names))
            .build();
        let selected_bank = settings["bank"].as_str().unwrap_or_default();
        bank.set_selected(
            bank_ids
                .iter()
                .position(|id| id == selected_bank)
                .unwrap_or(0) as u32,
        );
        group.add(&bank);
        let import = adw::ActionRow::builder()
            .title("Install Sound Bank")
            .subtitle("Original files are kept.")
            .build();
        let choose = form::button("Import…", None);
        import.add_suffix(&choose);
        group.add(&import);
        for item in data["banks"].as_array().into_iter().flatten() {
            group.add(
                &adw::ActionRow::builder()
                    .title(item["name"].as_str().unwrap_or_default())
                    .subtitle(format!(
                        "{} · {}",
                        item["format"].as_str().unwrap_or_default().to_uppercase(),
                        item["license"].as_str().unwrap_or("User supplied")
                    ))
                    .use_markup(false)
                    .build(),
            );
        }

        let group = form::group(
            "MIDI Mapping",
            Some("Route each incoming channel or apply the selected preset's defaults."),
        );
        page.add(&group);
        let guide = adw::ExpanderRow::builder()
            .title("Mapping Guide")
            .subtitle("Program numbering and instrument compatibility")
            .build();
        guide.add_row(
            &adw::ActionRow::builder()
                .title("Programs and Banks")
                .subtitle(
                    "Program 0 follows MIDI; 1–128 pins an instrument. Bank −1 and Follow Profile inherit the \
                     mapping. GS/XG use available variations with a GM fallback. SFZ defines its own instruments; \
                     bank/program/type controls apply to SF2, SF3 and DLS.",
                )
                .use_markup(false)
                .build(),
        );
        guide.add_row(
            &adw::ActionRow::builder()
                .title("MT-32 Compatibility")
                .subtitle("The MT-32 preset maps channels only. Authentic MT-32 synthesis needs Munt and your ROMs.")
                .use_markup(false)
                .build(),
        );
        group.add(&guide);
        let names: Vec<&str> = profile_names.iter().map(String::as_str).collect();
        let profile = adw::ComboRow::builder()
            .title("Mapping Preset")
            .model(&gtk::StringList::new(&names))
            .build();
        let selected_profile = settings["profile"].as_str().unwrap_or_default();
        profile.set_selected(
            profile_ids
                .iter()
                .position(|id| id == selected_profile)
                .unwrap_or(0) as u32,
        );
        group.add(&profile);
        let reset = adw::ActionRow::builder()
            .title("Apply Mapping Defaults")
            .subtitle("Resets this draft's 16 channels.")
            .build();
        let apply = form::button("Apply Defaults", None);
        reset.add_suffix(&apply);
        group.add(&reset);

        let mut channels = Vec::new();
        let outputs: Vec<String> = (1..=16).map(|channel| channel.to_string()).collect();
        let outputs: Vec<&str> = outputs.iter().map(String::as_str).collect();
        let mut bank_choices = vec!["Default Bank"];
        bank_choices.extend(bank_names.iter().map(String::as_str));
        for (number, saved) in settings["channels"]
            .as_array()
            .into_iter()
            .flatten()
            .enumerate()
        {
            let row = adw::ExpanderRow::builder()
                .title(format!("Channel {}", number + 1))
                .subtitle("Bank, instrument and output channel")
                .build();
            let target = adw::ComboRow::builder()
                .title("Output Channel")
                .model(&gtk::StringList::new(&outputs))
                .build();
            target.set_selected(index(integer(&saved["target"]) - 1));
            row.add_row(&target);
            let channel_bank = adw::ComboRow::builder()
                .title("Sound Bank")
                .model(&gtk::StringList::new(&bank_choices))
                .build();
            let saved_bank = saved["bank"].as_str().unwrap_or_default();
            channel_bank.set_selected(if saved_bank.is_empty() {
                0
            } else {
                bank_ids
                    .iter()
                    .position(|id| id == saved_bank)
                    .map_or(0, |position| position as u32 + 1)
            });
            row.add_row(&channel_bank);
            let program = adw::SpinRow::with_range(0.0, 128.0, 1.0);
            program.set_title("Program (0 Follows MIDI)");
            program.set_value((integer(&saved["program"]) + 1) as f64);
            row.add_row(&program);
            let bank_number = adw::SpinRow::with_range(-1.0, 16383.0, 1.0);
            bank_number.set_title("Bank Number (−1 Follows MIDI)");
            bank_number.set_value(integer(&saved["bankNumber"]) as f64);
            row.add_row(&bank_number);
            let percussion = adw::ComboRow::builder()
                .title("Channel Type")
                .model(&gtk::StringList::new(&[
                    "Follow Profile",
                    "Melodic",
                    "Percussion",
                ]))
                .build();
            percussion.set_selected(index(integer(&saved["percussion"]) + 1));
            row.add_row(&percussion);
            let muted = adw::SwitchRow::builder()
                .title("Mute")
                .active(saved["muted"].as_bool() == Some(true))
                .build();
            row.add_row(&muted);
            let weak = Rc::downgrade(self);
            channel_bank.connect_selected_notify(move |_| {
                if let Some(audio) = weak.upgrade() {
                    audio.update_controls();
                }
            });
            group.add(&row);
            channels.push(Channel {
                target,
                bank: channel_bank,
                program,
                number: bank_number,
                percussion,
                muted,
            });
        }
        self.data.replace(data);
        self.bank.replace(Some(bank.clone()));
        self.profile.replace(Some(profile));
        self.channels.replace(channels);
        let weak = Rc::downgrade(self);
        bank.connect_selected_notify(move |_| {
            if let Some(audio) = weak.upgrade() {
                audio.update_controls();
            }
        });
        let weak = Rc::downgrade(self);
        choose.connect_clicked(move |_| {
            if let Some(audio) = weak.upgrade() {
                audio.choose();
            }
        });
        let weak = Rc::downgrade(self);
        apply.connect_clicked(move |_| {
            if let Some(audio) = weak.upgrade() {
                audio.defaults();
            }
        });
        self.update_controls();
    }

    fn update_controls(&self) {
        let data = self.data.borrow();
        let default = self
            .bank
            .borrow()
            .as_ref()
            .map_or(0, adw::ComboRow::selected);
        for channel in self.channels.borrow().iter() {
            let enabled = bank_controls_apply(&data["banks"], channel.bank.selected(), default);
            channel.program.set_sensitive(enabled);
            channel.number.set_sensitive(enabled);
            channel.percussion.set_sensitive(enabled);
        }
    }

    /// The edited settings, keeping fields this dialog does not show.
    fn settings(&self) -> Value {
        let data = self.data.borrow();
        let mut settings = data["settings"].clone();
        let bank_ids = strings(&data["banks"], "id");
        let profile_ids = strings(&data["profiles"], "id");
        if let Some(profile) = self.profile.borrow().as_ref()
            && let Some(id) = profile_ids.get(profile.selected() as usize)
        {
            settings["profile"] = json!(id);
        }
        if let Some(bank) = self.bank.borrow().as_ref()
            && let Some(id) = bank_ids.get(bank.selected() as usize)
        {
            settings["bank"] = json!(id);
        }
        if let Some(saved) = settings["channels"].as_array_mut() {
            for (saved, channel) in saved.iter_mut().zip(self.channels.borrow().iter()) {
                let selected = channel.bank.selected() as usize;
                saved["target"] = json!(channel.target.selected() + 1);
                saved["bank"] = json!(if selected > 0 {
                    bank_ids.get(selected - 1).cloned().unwrap_or_default()
                } else {
                    String::new()
                });
                saved["program"] = json!(channel.program.value() as i64 - 1);
                saved["bankNumber"] = json!(channel.number.value() as i64);
                saved["percussion"] = json!(i64::from(channel.percussion.selected()) - 1);
                saved["muted"] = json!(channel.muted.is_active());
            }
        }
        settings
    }

    /// Apply the selected preset's channel defaults to the draft.
    fn defaults(&self) {
        let data = self.data.borrow();
        let Some(profile) = self.profile.borrow().as_ref().map(adw::ComboRow::selected) else {
            return;
        };
        let defaults = &data["profiles"][profile as usize]["channels"];
        for (values, channel) in defaults
            .as_array()
            .into_iter()
            .flatten()
            .zip(self.channels.borrow().iter())
        {
            channel
                .target
                .set_selected(index(integer(&values["target"]) - 1));
            channel.bank.set_selected(0);
            channel
                .program
                .set_value((integer(&values["program"]) + 1) as f64);
            channel
                .number
                .set_value(integer(&values["bankNumber"]) as f64);
            channel
                .percussion
                .set_selected(index(integer(&values["percussion"]) + 1));
            channel
                .muted
                .set_active(values["muted"].as_bool() == Some(true));
        }
    }

    fn choose(self: &Rc<Self>) {
        let Some(window) = self.window.upgrade() else {
            return;
        };
        let filter = gtk::FileFilter::new();
        filter.set_name(Some("Sound Banks (SF2, SF3, SFZ, DLS, ZIP)"));
        for suffix in ["sf2", "sf3", "sfz", "dls", "zip"] {
            filter.add_suffix(suffix);
        }
        let filters = gio::ListStore::new::<gtk::FileFilter>();
        filters.append(&filter);
        let chooser = gtk::FileDialog::builder()
            .title("Import Sound Bank")
            .modal(true)
            .filters(&filters)
            .build();
        let audio = self.clone();
        glib::spawn_future_local(async move {
            match chooser.open_future(Some(&window)).await {
                Ok(file) => match file.path() {
                    Some(path) => audio.import_file(&path),
                    None => audio.dialog.show_error("Choose a local sound-bank file."),
                },
                Err(error) => {
                    if !error.matches(gtk::DialogError::Dismissed)
                        && !error.matches(gtk::DialogError::Cancelled)
                    {
                        audio.dialog.show_error(error);
                    }
                }
            }
        });
    }

    /// Install a sound bank, keeping the edited mapping draft.
    pub fn import_file(self: &Rc<Self>, path: &std::path::Path) {
        let draft = self.settings();
        self.request(
            "import-soundfont",
            json!({"path": path}),
            move |audio, mut data| {
                data["settings"] = draft;
                audio.loaded(data);
            },
        );
    }

    fn submit(self: &Rc<Self>) {
        if self.data.borrow().is_null() {
            return;
        }
        let settings = self.settings();
        self.request("save-audio", json!({"settings": settings}), |audio, _| {
            if let Some(window) = audio.window.upgrade() {
                window.refresh(Some(audio.ball.clone()));
            }
            audio.dialog.close();
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sfz_banks_disable_instrument_controls() {
        let banks = json!([{"id": "fluid", "format": "sf2"}, {"id": "piano", "format": "sfz"}]);
        assert!(bank_controls_apply(&banks, 0, 0));
        assert!(!bank_controls_apply(&banks, 0, 1));
        assert!(!bank_controls_apply(&banks, 2, 0));
        assert!(bank_controls_apply(&banks, 1, 1));
    }
}
