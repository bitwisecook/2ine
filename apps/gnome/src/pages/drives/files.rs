// Public domain. No rights reserved. Wholly AI-authored.
//! A searchable, sortable file tree with Name, Size and Date Modified columns.
//!
//! Folders list before files. Each folder discloses children supplied by the
//! [`FileListDelegate`], which reads folder contents only when the user
//! expands them. Narrow layouts hide the secondary columns.

use std::cell::{Cell, OnceCell, RefCell};
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::rc::Rc;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, gdk, gio, glib};

use super::entry::{self, Entry};
use super::model::{VisibleNode, visible_tree};

/// The owner of a [`FileList`]: supplies folder contents and handles row actions.
pub trait FileListDelegate {
    /// The children shown below a folder entry.
    fn children(&self, entry: &Entry) -> Vec<Rc<Entry>>;
    /// Whether a non-folder entry is listed.
    fn accept(&self, entry: &Entry) -> bool;
    /// Whether an entry shows a progress indicator.
    fn activity(&self, entry: &Entry) -> bool;
    fn selected(&self, entry: Option<Rc<Entry>>);
    fn activated(&self, entry: Rc<Entry>);
    /// A folder row was expanded or collapsed by the user.
    fn expanded(&self, entry: Rc<Entry>, expanded: bool);
    /// Show a context menu for an entry at a point inside `widget`.
    fn context(&self, entry: Rc<Entry>, widget: &gtk::Widget, x: f64, y: f64);
    /// Host files dropped onto a folder row; returns whether they were accepted.
    fn dropped(&self, paths: Vec<PathBuf>, destination: &str) -> bool;
}

/// A row in the tree: an entry and its visible children.
struct Node {
    entry: Rc<Entry>,
    children: gio::ListStore,
}

fn node(object: &glib::Object) -> std::cell::Ref<'_, Node> {
    object
        .downcast_ref::<glib::BoxedAnyObject>()
        .expect("file list rows are nodes")
        .borrow::<Node>()
}

fn row_entry(object: &glib::Object) -> Option<Rc<Entry>> {
    let row = object.downcast_ref::<gtk::TreeListRow>()?;
    Some(node(&row.item()?).entry.clone())
}

fn node_store(nodes: Vec<VisibleNode<Rc<Entry>>>) -> gio::ListStore {
    let store = gio::ListStore::new::<glib::BoxedAnyObject>();
    let items: Vec<glib::BoxedAnyObject> = nodes
        .into_iter()
        .map(|visible| {
            glib::BoxedAnyObject::new(Node {
                entry: visible.data,
                children: node_store(visible.children),
            })
        })
        .collect();
    store.extend_from_slice(&items);
    store
}

/// Host paths of dropped files; None when any file is not available locally.
pub fn dropped_paths(value: &glib::Value) -> Option<Vec<PathBuf>> {
    let files = value.get::<gdk::FileList>().ok()?;
    files.files().iter().map(FileExt::path).collect()
}

mod imp {
    use super::{
        Cell, CompositeTemplate, Entry, FileListDelegate, HashMap, HashSet, OnceCell, Rc, RefCell,
        gio, glib,
    };
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/drives-file-list.ui")]
    pub struct FileList {
        #[template_child]
        pub search: TemplateChild<gtk::SearchEntry>,
        #[template_child]
        pub table: TemplateChild<gtk::ColumnView>,
        #[template_child]
        pub name_column: TemplateChild<gtk::ColumnViewColumn>,
        #[template_child]
        pub size_column: TemplateChild<gtk::ColumnViewColumn>,
        #[template_child]
        pub modified_column: TemplateChild<gtk::ColumnViewColumn>,

