// Public domain. No rights reserved. Wholly AI-authored.
//! Ball sections and the landing section chosen from engine readiness facts.

use serde_json::Value;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Section {
    pub name: &'static str,
    pub title: &'static str,
    pub icon: &'static str,
}

/// Sections in visible order; Alt+1 through Alt+6 follow this order.
pub const SECTIONS: [Section; 6] = [
    Section {
        name: "drives",
        title: "Drives",
        icon: "drive-harddisk-symbolic",
    },
    Section {
        name: "components",
        title: "Components",
        icon: "applications-system-symbolic",
    },
    Section {
        name: "application-installation",
        title: "Install",
        icon: "system-software-install-symbolic",
    },
    Section {
        name: "apps",
        title: "Applications",
        icon: "view-app-grid-symbolic",
    },
    Section {
        name: "settings",
        title: "Settings",
        icon: "preferences-system-symbolic",
    },
    Section {
        name: "activity",
        title: "Logs",
        icon: "document-open-recent-symbolic",
    },
];

pub fn section(name: &str) -> Option<&'static Section> {
    SECTIONS.iter().find(|section| section.name == name)
}

/// Map the engine's `ball-navigation-state` to the first section shown for a ball.
/// A newly created ball always starts in Drives.
pub fn initial_section(state: Option<&Value>, newly_created: bool) -> &'static str {
    if newly_created {
        return "drives";
    }
    match state.and_then(|state| state["page"].as_str()) {
        Some("applications") => "apps",
        Some("application-installation") => "application-installation",
        _ => "components",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn installation_stays_beside_applications() {
        let names: Vec<_> = SECTIONS.iter().map(|section| section.name).collect();
        assert_eq!(
            names,
            [
                "drives",
                "components",
                "application-installation",
                "apps",
                "settings",
                "activity"
            ]
        );
    }

    #[test]
    fn readiness_selects_the_landing_section() {
        assert_eq!(
            initial_section(Some(&json!({"page": "applications"})), true),
            "drives"
        );
        assert_eq!(
            initial_section(Some(&json!({"page": "applications"})), false),
            "apps"
        );
        assert_eq!(
            initial_section(Some(&json!({"page": "application-installation"})), false),
            "application-installation"
        );
        assert_eq!(
            initial_section(Some(&json!({"page": "system-components"})), false),
            "components"
        );
        assert_eq!(initial_section(None, false), "components");
    }
}
