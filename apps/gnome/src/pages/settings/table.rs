// Public domain. No rights reserved. Wholly AI-authored.
//! A searchable, sortable table or tree of settings rows.
//!
//! Rows are adapter-owned values of `T`. A search keeps rows whose text
//! contains the query's characters in order, together with their ancestors.
//! Below 600sp only the first column and compact columns remain visible.

use std::cell::{Cell, RefCell};
use std::collections::HashSet;
use std::rc::Rc;

use adw::prelude::*;
use gtk::{glib, pango};

type Text<T> = Rc<dyn Fn(&T) -> String>;
type Callback<T> = Rc<dyn Fn(Option<&T>)>;
type Action<T> = Rc<dyn Fn(&T)>;

/// One table column.
pub struct Column<T> {
    pub title: &'static str,
    pub text: Text<T>,
    pub width: i32,
    pub expand: bool,
    /// Remains visible in narrow layouts.
    pub compact: bool,
    pub sort: Option<Text<T>>,
    /// A flat button labelled with `text` that calls this action for its row.
    pub action: Option<Action<T>>,
}

impl<T> Column<T> {
    pub fn new(title: &'static str, text: impl Fn(&T) -> String + 'static) -> Self {
        Self {
            title,
            text: Rc::new(text),
            width: 160,
            expand: false,
            compact: false,
            sort: None,
            action: None,
        }
    }

    pub fn width(mut self, width: i32, expand: bool) -> Self {
        self.width = width;
        self.expand = expand;
        self
    }

    pub fn compact(mut self) -> Self {
        self.compact = true;
        self
    }

    pub fn sorted(mut self, key: impl Fn(&T) -> String + 'static) -> Self {
        self.sort = Some(Rc::new(key));
        self
    }

    pub fn action(mut self, action: impl Fn(&T) + 'static) -> Self {
        self.action = Some(Rc::new(action));
        self
    }
}

/// A row and its children.
#[derive(Debug, Clone)]
pub struct Tree<T> {
    pub data: T,
    pub children: Vec<Tree<T>>,
}

impl<T> Tree<T> {
    pub fn leaf(data: T) -> Self {
        Self {
            data,
            children: Vec::new(),
        }
    }
}

/// Options for [`Table::new`].
pub struct Options<T> {
    pub title: &'static str,
    pub search_hint: &'static str,
    pub identity: Text<T>,
    pub searchable: Text<T>,
    pub tooltip: Option<Text<T>>,
    pub single_click: bool,
    pub height: i32,
}

/// Remove case and diacritics for searching.
pub fn folded(value: &str) -> String {
    glib::normalize(glib::casefold(value), glib::NormalizeMode::All)
        .chars()
        .filter(|character| {
            !matches!(u32::from(*character),
                0x0300..=0x036F | 0x1AB0..=0x1AFF | 0x1DC0..=0x1DFF | 0x20D0..=0x20FF | 0xFE20..=0xFE2F)
        })
        .collect()
}

/// Whether `text` contains every character of `query` in order.
pub fn matches(text: &str, query: &str) -> bool {
    let text = folded(text);
    let mut remaining = text.chars();
    folded(query)
        .chars()
        .all(|wanted| remaining.any(|candidate| candidate == wanted))
}

/// The rows visible for a query: matches, their descendants and their ancestors.
pub fn visible_tree<T: Clone>(
    roots: &[Tree<T>],
    query: &str,
    text: &dyn Fn(&T) -> String,
) -> Vec<Tree<T>> {
    fn visit<T: Clone>(
        node: &Tree<T>,
        query: &str,
        inherited: bool,
        text: &dyn Fn(&T) -> String,
    ) -> Option<Tree<T>> {
        let matching = inherited || query.is_empty() || matches(&text(&node.data), query);
        let children: Vec<_> = node
            .children
            .iter()
            .filter_map(|child| visit(child, query, matching, text))
            .collect();
        (matching || !children.is_empty()).then(|| Tree {
            data: node.data.clone(),
            children,
        })
    }
    roots
        .iter()
        .filter_map(|root| visit(root, query, false, text))
        .collect()
}

struct Item<T> {
    data: T,
    children: Vec<Rc<Item<T>>>,
}

fn items<T: Clone>(nodes: Vec<Tree<T>>) -> Vec<Rc<Item<T>>> {
    nodes
        .into_iter()
        .map(|node| {
            Rc::new(Item {
                data: node.data,
                children: items(node.children),
            })
        })
        .collect()
}

