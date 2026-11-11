// Public domain. No rights reserved. Wholly AI-authored.
//! Binary-safe editors for OS/2 profiles (INI files) and the Open32 registry.
//!
//! Values are exchanged with the engine as hexadecimal bytes, so unedited
//! values keep their exact bytes. Every write carries the revision read
//! before editing; the engine refuses it when the file changed on disk.

// List positions are small indexes into GTK models.
#![allow(clippy::cast_possible_truncation)]

use std::cell::{Cell, RefCell};
use std::rc::{Rc, Weak};

use adw::prelude::*;
use gtk::{glib, pango};
use serde::Deserialize;
use serde_json::{Value, json};

use super::registry::{KeyNode, registry_tree};
use super::registry_values::{
    self as values, Invalid, Mode, REGISTRY_TYPES, fold, hex_bytes, latin1, to_hex, to_latin1,
};
use super::table::{Column, Options, Table, Tree};
use super::{Context, flat_button};
use crate::dialogs::FormDialog;
use crate::engine;
use crate::window::Work;

/// One stored profile or registry value; `data` is hexadecimal.
#[derive(Debug, Clone, Default, PartialEq, Eq, Deserialize)]
#[serde(default)]
pub struct Entry {
    pub application: String,
    pub key: String,
    pub data: String,
}

/// A profile as read by `configuration-read` or `configuration-file-read`.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct ProfileSnapshot {
    pub path: String,
    pub revision: String,
    pub format: String,
    pub entries: Vec<Entry>,
}

/// Where a profile editor reads and writes its values.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Source {
    /// The registry, shown as a key tree.
    Registry,
    /// A profile file found in the ball, with its role (`user`, `system`, …).
    File { path: String, role: String },
}

impl Source {
    pub fn area(&self) -> &str {
        match self {
            Self::Registry => "registry",
            Self::File { role, .. } => role,
        }
    }

    fn named(&self) -> bool {
        matches!(self.area(), "registry" | "user" | "system")
    }

    /// The read request: action and fields.
    pub fn read(&self) -> (&'static str, Value) {
        match self {
            Self::File { path, .. } if !self.named() => {
                ("configuration-file-read", json!({"path": path}))
            }
            _ => ("configuration-read", json!({"name": self.area()})),
        }
    }

    /// The write request for one value; `text` is hexadecimal.
    pub fn write(
        &self,
        expected: &str,
        application: &str,
        key: &str,
        text: &str,
        remove: bool,
    ) -> (&'static str, Value) {
        match self {
            Self::File { path, .. } if !self.named() => (
                "configuration-file-write",
                json!({"path": path, "expected": expected, "application": application,
                       "key": key, "data": text, "remove": remove}),
            ),
            _ => (
                "configuration-write",
                json!({"name": self.area(), "expected": expected, "path": application,
                       "key": key, "text": text, "remove": remove}),
            ),
        }
    }
}

/// A row in the values table.
#[derive(Debug, Clone)]
pub struct Row {
    pub name: String,
    pub summary: String,
    pub entry: Entry,
}

/// The list name and summary of an entry; registry lists only typed `v:` values.
pub fn entry_row(registry: bool, entry: &Entry) -> Option<Row> {
    if registry && !entry.key.starts_with("v:") {
        return None;
    }
    let key = if registry {
        &entry.key[2..]
    } else {
        entry.key.as_str()
    };
    let data = hex_bytes(&entry.data).unwrap_or_default();
    let short_header = registry && data.len() < 4;
    let kind = if registry {
        values::header(&data)
    } else {
        None
    };
    let payload = if registry && !short_header {
        &data[4..]
    } else {
        &data[..]
    };
    let summary = if short_header {
        format!("Incomplete type header: {}", to_hex(payload))
    } else if kind == Some(4) && payload.len() == 4 {
        format!(
            "DWORD: {}",
            u32::from_le_bytes([payload[0], payload[1], payload[2], payload[3]])
        )
    } else if let Some((0, body)) = payload.split_last()
        && !body.contains(&0)
    {
        latin1(body).replace('\n', " ↵ ")
    } else {
        format!(
            "{} bytes: {}",
            payload.len(),
            to_hex(&payload[..payload.len().min(32)])
        )
    };
    let shown = if key.is_empty() { "(Default)" } else { key };
    Some(if registry {
        Row {
            name: shown.to_owned(),
            summary: match kind {
                Some(kind) if !short_header => format!(
                    "{} · {summary}",
                    values::selector_title(kind)
                        .map_or_else(|| format!("Type {kind}"), str::to_owned)
                ),
                _ => summary,
            },
            entry: entry.clone(),
        }
    } else {
        Row {
            name: format!("{} · {shown}", entry.application),
            summary,
            entry: entry.clone(),
        }
    })
}

