// Public domain. No rights reserved. Wholly AI-authored.
//! Graphics & Sound: portable per-ball preferences, resolved against the
//! receiving runtime's reported features.

// List positions are small indexes into GTK models.
#![allow(clippy::cast_possible_truncation)]

use std::cell::{Cell, RefCell};
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::rc::Rc;
use std::time::Duration;

use adw::prelude::*;
use gtk::glib;
use serde::Deserialize;
use serde_json::json;

use super::{Context, flat_button, save_system};
use crate::model::Snapshot;
use crate::window::BallWindow;

const DPI_PRESETS: [i64; 4] = [96, 120, 144, 192];
const DEPTHS: [i64; 4] = [24, 16, 8, 4];
const CHROME: [&str; 3] = ["native", "os2", "both"];
const PROVIDERS: [Option<&str>; 4] = [None, Some("mesa"), Some("original"), Some("off")];
const PROVIDER_TITLES: [(&str, &str); 3] = [
    ("mesa", "Mesa"),
    ("original", "Original OS/2 (Unqualified)"),
    ("off", "Off"),
];
const FEATURES: [(&str, &str, &str); 4] = [
    (
        "video",
        "Video Playback",
        "Movie playback. PCM audio controls movie sound.",
    ),
    (
        "pcm",
        "PCM Audio",
        "Sampled audio, DART and movie sound. PC speaker has its own Beep Volume.",
    ),
    (
        "midi",
        "MIDI Synthesis",
        "MIDI synthesis uses the sound banks and channel mapping below.",
    ),
    (
        "dive",
        "DIVE Graphics",
        "Direct video access used by OS/2 applications and games.",
    ),
];
/// The pause after the last PC Speaker Volume change before it is saved.
const BEEP_DELAY: Duration = Duration::from_millis(600);

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct Provider {
    pub available: bool,
    pub reason: String,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct OpenGl {
    pub providers: BTreeMap<String, Provider>,
    pub requested: Option<String>,
    pub default: String,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct Feature {
    pub requested: Option<bool>,
    pub default: bool,
    pub available: bool,
    pub reason: String,
}

/// The engine's `runtime-features` report for the receiving runtime.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct RuntimeFeatures {
    pub opengl: OpenGl,
    pub video: Feature,
    pub pcm: Feature,
    pub midi: Feature,
    pub dive: Feature,
}

impl RuntimeFeatures {
    fn feature(&self, key: &str) -> &Feature {
        match key {
            "video" => &self.video,
            "pcm" => &self.pcm,
            "midi" => &self.midi,
            _ => &self.dive,
        }
    }
}

/// DPI choices: the presets plus a stored custom value.
pub fn dpi_values(dpi: i64) -> Vec<i64> {
    let mut values = DPI_PRESETS.to_vec();
    if !values.contains(&dpi) {
        values.push(dpi);
    }
    values
}

pub fn dpi_label(value: i64) -> String {
    format!("{value} DPI ({}%)", value * 100 / 96)
}

/// A numeric `system.*` setting stored as text, truncated to an integer.
#[allow(clippy::cast_possible_truncation)]
fn integer_setting(snapshot: &Snapshot, key: &str, default: i64) -> i64 {
    snapshot
        .system_text(key)
        .and_then(|text| text.trim().parse::<f64>().ok())
        .filter(|value| value.is_finite())
        .map_or(default, |value| value.trunc() as i64)
}

/// PC Speaker Volume percentage for a stored 0–1 fraction; 5% when unreadable.
#[allow(clippy::cast_possible_truncation)]
pub fn beep_percent(stored: &str) -> f64 {
    stored
        .trim()
        .parse::<f64>()
        .ok()
        .filter(|value| !value.is_nan())
        .map_or(5.0, |value| (value.clamp(0.0, 1.0) * 100.0).round())
}

/// The stored fraction for a PC Speaker Volume percentage.
pub fn beep_fraction(percent: f64) -> String {
    format!("{}", percent / 100.0)
}

/// The OpenGL row subtitle for a features report.
pub fn opengl_status(gl: &OpenGl) -> String {
    let chosen = gl.requested.clone().unwrap_or_else(|| gl.default.clone());
    let title = match chosen.as_str() {
        "original" => "Original OS/2",
        "off" => "Off",
        _ => "Mesa",
    };
    let mut status = format!(
        "{}{title}.",
        if gl.requested.is_none() {
            "Runtime default: "
        } else {
            "Saved choice: "
        }
    );
    let provider = gl.providers.get(&chosen).cloned().unwrap_or_default();
    if !provider.available {
        status.push_str(" Unavailable here.");
    }
    if !provider.reason.is_empty() {
        status.push(' ');
        status.push_str(&provider.reason);
    }
    status
}

/// A media switch's effective state and subtitle for a features report.
pub fn feature_status(description: &str, feature: &Feature) -> (bool, String) {
    let selected = feature.requested.unwrap_or(feature.default);
    let mut status = format!(
        "{description} {}{}",
        if feature.requested.is_none() {
            "Runtime default: "
        } else {
            "Saved choice: "
        },
        if selected { "On." } else { "Off." }
    );
    if !feature.available {
        status.push_str(" Unavailable here. ");
        status.push_str(&feature.reason);
    }
    (selected, status)
}

pub struct GraphicsPage {
    pub page: adw::PreferencesPage,
    pub opengl: adw::ComboRow,
    context: Rc<Context>,
    support_group: adw::PreferencesGroup,
    support_row: adw::ActionRow,
    dpi: adw::ComboRow,
    dpi_values: RefCell<Vec<i64>>,
    depth: adw::ComboRow,
    chrome: adw::ComboRow,
    rows: Vec<(&'static str, adw::SwitchRow, gtk::Button)>,
    beep: adw::SpinRow,
    beep_reset: gtk::Button,
    beep_timer: RefCell<Option<glib::SourceId>>,
    build: RefCell<PathBuf>,
    features: RefCell<Option<RuntimeFeatures>>,
    loading: Cell<bool>,
    updating: Cell<bool>,
}

impl GraphicsPage {
    pub fn new(context: &Rc<Context>) -> Rc<Self> {
        let page = adw::PreferencesPage::new();
        let support_group = adw::PreferencesGroup::builder()
            .title("Runtime Support")
            .build();
        let support_row = adw::ActionRow::builder()
            .title("Checking graphics and media support…")
            .use_markup(false)
            .build();
        let retry = flat_button("Retry", None);
        support_row.add_suffix(&retry);
        support_group.add(&support_row);
        page.add(&support_group);

        let display = adw::PreferencesGroup::builder()
            .title("OS/2 Display")
            .description(
                "Applies next launch. Fonts, dialog units and controls scale together. Printer resolution \
                 remains 300 DPI.",
            )
            .build();
        page.add(&display);
        let dpi = adw::ComboRow::builder().title("Display DPI").build();
        display.add(&dpi);
        let depth = adw::ComboRow::builder()
            .title("Colour Depth")
            .model(&gtk::StringList::new(
                &DEPTHS
                    .map(|value| format!("{value}-bit"))
                    .each_ref()
                    .map(String::as_str),
            ))
            .build();
        display.add(&depth);
        let chrome = adw::ComboRow::builder()
            .title("Title Bars")
            .subtitle("Applies next launch. Document windows keep their OS/2 controls.")
            .model(&gtk::StringList::new(&["Native", "OS/2", "Both"]))
            .build();
        display.add(&chrome);

        let graphics = adw::PreferencesGroup::builder()
            .title("OpenGL")
            .description(
                "Original OS/2 uses your own OPENGL.DLL and its dependencies. Its compatibility is not yet \
                 qualified.",
            )
            .build();
        page.add(&graphics);
        let opengl = adw::ComboRow::builder()
            .title("OpenGL Renderer")
            .model(&gtk::StringList::new(&[
                "Receiving Runtime Default",
                "Mesa",
                "Original OS/2 (Unqualified)",
                "Off",
            ]))
            .use_markup(false)
            .build();
        graphics.add(&opengl);
        let install_opengl = adw::ButtonRow::builder()
            .title("Install OS/2 System Component…")
            .build();
        graphics.add(&install_opengl);

        let media = adw::PreferencesGroup::builder()
            .title("Playback and DIVE")
            .description(
                "Playback is provided by 2ine; original OS/2 multimedia drivers cannot currently be selected. \
                 Changes apply next launch. Reset an override to follow the receiving runtime. Unavailable \
                 options can be saved for another computer; enabling them requires a supporting runtime \
                 before launch.",
            )
            .build();
        page.add(&media);
        let install_media = adw::ButtonRow::builder()
            .title("Install OS/2 System Component…")
            .build();
        media.add(&install_media);
        let mut rows = Vec::new();
        for (key, title, description) in FEATURES {
            let row = adw::SwitchRow::builder()
                .title(title)
                .subtitle(format!("{description} Checking runtime support…"))
                .use_markup(false)
                .build();
            let reset = flat_button(
                &format!("Use Receiving Runtime Default for {title}"),
                Some("edit-clear-symbolic"),
            );
            row.add_suffix(&reset);
            media.add(&row);
            rows.push((key, row, reset));
        }

        let sound = adw::PreferencesGroup::builder().title("Sound").build();
        page.add(&sound);
        let beep = adw::SpinRow::builder()
            .title("PC Speaker Volume")
            .subtitle("Percentage of full scale. The runtime default is 5%.")
            .adjustment(&gtk::Adjustment::new(5.0, 0.0, 100.0, 1.0, 10.0, 0.0))
            .digits(0)
            .numeric(true)
            .build();
        let beep_reset = flat_button("Use Default PC Speaker Volume", Some("edit-clear-symbolic"));
        beep.add_suffix(&beep_reset);
        sound.add(&beep);
        let banks = adw::ActionRow::builder()
            .title("Sound Banks and Channel Mapping")
            .subtitle("Import SF2, SF3, SFZ and DLS; configure all 16 MIDI channels.")
            .activatable(true)
            .build();
        let open_banks = flat_button("Open Sound Banks and MIDI", None);
        banks.add_suffix(&open_banks);
        sound.add(&banks);

        let graphics = Rc::new(Self {
            page,
            opengl,
            context: context.clone(),
            support_group,
            support_row,
            dpi,
            dpi_values: RefCell::default(),
            depth,
            chrome,
            rows,
            beep,
            beep_reset,
            beep_timer: RefCell::default(),
            build: RefCell::new(
                context
                    .window()
                    .map(|window| window.build())
                    .unwrap_or_default(),
            ),
            features: RefCell::default(),
            loading: Cell::new(false),
            updating: Cell::new(false),
        });
        let weak = Rc::downgrade(&graphics);
        retry.connect_clicked(move |_| {
            if let Some(graphics) = weak.upgrade() {
                graphics.load();
            }
        });
        let open = {
            let context = context.clone();
            move || {
                if let Some(window) = context.current() {
                    window.audio_dialog();
                }
            }
        };
        let activate = open.clone();
        open_banks.connect_clicked(move |_| open());
        banks.connect_activated(move |_| activate());
        install_opengl.connect_activated(glib::clone!(
            #[strong]
            context,
            move |_| {
                if let Some(window) = context.current() {
                    window.install_opengl_component();
                }
            }
        ));
        install_media.connect_activated(glib::clone!(
            #[strong]
            context,
            move |_| {
                if let Some(window) = context.current() {
                    window.show_component_media("multimedia", true);
                }
            }
        ));
        graphics.connect_changes();
        let weak = Rc::downgrade(&graphics);
        graphics.page.connect_map(move |_| {
            if let Some(graphics) = weak.upgrade()
                && graphics.features.borrow().is_none()
            {
                graphics.load();
            }
        });
        if let Some(window) = context.current()
            && let Some(snapshot) = window.snapshot()
        {
            graphics.refresh(&window, &snapshot);
        }
        graphics
    }

    /// The Video Playback row, highlighted for graphics component settings.
    pub fn video_row(&self) -> adw::SwitchRow {
        self.rows[0].1.clone()
    }

    fn current(&self) -> Option<BallWindow> {
        self.context
            .current()
            .filter(|window| window.build() == *self.build.borrow())
    }

    fn save(&self, key: &str, value: Option<&str>) {
        if self.updating.get() {
            return;
        }
        if let Some(window) = self.current() {
            save_system(&window, key, value);
        }
    }

    fn connect_changes(self: &Rc<Self>) {
        let weak = Rc::downgrade(self);
        self.dpi.connect_selected_notify(move |row| {
            if let Some(graphics) = weak.upgrade() {
                let value = graphics
                    .dpi_values
                    .borrow()
                    .get(row.selected() as usize)
                    .copied();
                if let Some(value) = value {
                    graphics.save("display_dpi", Some(&value.to_string()));
                }
            }
        });
        let weak = Rc::downgrade(self);
        self.depth.connect_selected_notify(move |row| {
            if let Some(graphics) = weak.upgrade()
                && let Some(value) = DEPTHS.get(row.selected() as usize)
            {
                graphics.save("display_depth", Some(&value.to_string()));
            }
        });
        let weak = Rc::downgrade(self);
        self.chrome.connect_selected_notify(move |row| {
            if let Some(graphics) = weak.upgrade()
                && let Some(value) = CHROME.get(row.selected() as usize)
            {
                graphics.save("window_chrome", Some(value));
            }
        });
        let weak = Rc::downgrade(self);
        self.opengl.connect_selected_notify(move |row| {
            if let Some(graphics) = weak.upgrade() {
                graphics.opengl_changed(row.selected());
            }
        });
        for &(key, ref row, ref reset) in &self.rows {
            let weak = Rc::downgrade(self);
            row.connect_active_notify(move |row| {
                if let Some(graphics) = weak.upgrade() {
                    graphics.save(key, Some(if row.is_active() { "true" } else { "false" }));
                }
            });
            let weak = Rc::downgrade(self);
            reset.connect_clicked(move |_| {
                if let Some(graphics) = weak.upgrade() {
                    graphics.save(key, None);
                }
            });
        }
        let weak = Rc::downgrade(self);
        self.beep.connect_value_notify(move |row| {
            if let Some(graphics) = weak.upgrade() {
                graphics.beep_changed(row.value());
            }
        });
        let weak = Rc::downgrade(self);
        self.beep_reset.connect_clicked(move |_| {
            if let Some(graphics) = weak.upgrade() {
                graphics.save("beep_volume", None);
            }
        });
    }

    fn opengl_changed(&self, index: u32) {
        let value = PROVIDERS.get(index as usize).copied().flatten();
        if value == Some("original")
            && !self.updating.get()
            && let Some(window) = self.current()
            && !window.busy()
        {
            window.install_opengl_component();
        } else {
            self.save("opengl", value);
        }
    }

    fn beep_changed(self: &Rc<Self>, percent: f64) {
        if self.updating.get() {
            return;
        }
        let value = beep_fraction(percent);
        if let Some(window) = self.context.window() {
            window.setting_drafts().borrow_mut().insert(
                (self.context.ball.clone(), "beep_volume".into()),
                value.clone(),
            );
        }
        if let Some(previous) = self.beep_timer.take() {
            previous.remove();
        }
        let weak = Rc::downgrade(self);
        let source = glib::timeout_add_local_once(BEEP_DELAY, move || {
            if let Some(graphics) = weak.upgrade() {
                graphics.beep_timer.replace(None);
                graphics.save("beep_volume", Some(&value));
            }
        });
        self.beep_timer.replace(Some(source));
    }

    /// Show the ball's stored preferences; recheck runtime support after changes.
    pub fn refresh(self: &Rc<Self>, window: &BallWindow, snapshot: &Snapshot) {
        self.updating.set(true);
        let dpi = integer_setting(snapshot, "display_dpi", 96);
        let values = dpi_values(dpi);
        let labels: Vec<String> = values.iter().map(|value| dpi_label(*value)).collect();
        if *self.dpi_values.borrow() != values {
            self.dpi.set_model(Some(&gtk::StringList::new(
                &labels.iter().map(String::as_str).collect::<Vec<_>>(),
            )));
            self.dpi_values.replace(values.clone());
        }
        self.dpi
            .set_selected(values.iter().position(|value| *value == dpi).unwrap_or(0) as u32);
        let depth = integer_setting(snapshot, "display_depth", 24);
        self.depth
            .set_selected(DEPTHS.iter().position(|value| *value == depth).unwrap_or(0) as u32);
        let chrome = snapshot
            .system_text("window_chrome")
            .unwrap_or_else(|| "both".into());
        self.chrome.set_selected(
            CHROME
                .iter()
                .position(|value| *value == chrome)
                .unwrap_or(2) as u32,
        );
        let requested = snapshot.system_text("opengl");
        self.opengl.set_selected(
            PROVIDERS
                .iter()
                .position(|value| *value == requested.as_deref())
                .unwrap_or(0) as u32,
        );
        for (key, row, reset) in &self.rows {
            row.set_active(snapshot.system_flag(key, false));
            reset.set_sensitive(snapshot.system(key).is_some());
        }
        let draft = window
            .setting_drafts()
            .borrow()
            .get(&(self.context.ball.clone(), "beep_volume".to_owned()))
            .cloned();
        let stored = draft
            .or_else(|| snapshot.system_text("beep_volume"))
            .unwrap_or_else(|| "0.05".into());
        if self.beep_timer.borrow().is_none() {
            self.beep.set_value(beep_percent(&stored));
        }
        self.beep_reset
            .set_sensitive(snapshot.system("beep_volume").is_some());
        self.updating.set(false);
        let build = window.build();
        let rebuilt = *self.build.borrow() != build;
        self.build.replace(build);
        if rebuilt || self.features.borrow().is_some() {
            self.load();
        }
    }

    /// Ask the receiving runtime which graphics and media features it supports.
    pub fn load(self: &Rc<Self>) {
        if self.loading.replace(true) {
            return;
        }
        self.support_row
            .set_title("Checking graphics and media support…");
        self.support_row.set_subtitle("");
        let Some(window) = self.current() else {
            self.loading.set(false);
            return;
        };
        let build = self.build.borrow().clone();
        let weak = Rc::downgrade(self);
        glib::spawn_future_local(async move {
            let result = window
                .api("runtime-features", json!({"build": build}))
                .await;
            let Some(graphics) = weak.upgrade() else {
                return;
            };
            graphics.loading.set(false);
            if graphics.current().is_none() {
                return;
            }
            match result.and_then(|value| Ok(serde_json::from_value::<RuntimeFeatures>(value)?)) {
                Ok(features) => {
                    graphics.support_group.set_visible(false);
                    graphics.update_capabilities(&features);
                    graphics.features.replace(Some(features));
                }
                Err(error) => {
                    graphics.support_group.set_visible(true);
                    graphics
                        .support_row
                        .set_title("Runtime Support Could Not Be Checked");
                    graphics.support_row.set_subtitle(&format!(
                        "{error} You can still save preferences for another runtime."
                    ));
                    graphics
                        .opengl
                        .set_subtitle("Runtime support has not been checked.");
                    for (key, row, _) in &graphics.rows {
                        let description = FEATURES
                            .iter()
                            .find(|feature| feature.0 == *key)
                            .map_or("", |f| f.2);
                        row.set_subtitle(&format!(
                            "{description} Runtime support has not been checked."
                        ));
                    }
                }
            }
        });
    }

    fn update_capabilities(&self, features: &RuntimeFeatures) {
        self.updating.set(true);
        let gl = &features.opengl;
        let mut labels = vec!["Receiving Runtime Default".to_owned()];
        for (key, title) in PROVIDER_TITLES {
            let available = gl
                .providers
                .get(key)
                .is_some_and(|provider| provider.available);
            labels.push(if available {
                title.to_owned()
            } else {
                format!("{title} — Unavailable Here")
            });
        }
        self.opengl.set_model(Some(&gtk::StringList::new(
            &labels.iter().map(String::as_str).collect::<Vec<_>>(),
        )));
        self.opengl.set_selected(
            PROVIDERS
                .iter()
                .position(|value| *value == gl.requested.as_deref())
                .unwrap_or(0) as u32,
        );
        self.opengl.set_subtitle(&opengl_status(gl));
        for (key, row, reset) in &self.rows {
            let feature = features.feature(key);
            let description = FEATURES
                .iter()
                .find(|entry| entry.0 == *key)
                .map_or("", |entry| entry.2);
            let (selected, status) = feature_status(description, feature);
            row.set_active(selected);
            row.set_subtitle(&status);
            reset.set_sensitive(feature.requested.is_some());
        }
        self.updating.set(false);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn dpi_presets_keep_a_custom_value() {
        assert_eq!(dpi_values(144), [96, 120, 144, 192]);
        assert_eq!(dpi_values(110), [96, 120, 144, 192, 110]);
        assert_eq!(dpi_label(120), "120 DPI (125%)");
    }

    #[test]
    fn beep_volume_is_a_clamped_percentage() {
        assert!((beep_percent("0.05") - 5.0).abs() < f64::EPSILON);
        assert!((beep_percent("2") - 100.0).abs() < f64::EPSILON);
        assert!((beep_percent("loud") - 5.0).abs() < f64::EPSILON);
        assert_eq!(beep_fraction(7.0), "0.07");
        assert_eq!(beep_fraction(100.0), "1");
        assert_eq!(beep_fraction(0.0), "0");
    }

    #[test]
    fn features_explain_saved_and_default_choices() {
        let features: RuntimeFeatures = serde_json::from_value(json!({
            "opengl": {
                "requested": null,
                "default": "mesa",
                "providers": {
                    "mesa": {"available": false, "reason": "Choose a runtime with Mesa OpenGL support."},
                    "original": {"available": false, "reason": ""},
                    "off": {"available": true, "reason": ""}
                }
            },
            "video": {"requested": null, "default": true, "available": true, "reason": ""},
            "pcm": {"requested": false, "default": true, "available": true, "reason": ""},
            "midi": {"requested": true, "default": false, "available": false, "reason": "Not built."},
            "dive": {"requested": null, "default": false, "available": true, "reason": ""}
        }))
        .unwrap();
        assert_eq!(
            opengl_status(&features.opengl),
            "Runtime default: Mesa. Unavailable here. Choose a runtime with Mesa OpenGL support."
        );
        assert_eq!(
            feature_status("Video.", &features.video),
            (true, "Video. Runtime default: On.".into())
        );
        assert_eq!(
            feature_status("PCM.", &features.pcm),
            (false, "PCM. Saved choice: Off.".into())
        );
        assert_eq!(
            feature_status("MIDI.", &features.midi),
            (
                true,
                "MIDI. Saved choice: On. Unavailable here. Not built.".into()
            )
        );
    }
}
