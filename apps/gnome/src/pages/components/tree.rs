// Public domain. No rights reserved. Wholly AI-authored.
//! The component tree: a searchable column view with tri-state checks.
//!
//! Rows are built from [`ComponentModel::visible`] for the current search;
//! each row item holds its node id and its visible children. The model keeps
//! the selection, so rebuilding rows for a search or a status change never
//! changes what is ticked. Expanded rows are remembered by id across rebuilds.

use std::cell::{Cell, RefCell};
use std::collections::HashSet;
use std::rc::{Rc, Weak};

use adw::prelude::*;
use gtk::{gio, glib, pango};

use super::model::{ComponentModel, DEFAULT_EXPANDED, VisibleNode};

/// One row: its node id and the visible children shown beneath it.
struct RowEntry {
    id: String,
    children: gio::ListStore,
}

type Changed = Box<dyn Fn()>;
type OpenSetting = Box<dyn Fn(&str)>;

struct Inner {
    widget: gtk::Box,
    search: gtk::SearchEntry,
    table: gtk::ColumnView,
    model: Rc<RefCell<ComponentModel>>,
    expanded: RefCell<HashSet<String>>,
    last_query: RefCell<String>,
    rows: RefCell<Option<gtk::TreeListModel>>,
    selection: RefCell<Option<gtk::SingleSelection>>,
    bound: RefCell<Vec<(glib::WeakRef<gtk::ListItem>, usize)>>,
    updating: Cell<bool>,
    changed: Changed,
    setting: OpenSetting,
}

/// The "Components and files" tree for one extraction plan.
#[derive(Clone)]
pub struct ComponentTree(Rc<Inner>);

fn row_id(object: &glib::Object) -> Option<String> {
    let object = object
        .downcast_ref::<gtk::TreeListRow>()
        .and_then(gtk::TreeListRow::item)
        .unwrap_or_else(|| object.clone());
    let entry = object.downcast_ref::<glib::BoxedAnyObject>()?;
    entry
        .try_borrow::<RowEntry>()
        .ok()
        .map(|entry| entry.id.clone())
}

fn item_id(item: &gtk::ListItem) -> Option<String> {
    item.item().as_ref().and_then(row_id)
}

fn build_rows(nodes: &[VisibleNode]) -> gio::ListStore {
    let store = gio::ListStore::new::<glib::BoxedAnyObject>();
    for node in nodes {
        store.append(&glib::BoxedAnyObject::new(RowEntry {
            id: node.id.clone(),
            children: build_rows(&node.children),
        }));
    }
    store
}

impl ComponentTree {
    pub fn new(
        model: ComponentModel,
        changed: impl Fn() + 'static,
        setting: impl Fn(&str) + 'static,
    ) -> Self {
        let search = gtk::SearchEntry::builder()
            .placeholder_text("Search Components and Files")
            .hexpand(true)
            .build();
        search.update_property(&[gtk::accessible::Property::Label(
            "Search Components and Files",
        )]);
        let table = gtk::ColumnView::builder()
            .show_row_separators(true)
            .css_classes(["data-table"])
            .build();
        table.update_property(&[gtk::accessible::Property::Label("Components and files")]);
        search.set_key_capture_widget(Some(&table));
        let scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .min_content_height(280)
            .child(&table)
            .build();
        let layout = adw::BreakpointBin::builder()
            .child(&scroll)
            .width_request(180)
            .height_request(280)
            .vexpand(true)
            .build();
        let widget = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(8)
            .vexpand(true)
            .build();
        widget.append(&search);
        widget.append(&layout);
        let mut expanded: HashSet<String> =
            DEFAULT_EXPANDED.iter().map(ToString::to_string).collect();
        expanded.insert(model.root.clone());
        let tree = Self(Rc::new(Inner {
            widget,
            search,
            table,
            model: Rc::new(RefCell::new(model)),
            expanded: RefCell::new(expanded),
            last_query: RefCell::default(),
            rows: RefCell::default(),
            selection: RefCell::default(),
            bound: RefCell::default(),
            updating: Cell::new(false),
            changed: Box::new(changed),
            setting: Box::new(setting),
        }));
        tree.add_columns(&layout);
        let weak = Rc::downgrade(&tree.0);
        tree.0.search.connect_search_changed(move |_| {
            if let Some(inner) = weak.upgrade() {
                Self(inner).refresh();
            }
        });
        tree.refresh();
        tree
    }