struct Registry {
    tree: Rc<Table<KeyNode>>,
    split: adw::OverlaySplitView,
    key_path: gtk::Label,
    selected_key: RefCell<String>,
    building: Cell<bool>,
}

pub struct ProfilePage {
    pub widget: gtk::Box,
    context: Rc<Context>,
    source: Source,
    add: gtk::Button,
    path: gtk::Label,
    notice: adw::Banner,
    values: Rc<Table<Row>>,
    snapshot: RefCell<Option<ProfileSnapshot>>,
    registry: Option<Registry>,
}

impl ProfilePage {
    pub fn new(context: &Rc<Context>, source: Source) -> Rc<Self> {
        let page = Rc::new_cyclic(|weak: &Weak<Self>| {
            let edit = weak.clone();
            let values = Table::new(
                vec![
                    Column::new("Key", |row: &Row| row.name.clone())
                        .width(300, true)
                        .sorted(|row| glib::casefold(&row.name).into()),
                    Column::new("Value", |row: &Row| row.summary.clone())
                        .width(320, true)
                        .compact()
                        .sorted(|row| glib::casefold(&row.summary).into()),
                    Column::new("Edit", |_: &Row| "Edit Value…".into())
                        .width(110, false)
                        .compact()
                        .action(move |row| {
                            if let Some(page) = edit.upgrade() {
                                page.edit(Some(&row.entry));
                            }
                        }),
                ],
                Options {
                    title: "Profile and registry values",
                    search_hint: "Search keys and values",
                    identity: Rc::new(|row: &Row| {
                        format!("{}\0{}", row.entry.application, row.entry.key)
                    }),
                    searchable: Rc::new(|row: &Row| format!("{} {}", row.name, row.summary)),
                    tooltip: None,
                    single_click: true,
                    height: 220,
                },
            );
            let registry = (source == Source::Registry).then(|| Self::registry_parts(weak));
            Self {
                widget: gtk::Box::builder()
                    .orientation(gtk::Orientation::Vertical)
                    .spacing(12)
                    .vexpand(true)
                    .build(),
                context: context.clone(),
                source,
                add: flat_button("Add Value…", None),
                path: gtk::Label::builder()
                    .xalign(0.0)
                    .selectable(true)
                    .hexpand(true)
                    .max_width_chars(1)
                    .ellipsize(pango::EllipsizeMode::Middle)
                    .css_classes(["dim-label"])
                    .build(),
                notice: adw::Banner::builder()
                    .use_markup(false)
                    .revealed(false)
                    .build(),
                values,
                snapshot: RefCell::default(),
                registry,
            }
        });
        page.layout();
        page
    }

    fn registry_parts(weak: &Weak<Self>) -> Registry {
        let tree = Table::new(
            vec![
                Column::new("Registry Key", |node: &KeyNode| node.name.clone())
                    .width(240, true)
                    .sorted(|node| fold(&node.name)),
            ],
            Options {
                title: "Registry keys",
                search_hint: "Search Registry Keys",
                identity: Rc::new(|node: &KeyNode| node.path.clone()),
                searchable: Rc::new(|node: &KeyNode| format!("{} {}", node.name, node.path)),
                tooltip: Some(Rc::new(|node: &KeyNode| node.path.clone())),
                single_click: false,
                height: 220,
            },
        );
        let selected = weak.clone();
        tree.connect_selected(move |node| {
            if let Some(page) = selected.upgrade() {
                page.key_selected(node);
            }
        });
        Registry {
            tree,
            split: adw::OverlaySplitView::builder()
                .min_sidebar_width(180.0)
                .max_sidebar_width(260.0)
                .vexpand(true)
                .build(),
            key_path: gtk::Label::builder()
                .xalign(0.0)
                .selectable(true)
                .wrap(true)
                .build(),
            selected_key: RefCell::new("hkcu".into()),
            building: Cell::new(false),
        }
    }

