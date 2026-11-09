// Public domain. No rights reserved. Wholly AI-authored.
//! Applications: searchable, sortable launchers with launch, edit, copy and
//! remove actions, and the program editor used by Applications and Install.

pub mod advice;
pub mod choice;
pub mod editor;
pub mod icons;
pub mod order;
mod row;

use std::cell::{Cell, OnceCell, RefCell};
use std::rc::Rc;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, gdk, gio, glib};

use crate::model::Snapshot;
use crate::window::BallWindow;
use order::SortMode;
pub use row::ApplicationRow;

mod imp {
    use super::{
        ApplicationRow, BallWindow, Cell, CompositeTemplate, OnceCell, Rc, RefCell, Snapshot,
        SortMode, gio, glib,
    };
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/applications-page.ui")]
    pub struct ApplicationsPage {
        #[template_child]
        pub advice_slot: TemplateChild<adw::Bin>,
        #[template_child]
        pub search: TemplateChild<gtk::SearchEntry>,
        #[template_child]
        pub list: TemplateChild<gtk::ListBox>,
        #[template_child]
        pub empty: TemplateChild<adw::StatusPage>,
        #[template_child]
        pub caption: TemplateChild<gtk::Label>,
        pub window: glib::WeakRef<BallWindow>,
        pub sort_mode: Cell<SortMode>,
        pub sort_action: OnceCell<gio::SimpleAction>,
        pub rows: RefCell<Vec<ApplicationRow>>,
        /// The snapshot the rows were built from.
        pub rendered: RefCell<Option<Rc<Snapshot>>>,
        pub handlers: RefCell<Vec<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ApplicationsPage {
        const NAME: &'static str = "TwoineApplicationsPage";
        type Type = super::ApplicationsPage;
        type ParentType = adw::Bin;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    #[gtk::template_callbacks]
    impl ApplicationsPage {
        #[template_callback]
        fn on_search_changed(&self) {
            self.obj().filter();
        }

        #[template_callback]
        fn on_row_selected(&self) {
            self.obj().update_actions();
        }
    }

    impl ObjectImpl for ApplicationsPage {
        fn dispose(&self) {
            if let Some(window) = self.window.upgrade() {
                for handler in self.handlers.take() {
                    window.disconnect(handler);
                }
            }
        }
    }

    impl WidgetImpl for ApplicationsPage {}
    impl BinImpl for ApplicationsPage {}
}

glib::wrapper! {
    pub struct ApplicationsPage(ObjectSubclass<imp::ApplicationsPage>)
        @extends adw::Bin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

pub fn page(window: &BallWindow) -> gtk::Widget {
    ApplicationsPage::new(window).upcast()
}

impl ApplicationsPage {
    pub fn new(window: &BallWindow) -> Self {
        let page: Self = glib::Object::new();
        let imp = page.imp();
        imp.window.set(Some(window));
        imp.sort_mode.set(SortMode::parse(
            window.preferences().application_sort.as_deref(),
        ));
        page.setup_actions();
        page.setup_list();
        let handlers = vec![
            window.connect_snapshot_changed(glib::clone!(
                #[weak]
                page,
                move |_| page.refresh()
            )),
            window.connect_sessions_changed(glib::clone!(
                #[weak]
                page,
                move |_| {
                    page.update_sessions();
                    page.update_actions();
                }
            )),
            window.connect_busy_notify(glib::clone!(
                #[weak]
                page,
                move |_| {
                    page.update_advice();
                    page.update_actions();
                }
            )),
        ];
        imp.handlers.replace(handlers);
        page.refresh();
        page
    }

    fn window(&self) -> Option<BallWindow> {
        self.imp().window.upgrade()
    }

    fn setup_actions(&self) {
        let group = gio::SimpleActionGroup::new();
        let sort = gio::SimpleAction::new_stateful(
            "sort",
            Some(glib::VariantTy::STRING),
            &self.imp().sort_mode.get().as_str().to_variant(),
        );
        sort.connect_change_state(glib::clone!(
            #[weak(rename_to = page)]
            self,
            move |_, value| {
                if let Some(mode) = value.and_then(glib::Variant::str) {
                    page.set_sort_mode(SortMode::parse(Some(mode)));
                }
            }
        ));
        group.add_action(&sort);
        let copy = gio::SimpleAction::new("copy", Some(glib::VariantTy::STRING));
        copy.connect_activate(glib::clone!(
            #[weak(rename_to = page)]
            self,
            move |_, value| {
                if let Some(name) = value.and_then(glib::Variant::str) {
                    page.copy_program(name);
                }
            }
        ));
        group.add_action(&copy);
        self.insert_action_group("applications", Some(&group));
        let _ = self.imp().sort_action.set(sort);
    }

    fn setup_list(&self) {
        let list = &self.imp().list;
        list.set_filter_func(glib::clone!(
            #[weak(rename_to = page)]
            self,
            #[upgrade_or]
            true,
            move |row| page.row_matches(row)
        ));
        list.set_sort_func(glib::clone!(
            #[weak(rename_to = page)]
            self,
            #[upgrade_or]
            gtk::Ordering::Equal,
            move |left, right| page.compare(left, right)
        ));
        list.connect_row_activated(glib::clone!(
            #[weak(rename_to = page)]
            self,
            move |_, row| {
                if let (Some(window), Some(row)) =
                    (page.window(), row.downcast_ref::<ApplicationRow>())
                {
                    ActionGroupExt::activate_action(
                        &window,
                        "launch-application",
                        Some(&row.application_name().to_variant()),
                    );
                }
            }
        ));
        let shortcuts = gtk::ShortcutController::new();
        shortcuts.add_shortcut(gtk::Shortcut::new(
            gtk::ShortcutTrigger::parse_string("Delete|KP_Delete"),
            Some(gtk::CallbackAction::new(glib::clone!(
                #[weak(rename_to = page)]
                self,
                #[upgrade_or]
                glib::Propagation::Proceed,
                move |_, _| {
                    page.remove_selected();
                    glib::Propagation::Stop
                }
            ))),
        ));
        shortcuts.add_shortcut(gtk::Shortcut::new(
            gtk::ShortcutTrigger::parse_string("<Control>c"),
            Some(gtk::CallbackAction::new(glib::clone!(
                #[weak(rename_to = page)]
                self,
                #[upgrade_or]
                glib::Propagation::Proceed,
                move |_, _| {
                    if let Some(text) = page.copy_selected_text() {
                        page.clipboard().set_text(&text);
                        return glib::Propagation::Stop;
                    }
                    glib::Propagation::Proceed
                }
            ))),
        ));
        list.add_controller(shortcuts);
    }

    /// Rebuild the launcher rows from the selected ball's snapshot.
    pub fn refresh(&self) {
        let Some(window) = self.window() else { return };
        let imp = self.imp();
        let snapshot = window.snapshot();
        let unchanged = match (&*imp.rendered.borrow(), &snapshot) {
            (Some(rendered), Some(current)) => Rc::ptr_eq(rendered, current),
            (None, None) => true,
            _ => false,
        };
        if unchanged && !imp.rows.borrow().is_empty() {
            self.update_advice();
            return;
        }
        imp.rendered.replace(snapshot.clone());
        self.update_advice();
        let selected = self.selected_name();
        let list = &imp.list;
        for row in imp.rows.take() {
            list.remove(&row);
        }
        let applications = snapshot
            .as_ref()
            .map(|snapshot| snapshot.manifest.applications.clone())
            .unwrap_or_default();
        let mut rows = Vec::with_capacity(applications.len());
        for (name, application) in &applications {
            let row = ApplicationRow::new(name, application);
            Self::attach_row(&window, &row);
            list.append(&row);
            rows.push(row);
        }
        imp.rows.replace(rows);
        let page = self.downgrade();
        for row in imp.rows.borrow().iter() {
            let target = row.downgrade();
            let page = page.clone();
            icons::load_application_icon(
                &window,
                &row.application_name(),
                &row.icon(),
                move |available| {
                    if let (Some(page), Some(row)) = (page.upgrade(), target.upgrade()) {
                        page.icon_loaded(&row, available);
                    }
                },
            );
        }
        if applications.is_empty() {
            imp.empty.set_title("A Home for Your Applications");
            imp.empty.set_description(Some(
                "Install software from your OS/2 media, then add its executable to keep it here.",
            ));
            imp.empty.set_icon_name(Some("view-app-grid-symbolic"));
        } else {
            imp.empty.set_title("No Matching Applications");
            imp.empty.set_description(None);
            imp.empty.set_icon_name(Some("system-search-symbolic"));
        }
        if let Some(name) = selected
            && let Some(row) = imp
                .rows
                .borrow()
                .iter()
                .find(|row| row.application_name() == name)
        {
            list.select_row(Some(row));
        }
        self.filter();
        self.update_sessions();
        self.update_actions();
    }

    fn attach_row(window: &BallWindow, row: &ApplicationRow) {
        let name = row.application_name();
        row.sessions_button().connect_clicked(glib::clone!(
            #[weak]
            window,
            move |_| window.show_application_sessions(&name)
        ));
        let gesture = gtk::GestureClick::builder()
            .button(gdk::BUTTON_SECONDARY)
            .build();
        gesture.connect_pressed(glib::clone!(
            #[weak]
            window,
            #[weak]
            row,
            move |gesture, _, x, y| {
                if window.busy() {
                    return;
                }
                gesture.set_state(gtk::EventSequenceState::Claimed);
                popup_menu(row.upcast_ref(), &row.menu(), x, y);
            }
        ));
        row.add_controller(gesture);
    }

    fn update_advice(&self) {
        let Some(window) = self.window() else { return };
        self.imp()
            .advice_slot
            .set_child(Some(&advice::dependency_advice_box(&window)));
    }

    /// Show each launcher's active session count.
    pub fn update_sessions(&self) {
        let Some(window) = self.window() else { return };
        let Some(ball) = window.ball() else { return };
        let active = window.sessions().borrow().active(Some(&ball));
        for row in self.imp().rows.borrow().iter() {
            let name = row.application_name();
            row.set_session_count(active.iter().filter(|session| session.name == name).count());
        }
    }

    /// Enable Launch Selected and Edit Selected for the current selection.
    pub fn update_actions(&self) {
        let Some(window) = self.window() else { return };
        let selected = self.selected_name().is_some();
        let ball = window.ball();
        let ready = selected
            && window.snapshot().is_some()
            && !window.operation_busy()
            && ball
                .as_deref()
                .is_some_and(|ball| !window.component_active(ball));
        let (can_start, idle) = ball.as_deref().map_or((false, false), |ball| {
            let sessions = window.sessions().borrow();
            (
                sessions.can_start(ball, false),
                sessions.active(Some(ball)).is_empty(),
            )
        });
        window.set_action_enabled("launch-selected", ready && can_start);
        window.set_action_enabled("edit-selected", ready && idle);
    }

    fn compare(&self, left: &gtk::ListBoxRow, right: &gtk::ListBoxRow) -> gtk::Ordering {
        let mode = self.imp().sort_mode.get();
        let key = |row: &gtk::ListBoxRow| {
            row.downcast_ref::<ApplicationRow>().map(|row| {
                order::key(
                    &row.application_name(),
                    &row.program(),
                    row.has_icon(),
                    mode,
                )
            })
        };
        key(left).cmp(&key(right)).into()
    }

    pub fn sort_mode(&self) -> SortMode {
        self.imp().sort_mode.get()
    }

    /// Change and remember the launcher order.
    pub fn set_sort_mode(&self, mode: SortMode) {
        let imp = self.imp();
        if let Some(action) = imp.sort_action.get()
            && action.state().as_ref().and_then(glib::Variant::str) != Some(mode.as_str())
        {
            action.set_state(&mode.as_str().to_variant());
        }
        if imp.sort_mode.replace(mode) == mode {
            return;
        }
        if let Some(window) = self.window() {
            window.update_preferences(|preferences| {
                preferences.application_sort = Some(mode.as_str().to_owned());
            });
        }
        imp.list.invalidate_sort();
    }

    fn icon_loaded(&self, row: &ApplicationRow, available: bool) {
        row.set_has_icon(available);
        if self.imp().sort_mode.get() == SortMode::Icons {
            self.imp().list.invalidate_sort();
        }
    }

    fn row_matches(&self, row: &gtk::ListBoxRow) -> bool {
        row.downcast_ref::<ApplicationRow>()
            .is_none_or(|row| order::matches(&row.search_text(), &self.imp().search.text()))
    }

    /// Apply the search to the list and show the empty state when nothing matches.
    pub fn filter(&self) {
        let imp = self.imp();
        imp.list.invalidate_filter();
        let rows = imp.rows.borrow();
        let visible: Vec<&ApplicationRow> = rows
            .iter()
            .filter(|row| self.row_matches(row.upcast_ref()))
            .collect();
        if let Some(selected) = imp.list.selected_row()
            && !visible
                .iter()
                .any(|row| row.upcast_ref::<gtk::ListBoxRow>() == &selected)
        {
            imp.list.select_row(None::<&gtk::ListBoxRow>);
        }
        imp.list.set_visible(!visible.is_empty());
        imp.empty.set_visible(visible.is_empty());
        imp.search.set_visible(!rows.is_empty());
        imp.caption.set_visible(!rows.is_empty());
    }

    /// The selected launcher's name.
    pub fn selected_name(&self) -> Option<String> {
        self.imp()
            .list
            .selected_row()
            .and_then(|row| row.downcast::<ApplicationRow>().ok())
            .map(|row| row.application_name())
    }

    /// The tab-separated clipboard text for the selected launcher.
    pub fn copy_selected_text(&self) -> Option<String> {
        let name = self.selected_name()?;
        let snapshot = self.window()?.snapshot()?;
        Some(order::copy_text(
            &snapshot.manifest.applications,
            [name.as_str()],
        ))
        .filter(|text| !text.is_empty())
    }

    /// Copy a launcher's program path.
    fn copy_program(&self, name: &str) {
        let Some(snapshot) = self.window().and_then(|window| window.snapshot()) else {
            return;
        };
        if let Some(application) = snapshot.manifest.applications.get(name) {
            self.clipboard().set_text(&application.program);
        }
    }

    /// Remove the selected launcher; installed files are kept. Returns whether
    /// removal was requested.
    pub fn remove_selected(&self) -> bool {
        let Some(window) = self.window() else {
            return false;
        };
        let Some(name) = self.selected_name() else {
            return false;
        };
        if window.operation_busy() {
            return false;
        }
        window.execute(vec!["remove-app".into(), name]);
        true
    }
}

/// Present a context menu model at a pointer position inside a widget.
fn popup_menu(widget: &gtk::Widget, menu: &gio::Menu, x: f64, y: f64) {
    let popover = gtk::PopoverMenu::from_model(Some(menu));
    popover.set_parent(widget);
    popover.set_has_arrow(false);
    #[allow(clippy::cast_possible_truncation)]
    popover.set_pointing_to(Some(&gdk::Rectangle::new(x as i32, y as i32, 1, 1)));
    popover.connect_closed(|popover| {
        let popover = popover.clone();
        glib::idle_add_local_once(move || popover.unparent());
    });
    popover.popup();
}
