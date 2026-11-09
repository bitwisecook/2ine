// Public domain. No rights reserved. Wholly AI-authored.

use std::cell::{Cell, OnceCell, RefCell};
use std::collections::{BTreeSet, HashMap};
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::OnceLock;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::glib::subclass::Signal;
use gtk::{CompositeTemplate, glib};

use super::history::HistoryItem;
use crate::engine::Worker;
use crate::model::Snapshot;
use crate::preferences::Preferences;
use crate::sessions::{Sessions, StartupPool};

#[derive(CompositeTemplate, glib::Properties, Default)]
#[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/window.ui")]
#[properties(wrapper_type = super::BallWindow)]
pub struct BallWindow {
    #[template_child]
    pub overlay: TemplateChild<adw::ToastOverlay>,
    #[template_child]
    pub split: TemplateChild<adw::NavigationSplitView>,
    #[template_child]
    pub sidebar: TemplateChild<adw::ToolbarView>,
    #[template_child]
    pub sidebar_menu: TemplateChild<gtk::MenuButton>,
    #[template_child]
    pub ball_list: TemplateChild<gtk::ListBox>,
    #[template_child]
    pub detail: TemplateChild<adw::NavigationPage>,
    #[template_child]
    pub header: TemplateChild<adw::HeaderBar>,
    #[template_child]
    pub title_stack: TemplateChild<gtk::Stack>,
    #[template_child]
    pub view_switcher: TemplateChild<adw::ViewSwitcher>,
    #[template_child]
    pub window_title: TemplateChild<adw::WindowTitle>,
    #[template_child]
    pub media_menu: TemplateChild<gtk::MenuButton>,
    #[template_child]
    pub spinner: TemplateChild<adw::Spinner>,
    #[template_child]
    pub banner: TemplateChild<adw::Banner>,
    #[template_child]
    pub pending_banner: TemplateChild<adw::Banner>,
    #[template_child]
    pub switcher_bar: TemplateChild<adw::ViewSwitcherBar>,
    #[template_child]
    pub content: TemplateChild<gtk::Stack>,
    #[template_child]
    pub unavailable_page: TemplateChild<adw::StatusPage>,
    #[template_child]
    pub forget_button: TemplateChild<gtk::Button>,
    #[template_child]
    pub stack: TemplateChild<adw::ViewStack>,

    /// True while a serialized ball operation is queued or running.
    #[property(get)]
    pub operation_busy: Cell<bool>,
    /// True while any operation or application session blocks edits to the selected ball.
    #[property(get)]
    pub busy: Cell<bool>,

    pub pending_operations: Cell<u32>,
    /// Set once remembered preferences and discovered balls have loaded.
    pub started: Cell<bool>,
    pub default_build: OnceCell<PathBuf>,
    pub build: RefCell<PathBuf>,
    pub ball: RefCell<Option<PathBuf>>,
    pub snapshot: RefCell<Option<Rc<Snapshot>>>,
    pub external: RefCell<Vec<PathBuf>>,
    pub missing: RefCell<BTreeSet<PathBuf>>,
    pub new_balls: RefCell<BTreeSet<PathBuf>>,
    pub preferences: RefCell<Preferences>,
    pub undo: RefCell<Vec<Rc<HistoryItem>>>,
    pub redo: RefCell<Vec<Rc<HistoryItem>>>,
    /// Unsaved CONFIG.SYS drafts: ball → (text loaded from disk, edited text).
    pub config_drafts: RefCell<HashMap<PathBuf, (String, String)>>,
    /// Unsaved OS/2 file editor drafts keyed by ball and guest path.
    /// Values are (revision the draft was loaded from, edited text).
    pub file_drafts: RefCell<HashMap<(PathBuf, String), (String, String)>>,
    /// Unapplied settings drafts keyed by ball and setting name.
    pub setting_drafts: RefCell<HashMap<(PathBuf, String), String>>,
    pub next_section: RefCell<Option<String>>,
    pub sessions: RefCell<Sessions>,
    pub startup_pool: RefCell<StartupPool<gtk::gio::Subprocess>>,
    pub component_balls: RefCell<BTreeSet<PathBuf>>,
    /// The session shown in Logs; None shows Activity and Audits.
    pub selected_session: RefCell<Option<String>>,
    /// Launches waiting for OS/2 components or configuration, per ball.
    pub pending_launches: RefCell<HashMap<PathBuf, super::launch::PendingLaunch>>,
    /// The last dependency audit shown in Logs: title, output and log file.
    pub audit_activity: RefCell<(String, String, Option<PathBuf>)>,
    /// The guest folder last shown in the Drives browser, per ball.
    pub browse_locations: RefCell<HashMap<PathBuf, String>>,
    pub worker: OnceCell<Worker>,
}

#[glib::object_subclass]
impl ObjectSubclass for BallWindow {
    const NAME: &'static str = "TwoineBallWindow";
    type Type = super::BallWindow;
    type ParentType = adw::ApplicationWindow;

    fn class_init(klass: &mut Self::Class) {
        klass.bind_template();
    }

    fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
        object.init_template();
    }
}

#[glib::derived_properties]
impl ObjectImpl for BallWindow {
    fn signals() -> &'static [Signal] {
        static SIGNALS: OnceLock<Vec<Signal>> = OnceLock::new();
        SIGNALS.get_or_init(|| {
            vec![
                // Emitted after the selected ball's snapshot changes.
                Signal::builder("snapshot-changed").build(),
                // Emitted when application sessions start, change state or finish.
                Signal::builder("sessions-changed").build(),
                // Emitted when host application preferences change.
                Signal::builder("preferences-changed").build(),
            ]
        })
    }

    fn constructed(&self) {
        self.parent_constructed();
        self.worker
            .set(Worker::new("ball-engine"))
            .expect("worker initialised once");
    }
}

impl WidgetImpl for BallWindow {}

impl WindowImpl for BallWindow {
    fn close_request(&self) -> glib::Propagation {
        self.obj().close_requested()
    }
}

impl ApplicationWindowImpl for BallWindow {}
impl AdwApplicationWindowImpl for BallWindow {}