    fn layout(self: &Rc<Self>) {
        let bar = adw::WrapBox::builder()
            .child_spacing(6)
            .line_spacing(6)
            .build();
        let reload = flat_button("Reload", None);
        bar.append(&reload);
        bar.append(&self.add);
        self.widget.append(&bar);
        self.widget.append(&self.path);
        self.widget.append(&self.notice);
        let weak = Rc::downgrade(self);
        reload.connect_clicked(move |_| {
            if let Some(page) = weak.upgrade() {
                page.load();
            }
        });
        let weak = Rc::downgrade(self);
        self.add.connect_clicked(move |_| {
            if let Some(page) = weak.upgrade() {
                page.edit(None);
            }
        });
        let weak = Rc::downgrade(self);
        self.values.connect_activated(move |row| {
            if let Some(page) = weak.upgrade() {
                page.edit(Some(&row.entry));
            }
        });
        if let Some(registry) = &self.registry {
            self.values
                .search
                .set_placeholder_text(Some("Search values in selected key"));
            let keys = gtk::ToggleButton::builder()
                .icon_name("sidebar-show-symbolic")
                .tooltip_text("Show Registry Keys")
                .visible(false)
                .build();
            keys.update_property(&[gtk::accessible::Property::Label("Show registry keys")]);
            registry.split.set_sidebar(Some(&registry.tree.widget));
            registry.split.set_content(Some(&self.values.widget));
            registry.split.set_show_sidebar(true);
            keys.bind_property("active", &registry.split, "show-sidebar")
                .bidirectional()
                .sync_create()
                .build();
            registry.split.set_show_sidebar(true);
            bar.prepend(&keys);
            let layout = adw::BreakpointBin::builder()
                .child(&registry.split)
                .vexpand(true)
                .width_request(280)
                .height_request(220)
                .build();
            let narrow = adw::Breakpoint::new(
                adw::BreakpointCondition::parse("max-width: 600sp").expect("valid condition"),
            );
            narrow.add_setter(&registry.split, "collapsed", Some(&true.to_value()));
            narrow.add_setter(&keys, "visible", Some(&true.to_value()));
            layout.add_breakpoint(narrow);
            self.widget.append(&layout);
            self.widget.append(&registry.key_path);
        } else {
            self.widget.append(&self.values.widget);
        }
        let weak = Rc::downgrade(self);
        self.widget.connect_map(move |_| {
            if let Some(page) = weak.upgrade()
                && page.snapshot.borrow().is_none()
            {
                page.load();
            }
        });
        self.refresh_editability();
    }

    pub fn area(&self) -> &str {
        self.source.area()
    }

    fn registry(&self) -> bool {
        self.registry.is_some()
    }

    /// Forget the loaded values so that they are read again when shown.
    pub fn invalidate(&self) {
        self.snapshot.replace(None);
        if self.widget.is_mapped() {
            self.refresh_editability();
        }
    }

    pub fn load(self: &Rc<Self>) {
        let Some(window) = self.context.current() else {
            return;
        };
        let (action, fields) = self.source.read();
        let weak = Rc::downgrade(self);
        glib::spawn_future_local(async move {
            let result = window
                .api(action, fields)
                .await
                .and_then(|value| Ok(serde_json::from_value::<ProfileSnapshot>(value)?));
            let Some(page) = weak.upgrade() else { return };
            match result {
                Ok(snapshot) => {
                    if page.context.current().is_some() {
                        page.path.set_label(&snapshot.path);
                        page.notice.set_revealed(false);
                        page.snapshot.replace(Some(snapshot));
                        page.render();
                    }
                }
                Err(error) => page.show_error(&error.to_string()),
            }
        });
    }

    fn show_error(&self, message: &str) {
        self.notice.set_title(message);
        self.notice.set_revealed(true);
    }

    pub fn refresh_editability(&self) {
        let busy = self.context.window().is_none_or(|window| window.busy());
        self.add
            .set_sensitive(self.snapshot.borrow().is_some() && !busy);
        self.values.view.set_sensitive(!busy);
    }

    fn render(&self) {
        if let Some(registry) = &self.registry {
            if registry.building.replace(true) {
                return;
            }
            let entries = self
                .snapshot
                .borrow()
                .as_ref()
                .map(|snapshot| snapshot.entries.clone())
                .unwrap_or_default();
            let (roots, paths) = registry_tree(&entries);
            if !paths.contains(&registry.selected_key.borrow()) {
                registry.selected_key.replace("hkcu".into());
            }
            registry.tree.set_rows(roots);
            let selected = registry.selected_key.borrow().clone();
            registry.tree.focus(&selected);
            registry.building.set(false);
        }
        self.render_values();
    }

    fn render_values(&self) {
        let selected = self
            .registry
            .as_ref()
            .map(|registry| registry.selected_key.borrow().clone());
        let rows: Vec<Tree<Row>> = self
            .snapshot
            .borrow()
            .iter()
            .flat_map(|snapshot| snapshot.entries.iter())
            .filter(|entry| {
                selected
                    .as_ref()
                    .is_none_or(|key| fold(&entry.application) == *key)
            })
            .filter_map(|entry| entry_row(self.registry(), entry))
            .map(Tree::leaf)
            .collect();
        self.values.set_rows(rows);
        if let (Some(registry), Some(selected)) = (&self.registry, selected) {
            registry.key_path.set_label(&selected);
        }
        self.refresh_editability();
    }

