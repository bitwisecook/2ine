// Public domain. No rights reserved. Wholly AI-authored.
//! General settings: runtime build and backend, legacy TLS upgrade,
//! removable-device locks, diagnostics traces and the state folder.

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use adw::prelude::*;
use gtk::glib;

use super::{Context, flat_button};
use crate::model::{DriveKind, Snapshot, display_path};
use crate::window::BallWindow;

const BACKENDS: [Option<&str>; 3] = [None, Some("native"), Some("unicorn")];
const TRACES: [(&str, &str); 2] = [
    ("trace_native", "Trace Native Calls"),
    ("trace_events", "Trace Events"),
];

/// The Execution Backend row index for a stored `runtime.backend`.
pub fn backend_index(backend: Option<&str>) -> u32 {
    match backend {
        Some("native") => 1,
        Some("unicorn") => 2,
        _ => 0,
    }
}

/// The ball command for an Execution Backend row index.
pub fn backend_command(index: u32) -> Vec<String> {
    match BACKENDS.get(index as usize).copied().flatten() {
        Some(backend) => vec!["config".into(), "runtime.backend".into(), backend.into()],
        None => vec!["unset".into(), "runtime.backend".into()],
    }
}

/// A removable device row's subtitle: its image, or why it is ejected.
pub fn device_subtitle(snapshot: &Snapshot, letter: &str) -> String {
    if let Some(missing) = snapshot.unavailable_drives.get(letter) {
        return format!("Ejected — {}", display_path(missing));
    }
    snapshot
        .attached_image(letter)
        .map_or_else(|| "Ejected".into(), display_path)
}

struct DeviceRow {
    letter: String,
    kind: DriveKind,
    row: adw::ExpanderRow,
    lock: adw::SwitchRow,
    readonly: Option<adw::SwitchRow>,
}

