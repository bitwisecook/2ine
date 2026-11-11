// Public domain. No rights reserved. Wholly AI-authored.
//! The component tree's data: tri-state selection, search and row status.
//!
//! `plan-os2-extraction` returns a tree of groups, directories, files and
//! provider settings. Each file contributes its media source as a selectable
//! value unless 2ine provides it or a native provider disables its group.
//! Groups are checked, unchecked or mixed from the values beneath them;
//! filtering never changes the underlying selection.

use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::rc::Rc;

use gtk::glib;
use serde_json::{Map, Value};

use super::jobs::{self, Source};

/// Groups expanded when a component tree is first shown.
pub const DEFAULT_EXPANDED: [&str; 2] = ["all", "cmd"];

/// The check box state of one row.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[allow(clippy::struct_excessive_bools)]
pub struct CheckState {
    pub active: bool,
    pub mixed: bool,
    pub enabled: bool,
    pub visible: bool,
}

/// A row that remains visible for the current search, with its visible children.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VisibleNode {
    pub id: String,
    pub children: Vec<VisibleNode>,
}

/// The provider shown in a group's Status column, opening its setting.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProviderAction {
    pub label: String,
    pub tooltip: String,
    pub setting: String,
}

#[derive(Debug, Default)]
pub struct ComponentModel {
    pub root: String,
    order: Vec<String>,
    nodes: HashMap<String, Map<String, Value>>,
    children: HashMap<String, Vec<String>>,
    parents: HashMap<String, String>,
    values: HashMap<String, Vec<Source>>,
    sources: HashMap<String, Source>,
    pub disabled: HashSet<String>,
    pub selected: HashSet<Source>,
    pub sizes: HashMap<String, u64>,
    pub protected_sources: HashSet<Source>,
    pub active_ids: HashSet<String>,
    pub updating_ids: HashSet<String>,
    pub queued_ids: HashSet<String>,
    file_cache: RefCell<HashMap<String, Rc<[String]>>>,
}

fn text<'a>(node: &'a Map<String, Value>, key: &str) -> Option<&'a str> {
    node.get(key).and_then(Value::as_str)
}

fn flag(node: &Map<String, Value>, key: &str) -> bool {
    node.get(key).and_then(Value::as_bool) == Some(true)
}

impl ComponentModel {
    /// Index a plan tree and keep the ticked sources that it can offer.
    pub fn new(root: &Value, selected: &HashSet<String>) -> Self {
        let mut model = Self {
            root: root["id"].as_str().unwrap_or_default().to_owned(),
            ..Self::default()
        };
        let available = model.index(root, false, None);
        model.selected = available
            .into_iter()
            .filter(|source| selected.contains(&**source))
            .collect();
        let sizes: HashMap<String, u64> = model
            .values
            .iter()
            .map(|(id, values)| {
                let size = if model.kind(id) == "file" {
                    model.size_of(id)
                } else {
                    values.iter().map(|source| model.size_of(source)).sum()
                };
                (id.clone(), size)
            })
            .collect();
        model.sizes = sizes;
        model
    }

    fn size_of(&self, id: &str) -> u64 {
        self.nodes
            .get(id)
            .and_then(|node| node.get("size"))
            .and_then(Value::as_u64)
            .unwrap_or_default()
    }

    fn index(&mut self, data: &Value, inherited: bool, parent: Option<&str>) -> Vec<Source> {
        let id = data["id"].as_str().unwrap_or_default().to_owned();
        let mut node = data.as_object().cloned().unwrap_or_default();
        node.remove("children");
        let blocked = inherited || flag(&node, "disabled");
        if blocked {
            self.disabled.insert(id.clone());
        }
        let mut values: Vec<Source> = Vec::new();
        if !blocked
            && text(&node, "kind") == Some("file")
            && !flag(&node, "provided")
            && let Some(source) = text(&node, "source")
        {
            let source = self.source(source);
            values.push(source);
        }
        self.order.push(id.clone());
        self.nodes.insert(id.clone(), node);
        if let Some(parent) = parent {
            self.parents.insert(id.clone(), parent.to_owned());
        }
        let mut children = Vec::new();
        for child in data["children"].as_array().into_iter().flatten() {
            children.push(child["id"].as_str().unwrap_or_default().to_owned());
            values.extend(self.index(child, blocked, Some(&id)));
        }
        let mut seen = HashSet::new();
        values.retain(|source| seen.insert(source.clone()));
        self.children.insert(id.clone(), children);
        self.values.insert(id, values.clone());
        values
    }