    fn key_selected(&self, node: Option<&KeyNode>) {
        let Some(registry) = &self.registry else {
            return;
        };
        let Some(node) = node else { return };
        if registry.building.get() {
            return;
        }
        registry.selected_key.replace(node.path.clone());
        self.render_values();
        if registry.split.is_collapsed() {
            registry.split.set_show_sidebar(false);
        }
    }

    /// Select a registry key in the tree.
    pub fn select_key(&self, path: &str) {
        if let Some(registry) = &self.registry {
            registry.tree.focus(path);
        }
    }

    fn edit(self: &Rc<Self>, entry: Option<&Entry>) {
        let Some(window) = self.context.current() else {
            return;
        };
        let Some(revision) = self
            .snapshot
            .borrow()
            .as_ref()
            .map(|snapshot| snapshot.revision.clone())
        else {
            self.show_error(if self.registry() {
                "Stop the guest before editing the registry."
            } else {
                "Stop the guest before editing profiles or the registry."
            });
            return;
        };
        if window.busy() {
            self.show_error(if self.registry() {
                "Stop the guest before editing the registry."
            } else {
                "Stop the guest before editing profiles or the registry."
            });
            return;
        }
        let dialog = ValueDialog::new(self, entry.cloned(), revision);
        if entry.is_none()
            && let Some(registry) = &self.registry
        {
            dialog.application.set_text(&registry.selected_key.borrow());
        }
        dialog.dialog.present(Some(&window));
    }

    fn saved(&self, snapshot: ProfileSnapshot, application: &str) {
        self.snapshot.replace(Some(snapshot));
        if let Some(registry) = &self.registry {
            registry.selected_key.replace(application.to_owned());
        }
        self.render();
    }
}

const RAW_CAPTION: &str = "Includes the four-byte little-endian type header followed by the exact value bytes. \
                           Changing the header explicitly changes the value type.";
const LIST_CAPTION: &str = "One nonempty string per line. Empty data represents an empty list; a double NUL terminator is included.";

/// Representations of a plain profile value.
const PLAIN_MODES: [&str; 3] = ["Hexadecimal", "Latin-1 Text", "DWORD (Little Endian)"];

/// Bytes for a plain profile value in representation `mode` (see [`PLAIN_MODES`]).
pub fn plain_data(mode: u32, text: &str, terminated: bool) -> Result<Vec<u8>, Invalid> {
    match mode {
        0 => hex_bytes(text),
        1 => {
            if text.contains('\0') {
                return Err(Invalid::new(
                    "Text cannot contain NUL. Use hexadecimal for binary values.",
                ));
            }
            let mut data = to_latin1(text)?;
            if terminated {
                data.push(0);
            }
            Ok(data)
        }
        _ => text
            .trim()
            .parse::<u32>()
            .map(|value| value.to_le_bytes().to_vec())
            .map_err(|_| Invalid::new("Enter an unsigned 32-bit decimal number.")),
    }
}

/// Text for plain profile bytes in representation `mode`, and whether a text value was NUL-terminated.
pub fn plain_text(mode: u32, data: &[u8]) -> Result<(String, Option<bool>), Invalid> {
    match mode {
        0 => Ok((to_hex(data), None)),
        1 => {
            let terminated = data.last() == Some(&0);
            let value = if terminated {
                &data[..data.len() - 1]
            } else {
                data
            };
            if value.contains(&0) {
                return Err(Invalid::new(
                    "Embedded NUL bytes require hexadecimal editing.",
                ));
            }
            Ok((latin1(value), Some(terminated)))
        }
        _ => {
            let bytes: [u8; 4] = data
                .try_into()
                .map_err(|_| Invalid::new("An integer requires exactly four bytes."))?;
            Ok((u32::from_le_bytes(bytes).to_string(), None))
        }
    }
}

/// Check and normalize the names of a value before it is written.
pub fn registry_names(
    entry: Option<&Entry>,
    application: &str,
    key: &str,
) -> Result<(String, String), Invalid> {
    if application.contains('\0') || key.contains('\0') {
        return Err(Invalid::new("Registry names cannot contain NUL bytes."));
    }
    if to_latin1(application)?.len() > 32767 || to_latin1(key)?.len() + 2 > 32767 {
        return Err(Invalid::new(
            "Registry paths and v:-prefixed value names are limited to 32767 bytes.",
        ));
    }
    Ok(match entry {
        None => (fold(application), format!("v:{}", fold(key))),
        Some(entry) => (entry.application.clone(), entry.key.clone()),
    })
}