        pub delegate: RefCell<Option<Rc<dyn FileListDelegate>>>,
        pub roots: RefCell<Vec<Rc<Entry>>>,
        /// Paths of expanded folders, restored when rows are rebuilt.
        pub expanded: RefCell<HashSet<String>>,
        pub last_query: RefCell<String>,
        pub root_store: RefCell<Option<gio::ListStore>>,
        pub model: RefCell<Option<gtk::TreeListModel>>,
        pub selection: RefCell<Option<gtk::SingleSelection>>,
        pub sorter: OnceCell<gtk::MultiSorter>,
        pub rebuilding: Cell<bool>,
        /// Bound name cells and the expansion handlers of their rows.
        pub bound: RefCell<HashMap<gtk::ListItem, (gtk::TreeListRow, glib::SignalHandlerId)>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for FileList {
        const NAME: &'static str = "TwoineFileList";
        type Type = super::FileList;
        type ParentType = gtk::Box;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[gtk::template_callbacks]
    impl FileList {
        #[template_callback]
        fn on_search_changed(&self) {
            if self.search.text() != *self.last_query.borrow() {
                self.obj().refresh();
            }
        }
    }

    impl ObjectImpl for FileList {
        fn constructed(&self) {
            self.parent_constructed();
            self.search.set_key_capture_widget(Some(&*self.table));
            self.table.connect_activate(glib::clone!(
                #[weak(rename_to = list)]
                self.obj(),
                move |_, position| list.activate(position)
            ));
        }
    }

    impl WidgetImpl for FileList {}
    impl BoxImpl for FileList {}
}

glib::wrapper! {
    pub struct FileList(ObjectSubclass<imp::FileList>)
        @extends gtk::Box, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget, gtk::Orientable;
}

impl FileList {
    /// Install the delegate and the column cells; `drops` accepts host files on folder rows.
    pub fn setup(&self, delegate: Rc<dyn FileListDelegate>, drops: bool) {
        let imp = self.imp();
        imp.delegate.replace(Some(delegate));
        imp.name_column.set_factory(Some(&self.name_factory(drops)));
        imp.size_column
            .set_factory(Some(&text_factory(1.0, entry::file_size)));
        imp.modified_column
            .set_factory(Some(&text_factory(0.0, entry::file_date)));

        let by = |compare: fn(&Entry, &Entry) -> std::cmp::Ordering| {
            gtk::CustomSorter::new(move |first, second| {
                compare(&node(first).entry, &node(second).entry).into()
            })
        };
        imp.name_column.set_sorter(Some(&by(|first, second| {
            entry::compare_names(&first.name, &second.name)
        })));
        imp.size_column.set_sorter(Some(&by(|first, second| {
            let size = |entry: &Entry| if entry.directory { None } else { entry.size };
            size(first).cmp(&size(second))
        })));
        imp.modified_column.set_sorter(Some(&by(|first, second| {
            first.modified.cmp(&second.modified)
        })));
        let sorter = gtk::MultiSorter::new();
        sorter.append(by(|first, second| second.directory.cmp(&first.directory)));
        if let Some(columns) = imp.table.sorter() {
            sorter.append(columns);
        }
        imp.sorter.set(sorter).ok();
        imp.table
            .sort_by_column(Some(&*imp.name_column), gtk::SortType::Ascending);
        self.refresh();
    }

    fn delegate(&self) -> Option<Rc<dyn FileListDelegate>> {
        self.imp().delegate.borrow().clone()
    }

    /// Show a new folder listing, clearing the search and selection.
    pub fn set_entries(&self, entries: Vec<Rc<Entry>>) {
        let imp = self.imp();
        imp.roots.replace(entries);
        imp.last_query.replace(String::new());
        imp.search.set_text("");
        self.refresh();
        if let Some(selection) = imp.selection.borrow().as_ref() {
            selection.set_selected(gtk::INVALID_LIST_POSITION);
        }
    }

    pub fn selected_entry(&self) -> Option<Rc<Entry>> {
        let selection = self.imp().selection.borrow().clone()?;
        row_entry(&selection.selected_item()?)
    }

    fn visible(&self, roots: &[Rc<Entry>], query: &str) -> Vec<VisibleNode<Rc<Entry>>> {
        let Some(delegate) = self.delegate() else {
            return Vec::new();
        };
        visible_tree(
            roots,
            query,
            &|entry: &Rc<Entry>| entry.name.clone(),
            &|entry: &Rc<Entry>| delegate.children(entry),
            Some(&|entry: &Rc<Entry>| {
                entry.placeholder || entry.directory || delegate.accept(entry)
            }),
        )
    }

