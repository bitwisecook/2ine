// Public domain. No rights reserved. Wholly AI-authored.
//! Environment: values inherited from CONFIG.SYS and the runtime, and the
//! ball's overrides.

use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet};
use std::rc::Rc;

use adw::prelude::*;
use gtk::glib;

use super::{Context, flat_button};
use crate::dialogs::FormDialog;
use crate::model::Snapshot;
use crate::window::BallWindow;

/// One environment variable as shown in the list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Variable {
    /// The normalized name; the guest PATH is `OS2PATH`.
    pub name: String,
    /// The name stored in the ball override, if any.
    pub override_key: String,
    pub override_value: Option<String>,
    pub subtitle: String,
    /// The value an override dialog starts with.
    pub initial: String,
}

/// Normalize a variable name; the guest PATH is shown as `OS2PATH`.
pub fn normalized(key: &str) -> String {
    let key = key.trim().to_uppercase();
    if key == "PATH" { "OS2PATH".into() } else { key }
}

/// `SET` and `LIBPATH=` values from CONFIG.SYS text.
pub fn configured(config_sys: &str) -> BTreeMap<String, String> {
    let mut values = BTreeMap::new();
    for line in config_sys.lines() {
        let line = line.trim();
        let prefixed = |prefix: &str| {
            line.get(..prefix.len())
                .is_some_and(|start| start.eq_ignore_ascii_case(prefix))
        };
        let entry = if prefixed("SET ") {
            &line[4..]
        } else if prefixed("LIBPATH=") {
            line
        } else {
            continue;
        };
        if let Some((key, value)) = entry.split_once('=') {
            values.insert(normalized(key), value.trim().to_owned());
        }
    }
    values
}

/// Every variable known from CONFIG.SYS, the ball overrides and the runtime defaults.
pub fn variables(snapshot: &Snapshot) -> Vec<Variable> {
    let configured = configured(&snapshot.config_sys);
    let overrides: BTreeMap<String, (String, String)> = snapshot
        .manifest
        .environment
        .iter()
        .map(|(key, value)| (normalized(key), (key.clone(), value.clone())))
        .collect();
    let effective: BTreeMap<String, String> = snapshot
        .environment_defaults
        .iter()
        .map(|(key, value)| (normalized(key), value.clone()))
        .collect();
    let names: BTreeSet<&String> = configured
        .keys()
        .chain(overrides.keys())
        .chain(effective.keys())
        .collect();
    names
        .into_iter()
        .map(|name| {
            let (override_key, override_value) = overrides.get(name).map_or_else(
                || (name.clone(), None),
                |(key, value)| (key.clone(), Some(value.clone())),
            );
            let inherited = configured
                .get(name)
                .map(|value| format!("CONFIG.SYS: {value}"));
            let source = match &override_value {
                Some(value) => format!(
                    "{}\nBall override: {value}",
                    inherited.unwrap_or_else(|| "Runtime default overridden".into())
                ),
                None => inherited.unwrap_or_else(|| "Runtime default".into()),
            };
            let current = effective.get(name).cloned().unwrap_or_default();
            Variable {
                name: name.clone(),
                override_key,
                initial: override_value.clone().unwrap_or_else(|| current.clone()),
                override_value,
                subtitle: format!("{source}\nEffective: {current}"),
            }
        })
        .collect()
}

pub struct EnvironmentPage {
    pub page: adw::PreferencesPage,
    context: Rc<Context>,
    group: adw::PreferencesGroup,
    rows: RefCell<Vec<adw::ActionRow>>,
    shown: RefCell<Vec<Variable>>,
}

impl EnvironmentPage {
    pub fn new(context: &Rc<Context>) -> Rc<Self> {
        let page = adw::PreferencesPage::new();
        let group = adw::PreferencesGroup::builder()
            .title("Guest Environment")
            .description(
                "CONFIG.SYS supplies the starting values. Ball overrides take precedence and may refer to \
                 inherited values with %NAME%. Application-specific overrides apply when that application \
                 launches. OS2PATH is the guest PATH; the host PATH is separate.",
            )
            .build();
        let add = flat_button("Add Override…", None);
        group.set_header_suffix(Some(&add));
        page.add(&group);
        add.connect_clicked(glib::clone!(
            #[strong]
            context,
            move |_| {
                if let Some(window) = context.current() {
                    environment_dialog(&window, "", "");
                }
            }
        ));
        let environment = Rc::new(Self {
            page,
            context: context.clone(),
            group,
            rows: RefCell::default(),
            shown: RefCell::default(),
        });
        if let Some(snapshot) = context.current().and_then(|window| window.snapshot()) {
            environment.refresh(&snapshot);
        }
        environment
    }