/// Add or edit one profile or registry value.
struct ValueDialog {
    dialog: FormDialog,
    page: Rc<ProfilePage>,
    entry: Option<Entry>,
    revision: String,
    application: adw::EntryRow,
    key: adw::EntryRow,
    editor: gtk::TextView,
    terminated: adw::SwitchRow,
    caption: gtk::Label,
    format: adw::ComboRow,
    kind: adw::EntryRow,
    type_selector: Option<adw::ComboRow>,
    /// The registry representation in use.
    mode: Cell<Mode>,
    modes: RefCell<Vec<Mode>>,
    /// The plain profile representation in use, an index into [`PLAIN_MODES`].
    plain_mode: Cell<u32>,
    converting: Cell<bool>,
}

impl ValueDialog {
    fn new(page: &Rc<ProfilePage>, entry: Option<Entry>, revision: String) -> Rc<Self> {
        let window = page.context.window().expect("window");
        let registry = page.registry();
        let dialog = FormDialog::new(
            &window,
            if entry.is_none() {
                "Add Value"
            } else {
                "Edit Value"
            },
            "Save",
        )
        .with_size(620, 650);
        let application = adw::EntryRow::builder()
            .title(if registry {
                "Registry Key"
            } else {
                "Application"
            })
            .text(
                entry
                    .as_ref()
                    .map_or(if registry { "hkcu\\software\\" } else { "" }, |entry| {
                        entry.application.as_str()
                    }),
            )
            .sensitive(entry.is_none())
            .build();
        let key = adw::EntryRow::builder()
            .title(if registry {
                "Value Name (empty for default)"
            } else {
                "Key"
            })
            .text(entry.as_ref().map_or("", |entry| {
                if registry {
                    &entry.key[2.min(entry.key.len())..]
                } else {
                    &entry.key
                }
            }))
            .sensitive(entry.is_none())
            .build();
        let editor = gtk::TextView::builder()
            .monospace(true)
            .wrap_mode(gtk::WrapMode::WordChar)
            .left_margin(12)
            .right_margin(12)
            .top_margin(12)
            .bottom_margin(12)
            .build();
        editor.update_property(&[gtk::accessible::Property::Label("Configuration value data")]);
        let selector = (registry && entry.is_none()).then(adw::ComboRow::new);
        let this = Rc::new(Self {
            dialog,
            page: page.clone(),
            entry,
            revision,
            application,
            key,
            editor,
            terminated: adw::SwitchRow::builder()
                .title("Include Terminating NUL Byte")
                .visible(false)
                .active(true)
                .build(),
            caption: gtk::Label::builder()
                .xalign(0.0)
                .wrap(true)
                .visible(false)
                .css_classes(["dim-label"])
                .build(),
            format: adw::ComboRow::builder().title("Edit As").build(),
            kind: adw::EntryRow::builder()
                .title("Type Number (decimal or 0x hexadecimal)")
                .build(),
            type_selector: selector,
            mode: Cell::new(Mode::Text),
            modes: RefCell::default(),
            plain_mode: Cell::new(0),
            converting: Cell::new(false),
        });
        this.build();
        let owner = RefCell::new(Some(this.clone()));
        this.dialog.connect_closed(move |_| {
            owner.take();
        });
        this
    }

    fn build(self: &Rc<Self>) {
        let preferences = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::builder().title("Value").build();
        preferences.add(&group);
        group.add(&self.application);
        group.add(&self.key);
        let data = self
            .entry
            .as_ref()
            .map(|entry| hex_bytes(&entry.data).unwrap_or_default())
            .unwrap_or_default();
        if self.page.registry() {
            self.registry_controls(&group, &data);
        } else {
            self.format
                .set_model(Some(&gtk::StringList::new(&PLAIN_MODES)));
            group.add(&self.format);
            group.add(&self.terminated);
            self.editor.buffer().set_text(&to_hex(&data));
        }
        let content = adw::PreferencesGroup::builder().title("Data").build();
        let scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .min_content_height(180)
            .has_frame(true)
            .child(&self.editor)
            .build();
        let column = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(12)
            .build();
        column.append(&scroll);
        column.append(&self.caption);
        let help = gtk::Label::builder()
            .label(format!(
                "{}Unedited values retain their exact bytes. Saving checks for external changes. Stop the guest \
                 before editing; changes apply next launch.",
                if self.page.registry() {
                    "Text uses Latin-1 bytes with a terminating NUL. Integers accept decimal or 0x hexadecimal. "
                } else {
                    ""
                }
            ))
            .xalign(0.0)
            .wrap(true)
            .css_classes(["dim-label"])
            .build();
        column.append(&help);
        if let Some(entry) = &self.entry {
            let delete = gtk::Button::builder()
                .label("Delete Value…")
                .halign(gtk::Align::Start)
                .build();
            delete.add_css_class("destructive-action");
            let weak = Rc::downgrade(self);
            delete.connect_clicked(move |_| {
                if let Some(dialog) = weak.upgrade() {
                    dialog.confirm_delete();
                }
            });
            column.append(&delete);
            if self.page.registry() && !values::normalized(&entry.application, &entry.key) {
                self.dialog.show_error(
                    "This stored name is not normalized for Open32. It can be inspected, but this editor will not \
                     silently rename or duplicate it.",
                );
                self.dialog.accept_button().set_sensitive(false);
                delete.set_sensitive(false);
            }
        }
        content.add(&column);
        preferences.add(&content);
        self.dialog.append(&preferences);
        let weak = Rc::downgrade(self);
        self.format.connect_selected_notify(move |row| {
            if let Some(dialog) = weak.upgrade() {
                dialog.convert(row);
            }
        });
        let weak = Rc::downgrade(self);
        self.dialog.connect_submit(move |_| {
            if let Some(dialog) = weak.upgrade() {
                dialog.submit(false);
            }
        });
    }

