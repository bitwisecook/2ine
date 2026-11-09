// Public domain. No rights reserved. Wholly AI-authored.
//! The inline OS/2 System Components pane of one ball.
//!
//! The pane browses chosen OS/2 media (`validate-os2-media`,
//! `open-os2-source`, `browse-media`), plans the component tree
//! (`plan-os2-extraction`) and reconciles ticked files with the ball in the
//! background (`import-os2-support`, `remove-os2-components`), polling
//! `os2-component-state` for progress and actual file state. Each ball owns
//! one pane for the life of the window so that its job continues while other
//! balls or sections are shown; the window's component-active state follows
//! the job and any queued changes.

use std::cell::RefCell;
use std::collections::{BTreeSet, HashSet};
use std::fmt::Display;
use std::path::PathBuf;
use std::rc::Rc;
use std::time::Duration;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{gio, glib};
use serde_json::{Map, Value, json};

use super::jobs::{self, Batch, DesiredComponents, JOBS, STATUS, Source, Step};
use super::media_list::MediaList;
use super::media_preferences;
use super::model::ComponentModel;
use super::tree::ComponentTree;
use crate::engine::{self, Error};
use crate::model::{Snapshot, ball_name, display_path};
use crate::window::BallWindow;

/// File extensions shown when "Relevant Files Only" is on.
const RELEVANT_EXTENSIONS: &[&str] = &[
    "exe", "com", "cmd", "bat", "dll", "sys", "os2", "msg", "hlp", "inf", "fon", "fnt", "ttf",
    "otf", "ttc", "pfb", "pfm", "afm", "ofm", "drv", "pdr", "ppd", "bmp", "ico", "ptr", "met",
    "wav", "mid", "avi", "mpg", "mpeg", "jpg", "jpeg", "png", "gif", "tif", "tiff", "au", "aiff",
    "aif", "mov", "mp4", "fli", "flc", "mkv", "webm",
];

/// Whether a media entry is shown by the relevant-files filter.
pub fn relevant(entry: &Value, relevant_only: bool) -> bool {
    let flag = |key: &str| entry[key].as_bool() == Some(true);
    if !relevant_only || flag("directory") || flag("container") {
        return true;
    }
    let name = entry["name"].as_str().unwrap_or_default();
    let extension = name
        .rsplit_once('.')
        .map(|(_, extension)| extension.to_lowercase())
        .unwrap_or_default();
    extension.is_empty()
        || extension.ends_with('_')
        || RELEVANT_EXTENSIONS.contains(&extension.as_str())
}

/// The status notice after opening media.
pub fn opened_notice(result: &Value, complete: bool) -> String {
    if !complete {
        return "This source could not be verified as OS/2 installation media. Choose another ISO, folder or image set, or browse this source manually.".into();
    }
    let mut notice = if result["installationDirectory"].as_bool() == Some(true) {
        "Found the OS/2 installation file set.".to_owned()
    } else if result["detectedISO"].as_bool() == Some(true) {
        "Detected ISO image inside the selected media.".to_owned()
    } else {
        String::new()
    };
    let warnings: Vec<String> = result["skippedSources"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|item| {
            format!(
                "Skipped {}: {}",
                item["path"].as_str().unwrap_or_default(),
                item["reason"].as_str().unwrap_or_default()
            )
        })
        .collect();
    if !warnings.is_empty() {
        notice.push('\n');
        notice.push_str(&warnings.join("\n"));
    }
    notice
}

/// A running or finished component job.
#[derive(Debug)]
struct Job {
    id: String,
    active: bool,
    sources: HashSet<Source>,
    batch: Batch,
    paths: HashSet<String>,
    progress: Value,
    synced_paths: HashSet<String>,
}

impl Job {
    /// Sources and lower-case paths being worked on now, or the whole batch.
    fn activity(&self) -> (HashSet<Source>, HashSet<String>) {
        let strings = |key: &str| -> Vec<String> {
            self.progress[key]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(Value::as_str)
                .map(str::to_owned)
                .collect()
        };
        let sources: HashSet<Source> = strings("activeSources")
            .into_iter()
            .map(Source::from)
            .collect();
        let paths: HashSet<String> = strings("activePaths")
            .iter()
            .map(|path| path.to_lowercase())
            .collect();
        if sources.is_empty() && paths.is_empty() {
            (
                self.sources.clone(),
                self.paths.iter().map(|path| path.to_lowercase()).collect(),
            )
        } else {
            (sources, paths)
        }
    }

    fn log_path(&self) -> Option<PathBuf> {
        self.progress["logPath"]
            .as_str()
            .filter(|path| !path.is_empty())
            .map(PathBuf::from)
    }
}

/// The planned files of the current extraction plan.
#[derive(Debug, Default)]
struct Plan {
    files: Vec<Value>,
}

/// Media, plan, job and reconciliation state of one pane.
#[derive(Debug)]
#[allow(clippy::struct_excessive_bools)]
struct State {
    sources: Vec<String>,
    directory: Option<Value>,
    selected_entry: Option<Value>,
    focus_category: Option<String>,
    selects_focus: bool,
    explicit_intent: bool,
    focus_only: bool,
    restore_selection: Option<HashSet<String>>,
    opening_sources: Option<Vec<String>>,
    queued_sources: Option<Vec<String>>,
    plan: Option<Plan>,
    extraction_sources: Vec<String>,
    categories: BTreeSet<String>,
    working: bool,
    polling: bool,
    job: Option<Rc<RefCell<Job>>>,
    desired: Option<DesiredComponents>,
    status_ready: bool,
    finalizing: bool,
    failed_batch: Option<Batch>,
    first_read: bool,
    read_failure: bool,
    read_failure_batch: Option<Batch>,
    continuation_needs_check: bool,
    queue_had_error: bool,
}