fn store<T: 'static>(nodes: &[Rc<Item<T>>]) -> gtk::gio::ListStore {
    let store = gtk::gio::ListStore::new::<glib::BoxedAnyObject>();
    for node in nodes {
        store.append(&glib::BoxedAnyObject::new(node.clone()));
    }
    store
}

fn item_of<T: 'static>(object: &glib::Object) -> Option<Rc<Item<T>>> {
    let object = match object.downcast_ref::<gtk::TreeListRow>() {
        Some(row) => row.item()?,
        None => object.clone(),
    };
    let boxed = object.downcast::<glib::BoxedAnyObject>().ok()?;
    let item = boxed.try_borrow::<Rc<Item<T>>>().ok()?.clone();
    Some(item)
}

pub struct Table<T: 'static> {
    pub widget: gtk::Box,
    pub search: gtk::SearchEntry,
    pub view: gtk::ColumnView,
    roots: RefCell<Vec<Tree<T>>>,
    model: RefCell<Option<gtk::SortListModel>>,
    selection: gtk::SingleSelection,
    sorter: gtk::TreeListRowSorter,
    identity: Text<T>,
    searchable: Text<T>,
    expanded: RefCell<HashSet<String>>,
    rebuilding: Cell<bool>,
    last_query: RefCell<String>,
    on_selected: RefCell<Option<Callback<T>>>,
    on_activated: RefCell<Option<Action<T>>>,
}