    /// Type and representation controls matching the stored record layout.
    fn registry_controls(self: &Rc<Self>, group: &adw::PreferencesGroup, data: &[u8]) {
        let kind = if self.entry.is_some() {
            values::header(data)
        } else {
            Some(1)
        };
        self.kind
            .set_text(&kind.map(|kind| kind.to_string()).unwrap_or_default());
        if let Some(selector) = &self.type_selector {
            selector.set_title("Registry Type");
            let mut titles: Vec<&str> = REGISTRY_TYPES.iter().map(|(_, title)| *title).collect();
            titles.push("Other Type Number…");
            selector.set_model(Some(&gtk::StringList::new(&titles)));
            selector.set_selected(1);
            self.kind.set_visible(false);
            let weak = Rc::downgrade(self);
            selector.connect_selected_notify(move |row| {
                if let Some(dialog) = weak.upgrade() {
                    dialog.type_selected(row.selected());
                }
            });
            let weak = Rc::downgrade(self);
            self.kind.connect_text_notify(move |_| {
                if let Some(dialog) = weak.upgrade() {
                    dialog.type_code_changed();
                }
            });
            group.add(selector);
            group.add(&self.kind);
        } else {
            let row = adw::ActionRow::builder()
                .title("Type")
                .subtitle(values::type_name(kind))
                .build();
            row.add_css_class("property");
            group.add(&row);
        }
        let (mode, text) = if self.entry.is_some() {
            values::initial(data)
        } else {
            (Mode::Text, String::new())
        };
        self.mode.set(mode);
        group.add(&self.format);
        self.set_modes(kind.unwrap_or(0));
        self.editor.buffer().set_text(&text);
        self.update_caption();
        if mode == Mode::Raw && self.entry.is_some() {
            self.dialog
                .show_error("Missing or truncated type header. Raw bytes are preserved.");
        }
    }

    fn set_modes(&self, kind: u32) {
        let mut modes = values::modes(kind);
        if !modes.contains(&self.mode.get()) {
            modes.insert(0, self.mode.get());
        }
        self.converting.set(true);
        let titles: Vec<&str> = modes.iter().map(|mode| mode.title()).collect();
        self.format.set_model(Some(&gtk::StringList::new(&titles)));
        self.format.set_selected(
            modes
                .iter()
                .position(|mode| *mode == self.mode.get())
                .unwrap_or(0) as u32,
        );
        self.modes.replace(modes);
        self.converting.set(false);
    }

    fn update_caption(&self) {
        let text = match self.mode.get() {
            Mode::List => LIST_CAPTION,
            Mode::Raw => RAW_CAPTION,
            _ => "",
        };
        self.caption.set_label(text);
        self.caption.set_visible(!text.is_empty());
        if let Some(selector) = &self.type_selector {
            let raw = self.mode.get() == Mode::Raw;
            selector.set_sensitive(!raw);
            self.kind.set_sensitive(!raw);
        }
    }

    fn registry_kind(&self) -> Option<u32> {
        values::parse_type(&self.kind.text())
    }

    fn type_selected(&self, index: u32) {
        if self.converting.get() {
            return;
        }
        let custom = index as usize == REGISTRY_TYPES.len();
        self.kind.set_visible(custom);
        let kind = if custom {
            3
        } else {
            REGISTRY_TYPES
                .get(index as usize)
                .map_or(3, |(code, _)| *code)
        };
        self.new_type(kind, false);
    }