impl Default for State {
    fn default() -> Self {
        Self {
            sources: Vec::new(),
            directory: None,
            selected_entry: None,
            focus_category: None,
            selects_focus: true,
            explicit_intent: false,
            focus_only: false,
            restore_selection: None,
            opening_sources: None,
            queued_sources: None,
            plan: None,
            extraction_sources: Vec::new(),
            categories: BTreeSet::from(["cmd".to_owned()]),
            working: false,
            polling: false,
            job: None,
            desired: None,
            status_ready: false,
            finalizing: false,
            failed_batch: None,
            first_read: false,
            read_failure: false,
            read_failure_batch: None,
            continuation_needs_check: false,
            queue_had_error: false,
        }
    }
}

impl State {
    fn job_active(&self) -> bool {
        self.job.as_ref().is_some_and(|job| job.borrow().active)
    }

    fn queued(&self) -> bool {
        self.desired
            .as_ref()
            .is_some_and(|desired| !desired.pending().is_empty())
    }

    fn pending_mutations(&self) -> bool {
        self.finalizing || (self.status_ready && self.queued())
    }
}

mod imp {
    use std::cell::{Cell, OnceCell, RefCell};
    use std::path::PathBuf;

    use adw::subclass::prelude::*;
    use gtk::{CompositeTemplate, gio, glib};

    use super::{ComponentTree, MediaList, State};
    use crate::window::BallWindow;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/components-pane.ui")]
    pub struct ComponentsPane {
        #[template_child]
        pub error_banner: TemplateChild<adw::Banner>,
        #[template_child]
        pub up_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub sources_button: TemplateChild<gtk::MenuButton>,
        #[template_child]
        pub address_label: TemplateChild<gtk::Label>,
        #[template_child]
        pub empty_page: TemplateChild<adw::StatusPage>,
        #[template_child]
        pub browse_box: TemplateChild<gtk::Box>,
        #[template_child]
        pub job_box: TemplateChild<adw::WrapBox>,
        #[template_child]
        pub relevant_check: TemplateChild<gtk::CheckButton>,
        #[template_child]
        pub files_box: TemplateChild<gtk::Box>,
        #[template_child]
        pub current_only_check: TemplateChild<gtk::CheckButton>,
        #[template_child]
        pub scan_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub tree_box: TemplateChild<gtk::Box>,
        #[template_child]
        pub summary_box: TemplateChild<adw::WrapBox>,
        #[template_child]
        pub summary_label: TemplateChild<gtk::Label>,
        #[template_child]
        pub recommended_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub recommended_row: TemplateChild<adw::ExpanderRow>,
        #[template_child]
        pub refresh_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub cancel_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub log_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub progress_spinner: TemplateChild<adw::Spinner>,
        #[template_child]
        pub notice_label: TemplateChild<gtk::Label>,

        pub window: glib::WeakRef<BallWindow>,
        pub ball: RefCell<PathBuf>,
        pub build: RefCell<PathBuf>,
        pub(super) state: RefCell<State>,
        pub tree: RefCell<Option<ComponentTree>>,
        pub files: OnceCell<MediaList>,
        pub source_menu: OnceCell<gio::Menu>,
        pub recommended_rows: RefCell<Vec<adw::ActionRow>>,
        pub drain_timer: RefCell<Option<glib::SourceId>>,
        /// The component-active state last reported to the window.
        pub reported_active: Cell<bool>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ComponentsPane {
        const NAME: &'static str = "TwoineComponentsPane";
        type Type = super::ComponentsPane;
        type ParentType = adw::BreakpointBin;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_instance_callbacks();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    impl ObjectImpl for ComponentsPane {}
    impl WidgetImpl for ComponentsPane {}
    impl BreakpointBinImpl for ComponentsPane {}
}

glib::wrapper! {
    pub struct ComponentsPane(ObjectSubclass<imp::ComponentsPane>)
        @extends adw::BreakpointBin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

#[gtk::template_callbacks]
impl ComponentsPane {
    /// Show the media chooser until media is open or a component tree exists.
    fn update_media_state(&self) {
        let imp = self.imp();
        let open = imp.address_label.is_visible() || imp.tree_box.first_child().is_some();
        imp.empty_page.set_visible(!open);
        imp.browse_box.set_visible(open);
        imp.job_box
            .set_visible(open || imp.cancel_button.is_sensitive());
    }

    #[template_callback]
    fn choose_media(&self) {
        let Some(window) = self.window() else { return };
        let dialog = window.os2_media_dialog("Choose OS/2 ISO, Floppy Images or Archives", false);
        let pane = self.clone();
        glib::spawn_future_local(async move {
            match dialog.open_multiple_future(Some(&window)).await {
                Ok(files) => {
                    let sources: Vec<String> = files
                        .iter::<gio::File>()
                        .filter_map(|file| file.ok()?.path())
                        .map(|path| path.to_string_lossy().into_owned())
                        .collect();
                    if !sources.is_empty() {
                        pane.open_sources(sources);
                    }
                }
                Err(error) if !error.matches(gtk::DialogError::Dismissed) => {
                    pane.error_message(error);
                }
                Err(_) => {}
            }
        });
    }

    #[template_callback]
    fn choose_folder(&self) {
        let Some(window) = self.window() else { return };
        let dialog = window.os2_media_dialog("Choose OS/2 Media Folder", true);
        let pane = self.clone();
        glib::spawn_future_local(async move {
            match dialog.select_folder_future(Some(&window)).await {
                Ok(folder) => {
                    if let Some(path) = folder.path() {
                        pane.open_sources(vec![path.to_string_lossy().into_owned()]);
                    }
                }
                Err(error) if !error.matches(gtk::DialogError::Dismissed) => {
                    pane.error_message(error);
                }
                Err(_) => {}
            }
        });
    }

    #[template_callback]
    fn go_up(&self) {
        let parent = self
            .state()
            .directory
            .as_ref()
            .and_then(|directory| directory["parent"].as_str().map(str::to_owned));
        if let Some(parent) = parent.filter(|parent| !parent.is_empty()) {
            self.navigate(&parent);
        }
    }

    #[template_callback]
    fn rescan(&self) {
        if let Some(window) = self.window() {
            window.audit_dependencies();
        }
    }

    #[template_callback]
    fn relevant_toggled(&self) {
        if let Some(files) = self.imp().files.get() {
            files.refilter();
        }
    }

    #[template_callback]
    fn browse_components(&self) {
        self.plan_sources();
    }

    #[template_callback]
    fn refresh_clicked(&self) {
        self.refresh_state();
    }

    #[template_callback]
    fn cancel_clicked(&self) {
        self.cancel_job();
    }

    #[template_callback]
    fn show_log(&self) {
        let path = self
            .state()
            .job
            .as_ref()
            .and_then(|job| job.borrow().log_path());
        if let (Some(path), Some(window)) = (path, self.window()) {
            window.open_path(&path);
        }
    }
}

impl ComponentsPane {
    pub fn new(window: &BallWindow, ball: PathBuf) -> Self {
        let pane: Self = glib::Object::new();
        let imp = pane.imp();
        imp.window.set(Some(window));
        imp.ball.replace(ball);
        imp.build.replace(window.build());

        let files = MediaList::new();
        let weak = pane.downgrade();
        files.set_accept(move |entry| {
            weak.upgrade()
                .is_none_or(|pane| relevant(entry, pane.imp().relevant_check.is_active()))
        });
        let weak = pane.downgrade();
        files.connect_selected(move |entry| {
            if let Some(pane) = weak.upgrade() {
                pane.state_mut().selected_entry = entry.cloned();
            }
        });
        let weak = pane.downgrade();
        files.connect_activated(move |entry| {
            if let (Some(pane), Some(entry)) = (weak.upgrade(), entry)
                && (entry["directory"].as_bool() == Some(true)
                    || entry["container"].as_bool() == Some(true))
                && let Some(path) = entry["path"].as_str()
            {
                pane.navigate(path);
            }
        });
        imp.files_box.append(files.widget());
        let _ = imp.files.set(files);

        let menu = gio::Menu::new();
        imp.sources_button.set_menu_model(Some(&menu));
        let _ = imp.source_menu.set(menu);
        let actions = gio::SimpleActionGroup::new();
        let source = gio::SimpleAction::new("source", Some(glib::VariantTy::STRING));
        let weak = pane.downgrade();
        source.connect_activate(move |_, value| {
            if let (Some(pane), Some(path)) =
                (weak.upgrade(), value.and_then(glib::Variant::get::<String>))
            {
                pane.navigate(&path);
            }
        });
        actions.add_action(&source);
        pane.insert_action_group("components", Some(&actions));

        pane.connect_map(|pane| {
            let weak = pane.downgrade();
            glib::idle_add_local_once(move || {
                if let Some(pane) = weak.upgrade() {
                    pane.shown();
                }
            });
        });
        window.connect_snapshot_changed(glib::clone!(
            #[weak]
            pane,
            move |_| pane.update_recommended()
        ));
        window.connect_sessions_changed(glib::clone!(
            #[weak]
            pane,
            move |_| pane.sessions_changed()
        ));
        pane.update_count();
        pane
    }

    // ----- Accessors -----------------------------------------------------------------------

    fn state(&self) -> std::cell::Ref<'_, State> {
        self.imp().state.borrow()
    }

    fn state_mut(&self) -> std::cell::RefMut<'_, State> {
        self.imp().state.borrow_mut()
    }

