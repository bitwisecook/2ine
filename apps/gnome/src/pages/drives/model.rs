// Public domain. No rights reserved. Wholly AI-authored.
//! Search, hierarchy pruning and optional check state for native browsers.
//!
//! Search is an accent- and case-insensitive ordered subsequence match.
//! Filtering never changes the rows it is given: visible nodes share the
//! caller's data, and checked selections live in [`CheckedTree`].

use std::collections::{BTreeSet, HashMap, HashSet};

use gtk::glib;
use serde_json::Value;

/// Case-folded text with compatibility decomposition and combining marks removed.
pub fn folded(value: &str) -> String {
    glib::normalize(glib::casefold(value), glib::NormalizeMode::All)
        .chars()
        .filter(|&character| !is_combining(character))
        .collect()
}

fn is_combining(character: char) -> bool {
    matches!(
        u32::from(character),
        0x0300..=0x036F | 0x1AB0..=0x1AFF | 0x1DC0..=0x1DFF | 0x20D0..=0x20FF | 0xFE20..=0xFE2F
    )
}

/// Whether every character of `query` occurs in `text`, in order.
pub fn matches(text: &str, query: &str) -> bool {
    let text = folded(text);
    let mut remaining = text.chars();
    folded(query)
        .chars()
        .all(|wanted| remaining.any(|candidate| candidate == wanted))
}

/// A row kept by [`visible_tree`] with its kept descendants.
#[derive(Debug, Clone)]
pub struct VisibleNode<T> {
    pub data: T,
    pub children: Vec<VisibleNode<T>>,
}

/// Prune a tree to rows matching `query` and accepted by `accept`.
///
/// A matching row keeps all of its descendants (subject to `accept`); a
/// non-matching row is kept only as an ancestor of a match.
pub fn visible_tree<T: Clone>(
    roots: &[T],
    query: &str,
    text: &dyn Fn(&T) -> String,
    children: &dyn Fn(&T) -> Vec<T>,
    accept: Option<&dyn Fn(&T) -> bool>,
) -> Vec<VisibleNode<T>> {
    fn visit<T: Clone>(
        data: &T,
        inherited: bool,
        query: &str,
        text: &dyn Fn(&T) -> String,
        children: &dyn Fn(&T) -> Vec<T>,
        accept: Option<&dyn Fn(&T) -> bool>,
    ) -> Option<VisibleNode<T>> {
        let matching = inherited || query.is_empty() || matches(&text(data), query);
        let descendants: Vec<_> = children(data)
            .iter()
            .filter_map(|child| visit(child, matching, query, text, children, accept))
            .collect();
        let allowed = accept.is_none_or(|accept| accept(data));
        (!descendants.is_empty() || (matching && allowed)).then(|| VisibleNode {
            data: data.clone(),
            children: descendants,
        })
    }
    roots
        .iter()
        .filter_map(|root| visit(root, false, query, text, children, accept))
        .collect()
}

/// The presentation of a tri-state check box.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[expect(
    clippy::struct_excessive_bools,
    reason = "independent check box properties"
)]
pub struct CheckState {
    pub active: bool,
    pub mixed: bool,
    pub enabled: bool,
    pub visible: bool,
}

pub fn check_state<S: std::hash::BuildHasher>(
    values: &BTreeSet<String>,
    selected: &HashSet<String, S>,
    visible: bool,
) -> CheckState {
    let count = values
        .iter()
        .filter(|value| selected.contains(*value))
        .count();
    CheckState {
        active: !values.is_empty() && count == values.len(),
        mixed: 0 < count && count < values.len(),
        enabled: !values.is_empty(),
        visible,
    }
}

/// Selected values for a tree of JSON nodes with `id`, `children`, `requires`,
/// `disabled` and `kind` fields. Filtering never changes the selection.
#[allow(
    dead_code,
    reason = "shared browser model for checked trees such as OS/2 component files"
)]
#[derive(Debug, Clone, Default)]
pub struct CheckedTree {
    pub nodes: HashMap<String, Value>,
    pub values: HashMap<String, BTreeSet<String>>,
    pub disabled: HashSet<String>,
    pub selected: HashSet<String>,
}

