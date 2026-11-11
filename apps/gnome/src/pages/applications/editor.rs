// Public domain. No rights reserved. Wholly AI-authored.
//! The program editor: add or edit a launcher, run a program once, or run an
//! installer from attached media.
//!
//! General holds the program, arguments and initial folder (and, for
//! launchers, the name and icon; for installers, the installation media).
//! Working Folders overrides per-drive defaults and Environment overrides
//! inherited guest variables. Arguments are parsed and formatted by the
//! engine, so no shell interprets them.

use std::cell::{Cell, OnceCell, RefCell};
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::time::Duration;

use adw::prelude::*;
use adw::subclass::prelude::*;
use gtk::{CompositeTemplate, gio, glib};
use serde_json::{Value, json};

use super::choice::{self, ProgramChoice};
use super::icons;
use crate::dialogs::FormDialog;
use crate::dialogs::files;
use crate::dialogs::guest_picker::PickerMode;
use crate::engine::{self, Error};
use crate::model::{Application, Device, DriveKind, Snapshot};
use crate::window::history::HistoryItem;
use crate::window::program_ops::ProgramAction;
use crate::window::{BallWindow, Work};

/// Delay after typing before the icon preview refreshes.
const PREVIEW_DELAY: Duration = Duration::from_millis(250);

mod imp {
    use super::{
        Application, BTreeMap, BallWindow, Cell, CompositeTemplate, FormDialog, OnceCell, PathBuf,
        ProgramAction, ProgramChoice, RefCell, glib,
    };
    use adw::subclass::prelude::*;
    use gtk::prelude::*;

    #[derive(CompositeTemplate, Default)]
    #[template(resource = "/io/github/bitwisecook/Twoine/Ball/ui/applications-editor.ui")]
    pub struct ProgramEditor {
        #[template_child]
        pub switcher: TemplateChild<adw::InlineViewSwitcher>,
        #[template_child]
        pub program_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub name_row: TemplateChild<adw::EntryRow>,
        #[template_child]
        pub program_row: TemplateChild<adw::EntryRow>,
        #[template_child]
        pub candidates_button: TemplateChild<gtk::MenuButton>,
        #[template_child]
        pub browse_program_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub arguments_row: TemplateChild<adw::EntryRow>,
        #[template_child]
        pub cwd_row: TemplateChild<adw::EntryRow>,
        #[template_child]
        pub browse_cwd_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub media_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub media_kind: TemplateChild<adw::ComboRow>,
        #[template_child]
        pub media_letter: TemplateChild<adw::ComboRow>,
        #[template_child]
        pub media_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub media_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub eject_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub icon_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub icon: TemplateChild<gtk::Image>,
        #[template_child]
        pub choose_icon_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub use_executable_icon_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub folders_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub environment_group: TemplateChild<adw::PreferencesGroup>,
        #[template_child]
        pub preview_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub variable_row: TemplateChild<adw::EntryRow>,

        pub window: glib::WeakRef<BallWindow>,
        pub dialog: glib::WeakRef<FormDialog>,
        pub action: OnceCell<ProgramAction>,
        /// The launcher being edited.
        pub original_name: RefCell<Option<String>>,
        pub initial: RefCell<Application>,
        /// The ball selected when the editor opened.
        pub ball: RefCell<Option<PathBuf>>,
        /// None keeps the launcher's icon, an empty string uses the executable's
        /// artwork and a host path imports that image into the ball.
        pub icon_source: RefCell<Option<String>>,
        pub preview_serial: Cell<u64>,
        /// The name last suggested from the executable's metadata.
        pub suggested_name: RefCell<String>,
        pub directory_rows: RefCell<Vec<(String, adw::ExpanderRow, adw::EntryRow)>>,
        pub environment_rows: RefCell<BTreeMap<String, (adw::ExpanderRow, adw::EntryRow)>>,
        /// Inherited guest environment values.
        pub defaults: RefCell<BTreeMap<String, String>>,
        pub choice: RefCell<ProgramChoice>,
        /// True while suggestions replace the program text.
        pub updating: Cell<bool>,
        pub media_letters: RefCell<Vec<String>>,
        pub media_busy: Cell<bool>,
        pub suggestions_ready: Cell<bool>,
        pub candidate_serial: Cell<u64>,
        pub busy_handler: RefCell<Option<glib::SignalHandlerId>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ProgramEditor {
        const NAME: &'static str = "TwoineProgramEditor";
        type Type = super::ProgramEditor;
        type ParentType = adw::Bin;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
        }

        fn instance_init(object: &glib::subclass::InitializingObject<Self>) {
            object.init_template();
        }
    }