    pub fn window(&self) -> Option<BallWindow> {
        self.imp().window.upgrade()
    }

    pub fn ball(&self) -> PathBuf {
        self.imp().ball.borrow().clone()
    }

    pub fn set_build(&self, build: PathBuf) {
        self.imp().build.replace(build);
    }

    fn tree(&self) -> Option<ComponentTree> {
        self.imp().tree.borrow().clone()
    }

    fn selected_ball(&self) -> bool {
        self.window()
            .and_then(|window| window.ball())
            .is_some_and(|ball| ball == self.ball())
    }

    fn sessions_active(&self) -> bool {
        let ball = self.ball();
        self.window()
            .is_some_and(|window| !window.sessions().borrow().active(Some(&ball)).is_empty())
    }

    fn operation_busy(&self) -> bool {
        self.selected_ball() && self.window().is_some_and(|window| window.operation_busy())
    }

    pub fn has_plan(&self) -> bool {
        self.state().plan.is_some()
    }

    pub fn has_tree(&self) -> bool {
        self.imp().tree.borrow().is_some()
    }

    pub fn job_active(&self) -> bool {
        self.state().job_active()
    }

    /// Queued or finalizing changes that still own the ball.
    pub fn pending_mutations(&self) -> bool {
        self.state().pending_mutations()
    }

    /// Whether this ball's component job or queued changes should block other edits.
    pub fn component_busy(&self) -> bool {
        let state = self.state();
        state.job_active() || state.pending_mutations()
    }

    /// Choose the group to show once components are planned.
    pub fn set_focus(&self, category: Option<&str>, focus_only: bool, selects_focus: bool) {
        let mut state = self.state_mut();
        state.focus_category = category.map(str::to_owned);
        state.focus_only = focus_only;
        state.selects_focus = selects_focus;
    }

    /// The component categories ticked by default in a new plan.
    pub fn set_categories(&self, categories: BTreeSet<String>) {
        self.state_mut().categories = categories;
    }

    pub fn focus_group(&self, category: &str) {
        if let Some(tree) = self.tree() {
            tree.focus_group(category);
        }
    }

    pub fn select_group(&self, category: &str) {
        if let Some(tree) = self.tree() {
            tree.select_group(category);
        }
    }

    /// The `{source, path}` selection for one of the plan's files.
    fn request(&self, action: &str, fields: Value) -> Value {
        let mut message = match fields {
            Value::Object(map) => map,
            _ => Map::new(),
        };
        message.insert("action".into(), json!(action));
        message.insert("ball".into(), json!(self.ball()));
        message.insert("build".into(), json!(*self.imp().build.borrow()));
        Value::Object(message)
    }

    // ----- Busy state ----------------------------------------------------------------------

    fn pending(&self, active: bool) {
        let mut state = self.state_mut();
        state.working = active;
        let spinning = active || state.job_active();
        let scan = !active && !state.sources.is_empty();
        drop(state);
        self.imp().progress_spinner.set_visible(spinning);
        self.imp().scan_button.set_sensitive(scan);
        self.update_count();
    }