pub struct GeneralPage {
    pub page: adw::PreferencesPage,
    context: Rc<Context>,
    runtime: adw::ActionRow,
    backend: adw::ComboRow,
    tls: adw::SwitchRow,
    traces: Vec<(&'static str, adw::SwitchRow)>,
    devices: adw::PreferencesGroup,
    device_rows: RefCell<Vec<DeviceRow>>,
    updating: Cell<bool>,
}

impl GeneralPage {
    pub fn new(context: &Rc<Context>) -> Rc<Self> {
        let page = adw::PreferencesPage::new();
        let runtime_group = adw::PreferencesGroup::builder().title("Runtime").build();
        page.add(&runtime_group);
        let runtime = adw::ActionRow::builder()
            .title("Runtime Build")
            .subtitle_lines(1)
            .use_markup(false)
            .activatable(true)
            .build();
        let choose = flat_button("Choose Runtime Build", Some("folder-open-symbolic"));
        runtime.add_suffix(&choose);
        runtime_group.add(&runtime);
        let packaged = adw::ActionRow::builder()
            .title("Use Packaged Runtime")
            .subtitle("Clear this ball's external build selection")
            .activatable(true)
            .build();
        packaged.add_suffix(&gtk::Image::from_icon_name("edit-undo-symbolic"));
        runtime_group.add(&packaged);
        let backend = adw::ComboRow::builder()
            .title("Execution Backend")
            .model(&gtk::StringList::new(&[
                "Build Default",
                "Native (x86 Linux)",
                "Unicorn",
            ]))
            .build();
        runtime_group.add(&backend);

        let network = adw::PreferencesGroup::builder()
            .title("Network Security")
            .description(
                "Allows supported applications to use TLS 1.2 or 1.3 through host OpenSSL. Off preserves \
                 the application's protocol selection. Certificate checks stay enabled. Applies next launch.",
            )
            .build();
        page.add(&network);
        let tls = adw::SwitchRow::builder()
            .title("Upgrade Legacy TLS")
            .build();
        network.add(&tls);

        let devices = adw::PreferencesGroup::builder()
            .title("Removable Devices")
            .description(
                "Locked devices keep their image attached. Unlock here before ejecting, replacing, or \
                 changing their access mode.",
            )
            .visible(false)
            .build();
        page.add(&devices);

        let diagnostics = adw::PreferencesGroup::builder()
            .title("Diagnostics")
            .description("Reset an override to inherit the runtime default.")
            .build();
        page.add(&diagnostics);
        let mut traces = Vec::new();
        for (key, title) in TRACES {
            let row = adw::SwitchRow::builder().title(title).build();
            let reset = flat_button(
                &format!("Use Runtime Default for {title}"),
                Some("edit-clear-symbolic"),
            );
            row.add_suffix(&reset);
            let context = context.clone();
            reset.connect_clicked(move |_| {
                if let Some(window) = context.current() {
                    window.execute(vec!["unset".into(), format!("system.{key}")]);
                }
            });
            diagnostics.add(&row);
            traces.push((key, row));
        }

        let state = adw::PreferencesGroup::builder()
            .title("Application State")
            .description(
                "Config Files contains startup and profile editors. Registry provides its own key tree. \
                 Shell metadata stays in this ball.",
            )
            .build();
        let folder = adw::ActionRow::builder()
            .title("Open State Folder")
            .activatable(true)
            .build();
        folder.add_suffix(&gtk::Image::from_icon_name("folder-open-symbolic"));
        state.add(&folder);
        page.add(&state);

        let general = Rc::new(Self {
            page,
            context: context.clone(),
            runtime,
            backend,
            tls,
            traces,
            devices,
            device_rows: RefCell::default(),
            updating: Cell::new(false),
        });
        let weak = Rc::downgrade(&general);
        let open_runtime = move || {
            if let Some(general) = weak.upgrade()
                && let Some(window) = general.context.current()
            {
                window.runtime_dialog();
            }
        };
        let open = open_runtime.clone();
        general.runtime.connect_activated(move |_| open());
        choose.connect_clicked(move |_| open_runtime());
        let context = context.clone();
        packaged.connect_activated(glib::clone!(
            #[strong]
            context,
            move |_| {
                if let Some(window) = context.current() {
                    window.set_runtime_build(None);
                }
            }
        ));
        folder.connect_activated(glib::clone!(
            #[strong]
            context,
            move |_| {
                if let Some(window) = context.current()
                    && let Some(root) = window
                        .snapshot()
                        .and_then(|snapshot| snapshot.roots.get("C").cloned())
                {
                    window.open_path(&root.join("OS2"));
                }
            }
        ));
        general.connect_changes();
        if let Some(window) = context.current()
            && let Some(snapshot) = window.snapshot()
        {
            general.refresh(&window, &snapshot);
        }
        general
    }

    fn execute(&self, arguments: Vec<String>) {
        if !self.updating.get()
            && let Some(window) = self.context.current()
        {
            window.execute(arguments);
        }
    }

    fn connect_changes(self: &Rc<Self>) {
        let weak = Rc::downgrade(self);
        self.backend.connect_selected_notify(move |row| {
            if let Some(general) = weak.upgrade() {
                general.execute(backend_command(row.selected()));
            }
        });
        let weak = Rc::downgrade(self);
        self.tls.connect_active_notify(move |row| {
            if let Some(general) = weak.upgrade() {
                general.execute(vec![
                    "config".into(),
                    "system.tls_upgrade".into(),
                    u8::from(row.is_active()).to_string(),
                ]);
            }
        });
        for &(key, ref row) in &self.traces {
            let weak = Rc::downgrade(self);
            row.connect_active_notify(move |row| {
                if let Some(general) = weak.upgrade() {
                    general.execute(vec![
                        "config".into(),
                        format!("system.{key}"),
                        u8::from(row.is_active()).to_string(),
                    ]);
                }
            });
        }
    }

    /// Show the selected ball's current settings.
    pub fn refresh(self: &Rc<Self>, window: &BallWindow, snapshot: &Snapshot) {
        self.updating.set(true);
        self.runtime.set_subtitle(&display_path(window.build()));
        self.backend
            .set_selected(backend_index(snapshot.manifest.runtime.backend.as_deref()));
        self.tls
            .set_active(snapshot.system_flag("tls_upgrade", true));
        for (key, row) in &self.traces {
            row.set_active(snapshot.system_flag(key, false));
        }
        self.refresh_devices(snapshot);
        self.updating.set(false);
    }