    /// Rebuild the rows from the roots, keeping expanded folders and the selection.
    pub fn refresh(&self) {
        let imp = self.imp();
        let Some(sorter) = imp.sorter.get().cloned() else {
            return;
        };
        let selected = self.selected_entry();
        let model = imp.model.borrow().clone();
        if let Some(model) = model
            && imp.last_query.borrow().is_empty()
        {
            let mut expanded = imp.expanded.borrow_mut();
            for index in 0..model.n_items() {
                let Some(row) = model.item(index).and_downcast::<gtk::TreeListRow>() else {
                    continue;
                };
                let Some(item) = row.item() else { continue };
                let path = node(&item).entry.path.clone();
                if row.is_expanded() {
                    expanded.insert(path);
                } else {
                    expanded.remove(&path);
                }
            }
        }
        let query: String = imp.search.text().into();
        imp.last_query.replace(query.clone());
        let roots = imp.roots.borrow().clone();
        let root_store = node_store(self.visible(&roots, &query));
        let sorted_roots = gtk::SortListModel::new(Some(root_store.clone()), Some(sorter.clone()));
        let model = gtk::TreeListModel::new(sorted_roots, false, false, move |item| {
            let children = node(item).children.clone();
            (children.n_items() > 0)
                .then(|| gtk::SortListModel::new(Some(children), Some(sorter.clone())).upcast())
        });
        let selection = gtk::SingleSelection::new(Some(model.clone()));
        selection.set_autoselect(false);
        selection.set_can_unselect(true);
        selection.connect_selected_item_notify(glib::clone!(
            #[weak(rename_to = list)]
            self,
            move |_| {
                if let Some(delegate) = list.delegate() {
                    delegate.selected(list.selected_entry());
                }
            }
        ));
        imp.root_store.replace(Some(root_store));
        imp.model.replace(Some(model.clone()));
        imp.selection.replace(Some(selection.clone()));
        imp.table.set_model(Some(&selection));
        imp.rebuilding.set(true);
        let mut index = 0;
        while index < model.n_items() {
            if let Some(row) = model.item(index).and_downcast::<gtk::TreeListRow>()
                && let Some(item) = row.item()
            {
                let path = node(&item).entry.path.clone();
                if !query.is_empty() || imp.expanded.borrow().contains(&path) {
                    row.set_expanded(true);
                }
            }
            index += 1;
        }
        if let Some(selected) = selected
            && !self.focus(&selected.path)
            && let Some(delegate) = self.delegate()
        {
            delegate.selected(None);
        }
        imp.rebuilding.set(false);
    }

    /// Replace the rows below every folder row for `path` with its current children.
    pub fn update_children(&self, path: &str) {
        let imp = self.imp();
        if !imp.last_query.borrow().is_empty() {
            self.refresh();
            return;
        }
        let Some(root) = imp.root_store.borrow().clone() else {
            return;
        };
        let Some(delegate) = self.delegate() else {
            return;
        };
        let key = glib::casefold(path);
        let mut pending = vec![root];
        while let Some(store) = pending.pop() {
            for object in store.iter::<glib::Object>().flatten() {
                let (entry, children) = {
                    let node = node(&object);
                    (node.entry.clone(), node.children.clone())
                };
                if entry.directory && !entry.placeholder && glib::casefold(&entry.path) == key {
                    let fresh = node_store(self.visible(&delegate.children(&entry), ""));
                    let items: Vec<glib::Object> = fresh.iter::<glib::Object>().flatten().collect();
                    children.splice(0, children.n_items(), &items);
                }
                pending.push(children);
            }
        }
        self.refresh_cells();
    }

    /// Expand the ancestors of `path`, then select and scroll to its row.
    pub fn focus(&self, path: &str) -> bool {
        fn ancestors(
            entries: &[Rc<Entry>],
            path: &str,
            trail: &mut Vec<String>,
            delegate: &dyn FileListDelegate,
        ) -> bool {
            for entry in entries {
                if entry.path == path {
                    return true;
                }
                trail.push(entry.path.clone());
                if ancestors(&delegate.children(entry), path, trail, delegate) {
                    return true;
                }
                trail.pop();
            }
            false
        }
        let imp = self.imp();
        let Some(delegate) = self.delegate() else {
            return false;
        };
        let mut parents = Vec::new();
        if !ancestors(
            &imp.roots.borrow().clone(),
            path,
            &mut parents,
            delegate.as_ref(),
        ) {
            return false;
        }
        let (Some(model), Some(selection)) =
            (imp.model.borrow().clone(), imp.selection.borrow().clone())
        else {
            return false;
        };
        let mut index = 0;
        while index < model.n_items() {
            if let Some(row) = model.item(index).and_downcast::<gtk::TreeListRow>()
                && let Some(item) = row.item()
            {
                let own = node(&item).entry.path.clone();
                if parents.contains(&own) {
                    row.set_expanded(true);
                }
                if own == path {
                    selection.set_selected(index);
                    imp.table
                        .scroll_to(index, None, gtk::ListScrollFlags::FOCUS, None);
                    return true;
                }
            }
            index += 1;
        }
        false
    }

