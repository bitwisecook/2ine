// Public domain. No rights reserved. Wholly AI-authored.
//! A searchable table of one folder inside chosen OS/2 media.
//!
//! Entries come from `browse-media` and `open-os2-source`: `name`, `path`,
//! `directory`, `container`, `size` and `modified`. Folders sort first;
//! containers (ISO images and archives) open like folders.

use std::cell::{Cell, RefCell};
use std::cmp::Ordering;
use std::rc::{Rc, Weak};

use adw::prelude::*;
use gtk::{gio, glib, pango};
use serde_json::Value;

use super::model;

type EntryCallback = Box<dyn Fn(Option<&Value>)>;
type Accept = Box<dyn Fn(&Value) -> bool>;

struct Inner {
    widget: gtk::Box,
    search: gtk::SearchEntry,
    table: gtk::ColumnView,
    store: gio::ListStore,
    filter: gtk::CustomFilter,
    selection: gtk::SingleSelection,
    selected: RefCell<Option<EntryCallback>>,
    activated: RefCell<Option<EntryCallback>>,
    accept: RefCell<Option<Accept>>,
    resetting: Cell<bool>,
}

/// The media folder browser shown above the component tree.
#[derive(Clone)]
pub struct MediaList(Rc<Inner>);

fn entry(object: &glib::Object) -> Option<Value> {
    let row = object.downcast_ref::<glib::BoxedAnyObject>()?;
    row.try_borrow::<Value>().ok().map(|value| value.clone())
}

fn flag(entry: &Value, key: &str) -> bool {
    entry[key].as_bool() == Some(true)
}

fn size_text(entry: &Value) -> String {
    match entry["size"].as_u64() {
        Some(size) if !flag(entry, "directory") => glib::format_size(size).into(),
        _ => "—".into(),
    }
}

fn date_text(entry: &Value) -> String {
    entry["modified"]
        .as_f64()
        .and_then(|stamp| {
            #[allow(clippy::cast_possible_truncation)]
            let seconds = stamp as i64;
            glib::DateTime::from_unix_local(seconds).ok()
        })
        .and_then(|date| date.format("%x %H:%M").ok())
        .map_or_else(|| "—".into(), Into::into)
}

fn icon(entry: &Value) -> &'static str {
    if flag(entry, "directory") {
        "folder-symbolic"
    } else if flag(entry, "container") {
        "package-x-generic-symbolic"
    } else {
        "text-x-generic-symbolic"
    }
}

fn sorter(compare: impl Fn(&Value, &Value) -> Ordering + 'static) -> gtk::CustomSorter {
    gtk::CustomSorter::new(move |first, second| match (entry(first), entry(second)) {
        (Some(first), Some(second)) => compare(&first, &second).into(),
        _ => gtk::Ordering::Equal,
    })
}

fn number(entry: &Value, key: &str) -> f64 {
    entry[key].as_f64().unwrap_or(-1.0)
}

impl MediaList {
    pub fn new() -> Self {
        let search = gtk::SearchEntry::builder()
            .placeholder_text("Search Files")
            .hexpand(true)
            .build();
        search.update_property(&[gtk::accessible::Property::Label("Search Files")]);
        let table = gtk::ColumnView::builder()
            .show_row_separators(true)
            .css_classes(["data-table"])
            .build();
        table.update_property(&[gtk::accessible::Property::Label("Files")]);
        search.set_key_capture_widget(Some(&table));

        let store = gio::ListStore::new::<glib::BoxedAnyObject>();
        let filter = gtk::CustomFilter::new(|_| true);
        let filtered = gtk::FilterListModel::new(Some(store.clone()), Some(filter.clone()));
        let sorters = gtk::MultiSorter::new();
        sorters.append(sorter(|first, second| {
            flag(second, "directory").cmp(&flag(first, "directory"))
        }));
        if let Some(columns) = table.sorter() {
            sorters.append(columns);
        }
        let sorted = gtk::SortListModel::new(Some(filtered), Some(sorters));
        let selection = gtk::SingleSelection::builder()
            .autoselect(false)
            .can_unselect(true)
            .model(&sorted)
            .build();
        selection.set_selected(gtk::INVALID_LIST_POSITION);
        table.set_model(Some(&selection));

        let scroll = gtk::ScrolledWindow::builder()
            .min_content_height(150)
            .max_content_height(260)
            .propagate_natural_height(true)
            .child(&table)
            .build();
        let layout = adw::BreakpointBin::builder()
            .child(&scroll)
            .width_request(180)
            .height_request(150)
            .build();
        let widget = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(8)
            .build();
        widget.append(&search);
        widget.append(&layout);

        let list = Self(Rc::new(Inner {
            widget,
            search,
            table,
            store,
            filter,
            selection,
            selected: RefCell::default(),
            activated: RefCell::default(),
            accept: RefCell::default(),
            resetting: Cell::new(false),
        }));
        list.add_columns(&layout);
        list.connect_signals();
        list
    }

