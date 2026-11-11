// Public domain. No rights reserved. Wholly AI-authored.
//! The registry key tree shown beside the registry values.

use std::collections::BTreeMap;

use super::profile::Entry;
use super::registry_values::fold;
use super::table::Tree;

/// A registry key in the tree.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KeyNode {
    pub name: String,
    pub path: String,
}

const ROOTS: [(&str, &str); 4] = [
    ("hkcr", "HKEY_CLASSES_ROOT"),
    ("hkcu", "HKEY_CURRENT_USER"),
    ("hklm", "HKEY_LOCAL_MACHINE"),
    ("hku", "HKEY_USERS"),
];

fn build(
    path: &str,
    names: &BTreeMap<String, String>,
    children: &BTreeMap<String, Vec<String>>,
) -> Tree<KeyNode> {
    Tree {
        data: KeyNode {
            name: names[path].clone(),
            path: path.to_owned(),
        },
        children: children
            .get(path)
            .into_iter()
            .flatten()
            .map(|child| build(child, names, children))
            .collect(),
    }
}

/// The registry key tree under the four roots, and every key path in it.
pub fn registry_tree(entries: &[Entry]) -> (Vec<Tree<KeyNode>>, Vec<String>) {
    let mut children: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut names: BTreeMap<String, String> = BTreeMap::new();
    for (path, title) in ROOTS {
        names.insert(path.into(), title.into());
    }
    let mut applications: Vec<&str> = entries
        .iter()
        .map(|entry| entry.application.as_str())
        .collect();
    applications.sort_unstable();
    for application in applications {
        let folded = fold(application);
        let pieces: Vec<&str> = folded.split('\\').collect();
        if !names.contains_key(pieces[0]) {
            continue;
        }
        let mut parent = pieces[0].to_owned();
        for index in 1..pieces.len() {
            let path = pieces[..=index].join("\\");
            if !names.contains_key(&path) {
                names.insert(path.clone(), pieces[index].to_owned());
                children
                    .entry(parent.clone())
                    .or_default()
                    .push(path.clone());
            }
            parent = path;
        }
    }
    let roots = ROOTS
        .iter()
        .map(|(path, _)| build(path, &names, &children))
        .collect();
    (roots, names.into_keys().collect())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pages::settings::registry_values::to_hex;

    fn entry(application: &str, key: &str, data: &[u8]) -> Entry {
        Entry {
            application: application.into(),
            key: key.into(),
            data: to_hex(data),
        }
    }

    #[test]
    fn registry_keys_form_a_tree_under_the_standard_roots() {
        let (roots, paths) = registry_tree(&[
            entry("HKCU\\Software\\Fixture", "v:a", b""),
            entry("hkcu\\software", "k:", b""),
            entry("other\\key", "v:b", b""),
        ]);
        assert_eq!(roots.len(), 4);
        assert_eq!(roots[1].data.name, "HKEY_CURRENT_USER");
        assert_eq!(roots[1].children[0].data.path, "hkcu\\software");
        assert_eq!(roots[1].children[0].children[0].data.name, "fixture");
        assert!(paths.contains(&"hkcu\\software\\fixture".to_owned()));
        assert!(!paths.iter().any(|path| path.starts_with("other")));
    }
}