    /// Update progress indicators in the visible name cells.
    pub fn refresh_cells(&self) {
        let items: Vec<gtk::ListItem> = self.imp().bound.borrow().keys().cloned().collect();
        for item in items {
            self.bind_name(&item);
        }
    }

    fn activate(&self, position: u32) {
        let Some(model) = self.imp().model.borrow().clone() else {
            return;
        };
        if let Some(entry) = model.item(position).as_ref().and_then(row_entry)
            && let Some(delegate) = self.delegate()
        {
            delegate.activated(entry);
        }
    }

    fn name_factory(&self, drops: bool) -> gtk::SignalListItemFactory {
        let factory = gtk::SignalListItemFactory::new();
        factory.connect_setup(glib::clone!(
            #[weak(rename_to = list)]
            self,
            move |_, object| {
                let Some(item) = object.downcast_ref::<gtk::ListItem>() else {
                    return;
                };
                let content = gtk::Box::new(gtk::Orientation::Horizontal, 8);
                content.append(&gtk::Image::new());
                content.append(&adw::Spinner::builder().visible(false).build());
                content.append(
                    &gtk::Label::builder()
                        .xalign(0.0)
                        .ellipsize(gtk::pango::EllipsizeMode::End)
                        .hexpand(true)
                        .build(),
                );
                if drops {
                    let target =
                        gtk::DropTarget::new(gdk::FileList::static_type(), gdk::DragAction::COPY);
                    target.connect_drop(glib::clone!(
                        #[weak]
                        list,
                        #[weak]
                        item,
                        #[upgrade_or]
                        false,
                        move |_, value, _, _| list.drop_on(&item, value)
                    ));
                    content.add_controller(target);
                }
                let click = gtk::GestureClick::builder()
                    .button(gdk::BUTTON_SECONDARY)
                    .build();
                click.connect_pressed(glib::clone!(
                    #[weak]
                    list,
                    #[weak]
                    item,
                    move |gesture, _, x, y| {
                        if let Some(widget) = gesture.widget() {
                            list.context(&item, &widget, x, y);
                        }
                    }
                ));
                content.add_controller(click);
                let press = gtk::GestureLongPress::builder().touch_only(true).build();
                press.connect_pressed(glib::clone!(
                    #[weak]
                    list,
                    #[weak]
                    item,
                    move |gesture, x, y| {
                        if let Some(widget) = gesture.widget() {
                            list.context(&item, &widget, x, y);
                        }
                    }
                ));
                content.add_controller(press);
                let expander = gtk::TreeExpander::new();
                expander.set_child(Some(&content));
                item.set_child(Some(&expander));
            }
        ));
        factory.connect_bind(glib::clone!(
            #[weak(rename_to = list)]
            self,
            move |_, object| {
                let Some(item) = object.downcast_ref::<gtk::ListItem>() else {
                    return;
                };
                let Some(row) = item.item().and_downcast::<gtk::TreeListRow>() else {
                    return;
                };
                if let Some(expander) = item.child().and_downcast::<gtk::TreeExpander>() {
                    expander.set_list_row(Some(&row));
                }
                let handler = row.connect_expanded_notify(glib::clone!(
                    #[weak]
                    list,
                    move |row| list.expansion_changed(row)
                ));
                if let Some((previous, handler)) = list
                    .imp()
                    .bound
                    .borrow_mut()
                    .insert(item.clone(), (row, handler))
                {
                    previous.disconnect(handler);
                }
                list.bind_name(item);
            }
        ));
        factory.connect_unbind(glib::clone!(
            #[weak(rename_to = list)]
            self,
            move |_, object| {
                let Some(item) = object.downcast_ref::<gtk::ListItem>() else {
                    return;
                };
                if let Some((row, handler)) = list.imp().bound.borrow_mut().remove(item) {
                    row.disconnect(handler);
                }
                if let Some(expander) = item.child().and_downcast::<gtk::TreeExpander>() {
                    expander.set_list_row(None);
                }
            }
        ));
        factory
    }