    fn type_code_changed(&self) {
        if self.converting.get() {
            return;
        }
        let Some(kind) = self.registry_kind() else {
            return;
        };
        self.converting.set(true);
        if let (Some(selector), Some(index)) = (&self.type_selector, values::selector_index(kind)) {
            selector.set_selected(index as u32);
        }
        self.converting.set(false);
        self.kind.set_visible(true);
        self.new_type(kind, true);
    }

    fn new_type(&self, kind: u32, keep_code: bool) {
        if !keep_code {
            self.converting.set(true);
            self.kind.set_text(&kind.to_string());
            self.converting.set(false);
        }
        self.mode.set(values::typed_mode(kind).unwrap_or(Mode::Hex));
        self.set_modes(kind);
        self.editor
            .buffer()
            .set_text(if self.mode.get() == Mode::Integer {
                "0"
            } else {
                ""
            });
        self.update_caption();
        self.dialog.clear_error();
    }

    fn text(&self) -> String {
        let buffer = self.editor.buffer();
        buffer
            .text(&buffer.start_iter(), &buffer.end_iter(), true)
            .into()
    }

    fn data(&self) -> Result<Vec<u8>, Invalid> {
        if self.page.registry() {
            values::encode(self.mode.get(), self.registry_kind(), &self.text())
        } else {
            plain_data(
                self.plain_mode.get(),
                &self.text(),
                self.terminated.is_active(),
            )
        }
    }

    fn convert(&self, row: &adw::ComboRow) {
        if self.converting.get() {
            return;
        }
        let new = row.selected();
        let result = if self.page.registry() {
            self.convert_registry(new)
        } else {
            self.convert_plain(new)
        };
        if let Err(error) = result {
            self.converting.set(true);
            let previous = if self.page.registry() {
                self.modes
                    .borrow()
                    .iter()
                    .position(|mode| *mode == self.mode.get())
                    .unwrap_or(0) as u32
            } else {
                self.plain_mode.get()
            };
            row.set_selected(previous);
            self.converting.set(false);
            self.dialog.show_error(error);
        }
    }

    fn convert_registry(&self, index: u32) -> Result<(), Invalid> {
        let Some(mode) = self.modes.borrow().get(index as usize).copied() else {
            return Ok(());
        };
        let wire = self.data()?;
        let text = values::render(mode, &wire)?;
        if let (Some(kind), Some(selector)) = (values::header(&wire), &self.type_selector) {
            self.converting.set(true);
            self.kind.set_text(&kind.to_string());
            if let Some(index) = values::selector_index(kind) {
                selector.set_selected(index as u32);
            } else {
                selector.set_selected(REGISTRY_TYPES.len() as u32);
                self.kind.set_visible(true);
            }
            self.converting.set(false);
        }
        self.editor.buffer().set_text(&text);
        self.mode.set(mode);
        self.update_caption();
        self.dialog.clear_error();
        Ok(())
    }

    fn convert_plain(&self, new: u32) -> Result<(), Invalid> {
        let data = self.data()?;
        let (text, terminated) = plain_text(new, &data)?;
        if let Some(terminated) = terminated {
            self.terminated.set_active(terminated);
        }
        self.editor.buffer().set_text(&text);
        self.plain_mode.set(new);
        self.terminated.set_visible(new == 1);
        self.dialog.clear_error();
        Ok(())
    }

    fn request(&self, remove: bool) -> Result<(String, Value), Invalid> {
        let data = if remove { Vec::new() } else { self.data()? };
        let registry = self.page.registry();
        if registry && !remove {
            values::validate_wire(&data)?;
            values::validate_value(values::header(&data), &data[4..])?;
        }
        let (application, key) = if registry {
            registry_names(
                self.entry.as_ref(),
                &self.application.text(),
                &self.key.text(),
            )?
        } else {
            (self.application.text().into(), self.key.text().into())
        };
        let (action, fields) =
            self.page
                .source
                .write(&self.revision, &application, &key, &to_hex(&data), remove);
        let window = self
            .page
            .context
            .window()
            .ok_or_else(|| Invalid::new("The window closed."))?;
        Ok((application, window.message(action, fields)))
    }

    fn submit(self: &Rc<Self>, remove: bool) {
        let (application, message) = match self.request(remove) {
            Ok(request) => request,
            Err(error) => {
                self.dialog.show_error(error);
                return;
            }
        };
        let Some(window) = self.page.context.window() else {
            return;
        };
        if window.ball_locked(&self.page.context.ball) {
            self.dialog.show_error(
                "An application or component job is active in this ball. Finish it before editing.",
            );
            return;
        }
        let this = self.clone();
        glib::spawn_future_local(async move {
            let saved = this
                .dialog
                .run(&window, Work::default(), move || {
                    engine::request_as::<ProfileSnapshot>(&message)
                })
                .await;
            if let Some(snapshot) = saved {
                this.page.saved(snapshot, &application);
                this.dialog.close();
            }
        });
    }