    fn source(&mut self, source: &str) -> Source {
        self.sources
            .entry(source.to_owned())
            .or_insert_with(|| Source::from(source))
            .clone()
    }

    /// Intern a source name so that it matches this tree's shared values.
    pub fn known_source(&self, source: &str) -> Option<Source> {
        self.sources.get(source).cloned()
    }

    pub fn node(&self, id: &str) -> Option<&Map<String, Value>> {
        self.nodes.get(id)
    }

    pub fn node_mut(&mut self, id: &str) -> Option<&mut Map<String, Value>> {
        self.nodes.get_mut(id)
    }

    /// Every node id in tree order.
    pub fn ids(&self) -> &[String] {
        &self.order
    }

    pub fn kind(&self, id: &str) -> &str {
        self.nodes
            .get(id)
            .and_then(|node| text(node, "kind"))
            .unwrap_or_default()
    }

    pub fn title(&self, id: &str) -> String {
        self.nodes
            .get(id)
            .and_then(|node| text(node, "title"))
            .unwrap_or_default()
            .to_owned()
    }

    /// Selectable sources at or beneath a node.
    pub fn values(&self, id: &str) -> &[Source] {
        self.values.get(id).map(Vec::as_slice).unwrap_or_default()
    }

    /// Children shown in the tree; a provider group shows its setting in Status instead.
    pub fn visible_children(&self, id: &str) -> Vec<String> {
        let provider = self
            .nodes
            .get(id)
            .is_some_and(|node| node.get("provider").is_some_and(|value| !value.is_null()));
        self.children
            .get(id)
            .into_iter()
            .flatten()
            .filter(|child| !(provider && self.kind(child) == "setting"))
            .cloned()
            .collect()
    }

    /// The ids of a node and its ancestors' ids, nearest parent last.
    pub fn ancestors(&self, id: &str) -> Option<Vec<String>> {
        if !self.nodes.contains_key(id) {
            return None;
        }
        let mut path = Vec::new();
        let mut current = id;
        while let Some(parent) = self.parents.get(current) {
            path.push(parent.clone());
            current = parent;
        }
        path.reverse();
        Some(path)
    }

    pub fn state(&self, id: &str) -> CheckState {
        let values = self.values(id);
        let count = values
            .iter()
            .filter(|source| self.selected.contains(*source))
            .count();
        let node = self.nodes.get(id);
        let protected = node
            .and_then(|node| text(node, "source"))
            .is_some_and(|source| self.protected_sources.contains(source));
        CheckState {
            active: !values.is_empty() && count == values.len(),
            mixed: count > 0 && count < values.len(),
            enabled: !values.is_empty() && !protected,
            visible: node.and_then(|node| text(node, "kind")) != Some("setting"),
        }
    }

    /// Tick or untick everything beneath a node; ticking also ticks required groups.
    pub fn toggle(&mut self, id: &str) {
        let values = self.values(id).to_vec();
        if values.is_empty() {
            return;
        }
        if values.iter().all(|source| self.selected.contains(source)) {
            for source in &values {
                self.selected.remove(source);
            }
            return;
        }
        self.selected.extend(values);
        let mut pending = self.requires(id);
        let mut seen = HashSet::new();
        while let Some(requirement) = pending.pop() {
            if !self.nodes.contains_key(&requirement) || !seen.insert(requirement.clone()) {
                continue;
            }
            self.selected.extend(self.values(&requirement).to_vec());
            pending.extend(self.requires(&requirement));
        }
    }

    fn requires(&self, id: &str) -> Vec<String> {
        self.nodes
            .get(id)
            .and_then(|node| node.get("requires"))
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
            .map(str::to_owned)
            .collect()
    }

    /// Tick a whole component group and the groups it directly requires.
    pub fn select_group(&mut self, category: &str) -> bool {
        let values = self.values(category).to_vec();
        if values.is_empty() {
            return false;
        }
        self.selected.extend(values);
        for required in self.requires(category) {
            self.selected.extend(self.values(&required).to_vec());
        }
        true
    }

    pub fn complete(&self, id: &str) -> bool {
        let values = self.values(id);
        !values.is_empty() && values.iter().all(|source| self.selected.contains(source))
    }