    fn bind_name(&self, item: &gtk::ListItem) {
        let Some(entry) = item.item().as_ref().and_then(row_entry) else {
            return;
        };
        let Some(expander) = item.child().and_downcast::<gtk::TreeExpander>() else {
            return;
        };
        let Some(content) = expander.child() else {
            return;
        };
        let Some(image) = content.first_child().and_downcast::<gtk::Image>() else {
            return;
        };
        let Some(spinner) = image.next_sibling() else {
            return;
        };
        let Some(label) = spinner.next_sibling().and_downcast::<gtk::Label>() else {
            return;
        };
        image.set_icon_name(Some(entry.icon()));
        image.set_visible(!entry.placeholder);
        spinner.set_visible(
            self.delegate()
                .is_some_and(|delegate| delegate.activity(&entry)),
        );
        label.set_text(&entry.name);
        if entry.placeholder {
            label.add_css_class("dim-label");
        } else {
            label.remove_css_class("dim-label");
        }
        expander.set_tooltip_text(Some(&entry.name));
    }

    fn expansion_changed(&self, row: &gtk::TreeListRow) {
        let imp = self.imp();
        if imp.rebuilding.get() {
            return;
        }
        let Some(entry) = row_entry(row.upcast_ref()) else {
            return;
        };
        if row.is_expanded() {
            imp.expanded.borrow_mut().insert(entry.path.clone());
        } else {
            imp.expanded.borrow_mut().remove(&entry.path);
        }
        if let Some(delegate) = self.delegate() {
            delegate.expanded(entry, row.is_expanded());
        }
    }

    fn drop_on(&self, item: &gtk::ListItem, value: &glib::Value) -> bool {
        let Some(entry) = item.item().as_ref().and_then(row_entry) else {
            return false;
        };
        if !entry.directory || entry.placeholder {
            return false;
        }
        let (Some(paths), Some(delegate)) = (dropped_paths(value), self.delegate()) else {
            return false;
        };
        delegate.dropped(paths, &entry.path)
    }

    fn context(&self, item: &gtk::ListItem, widget: &gtk::Widget, x: f64, y: f64) {
        if let Some(entry) = item.item().as_ref().and_then(row_entry)
            && let Some(delegate) = self.delegate()
        {
            delegate.context(entry, widget, x, y);
        }
    }
}

/// A factory for a right- or left-aligned text column.
fn text_factory(xalign: f32, text: fn(&Entry) -> String) -> gtk::SignalListItemFactory {
    let factory = gtk::SignalListItemFactory::new();
    factory.connect_setup(move |_, object| {
        if let Some(item) = object.downcast_ref::<gtk::ListItem>() {
            item.set_child(Some(
                &gtk::Label::builder()
                    .xalign(xalign)
                    .ellipsize(gtk::pango::EllipsizeMode::End)
                    .build(),
            ));
        }
    });
    factory.connect_bind(move |_, object| {
        let Some(item) = object.downcast_ref::<gtk::ListItem>() else {
            return;
        };
        let Some(entry) = item.item().as_ref().and_then(row_entry) else {
            return;
        };
        if let Some(label) = item.child().and_downcast::<gtk::Label>() {
            let value = if entry.placeholder {
                String::new()
            } else {
                text(&entry)
            };
            label.set_text(&value);
            label.set_tooltip_text((!value.is_empty()).then_some(value.as_str()));
        }
    });
    factory
}

/// Present a context menu at a point inside `widget`.
pub fn popup_menu(widget: &gtk::Widget, menu: &gio::Menu, x: f64, y: f64) {
    let popover = gtk::PopoverMenu::from_model(Some(menu));
    popover.set_parent(widget);
    popover.set_has_arrow(false);
    #[expect(
        clippy::cast_possible_truncation,
        reason = "pointer positions are small widget coordinates"
    )]
    popover.set_pointing_to(Some(&gdk::Rectangle::new(x as i32, y as i32, 1, 1)));
    popover.connect_closed(|popover| {
        let popover = popover.clone();
        glib::idle_add_local_once(move || popover.unparent());
    });
    popover.popup();
}