    pub fn widget(&self) -> &gtk::Box {
        &self.0.widget
    }

    /// The table, made insensitive while the ball's applications run.
    pub fn table(&self) -> &gtk::ColumnView {
        &self.0.table
    }

    pub fn model(&self) -> &Rc<RefCell<ComponentModel>> {
        &self.0.model
    }

    pub fn expanded(&self) -> HashSet<String> {
        self.0.expanded.borrow().clone()
    }

    fn add_columns(&self, layout: &adw::BreakpointBin) {
        let weak = Rc::downgrade(&self.0);
        let name = gtk::ColumnViewColumn::builder()
            .title("Component")
            .factory(&self.factory(0))
            .expand(true)
            .resizable(true)
            .fixed_width(380)
            .build();
        let status = gtk::ColumnViewColumn::builder()
            .title("Status")
            .factory(&self.factory(1))
            .resizable(true)
            .fixed_width(180)
            .build();
        let size_sorter = gtk::CustomSorter::new(move |first, second| {
            let Some(inner) = weak.upgrade() else {
                return gtk::Ordering::Equal;
            };
            let model = inner.model.borrow();
            let size = |object: &glib::Object| row_id(object).map_or(0, |id| model.size(&id));
            size(first).cmp(&size(second)).into()
        });
        let size = gtk::ColumnViewColumn::builder()
            .title("Size")
            .factory(&self.factory(2))
            .resizable(true)
            .fixed_width(90)
            .sorter(&size_sorter)
            .build();
        for column in [&name, &status, &size] {
            self.0.table.append_column(column);
        }
        if let Ok(condition) = adw::BreakpointCondition::parse("max-width: 600sp") {
            let breakpoint = adw::Breakpoint::new(condition);
            breakpoint.add_setter(&size, "visible", Some(&false.to_value()));
            breakpoint.add_setter(&name, "fixed-width", Some(&(-1).to_value()));
            layout.add_breakpoint(breakpoint);
        }
    }

    fn factory(&self, column: usize) -> gtk::SignalListItemFactory {
        let factory = gtk::SignalListItemFactory::new();
        let weak = Rc::downgrade(&self.0);
        factory.connect_setup(move |_, item| {
            let Some(item) = item.downcast_ref::<gtk::ListItem>() else {
                return;
            };
            match column {
                0 => item.set_child(Some(&Self::name_cell(&weak, item))),
                1 => item.set_child(Some(&gtk::Box::builder().spacing(4).build())),
                _ => item.set_child(Some(
                    &gtk::Label::builder()
                        .xalign(1.0)
                        .ellipsize(pango::EllipsizeMode::End)
                        .build(),
                )),
            }
        });
        let bind = Rc::downgrade(&self.0);
        factory.connect_bind(move |_, item| {
            let (Some(inner), Some(item)) = (bind.upgrade(), item.downcast_ref::<gtk::ListItem>())
            else {
                return;
            };
            let mut bound = inner.bound.borrow_mut();
            if !bound
                .iter()
                .any(|(known, _)| known.upgrade().as_ref() == Some(item))
            {
                bound.push((item.downgrade(), column));
            }
            drop(bound);
            Self(inner).bind(item, column);
        });
        let unbind = Rc::downgrade(&self.0);
        factory.connect_unbind(move |_, item| {
            let (Some(inner), Some(item)) =
                (unbind.upgrade(), item.downcast_ref::<gtk::ListItem>())
            else {
                return;
            };
            inner
                .bound
                .borrow_mut()
                .retain(|(known, _)| known.upgrade().is_some_and(|known| &known != item));
            if column == 0
                && let Some(expander) = item.child().and_downcast::<gtk::TreeExpander>()
            {
                expander.set_list_row(None);
            }
        });
        factory
    }