    /// Start pane-owned work unless other work is running.
    fn begin(&self) -> bool {
        if self.state().working {
            return false;
        }
        self.imp().error_banner.set_revealed(false);
        self.pending(true);
        true
    }

    /// Report this ball's component activity to the window when it changes.
    fn report_activity(&self) {
        let active = self.component_busy();
        if self.imp().reported_active.replace(active) != active
            && let Some(window) = self.window()
        {
            window.set_component_active(&self.ball(), active);
            window.update_component_indicators();
        }
    }

    fn update_count(&self) {
        let imp = self.imp();
        let state = self.state();
        let active = state.job_active();
        imp.refresh_button
            .set_sensitive(state.plan.is_some() && !state.polling);
        imp.cancel_button.set_sensitive(active || state.queued());
        self.update_media_state();
        imp.sources_button.set_sensitive(!state.working && !active);
        imp.log_button.set_visible(
            state
                .job
                .as_ref()
                .is_some_and(|job| job.borrow().log_path().is_some()),
        );
        let pending = state
            .desired
            .as_ref()
            .map(|desired| desired.pending().len())
            .unwrap_or_default();
        drop(state);
        let tree = self.tree();
        imp.summary_box.set_visible(tree.is_some());
        if let Some(tree) = tree {
            let ticked = tree.model().borrow().selected.len();
            imp.summary_label
                .set_label(&jobs::selection_summary(ticked, pending));
            tree.table().set_sensitive(!self.sessions_active());
        }
        self.report_activity();
    }

    fn sessions_changed(&self) {
        self.update_count();
        if self.pending_mutations() && !self.sessions_active() {
            self.schedule_drain();
        }
    }

    fn notice(&self, text: &str) {
        self.imp().notice_label.set_label(text);
    }

    pub fn error_message(&self, error: impl Display) {
        let opening = self.state_mut().opening_sources.take();
        if let (Some(opening), Some(window)) = (opening, self.window())
            && opening == media_preferences::clean(&window.preferences().last_os2_media_sources)
        {
            window.forget_os2_media();
        }
        self.pending(false);
        let queued = self.state_mut().queued_sources.take();
        if let Some(queued) = queued {
            self.open_sources(queued);
            return;
        }
        let banner = &self.imp().error_banner;
        banner.set_title(&error.to_string());
        banner.set_revealed(true);
    }

    // ----- Media browsing ------------------------------------------------------------------

    fn shown(&self) {
        self.update_recommended();
        if self.has_plan() {
            self.refresh_state();
            return;
        }
        let idle = {
            let state = self.state();
            state.sources.is_empty() && !state.working
        };
        if idle
            && self.selected_ball()
            && let Some(window) = self.window()
        {
            let sources =
                media_preferences::preferred(window.snapshot().as_deref(), &window.preferences());
            if !sources.is_empty() {
                self.open_sources(sources);
            }
        }
    }

    fn show_directory(&self, directory: Value) {
        let imp = self.imp();
        let label = directory["label"].as_str().unwrap_or_default();
        imp.address_label.set_label(&display_path(label));
        imp.address_label.set_visible(!label.is_empty());
        self.update_media_state();
        imp.up_button.set_sensitive(
            directory["parent"]
                .as_str()
                .is_some_and(|parent| !parent.is_empty()),
        );
        let entries = directory["entries"].as_array().cloned().unwrap_or_default();
        {
            let mut state = self.state_mut();
            state.directory = Some(directory);
            state.selected_entry = None;
        }
        if let Some(files) = imp.files.get() {
            files.set_entries(&entries);
        }
    }