    /// Components configured after installation, such as the SOM repository.
    pub fn configured_components(&self) -> Vec<String> {
        if self.complete("som") {
            vec!["som".into()]
        } else {
            Vec::new()
        }
    }

    /// File nodes beneath a node, cached per node.
    fn file_ids(&self, id: &str) -> Rc<[String]> {
        if let Some(files) = self.file_cache.borrow().get(id) {
            return files.clone();
        }
        let mut files = Vec::new();
        let mut pending = vec![id.to_owned()];
        while let Some(current) = pending.pop() {
            if self.kind(&current) == "file" {
                files.push(current);
            } else if let Some(children) = self.children.get(&current) {
                pending.extend(children.iter().rev().cloned());
            }
        }
        let files: Rc<[String]> = files.into();
        self.file_cache
            .borrow_mut()
            .insert(id.to_owned(), files.clone());
        files
    }

    pub fn provider_name(value: &str) -> String {
        match value {
            "mesa" => "Mesa".into(),
            "original" => "Original OS/2".into(),
            "off" => "Off".into(),
            "2ine" => "Provided by 2ine".into(),
            other => other.to_owned(),
        }
    }

    /// The Status column text.
    pub fn status(&self, id: &str) -> String {
        let Some(node) = self.nodes.get(id) else {
            return String::new();
        };
        if let Some(provider) = node.get("provider").and_then(Value::as_object) {
            return Self::provider_name(text(provider, "selection").unwrap_or("2ine"));
        }
        if self.disabled.contains(id) {
            return "Provided by 2ine".into();
        }
        match text(node, "kind") {
            Some("setting") => Self::provider_name(text(node, "selection").unwrap_or("2ine")),
            Some("file") => jobs::state_label(
                node,
                self.updating_ids.contains(id),
                self.queued_ids.contains(id),
            )
            .to_owned(),
            _ => {
                let files = self.file_ids(id);
                jobs::group_label(
                    self.values(id),
                    &self.selected,
                    files.iter().filter_map(|file| self.nodes.get(file)),
                )
            }
        }
    }

    /// A button that opens the setting choosing this group's provider.
    pub fn provider_action(&self, id: &str) -> Option<ProviderAction> {
        let node = self.nodes.get(id)?;
        let provider = match node.get("provider").and_then(Value::as_object) {
            Some(provider) => provider,
            None if text(node, "kind") == Some("setting") => node,
            None => return None,
        };
        let setting = text(provider, "setting").filter(|setting| !setting.is_empty())?;
        let selected = text(provider, "selection").unwrap_or("2ine");
        let native = provider
            .get("nativeSelected")
            .and_then(Value::as_bool)
            .unwrap_or(matches!(selected, "mesa" | "2ine"));
        let label = if native && selected == "mesa" {
            "2ine (Mesa)".to_owned()
        } else if native {
            "2ine".to_owned()
        } else {
            Self::provider_name(selected)
        };
        Some(ProviderAction {
            label,
            tooltip: format!(
                "Open {}",
                text(provider, "title").unwrap_or("2ine settings")
            ),
            setting: setting.to_owned(),
        })
    }

    pub fn tooltip(&self, id: &str) -> String {
        self.nodes
            .get(id)
            .map(jobs::file_tooltip)
            .unwrap_or_default()
    }