    fn refresh_devices(self: &Rc<Self>, snapshot: &Snapshot) {
        let devices: Vec<(String, DriveKind)> = snapshot
            .devices()
            .map(|(letter, device)| (letter.to_owned(), device.device))
            .collect();
        let same = {
            let rows = self.device_rows.borrow();
            rows.len() == devices.len()
                && rows
                    .iter()
                    .zip(&devices)
                    .all(|(row, (letter, kind))| row.letter == *letter && row.kind == *kind)
        };
        if !same {
            for row in self.device_rows.take() {
                self.devices.remove(&row.row);
            }
            let rows = devices
                .iter()
                .map(|(letter, kind)| self.device_row(letter, *kind))
                .collect();
            self.device_rows.replace(rows);
        }
        self.devices.set_visible(!devices.is_empty());
        for row in self.device_rows.borrow().iter() {
            let Some(device) = snapshot.drive(&row.letter).and_then(|drive| drive.device()) else {
                continue;
            };
            let image = snapshot.attached_image(&row.letter).is_some();
            row.row
                .set_subtitle(&device_subtitle(snapshot, &row.letter));
            row.lock.set_active(device.locked);
            row.lock.set_sensitive(image || device.locked);
            if let Some(readonly) = &row.readonly {
                readonly.set_active(device.readonly);
                readonly.set_sensitive(!device.locked);
            }
        }
    }

    fn device_row(self: &Rc<Self>, letter: &str, kind: DriveKind) -> DeviceRow {
        let row = adw::ExpanderRow::builder()
            .title(format!("{letter}: — {} Drive", kind.title()))
            .subtitle_lines(1)
            .use_markup(false)
            .build();
        let lock = adw::SwitchRow::builder()
            .title("Lock Device to Image")
            .build();
        row.add_row(&lock);
        let media = |command: &'static str| {
            let weak = Rc::downgrade(self);
            let letter = letter.to_owned();
            move |row: &adw::SwitchRow| {
                if let Some(general) = weak.upgrade()
                    && !general.updating.get()
                    && let Some(window) = general.context.current()
                {
                    let mode = if row.is_active() { "on" } else { "off" };
                    window.media_command(vec![command.into(), letter.clone(), mode.into()]);
                }
            }
        };
        lock.connect_active_notify(media("device-lock"));
        let readonly = (kind != DriveKind::Cdrom).then(|| {
            let readonly = adw::SwitchRow::builder().title("Read Only").build();
            readonly.connect_active_notify(media("media-readonly"));
            row.add_row(&readonly);
            readonly
        });
        self.devices.add(&row);
        DeviceRow {
            letter: letter.to_owned(),
            kind,
            row,
            lock,
            readonly,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn backend_choices_map_to_runtime_backend() {
        assert_eq!(backend_index(None), 0);
        assert_eq!(backend_index(Some("unicorn")), 2);
        assert_eq!(backend_command(0), ["unset", "runtime.backend"]);
        assert_eq!(backend_command(1), ["config", "runtime.backend", "native"]);
    }

    #[test]
    fn device_subtitles_explain_ejected_media() {
        let snapshot: Snapshot = serde_json::from_value(json!({
            "manifest": {"drives": {
                "A": {"device": "floppy"},
                "D": {"device": "cdrom", "image": "/media/x.iso"},
                "E": {"device": "cdrom", "image": "/gone.iso"}
            }},
            "unavailableDrives": {"E": "/gone.iso"}
        }))
        .unwrap();
        assert_eq!(device_subtitle(&snapshot, "A"), "Ejected");
        assert_eq!(device_subtitle(&snapshot, "D"), "/media/x.iso");
        assert_eq!(device_subtitle(&snapshot, "E"), "Ejected — /gone.iso");
    }
}