impl<T: Clone + 'static> Table<T> {
    pub fn new(columns: Vec<Column<T>>, options: Options<T>) -> Rc<Self> {
        let widget = gtk::Box::builder()
            .orientation(gtk::Orientation::Vertical)
            .spacing(8)
            .vexpand(true)
            .build();
        let search = gtk::SearchEntry::builder()
            .placeholder_text(options.search_hint)
            .hexpand(true)
            .build();
        search.update_property(&[gtk::accessible::Property::Label(options.search_hint)]);
        widget.append(&search);
        let view = gtk::ColumnView::builder()
            .single_click_activate(options.single_click)
            .show_row_separators(true)
            .reorderable(false)
            .build();
        view.add_css_class("data-table");
        view.update_property(&[gtk::accessible::Property::Label(options.title)]);
        search.set_key_capture_widget(Some(&view));
        let selection = gtk::SingleSelection::builder()
            .autoselect(false)
            .can_unselect(true)
            .build();
        let table = Rc::new(Self {
            widget,
            search,
            view,
            roots: RefCell::default(),
            model: RefCell::default(),
            selection,
            sorter: gtk::TreeListRowSorter::new(None::<gtk::Sorter>),
            identity: options.identity,
            searchable: options.searchable,
            expanded: RefCell::default(),
            rebuilding: Cell::new(false),
            last_query: RefCell::default(),
            on_selected: RefCell::default(),
            on_activated: RefCell::default(),
        });
        let mut view_columns = Vec::new();
        for (index, definition) in columns.into_iter().enumerate() {
            let column = gtk::ColumnViewColumn::builder()
                .title(definition.title)
                .factory(&Self::factory(index, &definition, options.tooltip.clone()))
                .resizable(true)
                .expand(definition.expand)
                .fixed_width(definition.width)
                .build();
            if let Some(key) = definition.sort.clone() {
                column.set_sorter(Some(&gtk::CustomSorter::new(move |first, second| {
                    match (item_of::<T>(first), item_of::<T>(second)) {
                        (Some(first), Some(second)) => {
                            key(&first.data).cmp(&key(&second.data)).into()
                        }
                        _ => gtk::Ordering::Equal,
                    }
                })));
            }
            table.view.append_column(&column);
            view_columns.push((column, definition.compact));
        }
        table.sorter.set_sorter(table.view.sorter().as_ref());
        if let Some((first, _)) = view_columns.first()
            && first.sorter().is_some()
        {
            table
                .view
                .sort_by_column(Some(first), gtk::SortType::Ascending);
        }
        table.view.set_model(Some(&table.selection));
        let scroll = gtk::ScrolledWindow::builder()
            .vexpand(true)
            .min_content_height(options.height)
            .child(&table.view)
            .build();
        let layout = adw::BreakpointBin::builder()
            .child(&scroll)
            .vexpand(true)
            .width_request(180)
            .height_request(options.height)
            .build();
        let narrow = adw::Breakpoint::new(
            adw::BreakpointCondition::parse("max-width: 600sp").expect("valid condition"),
        );
        for (column, compact) in view_columns.iter().skip(1) {
            if !compact {
                narrow.add_setter(column, "visible", Some(&false.to_value()));
            }
        }
        if let Some((first, _)) = view_columns.first() {
            narrow.add_setter(first, "fixed-width", Some(&(-1).to_value()));
        }
        layout.add_breakpoint(narrow);
        table.widget.append(&layout);

        let weak = Rc::downgrade(&table);
        table.search.connect_search_changed(move |_| {
            if let Some(table) = weak.upgrade() {
                table.refresh();
            }
        });
        let weak = Rc::downgrade(&table);
        table.selection.connect_selected_item_notify(move |_| {
            if let Some(table) = weak.upgrade()
                && !table.rebuilding.get()
            {
                table.emit_selected();
            }
        });
        let weak = Rc::downgrade(&table);
        table.view.connect_activate(move |_, position| {
            if let Some(table) = weak.upgrade()
                && let Some(object) = table.selection.item(position)
                && let Some(item) = item_of::<T>(&object)
            {
                let callback = table.on_activated.borrow().clone();
                if let Some(callback) = callback {
                    callback(&item.data);
                }
            }
        });
        table.refresh();
        table
    }

    fn factory(
        index: usize,
        definition: &Column<T>,
        tooltip: Option<Text<T>>,
    ) -> gtk::SignalListItemFactory {
        let factory = gtk::SignalListItemFactory::new();
        let action = definition.action.clone();
        factory.connect_setup(move |_, item| {
            let Some(item) = item.downcast_ref::<gtk::ListItem>() else {
                return;
            };
            if index == 0 {
                let label = gtk::Label::builder()
                    .xalign(0.0)
                    .ellipsize(pango::EllipsizeMode::End)
                    .hexpand(true)
                    .build();
                item.set_child(Some(&gtk::TreeExpander::builder().child(&label).build()));
            } else if let Some(action) = action.clone() {
                let button = gtk::Button::builder().valign(gtk::Align::Center).build();
                button.add_css_class("flat");
                let cell = item.downgrade();
                button.connect_clicked(move |_| {
                    if let Some(cell) = cell.upgrade()
                        && let Some(object) = cell.item()
                        && let Some(row) = item_of::<T>(&object)
                    {
                        action(&row.data);
                    }
                });
                item.set_child(Some(&button));
            } else {
                item.set_child(Some(
                    &gtk::Label::builder()
                        .xalign(0.0)
                        .ellipsize(pango::EllipsizeMode::End)
                        .build(),
                ));
            }
        });
        let text = definition.text.clone();
        factory.connect_bind(move |_, item| {
            let Some(item) = item.downcast_ref::<gtk::ListItem>() else {
                return;
            };
            let Some(object) = item.item() else { return };
            let Some(row) = item_of::<T>(&object) else {
                return;
            };
            let value = text(&row.data);
            let Some(child) = item.child() else { return };
            if let Some(expander) = child.downcast_ref::<gtk::TreeExpander>() {
                expander.set_list_row(object.downcast_ref::<gtk::TreeListRow>());
                if let Some(label) = expander.child().and_downcast::<gtk::Label>() {
                    label.set_text(&value);
                }
                let tip = tooltip
                    .as_ref()
                    .map_or_else(|| value.clone(), |tooltip| tooltip(&row.data));
                expander.set_tooltip_text(Some(&tip));
            } else if let Some(button) = child.downcast_ref::<gtk::Button>() {
                button.set_label(&value);
            } else if let Some(label) = child.downcast_ref::<gtk::Label>() {
                label.set_text(&value);
                label.set_tooltip_text(Some(&value));
            }
        });
        factory.connect_unbind(|_, item| {
            if let Some(item) = item.downcast_ref::<gtk::ListItem>()
                && let Some(expander) = item.child().and_downcast::<gtk::TreeExpander>()
            {
                expander.set_list_row(None);
            }
        });
        factory
    }

    pub fn connect_selected(&self, callback: impl Fn(Option<&T>) + 'static) {
        self.on_selected.replace(Some(Rc::new(callback)));
    }

    pub fn connect_activated(&self, callback: impl Fn(&T) + 'static) {
        self.on_activated.replace(Some(Rc::new(callback)));
    }

    fn emit_selected(&self) {
        let callback = self.on_selected.borrow().clone();
        if let Some(callback) = callback {
            let selected = self.selected();
            callback(selected.as_ref());
        }
    }

    pub fn set_rows(&self, rows: Vec<Tree<T>>) {
        self.roots.replace(rows);
        self.refresh();
    }

    pub fn selected(&self) -> Option<T> {
        self.selection
            .selected_item()
            .and_then(|object| item_of::<T>(&object))
            .map(|item| item.data.clone())
    }

    fn rows(&self) -> impl Iterator<Item = (u32, gtk::TreeListRow)> + '_ {
        let mut index = 0;
        std::iter::from_fn(move || {
            let row = self
                .selection
                .item(index)?
                .downcast::<gtk::TreeListRow>()
                .ok()?;
            index += 1;
            Some((index - 1, row))
        })
    }

    fn refresh(&self) {
        let selected = self.selected();
        if self.last_query.borrow().is_empty() {
            let mut expanded = self.expanded.borrow_mut();
            for (_, row) in self.rows() {
                if let Some(item) = row.item().and_then(|object| item_of::<T>(&object)) {
                    let key = (self.identity)(&item.data);
                    if row.is_expanded() {
                        expanded.insert(key);
                    } else {
                        expanded.remove(&key);
                    }
                }
            }
        }
        let query = self.search.text().to_string();
        self.last_query.replace(query.clone());
        let visible = items(visible_tree(
            &self.roots.borrow(),
            &query,
            &*self.searchable,
        ));
        let tree = gtk::TreeListModel::new(store(&visible), false, false, |object| {
            let item = item_of::<T>(object)?;
            (!item.children.is_empty()).then(|| store(&item.children).upcast())
        });
        let model = gtk::SortListModel::new(Some(tree), Some(self.sorter.clone()));
        self.rebuilding.set(true);
        self.selection.set_model(Some(&model));
        self.model.replace(Some(model));
        let mut index = 0;
        while let Some(object) = self.selection.item(index) {
            if let Ok(row) = object.downcast::<gtk::TreeListRow>()
                && let Some(item) = row.item().and_then(|object| item_of::<T>(&object))
                && (!query.is_empty()
                    || self
                        .expanded
                        .borrow()
                        .contains(&(self.identity)(&item.data)))
            {
                row.set_expanded(true);
            }
            index += 1;
        }
        let restored = selected
            .as_ref()
            .is_some_and(|data| self.focus(&(self.identity)(data)));
        self.rebuilding.set(false);
        if selected.is_some() && !restored {
            self.emit_selected();
        }
    }

    /// Select and scroll to the row with `key`, expanding its ancestors.
    pub fn focus(&self, key: &str) -> bool {
        fn ancestors<T>(
            nodes: &[Tree<T>],
            key: &str,
            identity: &dyn Fn(&T) -> String,
            path: &mut Vec<String>,
        ) -> bool {
            for node in nodes {
                let own = identity(&node.data);
                if own == key {
                    return true;
                }
                path.push(own);
                if ancestors(&node.children, key, identity, path) {
                    return true;
                }
                path.pop();
            }
            false
        }
        let mut parents = Vec::new();
        if !ancestors(&self.roots.borrow(), key, &*self.identity, &mut parents) {
            return false;
        }
        let mut index = 0;
        while let Some(object) = self.selection.item(index) {
            if let Ok(row) = object.downcast::<gtk::TreeListRow>()
                && let Some(item) = row.item().and_then(|object| item_of::<T>(&object))
            {
                let own = (self.identity)(&item.data);
                if parents.contains(&own) {
                    row.set_expanded(true);
                }
                if own == key {
                    self.selection.set_selected(index);
                    self.view.scroll_to(
                        index,
                        None::<&gtk::ColumnViewColumn>,
                        gtk::ListScrollFlags::FOCUS,
                        None,
                    );
                    return true;
                }
            }
            index += 1;
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn search_matches_ordered_characters_without_case_or_accents() {
        assert!(matches("CONFIG.SYS", "cfg"));
        assert!(matches("Café", "cafe"));
        assert!(!matches("abc", "ca"));
        assert!(matches("anything", ""));
    }

    #[test]
    fn search_keeps_ancestors_and_descendants_of_matches() {
        let roots = vec![Tree {
            data: "hkcu".to_owned(),
            children: vec![Tree {
                data: "software".into(),
                children: vec![Tree::leaf("fixture".into())],
            }],
        }];
        let text = |value: &String| value.clone();
        let visible = visible_tree(&roots, "soft", &text);
        assert_eq!(visible.len(), 1);
        assert_eq!(visible[0].children[0].data, "software");
        assert_eq!(visible[0].children[0].children[0].data, "fixture");
        assert!(visible_tree(&roots, "zzz", &text).is_empty());
    }
}