    pub fn icon(&self, id: &str) -> &'static str {
        if matches!(self.kind(id), "directory" | "group") {
            "folder-symbolic"
        } else {
            "document-properties-symbolic"
        }
    }

    pub fn size(&self, id: &str) -> u64 {
        self.sizes.get(id).copied().unwrap_or_default()
    }

    /// Rows that contain a working file: those whose source is listed, or,
    /// when no sources are known, whose planned or actual paths are listed.
    /// `paths` must be lower-case.
    pub fn activity_ids(
        &self,
        sources: &HashSet<Source>,
        paths: &HashSet<String>,
    ) -> HashSet<String> {
        let mut result = HashSet::new();
        if !self.root.is_empty() {
            self.visit_activity(&self.root, sources, paths, &mut result);
        }
        result
    }

    fn visit_activity(
        &self,
        id: &str,
        sources: &HashSet<Source>,
        paths: &HashSet<String>,
        result: &mut HashSet<String>,
    ) -> bool {
        let Some(node) = self.nodes.get(id) else {
            return false;
        };
        let own = text(node, "source").is_some_and(|source| sources.contains(source))
            || (sources.is_empty()
                && std::iter::once(text(node, "path").unwrap_or_default())
                    .chain(
                        node.get("actualPaths")
                            .and_then(Value::as_array)
                            .into_iter()
                            .flatten()
                            .filter_map(Value::as_str),
                    )
                    .any(|path| paths.contains(&path.to_lowercase())));
        let mut any = false;
        for child in self.children.get(id).into_iter().flatten() {
            any |= self.visit_activity(child, sources, paths, result);
        }
        if own || any {
            result.insert(id.to_owned());
        }
        own || any
    }

    /// The rows matching a search, with their ancestors and every descendant of a match.
    pub fn visible(&self, query: &str) -> Vec<VisibleNode> {
        let query = fold(query);
        self.visit_visible(&self.root, &query, false)
            .into_iter()
            .collect()
    }

    fn visit_visible(&self, id: &str, query: &str, inherited: bool) -> Option<VisibleNode> {
        let node = self.nodes.get(id)?;
        let matching = inherited || query.is_empty() || {
            let searchable = ["title", "name", "path"]
                .map(|key| text(node, key).unwrap_or_default())
                .join(" ");
            matches_folded(&fold(&searchable), query)
        };
        let children: Vec<VisibleNode> = self
            .visible_children(id)
            .iter()
            .filter_map(|child| self.visit_visible(child, query, matching))
            .collect();
        (matching || !children.is_empty()).then(|| VisibleNode {
            id: id.to_owned(),
            children,
        })
    }
}

/// Case-fold and strip combining marks for accent-insensitive search.
pub fn fold(value: &str) -> String {
    let decomposed = glib::normalize(glib::casefold(value), glib::NormalizeMode::All);
    decomposed
        .chars()
        .filter(|char| {
            !matches!(*char as u32, 0x0300..=0x036F | 0x1AB0..=0x1AFF | 0x1DC0..=0x1DFF | 0x20D0..=0x20FF | 0xFE20..=0xFE2F)
        })
        .collect()
}

/// Whether the query's characters appear in order in the text.
pub fn matches(text: &str, query: &str) -> bool {
    matches_folded(&fold(text), &fold(query))
}