    fn name_cell(weak: &Weak<Inner>, item: &gtk::ListItem) -> gtk::TreeExpander {
        let check = gtk::CheckButton::new();
        let list_item = item.downgrade();
        let weak = weak.clone();
        check.connect_toggled(move |_| {
            let (Some(inner), Some(item)) = (weak.upgrade(), list_item.upgrade()) else {
                return;
            };
            if inner.updating.get() {
                return;
            }
            let Some(id) = item_id(&item) else { return };
            inner.model.borrow_mut().toggle(&id);
            (inner.changed)();
            Self(inner).refresh_cells();
        });
        let row = gtk::Box::builder().spacing(8).build();
        row.append(&check);
        row.append(&gtk::Image::new());
        row.append(&adw::Spinner::builder().visible(false).build());
        row.append(
            &gtk::Label::builder()
                .xalign(0.0)
                .ellipsize(pango::EllipsizeMode::End)
                .hexpand(true)
                .build(),
        );
        gtk::TreeExpander::builder().child(&row).build()
    }

    fn bind(&self, item: &gtk::ListItem, column: usize) {
        let Some(id) = item_id(item) else { return };
        let Some(child) = item.child() else { return };
        let model = self.0.model.borrow();
        match column {
            0 => {
                let Some(expander) = child.downcast_ref::<gtk::TreeExpander>() else {
                    return;
                };
                expander.set_list_row(item.item().and_downcast::<gtk::TreeListRow>().as_ref());
                let Some(row) = expander.child() else { return };
                let mut widgets =
                    std::iter::successors(row.first_child(), gtk::Widget::next_sibling);
                let (Some(check), Some(image), Some(spinner), Some(label)) = (
                    widgets.next(),
                    widgets.next(),
                    widgets.next(),
                    widgets.next(),
                ) else {
                    return;
                };
                let title = model.title(&id);
                if let Some(check) = check.downcast_ref::<gtk::CheckButton>() {
                    let state = model.state(&id);
                    self.0.updating.set(true);
                    check.set_active(state.active);
                    check.set_inconsistent(state.mixed);
                    check.set_sensitive(state.enabled);
                    check.set_visible(state.visible);
                    check.update_property(&[gtk::accessible::Property::Label(&format!(
                        "Select {title}"
                    ))]);
                    self.0.updating.set(false);
                }
                if let Some(image) = image.downcast_ref::<gtk::Image>() {
                    image.set_icon_name(Some(model.icon(&id)));
                }
                spinner.set_visible(model.active_ids.contains(&id));
                if let Some(label) = label.downcast_ref::<gtk::Label>() {
                    label.set_text(&title);
                    if model.disabled.contains(&id) {
                        label.add_css_class("dim-label");
                    } else {
                        label.remove_css_class("dim-label");
                    }
                }
                expander.set_tooltip_text(Some(&model.tooltip(&id)));
            }
            1 => {
                let Some(cell) = child.downcast_ref::<gtk::Box>() else {
                    return;
                };
                while let Some(widget) = cell.first_child() {
                    cell.remove(&widget);
                }
                if let Some(action) = model.provider_action(&id) {
                    let button = gtk::Button::builder()
                        .label(&action.label)
                        .tooltip_text(&action.tooltip)
                        .css_classes(["flat"])
                        .build();
                    let weak = Rc::downgrade(&self.0);
                    button.connect_clicked(move |_| {
                        if let Some(inner) = weak.upgrade() {
                            (inner.setting)(&action.setting);
                        }
                    });
                    cell.append(&button);
                } else {
                    let status = model.status(&id);
                    let label = gtk::Label::builder()
                        .label(&status)
                        .tooltip_text(&status)
                        .xalign(0.0)
                        .ellipsize(pango::EllipsizeMode::End)
                        .build();
                    cell.append(&label);
                }
            }
            _ => {
                if let Some(label) = child.downcast_ref::<gtk::Label>() {
                    let size = model.size(&id);
                    let text = if size > 0 {
                        glib::format_size(size).to_string()
                    } else {
                        String::new()
                    };
                    label.set_text(&text);
                    label.set_tooltip_text(Some(&text));
                }
            }
        }
    }