#[allow(
    dead_code,
    reason = "shared browser model for checked trees such as OS/2 component files"
)]
impl CheckedTree {
    /// Index `roots`; `value` names the selectable value a node contributes.
    pub fn new(
        roots: &[Value],
        selected: impl IntoIterator<Item = String>,
        value: &dyn Fn(&Value) -> Option<String>,
    ) -> Self {
        Self::with_children(roots, selected, value, &|data| {
            data["children"].as_array().cloned().unwrap_or_default()
        })
    }

    pub fn with_children(
        roots: &[Value],
        selected: impl IntoIterator<Item = String>,
        value: &dyn Fn(&Value) -> Option<String>,
        children: &dyn Fn(&Value) -> Vec<Value>,
    ) -> Self {
        let mut tree = Self::default();
        let mut available = BTreeSet::new();
        for root in roots {
            available.extend(tree.index(root, false, value, children));
        }
        tree.selected = selected
            .into_iter()
            .filter(|value| available.contains(value))
            .collect();
        tree
    }

    fn index(
        &mut self,
        data: &Value,
        inherited: bool,
        value: &dyn Fn(&Value) -> Option<String>,
        children: &dyn Fn(&Value) -> Vec<Value>,
    ) -> BTreeSet<String> {
        let key = node_id(data);
        self.nodes.insert(key.clone(), data.clone());
        let blocked = inherited || data["disabled"].as_bool() == Some(true);
        if blocked {
            self.disabled.insert(key.clone());
        }
        let mut values: BTreeSet<String> = if blocked { None } else { value(data) }
            .into_iter()
            .collect();
        for child in children(data) {
            values.extend(self.index(&child, blocked, value, children));
        }
        self.values.insert(key, values.clone());
        values
    }

    pub fn state(&self, key: &str) -> CheckState {
        let empty = BTreeSet::new();
        let visible = self
            .nodes
            .get(key)
            .is_none_or(|node| node["kind"] != "setting");
        check_state(
            self.values.get(key).unwrap_or(&empty),
            &self.selected,
            visible,
        )
    }

    /// Select or clear a node's values; selecting adds the values of every required node.
    pub fn toggle(&mut self, key: &str) {
        let Some(values) = self
            .values
            .get(key)
            .filter(|values| !values.is_empty())
            .cloned()
        else {
            return;
        };
        if values.iter().all(|value| self.selected.contains(value)) {
            for value in &values {
                self.selected.remove(value);
            }
            return;
        }
        self.selected.extend(values);
        let mut pending = requirements(&self.nodes[key]);
        let mut seen = HashSet::new();
        while let Some(requirement) = pending.pop() {
            if !self.nodes.contains_key(&requirement) || !seen.insert(requirement.clone()) {
                continue;
            }
            self.selected
                .extend(self.values[&requirement].iter().cloned());
            pending.extend(requirements(&self.nodes[&requirement]));
        }
    }

    pub fn complete(&self, key: &str) -> bool {
        self.values.get(key).is_some_and(|values| {
            !values.is_empty() && values.iter().all(|value| self.selected.contains(value))
        })
    }
}

fn node_id(data: &Value) -> String {
    match &data["id"] {
        Value::String(text) => text.clone(),
        other => other.to_string(),
    }
}