    impl ObjectImpl for ProgramEditor {
        fn dispose(&self) {
            if let (Some(window), Some(handler)) = (self.window.upgrade(), self.busy_handler.take())
            {
                window.disconnect(handler);
            }
        }
    }

    impl WidgetImpl for ProgramEditor {}
    impl BinImpl for ProgramEditor {}
}

glib::wrapper! {
    pub struct ProgramEditor(ObjectSubclass<imp::ProgramEditor>)
        @extends adw::Bin, gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

fn editor_action(imp: &imp::ProgramEditor) -> ProgramAction {
    imp.action.get().copied().unwrap_or(ProgramAction::Save)
}

/// The editor title for an action, or for editing an existing launcher.
pub fn title(action: ProgramAction, editing: bool) -> &'static str {
    match action {
        _ if editing => "Edit Application",
        ProgramAction::Save => "Add Application",
        ProgramAction::Run => "Run Program",
        ProgramAction::Install => "Install Software",
    }
}

/// The editor's accept button label.
pub fn accept_label(action: ProgramAction) -> &'static str {
    match action {
        ProgramAction::Save => "Save",
        ProgramAction::Run => "Run",
        ProgramAction::Install => "Install",
    }
}

/// Validate the name and program before saving or starting.
pub fn validate(
    action: ProgramAction,
    name: &str,
    program: &str,
    original_name: Option<&str>,
    applications: &BTreeMap<String, Application>,
) -> Result<(), &'static str> {
    let saving = action == ProgramAction::Save;
    if program.is_empty() || (saving && name.is_empty()) {
        return Err("Enter an executable and an application name when saving a launcher.");
    }
    if saving && original_name != Some(name) && applications.contains_key(name) {
        return Err("An application with this name already exists.");
    }
    Ok(())
}

/// The `run` or `install` command line for a program.
pub fn launch_arguments(
    action: ProgramAction,
    cwd: &str,
    program: &str,
    argv: Vec<String>,
) -> Vec<String> {
    let mut arguments = vec![action.command().to_owned()];
    if !cwd.is_empty() {
        arguments.extend(["--cwd".to_owned(), cwd.to_owned()]);
    }
    arguments.push(program.to_owned());
    arguments.extend(argv);
    arguments
}

/// Undo and redo requests for saving a launcher named `name`, replacing
/// `original_name` (whose record before saving was `old_record`).
pub fn save_history(
    name: &str,
    original_name: Option<&str>,
    old_record: Option<&str>,
    new_record: &str,
) -> (Vec<Value>, Vec<Value>) {
    let command = |arguments: &[&str]| json!({"action": "command", "arguments": arguments});
    let mut undo = vec![command(&["remove-app", name])];
    if let (Some(original), Some(record)) = (original_name, old_record) {
        undo.push(command(&["restore-app", original, record]));
    }
    let mut redo = Vec::new();
    if let Some(original) = original_name
        && original != name
    {
        redo.push(command(&["remove-app", original]));
    }
    redo.push(command(&["restore-app", name, new_record]));
    (undo, redo)
}

/// Whether a guest path is on `letter`'s drive.
fn on_drive(path: &str, letter: &str) -> bool {
    path.get(..2)
        .is_some_and(|prefix| prefix.eq_ignore_ascii_case(&format!("{letter}:")))
}

fn text_map(value: Value) -> BTreeMap<String, String> {
    match value {
        Value::Object(map) => map
            .into_iter()
            .map(|(key, value)| {
                let value = match value {
                    Value::String(text) => text,
                    Value::Null => String::new(),
                    other => other.to_string(),
                };
                (key, value)
            })
            .collect(),
        _ => BTreeMap::new(),
    }
}

/// Open the program editor for `action`.
pub fn open(window: &BallWindow, action: ProgramAction, name: Option<&str>, program: &str) {
    let dialog = FormDialog::new(window, title(action, name.is_some()), accept_label(action))
        .with_size(700, 650);
    dialog.set_size_request(360, 360);
    let editor = ProgramEditor::new(window, &dialog, action, name, program);
    dialog.append(&editor);
    if let Ok(condition) = adw::BreakpointCondition::parse("max-width: 550sp") {
        let breakpoint = adw::Breakpoint::new(condition);
        breakpoint.add_setter(
            &*editor.imp().switcher,
            "display-mode",
            Some(&adw::InlineViewSwitcherDisplayMode::Icons.to_value()),
        );
        dialog.add_breakpoint(breakpoint);
    }
    dialog.connect_submit(glib::clone!(
        #[weak]
        editor,
        move |_| editor.submit()
    ));
    dialog.present(Some(window));
    editor.load_initial();
}