    fn add_columns(&self, layout: &adw::BreakpointBin) {
        let table = &self.0.table;
        let name = gtk::ColumnViewColumn::builder()
            .title("Name")
            .factory(&Self::factory(true, 0.0, |entry| {
                entry["name"].as_str().unwrap_or_default().to_owned()
            }))
            .expand(true)
            .resizable(true)
            .fixed_width(300)
            .sorter(&sorter(|first, second| {
                let key = |entry: &Value| {
                    glib::FilenameCollationKey::from(entry["name"].as_str().unwrap_or_default())
                };
                key(first).cmp(&key(second))
            }))
            .build();
        let size = gtk::ColumnViewColumn::builder()
            .title("Size")
            .factory(&Self::factory(false, 1.0, size_text))
            .resizable(true)
            .fixed_width(100)
            .sorter(&sorter(|first, second| {
                number(first, "size").total_cmp(&number(second, "size"))
            }))
            .build();
        let modified = gtk::ColumnViewColumn::builder()
            .title("Date Modified")
            .factory(&Self::factory(false, 0.0, date_text))
            .resizable(true)
            .fixed_width(170)
            .sorter(&sorter(|first, second| {
                number(first, "modified").total_cmp(&number(second, "modified"))
            }))
            .build();
        for column in [&name, &size, &modified] {
            table.append_column(column);
        }
        table.sort_by_column(Some(&name), gtk::SortType::Ascending);
        if let Ok(condition) = adw::BreakpointCondition::parse("max-width: 600sp") {
            let breakpoint = adw::Breakpoint::new(condition);
            breakpoint.add_setter(&size, "visible", Some(&false.to_value()));
            breakpoint.add_setter(&modified, "visible", Some(&false.to_value()));
            breakpoint.add_setter(&name, "fixed-width", Some(&(-1).to_value()));
            layout.add_breakpoint(breakpoint);
        }
    }

    fn factory(named: bool, align: f32, text: fn(&Value) -> String) -> gtk::SignalListItemFactory {
        let factory = gtk::SignalListItemFactory::new();
        factory.connect_setup(move |_, item| {
            let Some(item) = item.downcast_ref::<gtk::ListItem>() else {
                return;
            };
            let label = gtk::Label::builder()
                .xalign(align)
                .ellipsize(pango::EllipsizeMode::End)
                .hexpand(true)
                .build();
            if named {
                let row = gtk::Box::builder().spacing(8).build();
                row.append(&gtk::Image::new());
                row.append(&label);
                item.set_child(Some(&row));
            } else {
                item.set_child(Some(&label));
            }
        });
        factory.connect_bind(move |_, item| {
            let Some(item) = item.downcast_ref::<gtk::ListItem>() else {
                return;
            };
            let (Some(entry), Some(child)) = (item.item().as_ref().and_then(entry), item.child())
            else {
                return;
            };
            let value = text(&entry);
            let label = if let Some(row) = child.downcast_ref::<gtk::Box>() {
                if let Some(image) = row.first_child().and_downcast::<gtk::Image>() {
                    image.set_icon_name(Some(icon(&entry)));
                }
                row.last_child().and_downcast::<gtk::Label>()
            } else {
                child.downcast::<gtk::Label>().ok()
            };
            if let Some(label) = label {
                label.set_text(&value);
                label.set_tooltip_text(Some(&value));
            }
        });
        factory
    }

    fn connect_signals(&self) {
        let weak = Rc::downgrade(&self.0);
        let filter_weak: Weak<Inner> = weak.clone();
        self.0.filter.set_filter_func(move |object| {
            let (Some(inner), Some(entry)) = (filter_weak.upgrade(), entry(object)) else {
                return true;
            };
            let query = inner.search.text();
            let matching = query.is_empty()
                || model::matches(entry["name"].as_str().unwrap_or_default(), &query);
            matching
                && inner
                    .accept
                    .borrow()
                    .as_ref()
                    .is_none_or(|accept| accept(&entry))
        });
        let changed = weak.clone();
        self.0.search.connect_search_changed(move |_| {
            if let Some(inner) = changed.upgrade() {
                inner.filter.changed(gtk::FilterChange::Different);
            }
        });
        let selected = weak.clone();
        self.0
            .selection
            .connect_selected_item_notify(move |selection| {
                let Some(inner) = selected.upgrade() else {
                    return;
                };
                if inner.resetting.get() {
                    return;
                }
                let entry = selection.selected_item().as_ref().and_then(entry);
                if let Some(callback) = inner.selected.borrow().as_ref() {
                    callback(entry.as_ref());
                }
            });
        self.0.table.connect_activate(move |table, position| {
            let Some(inner) = weak.upgrade() else { return };
            let entry = table
                .model()
                .and_then(|model| model.item(position))
                .as_ref()
                .and_then(entry);
            if let (Some(entry), Some(callback)) = (entry, inner.activated.borrow().as_ref()) {
                callback(Some(&entry));
            }
        });
    }

    pub fn widget(&self) -> &gtk::Box {
        &self.0.widget
    }

    pub fn connect_selected(&self, callback: impl Fn(Option<&Value>) + 'static) {
        self.0.selected.replace(Some(Box::new(callback)));
    }

    pub fn connect_activated(&self, callback: impl Fn(Option<&Value>) + 'static) {
        self.0.activated.replace(Some(Box::new(callback)));
    }

    pub fn set_accept(&self, accept: impl Fn(&Value) -> bool + 'static) {
        self.0.accept.replace(Some(Box::new(accept)));
    }

    /// Re-apply the accept filter after its inputs change.
    pub fn refilter(&self) {
        self.0.filter.changed(gtk::FilterChange::Different);
    }

    /// Show a folder's entries with the search cleared and nothing selected.
    pub fn set_entries(&self, entries: &[Value]) {
        let inner = &self.0;
        inner.resetting.set(true);
        inner.search.set_text("");
        let items: Vec<glib::BoxedAnyObject> = entries
            .iter()
            .cloned()
            .map(glib::BoxedAnyObject::new)
            .collect();
        inner.store.splice(0, inner.store.n_items(), &items);
        inner.filter.changed(gtk::FilterChange::Different);
        inner.selection.set_selected(gtk::INVALID_LIST_POSITION);
        inner.resetting.set(false);
    }
}