    fn navigate(&self, path: &str) {
        if !self.begin() {
            return;
        }
        let message = self.request("browse-media", json!({"path": path}));
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let result = JOBS.run(move || engine::request(&message)).await;
            if pane.window().is_none() {
                return;
            }
            pane.pending(false);
            match result {
                Ok(directory) => pane.show_directory(directory),
                Err(error) => pane.error_message(error),
            }
        });
    }

    /// Validate and open media, then plan components when it is verified OS/2 media.
    pub fn open_sources(&self, sources: Vec<String>) {
        if self.state().working {
            self.state_mut().queued_sources = Some(sources);
            return;
        }
        if self
            .window()
            .is_some_and(|window| window.component_active(&self.ball()))
        {
            self.error_message(
                "Wait or cancel this ball’s component job before choosing different media.",
            );
            return;
        }
        self.state_mut().opening_sources = Some(sources.clone());
        self.notice("Looking for ISO images…");
        if !self.begin() {
            return;
        }
        let validate = self.request("validate-os2-media", json!({"sources": sources}));
        let open = self.request("open-os2-source", json!({}));
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let requested = sources.clone();
            let result = JOBS
                .run(move || -> Result<(Value, Value), Error> {
                    let validation = engine::request(&validate)?;
                    let mut open = open;
                    open["sources"] = if validation["complete"].as_bool() == Some(true) {
                        validation["sources"].clone()
                    } else {
                        json!(requested)
                    };
                    Ok((engine::request(&open)?, validation))
                })
                .await;
            if pane.window().is_none() {
                return;
            }
            pane.pending(false);
            match result {
                Ok((opened, validation)) => pane.sources_opened(&sources, opened, &validation),
                Err(error) => pane.error_message(error),
            }
        });
    }

    fn sources_opened(&self, sources: &[String], mut result: Value, validation: &Value) {
        let queued = self.state_mut().queued_sources.take();
        if let Some(queued) = queued {
            self.open_sources(queued);
            return;
        }
        self.state_mut().opening_sources = None;
        let complete = validation["complete"].as_bool() == Some(true);
        if let Some(window) = self.window() {
            if complete {
                window.remember_os2_media(validation);
            } else if sources
                == media_preferences::clean(&window.preferences().last_os2_media_sources)
            {
                window.forget_os2_media();
            }
        }
        let opened: Vec<String> = media_preferences::sources(&result["sources"]);
        let changed = self.state().sources != opened;
        if changed {
            self.clear_tree();
            let mut state = self.state_mut();
            state.desired = None;
            state.status_ready = false;
            state.restore_selection = None;
        }
        self.state_mut().sources = opened;
        self.update_source_menu();
        self.show_directory(result["directory"].take());
        self.imp().current_only_check.set_active(false);
        self.imp()
            .scan_button
            .set_sensitive(!self.state().sources.is_empty());
        self.notice(&opened_notice(&result, complete));
        if complete {
            self.plan_sources();
        }
    }

    fn clear_tree(&self) {
        self.imp().tree.replace(None);
        let tree_box = &self.imp().tree_box;
        while let Some(child) = tree_box.first_child() {
            tree_box.remove(&child);
        }
        self.update_recommended();
        self.update_count();
    }

    fn update_source_menu(&self) {
        let Some(menu) = self.imp().source_menu.get() else {
            return;
        };
        menu.remove_all();
        let sources = self.state().sources.clone();
        for (index, source) in sources.iter().enumerate() {
            let item = gio::MenuItem::new(Some(&format!("Source {}", index + 1)), None);
            item.set_action_and_target_value(Some("components.source"), Some(&source.to_variant()));
            menu.append_item(&item);
        }
        let button = &self.imp().sources_button;
        button.set_label(&format!("{} Sources", sources.len()));
        button.set_visible(sources.len() > 1);
    }

    fn update_recommended(&self) {
        let Some(window) = self.window() else { return };
        if !self.selected_ball() {
            return;
        }
        let imp = self.imp();
        let snapshot: Option<Rc<Snapshot>> = window.snapshot();
        let groups = jobs::recommended_groups(
            snapshot
                .as_ref()
                .and_then(|snapshot| snapshot.dependency_audit.as_ref()),
        );
        for row in imp.recommended_rows.take() {
            imp.recommended_row.remove(&row);
        }
        let mut rows = Vec::new();
        for group in &groups {
            let row = adw::ActionRow::builder()
                .title(&group.title)
                .subtitle(group.required_by.join(", "))
                .activatable(true)
                .use_markup(false)
                .tooltip_text(format!("Show {} in the component list", group.title))
                .build();
            row.add_suffix(&gtk::Image::from_icon_name("go-next-symbolic"));
            let category = group.id.clone();
            row.connect_activated(glib::clone!(
                #[weak(rename_to = pane)]
                self,
                move |_| pane.focus_group(&category)
            ));
            imp.recommended_row.add_row(&row);
            rows.push(row);
        }
        imp.recommended_rows.replace(rows);
        imp.recommended_group
            .set_visible(!groups.is_empty() && self.has_tree());
    }

    // ----- Planning ------------------------------------------------------------------------

    fn plan_sources(&self) {
        let blocked = {
            let state = self.state();
            state.directory.is_none() || state.working
        };
        if blocked
            || self
                .window()
                .is_some_and(|window| window.component_active(&self.ball()))
        {
            return;
        }
        let sources = {
            let mut state = self.state_mut();
            state.extraction_sources = if self.imp().current_only_check.is_active() {
                let entry = state.selected_entry.as_ref().or(state.directory.as_ref());
                entry
                    .and_then(|entry| entry["path"].as_str())
                    .map(|path| vec![path.to_owned()])
                    .unwrap_or_default()
            } else {
                state.sources.clone()
            };
            state.extraction_sources.clone()
        };
        self.notice("Examining OS/2 files… You can continue using other pages or balls.");
        if !self.begin() {
            return;
        }
        let message = self.request("plan-os2-extraction", json!({"sources": sources}));
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let result = JOBS.run(move || engine::request(&message)).await;
            if pane.window().is_none() {
                return;
            }
            pane.pending(false);
            match result {
                Ok(plan) => pane.show_options(plan),
                Err(error) => pane.error_message(error),
            }
        });
    }

    fn show_options(&self, mut plan: Value) {
        let had_tree = self.has_tree();
        let previous_selection: Option<HashSet<String>> = self.tree().map(|tree| {
            tree.model()
                .borrow()
                .selected
                .iter()
                .map(ToString::to_string)
                .collect()
        });
        let (previous_queue, selected) = {
            let mut state = self.state_mut();
            let previous_queue = if had_tree { state.desired.take() } else { None };
            let previous = state.restore_selection.take().or(previous_selection);
            let files: Vec<Value> = plan["files"]
                .as_array_mut()
                .map(std::mem::take)
                .unwrap_or_default();
            let explicit = previous.is_some();
            let selected = previous.unwrap_or_else(|| {
                files
                    .iter()
                    .filter(|file| file["provided"].as_bool() != Some(true))
                    .filter(|file| {
                        file["category"]
                            .as_str()
                            .is_some_and(|category| state.categories.contains(category))
                    })
                    .filter_map(|file| file["source"].as_str().map(str::to_owned))
                    .collect()
            });
            state.plan = Some(Plan { files });
            state.status_ready = false;
            state.first_read = previous_queue
                .as_ref()
                .is_none_or(|queue| !queue.initialized);
            state.explicit_intent = explicit;
            (previous_queue, selected)
        };
        let tree_box = &self.imp().tree_box;
        while let Some(child) = tree_box.first_child() {
            tree_box.remove(&child);
        }
        let model = ComponentModel::new(&plan["tree"], &selected);
        let allowed: Vec<Source> = model.values(&model.root).to_vec();
        let desired = match previous_queue {
            None => DesiredComponents::new(allowed, &model.selected),
            Some(mut queue) => {
                queue.allowed = allowed.into_iter().collect();
                let allowed = queue.allowed.clone();
                queue.actual.retain(|source, _| allowed.contains(source));
                queue.desired.retain(|source| allowed.contains(source));
                queue
            }
        };
        let changed = self.downgrade();
        let setting = self.downgrade();
        let tree = ComponentTree::new(
            model,
            move || {
                if let Some(pane) = changed.upgrade() {
                    pane.selection_changed();
                }
            },
            move |token| {
                if let Some(window) = setting.upgrade().and_then(|pane| pane.window()) {
                    window.show_component_setting(token);
                }
            },
        );
        self.state_mut().desired = Some(desired);
        tree_box.append(tree.widget());
        self.update_media_state();
        self.imp().tree.replace(Some(tree.clone()));
        let (focus, select) = {
            let state = self.state();
            (
                state.focus_category.clone(),
                state.selects_focus && !state.focus_only,
            )
        };
        if let Some(category) = focus {
            if select {
                tree.select_group(&category);
            } else {
                tree.focus_group(&category);
            }
        }
        self.notice("Tick files to install; untick to remove unchanged owned files. Changes apply in the background.");
        self.update_recommended();
        self.update_count();
        self.refresh_state();
    }

    /// Re-plan the current media focused on a group, keeping the ticked files.
    pub fn resume_components(&self, category: &str, selecting: bool) {
        {
            let mut state = self.state_mut();
            state.focus_category = Some(category.to_owned());
            state.selects_focus = selecting;
        }
        if self.has_plan() {
            let selection = self.tree().map(|tree| {
                tree.model()
                    .borrow()
                    .selected
                    .iter()
                    .map(ToString::to_string)
                    .collect()
            });
            self.state_mut().restore_selection = selection;
            self.plan_sources();
        } else if !self.state().sources.is_empty() {
            self.plan_sources();
        }
    }

    // ----- Reconciliation ------------------------------------------------------------------

    fn selection_changed(&self) {
        self.state_mut().explicit_intent = true;
        let Some(tree) = self.tree() else { return };
        let selected = tree.model().borrow().selected.clone();
        let protected = {
            let mut state = self.state_mut();
            let Some(desired) = state.desired.as_mut() else {
                return;
            };
            let protected = desired.edit(&selected);
            state.queue_had_error = false;
            protected
        };
        if !protected.is_empty() {
            self.notice("Modified, unowned or protected files are preserved and remain checked.");
        }
        self.sync_selection();
        self.schedule_drain();
    }

    fn sync_selection(&self) {
        if let Some(tree) = self.tree() {
            let desired = self
                .state()
                .desired
                .as_ref()
                .map(|desired| (desired.desired.clone(), desired.protected()));
            if let Some((desired, protected)) = desired {
                {
                    let mut model = tree.model().borrow_mut();
                    model.selected = desired.into_iter().collect();
                    model.protected_sources = protected;
                }
                tree.refresh_cells();
            }
        }
        self.update_row_activity();
    }

    pub fn schedule_drain(&self) {
        let imp = self.imp();
        if self.state().status_ready && imp.drain_timer.borrow().is_none() {
            let weak = self.downgrade();
            let source = glib::timeout_add_local_once(Duration::from_millis(100), move || {
                if let Some(pane) = weak.upgrade() {
                    pane.imp().drain_timer.replace(None);
                    pane.drain_changes();
                }
            });
            imp.drain_timer.replace(Some(source));
        }
        self.update_count();
    }

    fn drain_changes(&self) {
        {
            let state = self.state();
            if !state.status_ready || state.working || state.finalizing || state.job_active() {
                return;
            }
        }
        if self.sessions_active() {
            return;
        }
        if self.operation_busy() {
            self.schedule_drain();
            return;
        }
        let step = self
            .state_mut()
            .desired
            .as_mut()
            .and_then(DesiredComponents::next_step);
        self.sync_selection();
        let continuation = match step {
            Some((Step::Install, attempted)) => {
                let configure = self
                    .tree()
                    .map(|tree| tree.model().borrow().configured_components())
                    .unwrap_or_default();
                let fields = {
                    let state = self.state();
                    let desired: Vec<String> = state
                        .desired
                        .as_ref()
                        .map(|desired| desired.desired.iter().map(ToString::to_string).collect())
                        .unwrap_or_default();
                    json!({
                        "sources": state.extraction_sources,
                        "fileSelection": desired,
                        "categories": state.categories,
                        "configureComponents": configure,
                    })
                };
                self.start_job("import-os2-support", &attempted, fields);
                false
            }
            Some((Step::Remove, attempted)) => {
                let selection = {
                    let state = self.state();
                    let ids: HashSet<Source> = attempted.iter().cloned().collect();
                    jobs::state_selection(
                        state
                            .plan
                            .as_ref()
                            .map(|plan| plan.files.as_slice())
                            .unwrap_or_default(),
                        Some(&ids),
                    )
                };
                self.start_job(
                    "remove-os2-components",
                    &attempted,
                    json!({"selection": selection}),
                );
                false
            }
            None => std::mem::take(&mut self.state_mut().continuation_needs_check),
        };
        if continuation
            && self.selected_ball()
            && let Some(window) = self.window()
        {
            window.refresh_pending_launch(true);
        }
        self.update_count();
    }

    fn start_job(&self, action: &'static str, attempted: &BTreeSet<Source>, fields: Value) {
        {
            let state = self.state();
            if state.working || state.job_active() {
                return;
            }
        }
        if self.sessions_active() || self.operation_busy() {
            self.error_message("Wait for this ball’s current operation to finish.");
            return;
        }
        let id = glib::uuid_string_random().replace('-', "");
        let job = {
            let mut state = self.state_mut();
            let batch = state
                .desired
                .as_ref()
                .map(|desired| desired.batch(attempted))
                .unwrap_or_default();
            let job = Rc::new(RefCell::new(Job {
                id: id.clone(),
                active: true,
                sources: attempted.iter().cloned().collect(),
                batch,
                paths: HashSet::new(),
                progress: json!({}),
                synced_paths: HashSet::new(),
            }));
            state.job = Some(job.clone());
            job
        };
        let installing = action == "import-os2-support";
        self.notice(if installing {
            "Installing components…"
        } else {
            "Removing unchanged owned components…"
        });
        self.imp().progress_spinner.set_visible(true);
        self.update_count();
        self.update_row_activity();
        let weak = self.downgrade();
        glib::timeout_add_local(Duration::from_millis(300), move || match weak.upgrade() {
            Some(pane) => pane.poll_job(),
            None => glib::ControlFlow::Break,
        });
        let mut message = self.request(action, fields);
        message["operationID"] = json!(id);
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let result = JOBS.run(move || engine::request(&message)).await;
            if pane.window().is_none() {
                return;
            }
            {
                let mut state = pane.state_mut();
                let batch = job.borrow().batch.clone();
                state.failed_batch = Some(batch);
                state.continuation_needs_check = true;
                state.finalizing = true;
                if result.is_err() {
                    state.queue_had_error = true;
                }
            }
            job.borrow_mut().active = false;
            pane.imp()
                .progress_spinner
                .set_visible(pane.state().working);
            match result {
                Ok(result) => {
                    let count =
                        |key: &str| result[key].as_array().map(Vec::len).unwrap_or_default();
                    pane.notice(&if installing {
                        format!(
                            "Installed {} files. Existing files were preserved.",
                            count("imported")
                        )
                    } else {
                        format!(
                            "Removed {} unchanged owned files; preserved {} other files.",
                            count("removedPaths"),
                            count("preserved")
                        )
                    });
                    pane.update_count();
                    pane.update_row_activity();
                    pane.refresh_state();
                    pane.refresh_ball_summary();
                    if let Some(window) = pane.window() {
                        window.toast(format!(
                            "OS/2 component job finished for {}",
                            ball_name(&pane.ball())
                        ));
                    }
                }
                Err(error) => {
                    pane.update_count();
                    pane.update_row_activity();
                    pane.error_message(error);
                    pane.refresh_state();
                    pane.refresh_ball_summary();
                }
            }
        });
    }

    fn poll_job(&self) -> glib::ControlFlow {
        let Some(job) = self.state().job.clone() else {
            return glib::ControlFlow::Break;
        };
        if self.window().is_none() || !job.borrow().active {
            return glib::ControlFlow::Break;
        }
        if self.state().polling {
            return glib::ControlFlow::Continue;
        }
        self.state_mut().polling = true;
        let selection = {
            let job = job.borrow();
            let strings = |key: &str| -> Vec<String> {
                job.progress[key]
                    .as_array()
                    .into_iter()
                    .flatten()
                    .filter_map(Value::as_str)
                    .map(str::to_owned)
                    .collect()
            };
            let mut paths: Vec<String> = strings("completedPaths")
                .into_iter()
                .filter(|path| !job.synced_paths.contains(path))
                .collect();
            paths.extend(strings("activePaths"));
            let changed: HashSet<String> = paths.iter().map(|path| path.to_lowercase()).collect();
            let mut source_ids: HashSet<Source> = strings("activeSources")
                .into_iter()
                .map(Source::from)
                .collect();
            let state = self.state();
            let files = state
                .plan
                .as_ref()
                .map(|plan| plan.files.as_slice())
                .unwrap_or_default();
            for file in files {
                let mut own = std::iter::once(&file["path"])
                    .chain(file["actualPaths"].as_array().into_iter().flatten())
                    .filter_map(Value::as_str);
                if own.any(|path| changed.contains(&path.to_lowercase()))
                    && let Some(source) = file["source"].as_str()
                {
                    source_ids.insert(Source::from(source));
                }
            }
            jobs::state_selection(files, Some(&source_ids))
        };
        let id = job.borrow().id.clone();
        let message = self.request(
            "os2-component-state",
            json!({"selection": selection, "paths": [], "operationID": id}),
        );
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let result = STATUS.run(move || engine::request(&message)).await;
            if pane.window().is_none() {
                return;
            }
            if let Ok(progress) = result {
                pane.progress_received(progress, &job);
            } else {
                pane.state_mut().polling = false;
                if pane.state().finalizing {
                    pane.refresh_state();
                }
            }
        });
        glib::ControlFlow::Continue
    }

    fn progress_received(&self, mut result: Value, job: &Rc<RefCell<Job>>) {
        self.state_mut().polling = false;
        if !self
            .state()
            .job
            .as_ref()
            .is_some_and(|current| Rc::ptr_eq(current, job))
        {
            return;
        }
        if !job.borrow().active {
            self.refresh_state();
            return;
        }
        self.apply_file_state(&result);
        let progress = match result["progress"].take() {
            Value::Null => json!({}),
            progress => progress,
        };
        {
            let mut job = job.borrow_mut();
            let synced = result["files"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(|file| file["path"].as_str());
            job.synced_paths.extend(synced.map(str::to_owned));
            job.progress = progress.clone();
        }
        let notice = ["detail", "phase"]
            .into_iter()
            .find_map(|key| progress[key].as_str().filter(|text| !text.is_empty()))
            .unwrap_or("Working…");
        self.notice(notice);
        self.update_row_activity();
    }

    fn update_row_activity(&self) {
        if let Some(tree) = self.tree() {
            let (job, queued) = {
                let state = self.state();
                let job = state
                    .job
                    .as_ref()
                    .filter(|job| job.borrow().active)
                    .map(|job| job.borrow().activity());
                let queued: HashSet<Source> = if state.status_ready {
                    state
                        .desired
                        .as_ref()
                        .map(|desired| desired.pending().into_iter().collect())
                        .unwrap_or_default()
                } else {
                    HashSet::new()
                };
                (job, queued)
            };
            {
                let mut model = tree.model().borrow_mut();
                let updating = job
                    .map(|(sources, paths)| model.activity_ids(&sources, &paths))
                    .unwrap_or_default();
                let waiting = if queued.is_empty() {
                    HashSet::new()
                } else {
                    model.activity_ids(&queued, &HashSet::new())
                };
                model.queued_ids = waiting.difference(&updating).cloned().collect();
                model.active_ids = updating.union(&waiting).cloned().collect();
                model.updating_ids = updating;
            }
            tree.refresh_cells();
        }
        self.update_count();
    }

    fn cancel_job(&self) {
        self.state_mut().queue_had_error = true;
        let job = self.state().job.clone().filter(|job| job.borrow().active);
        let Some(job) = job else {
            let finalizing = self.state().finalizing;
            let mut state = self.state_mut();
            let Some(desired) = state.desired.as_mut() else {
                return;
            };
            if finalizing {
                let batch = desired.batch(&desired.allowed.clone());
                state.failed_batch = Some(batch);
                drop(state);
                self.notice("Queued changes cancelled; finishing installed-status refresh…");
                return;
            }
            desired.cancel_queued();
            drop(state);
            self.sync_selection();
            self.notice("Queued component changes cancelled.");
            self.update_count();
            return;
        };
        let id = job.borrow().id.clone();
        let message = self.request("cancel-os2-component-operation", json!({"operationID": id}));
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let result = STATUS.run(move || engine::request(&message)).await;
            if pane.window().is_none() {
                return;
            }
            match result {
                Ok(_) => pane.notice("Cancellation requested; finishing the current file safely…"),
                Err(error) => pane.error_message(error),
            }
        });
    }

    fn refresh_ball_summary(&self) {
        let ball = self.ball();
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let request = json!({"action": "snapshot", "ball": ball});
            let result = STATUS
                .run(move || engine::request_as::<Snapshot>(&request))
                .await;
            if let Ok(snapshot) = result
                && pane.selected_ball()
                && let Some(window) = pane.window()
            {
                window.apply_snapshot(snapshot);
            }
        });
    }

    /// Read the actual state of every planned file and reconcile the queue.
    pub fn refresh_state(&self) {
        let selection = {
            let mut state = self.state_mut();
            let Some(plan) = state.plan.as_ref() else {
                return;
            };
            if state.polling {
                return;
            }
            let selection = jobs::state_selection(&plan.files, None);
            state.polling = true;
            selection
        };
        self.update_count();
        let message = self.request(
            "os2-component-state",
            json!({"selection": selection, "paths": []}),
        );
        let pane = self.clone();
        glib::spawn_future_local(async move {
            let result = STATUS.run(move || engine::request(&message)).await;
            if pane.window().is_none() {
                return;
            }
            match result {
                Ok(result) => pane.state_received(&result),
                Err(error) => pane.state_failed(&error),
            }
        });
    }

    fn state_received(&self, result: &Value) {
        self.state_mut().polling = false;
        self.apply_file_state(result);
        let unfinished = {
            let mut guard = self.state_mut();
            let state = &mut *guard;
            state.status_ready = true;
            state.finalizing = false;
            let Some(desired) = state.desired.as_mut() else {
                return;
            };
            if state.read_failure {
                desired.failed(&state.read_failure_batch.take().unwrap_or_default());
                state.read_failure = false;
            }
            if state.first_read {
                desired.initialize(!result["progress"].is_null(), state.explicit_intent);
                state.first_read = false;
            }
            let unfinished = state
                .failed_batch
                .take()
                .is_some_and(|batch| !desired.settle(&batch).is_empty());
            if unfinished {
                state.queue_had_error = true;
            }
            desired.clamp_protected();
            unfinished
        };
        if unfinished {
            self.notice(
                "Some requested changes could not be applied. Existing or protected files were preserved; checks now show actual state.",
            );
        }
        self.sync_selection();
        self.schedule_drain();
    }

    fn state_failed(&self, error: &Error) {
        {
            let mut state = self.state_mut();
            state.queue_had_error = true;
            state.polling = false;
            state.status_ready = false;
            state.finalizing = false;
            state.read_failure = true;
            state.read_failure_batch = state
                .desired
                .as_ref()
                .map(|desired| desired.batch(&desired.allowed));
            state.first_read = false;
        }
        self.update_row_activity();
        self.error_message(format!(
            "Could not refresh installed status; refresh before applying more changes. {error}"
        ));
        self.update_count();
    }

    fn apply_file_state(&self, result: &Value) {
        let records = jobs::source_states(
            result["files"]
                .as_array()
                .map(Vec::as_slice)
                .unwrap_or_default(),
        );
        {
            let mut state = self.state_mut();
            if let Some(desired) = state.desired.as_mut() {
                desired.update_actual(&records);
            }
            if let Some(plan) = state.plan.as_mut() {
                for file in &mut plan.files {
                    if let Some(entry) = file["source"]
                        .as_str()
                        .and_then(|source| records.get(source))
                        && let Some(file) = file.as_object_mut()
                    {
                        entry.apply(file);
                    }
                }
            }
        }
        if let Some(tree) = self.tree() {
            {
                let mut model = tree.model().borrow_mut();
                let files: Vec<String> = model
                    .ids()
                    .iter()
                    .filter(|id| model.kind(id) == "file")
                    .cloned()
                    .collect();
                for id in files {
                    let entry = model
                        .node(&id)
                        .and_then(|node| node.get("source"))
                        .and_then(Value::as_str)
                        .and_then(|source| records.get(source));
                    if let Some(entry) = entry.cloned()
                        && let Some(node) = model.node_mut(&id)
                    {
                        entry.apply(node);
                    }
                }
            }
            tree.refresh_cells();
        }
        self.update_count();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relevant_files_keep_folders_containers_and_os2_types() {
        let entry = |name: &str, directory: bool| json!({"name": name, "directory": directory, "container": false});
        assert!(relevant(&entry("DISK_1", true), true));
        assert!(relevant(&json!({"name": "x.iso", "container": true}), true));
        assert!(relevant(&entry("CMD.EXE", false), true));
        assert!(relevant(&entry("README", false), true));
        assert!(relevant(&entry("PMSHELL.DL_", false), true));
        assert!(!relevant(&entry("NOTES.TXT", false), true));
        assert!(relevant(&entry("NOTES.TXT", false), false));
    }

    #[test]
    fn opened_notice_reports_detection_and_skipped_sources() {
        assert_eq!(
            opened_notice(
                &json!({"installationDirectory": true, "detectedISO": true}),
                true
            ),
            "Found the OS/2 installation file set."
        );
        assert_eq!(
            opened_notice(&json!({"detectedISO": true}), true),
            "Detected ISO image inside the selected media."
        );
        assert_eq!(
            opened_notice(
                &json!({"skippedSources": [{"path": "/a", "reason": "unreadable"}]}),
                true
            ),
            "\nSkipped /a: unreadable"
        );
        assert!(opened_notice(&json!({}), false).starts_with("This source could not be verified"));
    }
}