    pub fn refresh(&self, snapshot: &Snapshot) {
        let variables = variables(snapshot);
        if *self.shown.borrow() == variables {
            return;
        }
        for row in self.rows.take() {
            self.group.remove(&row);
        }
        let mut rows = Vec::new();
        for variable in &variables {
            let row = adw::ActionRow::builder()
                .title(&variable.name)
                .subtitle(&variable.subtitle)
                .use_markup(false)
                .build();
            let edit = flat_button(
                if variable.override_value.is_some() {
                    "Edit Override"
                } else {
                    "Override…"
                },
                None,
            );
            let (key, value) = (variable.override_key.clone(), variable.initial.clone());
            let context = self.context.clone();
            edit.connect_clicked(move |_| {
                if let Some(window) = context.current() {
                    environment_dialog(&window, &key, &value);
                }
            });
            row.add_suffix(&edit);
            if variable.override_value.is_some() {
                let reset = flat_button("Use Inherited Value", Some("edit-clear-symbolic"));
                let key = variable.override_key.clone();
                let context = self.context.clone();
                reset.connect_clicked(move |_| {
                    if let Some(window) = context.current() {
                        window.execute(vec!["unset".into(), format!("env.{key}")]);
                    }
                });
                row.add_suffix(&reset);
            }
            self.group.add(&row);
            rows.push(row);
        }
        self.rows.replace(rows);
        self.shown.replace(variables);
    }
}

/// Add or change a ball environment override.
pub fn environment_dialog(window: &BallWindow, key: &str, value: &str) {
    let dialog = FormDialog::new(window, "Environment Variable", "Save").with_size(520, 320);
    let group = adw::PreferencesGroup::builder()
        .margin_top(18)
        .margin_bottom(24)
        .margin_start(24)
        .margin_end(24)
        .build();
    let name = adw::EntryRow::builder()
        .title("Variable Name")
        .text(key)
        .build();
    let text = adw::EntryRow::builder().title("Value").text(value).build();
    for row in [&name, &text] {
        row.connect_entry_activated(glib::clone!(
            #[weak]
            dialog,
            move |_| dialog.emit_by_name::<()>("submit", &[])
        ));
        group.add(row);
    }
    dialog.append(&group);
    dialog.connect_map(glib::clone!(
        #[weak]
        name,
        move |_| {
            name.grab_focus();
        }
    ));
    dialog.connect_submit(glib::clone!(
        #[weak]
        window,
        #[weak]
        name,
        #[weak]
        text,
        move |dialog| {
            window.execute(vec![
                "config".into(),
                format!("env.{}", name.text()),
                text.text().into(),
            ]);
            dialog.close();
        }
    ));
    dialog.present(Some(window));
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn config_sys_values_and_overrides_explain_their_source() {
        let snapshot: Snapshot = serde_json::from_value(json!({
            "configSys": "REM x\r\nSET PATH=C:\\OS2;\r\nLIBPATH=.;C:\\OS2\\DLL;\r\nset Prompt = $p$g\r\n",
            "manifest": {"environment": {"Prompt": "[$p]", "EXTRA": "1"}},
            "environmentDefaults": {"PATH": "C:\\OS2;", "PROMPT": "[$p]", "EXTRA": "1", "LIBPATH": ".;C:\\OS2\\DLL;"}
        }))
        .unwrap();
        let variables = variables(&snapshot);
        let names: Vec<_> = variables
            .iter()
            .map(|variable| variable.name.as_str())
            .collect();
        assert_eq!(names, ["EXTRA", "LIBPATH", "OS2PATH", "PROMPT"]);
        assert_eq!(
            variables[0].subtitle,
            "Runtime default overridden\nBall override: 1\nEffective: 1"
        );
        assert_eq!(
            variables[2].subtitle,
            "CONFIG.SYS: C:\\OS2;\nEffective: C:\\OS2;"
        );
        assert_eq!(variables[3].override_key, "Prompt");
        assert_eq!(
            variables[3].subtitle,
            "CONFIG.SYS: $p$g\nBall override: [$p]\nEffective: [$p]"
        );
        assert_eq!(variables[3].initial, "[$p]");
    }
}