    fn confirm_delete(self: &Rc<Self>) {
        let alert = adw::AlertDialog::builder()
            .heading("Delete this value?")
            .body("Other profile and registry values will be kept.")
            .close_response("cancel")
            .build();
        alert.add_response("cancel", "Cancel");
        alert.add_response("delete", "Delete Value");
        alert.set_response_appearance("delete", adw::ResponseAppearance::Destructive);
        let this = self.clone();
        glib::spawn_future_local(async move {
            if alert.choose_future(Some(&this.dialog)).await == "delete" {
                this.submit(true);
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(application: &str, key: &str, data: &[u8]) -> Entry {
        Entry {
            application: application.into(),
            key: key.into(),
            data: to_hex(data),
        }
    }

    #[test]
    fn profile_rows_summarize_text_and_binary_values() {
        let text = entry_row(false, &entry("App", "Text", b"caf\xe9\0")).unwrap();
        assert_eq!(
            (text.name.as_str(), text.summary.as_str()),
            ("App · Text", "café")
        );
        let binary = entry_row(false, &entry("App", "", &[1, 2, 0, 3])).unwrap();
        assert_eq!(
            (binary.name.as_str(), binary.summary.as_str()),
            ("App · (Default)", "4 bytes: 01020003")
        );
        let lines = entry_row(false, &entry("App", "Lines", b"a\nb\0")).unwrap();
        assert_eq!(lines.summary, "a ↵ b");
    }

    #[test]
    fn registry_rows_show_typed_values_only() {
        assert!(entry_row(true, &entry("hkcu", "k:", b"\0")).is_none());
        let dword = entry_row(true, &entry("hkcu", "v:count", &[4, 0, 0, 0, 42, 0, 0, 0])).unwrap();
        assert_eq!(
            (dword.name.as_str(), dword.summary.as_str()),
            ("count", "REG_DWORD (Little Endian) · DWORD: 42")
        );
        let text = entry_row(true, &entry("hkcu", "v:", b"\x01\0\0\0hi\0")).unwrap();
        assert_eq!(
            (text.name.as_str(), text.summary.as_str()),
            ("(Default)", "REG_SZ · hi")
        );
        let short = entry_row(true, &entry("hkcu", "v:x", &[1, 2])).unwrap();
        assert_eq!(short.summary, "Incomplete type header: 0102");
        let unknown = entry_row(true, &entry("hkcu", "v:y", &[99, 0, 0, 0])).unwrap();
        assert_eq!(unknown.summary, "Type 99 · 0 bytes: ");
    }

    #[test]
    fn plain_values_convert_between_representations() {
        assert_eq!(plain_data(1, "café", true).unwrap(), b"caf\xe9\0");
        assert!(plain_data(1, "a\0b", true).is_err());
        assert_eq!(plain_data(2, " 42 ", false).unwrap(), [42, 0, 0, 0]);
        assert!(plain_data(2, "-1", false).is_err());
        assert_eq!(plain_text(1, b"abc").unwrap(), ("abc".into(), Some(false)));
        assert!(plain_text(1, b"a\0b\0").is_err());
        assert!(plain_text(2, b"abc").is_err());
        assert_eq!(plain_text(0, &[0xff]).unwrap().0, "ff");
    }

    #[test]
    fn new_registry_names_are_folded_and_prefixed() {
        assert_eq!(
            registry_names(None, "HKCU\\Software\\Fixture", "Count").unwrap(),
            ("hkcu\\software\\fixture".into(), "v:count".into())
        );
        let existing = entry("hkcu", "v:x", b"");
        assert_eq!(
            registry_names(Some(&existing), "ignored", "ignored").unwrap(),
            ("hkcu".into(), "v:x".into())
        );
        assert!(registry_names(None, "a\0", "b").is_err());
    }

    #[test]
    fn file_profiles_use_file_requests() {
        let file = Source::File {
            path: "C:\\APP\\APP.INI".into(),
            role: "profile".into(),
        };
        assert_eq!(file.read().0, "configuration-file-read");
        let (action, fields) = file.write("r", "App", "Key", "00", false);
        assert_eq!(action, "configuration-file-write");
        assert_eq!(fields["application"], "App");
        assert_eq!(fields["data"], "00");
        let user = Source::File {
            path: "C:\\OS2\\OS2.INI".into(),
            role: "user".into(),
        };
        assert_eq!(user.read(), ("configuration-read", json!({"name": "user"})));
        assert_eq!(user.write("r", "App", "Key", "00", true).1["path"], "App");
    }
}