impl ProgramEditor {
    fn new(
        window: &BallWindow,
        dialog: &FormDialog,
        action: ProgramAction,
        name: Option<&str>,
        program: &str,
    ) -> Self {
        let editor: Self = glib::Object::new();
        let imp = editor.imp();
        imp.window.set(Some(window));
        imp.dialog.set(Some(dialog));
        let _ = imp.action.set(action);
        imp.ball.replace(window.ball());
        imp.original_name.replace(name.map(str::to_owned));
        let snapshot = window.snapshot().unwrap_or_default();
        let existing = name
            .and_then(|name| snapshot.manifest.applications.get(name))
            .cloned();
        let program = existing.as_ref().map_or_else(
            || program.to_owned(),
            |application| application.program.clone(),
        );
        let initial = existing.unwrap_or_default();

        imp.program_group
            .set_title(if action == ProgramAction::Save {
                "Application"
            } else {
                "Program"
            });
        imp.name_row.set_visible(action == ProgramAction::Save);
        imp.name_row.set_text(name.unwrap_or_default());
        imp.program_row.set_text(&program);
        imp.cwd_row
            .set_text(initial.cwd.as_deref().unwrap_or_default());
        imp.icon_group.set_visible(action == ProgramAction::Save);
        imp.media_group
            .set_visible(action == ProgramAction::Install);
        imp.candidates_button
            .set_visible(action == ProgramAction::Install);
        imp.choice.replace(ProgramChoice::new(&program));

        for letter in snapshot.manifest.drives.keys() {
            editor.add_directory(window, &snapshot, &initial, letter);
        }
        imp.initial.replace(initial);
        editor.connect_handlers(window);
        if action == ProgramAction::Install {
            editor.media_choices();
        }
        editor.set_sensitive(false);
        editor
    }

    fn window(&self) -> Option<BallWindow> {
        self.imp().window.upgrade()
    }

    fn dialog(&self) -> Option<FormDialog> {
        self.imp().dialog.upgrade()
    }

    fn show_error(&self, error: impl std::fmt::Display) {
        if let Some(dialog) = self.dialog() {
            dialog.show_error(error);
        }
    }