    /// Re-render every bound cell from the model.
    pub fn refresh_cells(&self) {
        let bound: Vec<_> = self
            .0
            .bound
            .borrow()
            .iter()
            .filter_map(|(item, column)| Some((item.upgrade()?, *column)))
            .collect();
        for (item, column) in bound {
            if item.item().is_some() {
                self.bind(&item, column);
            }
        }
    }

    fn selected_id(&self) -> Option<String> {
        self.0
            .selection
            .borrow()
            .as_ref()
            .and_then(gtk::SingleSelection::selected_item)
            .as_ref()
            .and_then(row_id)
    }

    /// Rebuild rows for the current search, keeping expansion and selection.
    pub fn refresh(&self) {
        let inner = &self.0;
        let selected = self.selected_id();
        if let Some(rows) = inner.rows.borrow().as_ref()
            && inner.last_query.borrow().is_empty()
        {
            let mut expanded = inner.expanded.borrow_mut();
            for index in 0..rows.n_items() {
                let Some(row) = rows.row(index) else { continue };
                let Some(id) = row_id(row.upcast_ref()) else {
                    continue;
                };
                if row.is_expanded() {
                    expanded.insert(id);
                } else {
                    expanded.remove(&id);
                }
            }
        }
        let query = inner.search.text().to_string();
        inner.last_query.replace(query.clone());
        let visible = inner.model.borrow().visible(&query);
        let sorter = inner.table.sorter();
        let roots = gtk::SortListModel::new(Some(build_rows(&visible)), sorter.clone());
        let rows = gtk::TreeListModel::new(roots, false, false, move |object| {
            let entry = object.downcast_ref::<glib::BoxedAnyObject>()?;
            let entry = entry.try_borrow::<RowEntry>().ok()?;
            (entry.children.n_items() > 0).then(|| {
                gtk::SortListModel::new(Some(entry.children.clone()), sorter.clone()).upcast()
            })
        });
        let selection = gtk::SingleSelection::builder()
            .autoselect(false)
            .can_unselect(true)
            .model(&rows)
            .build();
        selection.set_selected(gtk::INVALID_LIST_POSITION);
        inner.table.set_model(Some(&selection));
        inner.rows.replace(Some(rows.clone()));
        inner.selection.replace(Some(selection));
        let expanded = inner.expanded.borrow().clone();
        let mut index = 0;
        while index < rows.n_items() {
            if let Some(row) = rows.row(index)
                && (!query.is_empty()
                    || row_id(row.upcast_ref()).is_some_and(|id| expanded.contains(&id)))
            {
                row.set_expanded(true);
            }
            index += 1;
        }
        if let Some(selected) = selected {
            self.focus(&selected);
        }
    }

    /// Expand a row's ancestors, then select and scroll to it.
    pub fn focus(&self, id: &str) -> bool {
        let Some(parents) = self.0.model.borrow().ancestors(id) else {
            return false;
        };
        let Some(rows) = self.0.rows.borrow().clone() else {
            return false;
        };
        let mut index = 0;
        while index < rows.n_items() {
            if let Some(row) = rows.row(index)
                && let Some(own) = row_id(row.upcast_ref())
            {
                if parents.contains(&own) {
                    row.set_expanded(true);
                }
                if own == id {
                    if let Some(selection) = self.0.selection.borrow().as_ref() {
                        selection.set_selected(index);
                    }
                    self.0
                        .table
                        .scroll_to(index, None, gtk::ListScrollFlags::FOCUS, None);
                    return true;
                }
            }
            index += 1;
        }
        false
    }

    /// Clear the search and show a component group.
    pub fn focus_group(&self, category: &str) {
        self.0.search.set_text("");
        self.refresh();
        self.focus(category);
    }

    /// Tick a component group and its requirements, then show it.
    pub fn select_group(&self, category: &str) {
        let selected = self.0.model.borrow_mut().select_group(category);
        if selected {
            self.refresh();
            (self.0.changed)();
        }
        self.focus_group(category);
    }
}