fn matches_folded(text: &str, query: &str) -> bool {
    let mut remaining = text.chars();
    query
        .chars()
        .all(|wanted| remaining.any(|candidate| candidate == wanted))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn tree() -> Value {
        json!({"id": "all", "kind": "group", "children": [
            {"id": "help", "kind": "group", "children": [
                {"id": "directory", "kind": "directory", "path": "C:\\OS2\\DLL", "children": [
                    {"id": "zip!/iso!/help", "kind": "file", "source": "zip!/iso!/help",
                     "path": "C:\\OS2\\DLL\\HELPMGR.DLL", "title": "HELPMGR.DLL", "size": 10}
                ]}
            ]},
            {"id": "fonts", "kind": "group", "title": "Fonts", "children": [
                {"id": "font", "kind": "file", "source": "font", "path": "C:\\OS2\\DLL\\FONT.FON", "title": "FONT.FON", "size": 5}
            ]}
        ]})
    }

    fn sources(values: &[&str]) -> HashSet<Source> {
        values.iter().map(|value| Source::from(*value)).collect()
    }

    fn ids(values: &[&str]) -> HashSet<String> {
        values.iter().map(|value| (*value).to_owned()).collect()
    }

    fn model() -> ComponentModel {
        ComponentModel::new(&tree(), &HashSet::new())
    }

    #[test]
    fn exact_active_source_marks_only_file_and_ancestors() {
        assert_eq!(
            model().activity_ids(&sources(&["zip!/iso!/help"]), &HashSet::new()),
            ids(&["all", "help", "directory", "zip!/iso!/help"])
        );
    }

    #[test]
    fn removal_paths_match_case_insensitively() {
        assert_eq!(
            model().activity_ids(&HashSet::new(), &ids(&["c:\\os2\\dll\\font.fon"])),
            ids(&["all", "fonts", "font"])
        );
        assert!(
            model()
                .activity_ids(&HashSet::new(), &HashSet::new())
                .is_empty()
        );
    }

    #[test]
    fn install_planning_marks_selected_files_without_materializing_media() {
        assert_eq!(
            model().activity_ids(&sources(&["font"]), &HashSet::new()),
            ids(&["all", "fonts", "font"])
        );
    }

    #[test]
    fn groups_are_tri_state_and_sizes_sum_their_files() {
        let mut model = model();
        model.toggle("font");
        assert_eq!(
            model.state("fonts"),
            CheckState {
                active: true,
                mixed: false,
                enabled: true,
                visible: true
            }
        );
        assert!(model.state("all").mixed);
        assert_eq!(model.size("all"), 15);
        assert_eq!(model.status("all"), "1/2 selected · 0 installed");
        assert_eq!(model.status("font"), "To add");
        model.toggle("all");
        assert!(model.state("all").active);
        model.toggle("all");
        assert!(model.selected.is_empty());
    }

    #[test]
    fn requirements_follow_transitively_and_selection_is_limited_to_the_tree() {
        let tree = json!({"id": "all", "kind": "group", "children": [
            {"id": "som", "kind": "group", "requires": ["runtime"], "children": [
                {"id": "s", "kind": "file", "source": "s"}]},
            {"id": "runtime", "kind": "group", "requires": ["base"], "children": [
                {"id": "r", "kind": "file", "source": "r"}]},
            {"id": "base", "kind": "group", "children": [{"id": "b", "kind": "file", "source": "b"}]},
            {"id": "native", "kind": "group", "disabled": true, "children": [
                {"id": "n", "kind": "file", "source": "n"}]},
            {"id": "given", "kind": "file", "source": "given", "provided": true}
        ]});
        let mut model = ComponentModel::new(&tree, &ids(&["n", "missing", "given"]));
        assert!(model.selected.is_empty());
        model.toggle("som");
        assert_eq!(model.selected, sources(&["s", "r", "b"]));
        assert_eq!(model.configured_components(), ["som"]);
        assert_eq!(model.status("native"), "Provided by 2ine");
        assert!(!model.state("native").enabled);
        let mut direct = ComponentModel::new(&tree, &HashSet::new());
        assert!(direct.select_group("som"));
        assert_eq!(direct.selected, sources(&["s", "r"]));
    }

    #[test]
    fn protected_sources_disable_their_check() {
        let mut model = model();
        model.protected_sources = sources(&["font"]);
        assert!(!model.state("font").enabled);
        assert!(model.state("fonts").enabled);
    }

    #[test]
    fn providers_show_their_selection_and_hide_their_setting_row() {
        let tree = json!({"id": "all", "kind": "group", "children": [
            {"id": "opengl", "kind": "group", "disabled": true,
             "provider": {"title": "Selected OpenGL Provider", "selection": "mesa", "setting": "graphics.opengl", "nativeSelected": true},
             "children": [
                {"id": "opengl.provider", "kind": "setting", "selection": "mesa", "setting": "graphics.opengl"},
                {"id": "gl", "kind": "file", "source": "gl"}]},
            {"id": "printers", "kind": "group",
             "provider": {"title": "Printing Provided by 2ine", "selection": "original", "setting": "printing", "nativeSelected": false},
             "children": []}
        ]});
        let model = ComponentModel::new(&tree, &HashSet::new());
        assert_eq!(model.visible_children("opengl"), ["gl"]);
        assert_eq!(model.status("opengl"), "Mesa");
        let action = model.provider_action("opengl").unwrap();
        assert_eq!(action.label, "2ine (Mesa)");
        assert_eq!(action.tooltip, "Open Selected OpenGL Provider");
        assert_eq!(action.setting, "graphics.opengl");
        assert_eq!(
            model.provider_action("printers").unwrap().label,
            "Original OS/2"
        );
        assert_eq!(
            model.provider_action("opengl.provider").unwrap().label,
            "2ine (Mesa)"
        );
        assert!(model.provider_action("gl").is_none());
        assert!(!model.state("opengl.provider").visible);
    }

    #[test]
    fn search_keeps_ancestors_and_descendants_of_matches() {
        let model = model();
        let visible = model.visible("fnt");
        assert_eq!(visible.len(), 1);
        assert_eq!(visible[0].children.len(), 1);
        assert_eq!(visible[0].children[0].id, "fonts");
        assert_eq!(visible[0].children[0].children[0].id, "font");
        assert_eq!(model.visible("zzz"), []);
        assert!(matches("Ünïcode Fonts", "unicode"));
        assert!(!matches("abc", "cba"));
        assert_eq!(model.ancestors("font").unwrap(), ["all", "fonts"]);
    }
}