    fn add_directory(
        &self,
        window: &BallWindow,
        snapshot: &Snapshot,
        initial: &Application,
        letter: &str,
    ) {
        let inherited = snapshot
            .working_directories
            .get(letter)
            .cloned()
            .unwrap_or_else(|| format!("{letter}:\\"));
        let existing = initial.working_directories.get(letter);
        let row = adw::ExpanderRow::builder()
            .title(format!("{letter}:"))
            .subtitle(format!("Ball default: {inherited}"))
            .use_markup(false)
            .show_enable_switch(true)
            .enable_expansion(existing.is_some())
            .expanded(existing.is_some())
            .build();
        let entry = adw::EntryRow::builder()
            .title("Working Folder")
            .text(existing.cloned().unwrap_or(inherited))
            .build();
        let label = format!("Choose {letter}: Folder");
        let browse = crate::dialogs::form::button(&label, Some("folder-open-symbolic"));
        browse.add_css_class("flat");
        let letter = letter.to_owned();
        browse.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            #[weak]
            window,
            #[weak]
            entry,
            #[strong]
            letter,
            #[strong]
            label,
            move |_| {
                let (label, letter) = (label.clone(), letter.clone());
                glib::spawn_future_local(async move {
                    let initial = entry.text().to_string();
                    let picked = window
                        .pick_guest_path(&label, PickerMode::Folder, Some(initial))
                        .await;
                    match picked {
                        Some(path) if on_drive(&path, &letter) => entry.set_text(&path),
                        Some(_) => {
                            editor.show_error(format!("Choose a folder on drive {letter}:."));
                        }
                        None => {}
                    }
                });
            }
        ));
        entry.add_suffix(&browse);
        row.add_row(&entry);
        self.imp().folders_group.add(&row);
        self.imp()
            .directory_rows
            .borrow_mut()
            .push((letter, row, entry));
    }

    fn connect_handlers(&self, window: &BallWindow) {
        let imp = self.imp();
        imp.program_row.connect_text_notify(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |row| {
                let imp = editor.imp();
                if !imp.updating.get() {
                    imp.choice.borrow_mut().edit(&row.text());
                }
                editor.schedule_preview();
            }
        ));
        imp.browse_program_button.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            #[weak]
            window,
            move |_| {
                glib::spawn_future_local(async move {
                    let text = editor.imp().program_row.text().to_string();
                    let initial = if text.is_empty() {
                        format!(
                            "{}:",
                            window
                                .snapshot()
                                .map(|snapshot| snapshot.current_drive.clone())
                                .unwrap_or_default()
                        )
                    } else {
                        text
                    };
                    if let Some(path) = window
                        .pick_guest_path("Browse Drives", PickerMode::Program, Some(initial))
                        .await
                    {
                        editor.set_program_text(&path);
                    }
                });
            }
        ));
        imp.browse_cwd_button.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            #[weak]
            window,
            move |_| {
                glib::spawn_future_local(async move {
                    let text = editor.imp().cwd_row.text().to_string();
                    let initial = if text.is_empty() {
                        "C:".to_owned()
                    } else {
                        text
                    };
                    if let Some(path) = window
                        .pick_guest_path(
                            "Choose Initial Working Folder",
                            PickerMode::Folder,
                            Some(initial),
                        )
                        .await
                    {
                        editor.imp().cwd_row.set_text(&path);
                    }
                });
            }
        ));
        imp.media_kind.connect_selected_notify(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.media_choices()
        ));
        imp.media_letter.connect_selected_notify(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.media_changed()
        ));
        imp.media_button.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.attach_installer()
        ));
        imp.eject_button.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.eject_installer()
        ));
        imp.choose_icon_button.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.choose_icon()
        ));
        imp.use_executable_icon_row.connect_activated(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.use_executable_icon()
        ));
        imp.variable_row.connect_apply(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.add_variable(None, None, true)
        ));
        imp.preview_button.connect_clicked(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.preview_environment()
        ));
        let actions = gio::SimpleActionGroup::new();
        let candidate = gio::SimpleAction::new("candidate", Some(glib::VariantTy::STRING));
        candidate.connect_activate(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_, value| {
                if let Some(path) = value.and_then(glib::Variant::str) {
                    editor.set_program_text(path);
                }
            }
        ));
        actions.add_action(&candidate);
        self.insert_action_group("editor", Some(&actions));
        let handler = window.connect_operation_busy_notify(glib::clone!(
            #[weak(rename_to = editor)]
            self,
            move |_| editor.media_controls()
        ));
        imp.busy_handler.replace(Some(handler));
    }

    /// Read the formatted arguments and inherited environment, then enable the form.
    fn load_initial(&self) {
        let Some(window) = self.window() else { return };
        let initial = self.imp().initial.borrow().clone();
        let format = window.message("format-arguments", json!({"arguments": initial.arguments}));
        let environment = window.message("guest-environment", json!({}));
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let result = window
                .run(
                    Work {
                        unlocked: true,
                        ..Work::default()
                    },
                    move || -> Result<(String, Value), Error> {
                        let text = engine::request(&format)?;
                        Ok((
                            text.as_str().unwrap_or_default().to_owned(),
                            engine::request(&environment)?,
                        ))
                    },
                )
                .await;
            let Some(editor) = editor.upgrade() else {
                return;
            };
            match result {
                Ok((arguments, defaults)) => {
                    editor.initialized(&initial, &arguments, text_map(defaults));
                }
                Err(error) => editor.show_error(error),
            }
        });
    }

    fn initialized(
        &self,
        initial: &Application,
        arguments: &str,
        defaults: BTreeMap<String, String>,
    ) {
        let imp = self.imp();
        imp.arguments_row.set_text(arguments);
        let mut keys: Vec<String> = defaults
            .keys()
            .chain(initial.environment.keys())
            .cloned()
            .collect();
        keys.sort();
        keys.dedup();
        imp.defaults.replace(defaults);
        for key in keys {
            let value = initial.environment.get(&key).cloned().flatten();
            let enabled = initial.environment.contains_key(&key);
            self.add_variable(Some(key), value, enabled);
        }
        self.set_sensitive(true);
        if editor_action(imp) == ProgramAction::Install {
            imp.suggestions_ready.set(true);
            self.discover_installers();
        }
        self.refresh_preview();
    }

    /// Add an environment override row, or enable an existing one.
    /// Without `key`, the name typed in Add an Override is used.
    pub fn add_variable(&self, key: Option<String>, value: Option<String>, enabled: bool) {
        let imp = self.imp();
        let key = key.unwrap_or_else(|| imp.variable_row.text().trim().to_owned());
        if key.is_empty() {
            return;
        }
        if let Some((row, entry)) = imp.environment_rows.borrow().get(&key) {
            row.set_enable_expansion(enabled);
            row.set_expanded(enabled);
            if let Some(value) = value {
                entry.set_text(&value);
            }
            return;
        }
        let inherited = imp.defaults.borrow().get(&key).cloned().unwrap_or_default();
        let row = adw::ExpanderRow::builder()
            .title(&key)
            .subtitle(format!("Inherited: {inherited}"))
            .use_markup(false)
            .show_enable_switch(true)
            .enable_expansion(enabled)
            .expanded(enabled)
            .subtitle_lines(1)
            .build();
        let entry = adw::EntryRow::builder()
            .title("Override")
            .text(value.unwrap_or(inherited))
            .build();
        row.add_row(&entry);
        imp.environment_group.add(&row);
        imp.environment_rows.borrow_mut().insert(key, (row, entry));
        imp.variable_row.set_text("");
    }

    /// Enabled per-drive working folders and environment overrides.
    pub fn overrides(&self) -> (BTreeMap<String, String>, BTreeMap<String, String>) {
        let imp = self.imp();
        let directories = imp
            .directory_rows
            .borrow()
            .iter()
            .filter(|(_, row, _)| row.enables_expansion())
            .map(|(letter, _, entry)| (letter.clone(), entry.text().into()))
            .collect();
        let environment = imp
            .environment_rows
            .borrow()
            .iter()
            .filter(|(_, (row, _))| row.enables_expansion())
            .map(|(key, (_, entry))| (key.clone(), entry.text().into()))
            .collect();
        (directories, environment)
    }

    fn preview_environment(&self) {
        let (Some(window), Some(parent)) = (self.window(), self.dialog()) else {
            return;
        };
        let message = window.message(
            "guest-environment",
            json!({"environment": self.overrides().1}),
        );
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let result = window
                .run(
                    Work {
                        unlocked: true,
                        ..Work::default()
                    },
                    move || engine::request(&message),
                )
                .await;
            let Some(editor) = editor.upgrade() else {
                return;
            };
            let values = match result {
                Ok(values) => text_map(values),
                Err(error) => return editor.show_error(error),
            };
            let dialog = FormDialog::new(&window, "Effective Guest Environment", "Close")
                .with_size(620, 560);
            dialog.connect_submit(|dialog| {
                dialog.close();
            });
            let page = adw::PreferencesPage::new();
            let group = crate::dialogs::form::group("Expanded Values", None);
            for (key, value) in values {
                let row = adw::ActionRow::builder()
                    .title(key)
                    .subtitle(value)
                    .subtitle_lines(0)
                    .use_markup(false)
                    .build();
                row.add_css_class("property");
                row.set_subtitle_selectable(true);
                group.add(&row);
            }
            page.add(&group);
            dialog.append(&page);
            dialog.present(Some(&parent));
        });
    }

    fn schedule_preview(&self) {
        let imp = self.imp();
        let serial = imp.preview_serial.get() + 1;
        imp.preview_serial.set(serial);
        glib::timeout_add_local_once(
            PREVIEW_DELAY,
            glib::clone!(
                #[weak(rename_to = editor)]
                self,
                move || {
                    if editor.imp().preview_serial.get() == serial {
                        editor.refresh_preview();
                    }
                }
            ),
        );
    }

    /// Show the launcher's icon and suggest a name from the executable's metadata.
    fn refresh_preview(&self) {
        let imp = self.imp();
        let program = imp.program_row.text().to_string();
        if editor_action(imp) != ProgramAction::Save || program.is_empty() {
            return;
        }
        let Some(window) = self.window() else { return };
        let serial = imp.preview_serial.get();
        let source = imp.icon_source.borrow().clone();
        let original = imp.original_name.borrow().clone();
        let name = original.filter(|_| source.is_none() && program == imp.initial.borrow().program);
        let message = match (&source, name) {
            (Some(path), _) if !path.is_empty() => {
                window.message("image-preview", json!({"path": path}))
            }
            (_, Some(name)) => window.message("application-icon", json!({"name": name})),
            _ => window.message("application-icon", json!({"program": program})),
        };
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let result = window
                .run(
                    Work {
                        unlocked: true,
                        ..Work::default()
                    },
                    move || engine::request(&message),
                )
                .await;
            let (Some(editor), Ok(result)) = (editor.upgrade(), result) else {
                return;
            };
            editor.preview_ready(serial, source.as_deref(), &result);
        });
    }

    fn preview_ready(&self, serial: u64, source: Option<&str>, result: &Value) {
        let imp = self.imp();
        if serial != imp.preview_serial.get() {
            return;
        }
        if let Some(suggested) = result["displayName"]
            .as_str()
            .filter(|name| !name.is_empty())
            && imp.original_name.borrow().is_none()
            && source.is_none_or(str::is_empty)
        {
            let current = imp.name_row.text();
            if current.is_empty() || current == *imp.suggested_name.borrow() {
                imp.name_row.set_text(suggested);
            }
            suggested.clone_into(&mut imp.suggested_name.borrow_mut());
        }
        match icons::texture(result) {
            Ok(Some(texture)) => imp.icon.set_paintable(Some(&texture)),
            Ok(None) => imp
                .icon
                .set_icon_name(Some("application-x-executable-symbolic")),
            Err(error) => self.show_error(error.message()),
        }
    }

    fn choose_icon(&self) {
        let Some(window) = self.window() else { return };
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let Some(path) = files::open_file(&window, "Choose Application Icon", None).await
            else {
                return;
            };
            let Some(editor) = editor.upgrade() else {
                return;
            };
            editor.set_icon_source(Some(path.to_string_lossy().into_owned()));
        });
    }

    fn use_executable_icon(&self) {
        self.set_icon_source(Some(String::new()));
    }

    fn set_icon_source(&self, source: Option<String>) {
        let imp = self.imp();
        imp.icon_source.replace(source);
        imp.preview_serial.set(imp.preview_serial.get() + 1);
        self.refresh_preview();
    }

    /// Set the program as if typed, keeping it when suggestions change.
    fn set_program_text(&self, text: &str) {
        let imp = self.imp();
        imp.choice.borrow_mut().edit(text);
        imp.program_row.set_text(text);
    }

    /// Offer installer suggestions; an automatic program choice follows them.
    fn set_candidates(&self, candidates: &[String]) {
        let imp = self.imp();
        let text = imp.choice.borrow_mut().update(candidates);
        let menu = gio::Menu::new();
        for candidate in &imp.choice.borrow().candidates {
            let item = gio::MenuItem::new(Some(candidate), None);
            item.set_action_and_target_value(
                Some("editor.candidate"),
                Some(&candidate.to_variant()),
            );
            menu.append_item(&item);
        }
        imp.candidates_button.set_menu_model(Some(&menu));
        imp.candidates_button.set_sensitive(menu.n_items() > 0);
        imp.updating.set(true);
        imp.program_row.set_text(&text);
        imp.updating.set(false);
    }

    fn media_kind(&self) -> DriveKind {
        if self.imp().media_kind.selected() == 1 {
            DriveKind::Floppy
        } else {
            DriveKind::Cdrom
        }
    }

    fn media_choices(&self) {
        let Some(snapshot) = self.window().and_then(|window| window.snapshot()) else {
            return;
        };
        let letters = choice::media_letters(&snapshot, self.media_kind());
        let labels: Vec<String> = letters.iter().map(|letter| format!("{letter}:")).collect();
        self.imp().media_letters.replace(letters);
        let labels: Vec<&str> = labels.iter().map(String::as_str).collect();
        self.imp()
            .media_letter
            .set_model(Some(&gtk::StringList::new(&labels)));
        self.media_changed();
    }

    /// The selected installation media letter and its device, if mapped.
    fn media_mapping(&self) -> Option<(String, Option<Device>)> {
        let imp = self.imp();
        let index = usize::try_from(imp.media_letter.selected()).ok()?;
        let letter = imp.media_letters.borrow().get(index).cloned()?;
        let snapshot = self.window()?.snapshot()?;
        let device = snapshot
            .drive(&letter)
            .and_then(|drive| drive.device().cloned());
        Some((letter, device))
    }

    fn attached(device: Option<&Device>) -> bool {
        device
            .and_then(|device| device.image.as_deref())
            .is_some_and(|image| !image.is_empty())
    }

    fn media_controls(&self) {
        let imp = self.imp();
        if editor_action(imp) != ProgramAction::Install {
            return;
        }
        let Some(window) = self.window() else { return };
        let mapping = self.media_mapping();
        let device = mapping.as_ref().and_then(|(_, device)| device.as_ref());
        let attached = Self::attached(device);
        let allowed = mapping.is_some()
            && !device.is_some_and(|device| device.locked)
            && !imp.media_busy.get()
            && !window.operation_busy()
            && !imp
                .ball
                .borrow()
                .as_deref()
                .is_some_and(|ball| window.component_active(ball));
        let title = if attached {
            "Change Image"
        } else {
            "Attach Image"
        };
        imp.media_row.set_title(title);
        imp.media_button.set_tooltip_text(Some(title));
        imp.media_button
            .update_property(&[gtk::accessible::Property::Label(title)]);
        imp.media_button.set_sensitive(allowed);
        imp.eject_button.set_sensitive(allowed && attached);
    }

    fn media_changed(&self) {
        self.media_controls();
        if self.imp().suggestions_ready.get() {
            self.discover_installers();
        }
    }

    /// A job that lists guest folders with the engine's `browse` request.
    fn browser(window: &BallWindow) -> impl FnMut(&str) -> Result<Value, Error> + Send + 'static {
        let template = window.message("browse", json!({}));
        move |path| {
            let mut message = template.clone();
            message["path"] = json!(path);
            engine::request(&message)
        }
    }

    fn discover_installers(&self) {
        let imp = self.imp();
        let serial = imp.candidate_serial.get() + 1;
        imp.candidate_serial.set(serial);
        self.set_candidates(&[]);
        let Some(window) = self.window() else { return };
        let Some((letter, _)) = self
            .media_mapping()
            .filter(|(_, device)| Self::attached(device.as_ref()))
        else {
            return;
        };
        let browse = Self::browser(&window);
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let candidates = window
                .run(
                    Work {
                        unlocked: true,
                        ..Work::default()
                    },
                    move || choice::installer_candidates(browse, &letter),
                )
                .await;
            let Some(editor) = editor.upgrade() else {
                return;
            };
            if serial == editor.imp().candidate_serial.get()
                && *editor.imp().ball.borrow() == window.ball()
            {
                editor.set_candidates(&candidates);
            }
        });
    }

    /// Run a media change through the dialog, keeping media controls disabled meanwhile.
    async fn media_work<T: Send + 'static>(
        &self,
        window: &BallWindow,
        job: impl FnOnce() -> Result<T, Error> + Send + 'static,
    ) -> Option<T> {
        let dialog = self.dialog()?;
        self.imp().media_busy.set(true);
        self.media_controls();
        let result = dialog.run(window, Work::default(), job).await;
        self.imp().media_busy.set(false);
        self.media_controls();
        result
    }

    fn eject_installer(&self) {
        let Some(window) = self.window() else { return };
        let Some((letter, Some(device))) = self.media_mapping() else {
            return;
        };
        if device.locked || self.imp().media_busy.get() || !Self::attached(Some(&device)) {
            return;
        }
        let eject = window.message("command", json!({"arguments": ["eject", letter]}));
        let snapshot = window.message("snapshot", json!({}));
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let Some(this) = editor.upgrade() else { return };
            let result = this
                .media_work(&window, move || {
                    engine::request(&eject)?;
                    engine::request_as::<Snapshot>(&snapshot)
                })
                .await;
            if let Some(snapshot) = result {
                window.apply_snapshot(snapshot);
                this.media_controls();
                this.discover_installers();
            }
        });
    }

    fn attach_installer(&self) {
        let imp = self.imp();
        let Some(window) = self.window() else { return };
        let mapping = self.media_mapping();
        if mapping
            .as_ref()
            .and_then(|(_, device)| device.as_ref())
            .is_some_and(|device| device.locked)
            || imp.media_busy.get()
        {
            return;
        }
        let Some((letter, device)) = mapping else {
            self.show_error("No drive letter is available.");
            return;
        };
        let mapped = device.is_some()
            || window
                .snapshot()
                .is_some_and(|snapshot| snapshot.drive(&letter).is_some());
        let kind = self.media_kind().as_str();
        let editor = self.downgrade();
        glib::spawn_future_local(async move {
            let Some(path) = files::open_file(&window, "Choose Installation Image", None).await
            else {
                return;
            };
            let Some(this) = editor.upgrade() else { return };
            let serial = this.imp().candidate_serial.get() + 1;
            this.imp().candidate_serial.set(serial);
            this.set_candidates(&[]);
            let device = window.message("command", json!({"arguments": ["device", letter, kind]}));
            let attach =
                window.message("command", json!({"arguments": ["map-image", letter, path]}));
            let snapshot = window.message("snapshot", json!({}));
            let browse = Self::browser(&window);
            let result = this
                .media_work(&window, move || {
                    if !mapped {
                        engine::request(&device)?;
                    }
                    engine::request(&attach)?;
                    let snapshot = engine::request_as::<Snapshot>(&snapshot)?;
                    Ok((snapshot, choice::installer_candidates(browse, &letter)))
                })
                .await;
            let Some((snapshot, candidates)) = result else {
                return;
            };
            window.apply_snapshot(snapshot);
            this.set_candidates(&candidates);
            this.media_controls();
            if candidates.is_empty() {
                this.show_error(
                    "No suggested installer found. Browse this drive to choose a program.",
                );
            }
        });
    }

    /// Validate, then save the launcher or start the program.
    fn submit(&self) {
        let imp = self.imp();
        let (Some(window), Some(dialog)) = (self.window(), self.dialog()) else {
            return;
        };
        let Some(snapshot) = window.snapshot() else {
            return;
        };
        let action = editor_action(imp);
        let name = imp.name_row.text().trim().to_owned();
        let program = imp.program_row.text().trim().to_owned();
        let arguments = imp.arguments_row.text().to_string();
        let original_name = imp.original_name.borrow().clone();
        if let Err(message) = validate(
            action,
            &name,
            &program,
            original_name.as_deref(),
            &snapshot.manifest.applications,
        ) {
            dialog.show_error(message);
            return;
        }
        let (directories, environment) = self.overrides();
        let cwd = imp.cwd_row.text().to_string();
        let parse = window.message("parse-arguments", json!({"text": arguments}));
        if action != ProgramAction::Save {
            glib::spawn_future_local(async move {
                let parsed = dialog
                    .run(&window, Work::default(), move || {
                        engine::request_as::<Vec<String>>(&parse)
                    })
                    .await;
                let Some(argv) = parsed else { return };
                let fields = json!({"workingDirectories": directories, "environment": environment});
                if window.launch(launch_arguments(action, &cwd, &program, argv), fields) {
                    dialog.close();
                } else {
                    dialog.show_error("The program could not be started. Check Logs for details.");
                }
            });
            return;
        }
        let Some(ball) = imp.ball.borrow().clone() else {
            return;
        };
        let old_record = original_name
            .as_ref()
            .and_then(|original| snapshot.application_records.get(original).cloned());
        let mut request = json!({
            "name": name,
            "program": program,
            "workingDirectories": directories,
            "environment": environment,
            "cwd": cwd,
        });
        if let Some(original) = &original_name {
            request["originalName"] = json!(original);
        }
        if let Some(source) = imp.icon_source.borrow().as_ref() {
            request["iconSource"] = json!(source);
        }
        let mut save = window.message("save-application", request);
        let read = window.message("snapshot", json!({}));
        glib::spawn_future_local(async move {
            let saved = dialog
                .run(&window, Work::default(), move || {
                    save["arguments"] = json!(engine::request_as::<Vec<String>>(&parse)?);
                    engine::request(&save)?;
                    engine::request_as::<Snapshot>(&read)
                })
                .await;
            let Some(snapshot) = saved else { return };
            let Some(record) = snapshot.application_records.get(&name).cloned() else {
                window.apply_snapshot(snapshot);
                dialog.close();
                return;
            };
            let (undo, redo) = save_history(
                &name,
                original_name.as_deref(),
                old_record.as_deref(),
                &record,
            );
            let label = if original_name.is_some() {
                "Edit Application"
            } else {
                "Add Application"
            };
            if window.ball().as_deref() == Some(ball.as_path()) {
                window.apply_snapshot(snapshot);
            }
            window.add_history(HistoryItem {
                ball,
                undo,
                redo,
                label: label.into(),
            });
            dialog.close();
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn titles_and_labels_follow_the_action() {
        assert_eq!(title(ProgramAction::Save, true), "Edit Application");
        assert_eq!(title(ProgramAction::Save, false), "Add Application");
        assert_eq!(title(ProgramAction::Run, false), "Run Program");
        assert_eq!(title(ProgramAction::Install, false), "Install Software");
        assert_eq!(accept_label(ProgramAction::Install), "Install");
    }

    #[test]
    fn launchers_need_a_unique_name_and_program() {
        let mut applications = BTreeMap::new();
        applications.insert("Edit".to_owned(), Application::default());
        let save = ProgramAction::Save;
        assert!(validate(save, "", "C:\\E.EXE", None, &applications).is_err());
        assert!(validate(ProgramAction::Run, "", "", None, &applications).is_err());
        assert!(validate(ProgramAction::Run, "", "C:\\E.EXE", None, &applications).is_ok());
        assert_eq!(
            validate(save, "Edit", "C:\\E.EXE", None, &applications),
            Err("An application with this name already exists.")
        );
        assert!(validate(save, "Edit", "C:\\E.EXE", Some("Edit"), &applications).is_ok());
    }

    #[test]
    fn launch_arguments_add_the_initial_folder() {
        let argv = vec!["a b".to_owned(), String::new()];
        assert_eq!(
            launch_arguments(ProgramAction::Install, "D:\\", "D:\\INSTALL.EXE", argv),
            ["install", "--cwd", "D:\\", "D:\\INSTALL.EXE", "a b", ""]
        );
        assert_eq!(
            launch_arguments(ProgramAction::Run, "", "C:\\X.EXE", Vec::new()),
            ["run", "C:\\X.EXE"]
        );
    }

    #[test]
    fn renaming_history_restores_the_original_record() {
        let (undo, redo) = save_history("New", Some("Old"), Some("old-record"), "new-record");
        assert_eq!(undo[0]["arguments"], json!(["remove-app", "New"]));
        assert_eq!(
            undo[1]["arguments"],
            json!(["restore-app", "Old", "old-record"])
        );
        assert_eq!(redo[0]["arguments"], json!(["remove-app", "Old"]));
        assert_eq!(
            redo[1]["arguments"],
            json!(["restore-app", "New", "new-record"])
        );
        let (undo, redo) = save_history("Added", None, None, "record");
        assert_eq!(undo.len(), 1);
        assert_eq!(redo.len(), 1);
    }

    #[test]
    fn folder_overrides_stay_on_their_drive() {
        assert!(on_drive("d:\\work", "D"));
        assert!(!on_drive("C:\\", "D"));
        assert!(!on_drive("", "D"));
    }
}