fn requirements(data: &Value) -> Vec<String> {
    data["requires"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(Value::as_str)
        .map(str::to_owned)
        .collect()
}

#[cfg(test)]
mod tests {
    use std::rc::Rc;

    use serde_json::json;

    use super::*;

    fn nodes() -> Value {
        json!({
            "id": "all", "title": "All Files", "kind": "group",
            "children": [
                {"id": "som", "title": "Object Runtime", "kind": "group", "requires": ["runtime"], "children": [
                    {"id": "a", "title": "SOM.DLL", "kind": "file", "source": "a"},
                    {"id": "b", "title": "SOMD.DLL", "kind": "file", "source": "b"},
                    {"id": "host", "title": "Host API", "kind": "file", "source": "host", "disabled": true},
                ]},
                {"id": "runtime", "title": "C Runtime", "kind": "group", "children": [
                    {"id": "c", "title": "LIBUNI.DLL", "kind": "file", "source": "c"},
                ]},
                {"id": "settings", "title": "Provider", "kind": "setting"},
            ],
        })
    }

    fn file_value(data: &Value) -> Option<String> {
        (data["kind"] == "file" && data["disabled"] != true)
            .then(|| data["source"].as_str().unwrap_or_default().to_owned())
    }

    fn source_value(data: &Value) -> Option<String> {
        if data["disabled"] == true {
            None
        } else {
            data["source"].as_str().map(str::to_owned)
        }
    }

    fn set(values: &[&str]) -> HashSet<String> {
        values.iter().map(ToString::to_string).collect()
    }

    fn selected(values: &[&str]) -> Vec<String> {
        values.iter().map(ToString::to_string).collect()
    }

    fn json_children(node: &Value) -> Vec<Value> {
        node["children"].as_array().cloned().unwrap_or_default()
    }

    #[test]
    fn fuzzy_search_is_accent_insensitive_and_ordered() {
        assert_eq!(folded("CAFÉ"), "cafe");
        assert!(matches("Café Report.txt", "cf rpt"));
        assert!(matches("report", ""));
        assert!(!matches("Café Report.txt", "trfe"));
    }

    #[test]
    fn filtered_flat_browse_keeps_the_adapter_default_filter() {
        let rows: Vec<Rc<(String, bool)>> = [
            ("Programs", true),
            ("REPORT.EXE", false),
            ("REPORT.TXT", false),
        ]
        .into_iter()
        .map(|(name, directory)| Rc::new((name.to_owned(), directory)))
        .collect();
        let accepted = |row: &Rc<(String, bool)>| {
            row.1
                || std::path::Path::new(&row.0)
                    .extension()
                    .is_some_and(|extension| extension.eq_ignore_ascii_case("exe"))
        };
        let text = |row: &Rc<(String, bool)>| row.0.clone();
        let none = |_: &Rc<(String, bool)>| Vec::new();
        let names = |nodes: Vec<VisibleNode<Rc<(String, bool)>>>| {
            nodes
                .into_iter()
                .map(|node| node.data.0.clone())
                .collect::<Vec<_>>()
        };
        assert_eq!(
            names(visible_tree(&rows, "", &text, &none, Some(&accepted))),
            ["Programs", "REPORT.EXE"]
        );
        assert_eq!(
            names(visible_tree(&rows, "rpt", &text, &none, Some(&accepted))),
            ["REPORT.EXE"]
        );
    }

    #[test]
    fn tree_search_keeps_ancestors_and_original_row_identity() {
        #[derive(Debug)]
        struct Node {
            id: &'static str,
            title: &'static str,
            children: Vec<Rc<Node>>,
        }
        let leaf = |id, title| {
            Rc::new(Node {
                id,
                title,
                children: vec![],
            })
        };
        let b = leaf("b", "SOMD.DLL");
        let som = Rc::new(Node {
            id: "som",
            title: "Object Runtime",
            children: vec![leaf("a", "SOM.DLL"), b.clone(), leaf("host", "Host API")],
        });
        let root = Rc::new(Node {
            id: "all",
            title: "All Files",
            children: vec![som],
        });
        let text = |node: &Rc<Node>| node.title.to_owned();
        let children = |node: &Rc<Node>| node.children.clone();
        let found = visible_tree(
            std::slice::from_ref(&root),
            "SOMD.DLL",
            &text,
            &children,
            None,
        );
        assert!(Rc::ptr_eq(&found[0].data, &root));
        assert_eq!(
            found[0]
                .children
                .iter()
                .map(|node| node.data.id)
                .collect::<Vec<_>>(),
            ["som"]
        );
        assert_eq!(
            found[0].children[0]
                .children
                .iter()
                .map(|node| node.data.id)
                .collect::<Vec<_>>(),
            ["b"]
        );
        assert!(Rc::ptr_eq(&found[0].children[0].children[0].data, &b));
        assert_eq!(root.children[0].children.len(), 3);
        assert!(visible_tree(&[root], "absent", &text, &children, None).is_empty());
    }

    #[test]
    fn matching_group_retains_its_descendants_without_changing_selection() {
        let root = nodes();
        let checks = CheckedTree::new(std::slice::from_ref(&root), selected(&["a"]), &file_value);
        let text = |node: &Value| node["title"].as_str().unwrap_or_default().to_owned();
        let found = visible_tree(&[root], "Object Runtime", &text, &json_children, None);
        assert_eq!(found[0].children[0].children.len(), 3);
        assert_eq!(checks.selected, set(&["a"]));
        assert!(checks.state("som").mixed);
        assert!(!checks.state("som").active);
    }

    #[test]
    fn parent_toggle_adds_requirements_but_leaf_omissions_remain_explicit() {
        let mut checks =
            CheckedTree::new(&[nodes()], selected(&["arbitrary", "host"]), &file_value);
        assert!(checks.selected.is_empty());
        checks.toggle("som");
        assert_eq!(checks.selected, set(&["a", "b", "c"]));
        assert!(checks.complete("som"));
        checks.toggle("b");
        assert_eq!(checks.selected, set(&["a", "c"]));
        assert!(!checks.complete("som"));
        assert!(checks.state("som").mixed);
        assert!(!checks.state("host").enabled);
        assert!(!checks.state("settings").visible);
        checks.toggle("a");
        assert_eq!(checks.selected, set(&["c"]));
        assert!(!checks.state("som").mixed);
    }

    #[test]
    fn cyclic_group_requirements_terminate_and_whole_root_can_clear() {
        let mut root = nodes();
        root["children"][1]["requires"] = json!(["som"]);
        let mut checks = CheckedTree::new(&[root], Vec::new(), &source_value);
        checks.toggle("som");
        assert_eq!(checks.selected, set(&["a", "b", "c"]));
        checks.toggle("all");
        assert!(checks.selected.is_empty());
    }

    #[test]
    fn disabled_group_blocks_descendants_but_settings_remain_visible() {
        let mut root = nodes();
        root["children"][0]["disabled"] = json!(true);
        root["children"][0]["children"]
            .as_array_mut()
            .expect("children")
            .push(json!({"id": "native-settings", "kind": "setting", "disabled": false}));
        let mut checks = CheckedTree::new(&[root], selected(&["a", "b"]), &|data| {
            data["source"].as_str().map(str::to_owned)
        });
        assert!(checks.selected.is_empty());
        assert!(!checks.state("som").enabled);
        assert!(!checks.state("a").enabled);
        assert!(!checks.state("native-settings").visible);
        assert!(checks.nodes.contains_key("native-settings"));
        checks.toggle("som");
        assert!(checks.selected.is_empty());
        checks.toggle("all");
        assert_eq!(checks.selected, set(&["c"]));
    }

    #[test]
    fn directory_selection_does_not_add_semantic_component_requirements() {
        let mut root = nodes();
        let files = root["children"][0]["children"].take();
        root["children"][0]["children"] = json!([{"id": "som-dir", "kind": "directory", "path": "C:\\OS2\\DLL", "children": files}]);
        let mut checks = CheckedTree::new(&[root], Vec::new(), &source_value);
        checks.toggle("som-dir");
        assert_eq!(checks.selected, set(&["a", "b"]));
        checks.toggle("b");
        assert!(checks.state("som-dir").mixed);
        checks.toggle("som");
        assert_eq!(checks.selected, set(&["a", "b", "c"]));
    }
}
