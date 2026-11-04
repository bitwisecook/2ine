# Public domain. No rights reserved. Wholly AI-authored.
"""Map core readiness facts to an initial page without affecting later refreshes."""

PAGE_ITEMS = (
    ("drives", "Drives", "drive-harddisk-symbolic"),
    ("components", "OS/2 Components", "applications-system-symbolic"),
    ("application-installation", "Application Installation", "system-software-install-symbolic"),
    ("apps", "Applications", "application-x-executable-symbolic"),
    ("settings", "Settings", "preferences-system-symbolic"),
    ("activity", "Logs", "document-open-recent-symbolic"),
)
PAGES = tuple(name for name, _title, _icon in PAGE_ITEMS)


def initial_page(state, *, newly_created=False):
    if newly_created:
        return "drives"
    return {
        "applications": "apps",
        "application-installation": "application-installation",
        "system-components": "components",
    }.get((state or {}).get("page"), "components")
