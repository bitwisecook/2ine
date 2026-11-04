# Public domain. No rights reserved. Wholly AI-authored.
"""Exact component selection using the shared native tree/table browser."""

from gi.repository import GLib

if __package__:
    from .gnome_browser import Column, NativeBrowser
    from .gnome_browser_model import CheckedTree
else:
    from gnome_browser import Column, NativeBrowser
    from gnome_browser_model import CheckedTree


class ComponentTree(NativeBrowser):
    def __init__(self, root, selected, *, changed, setting):
        self.root, self.changed, self.setting = root, changed, setting
        self.checks = CheckedTree(
            [root],
            selected,
            value=lambda data: (
                data["source"]
                if data["kind"] == "file" and not data.get("provided") and not data.get("disabled")
                else None
            ),
        )
        self.nodes, self.sources, self.selected = (
            self.checks.nodes,
            self.checks.values,
            self.checks.selected,
        )
        self.sizes = {
            key: self.nodes[key].get("size", 0)
            if self.nodes[key]["kind"] == "file"
            else sum(self.nodes[source].get("size", 0) for source in sources)
            for key, sources in self.sources.items()
        }
        super().__init__(
            [
                Column("name", "Component", lambda data: data["title"], width=380, expand=True),
                Column(
                    "status",
                    "Status",
                    self.status,
                    width=180,
                    action=self.provider_action,
                    compact=True,
                ),
                Column(
                    "size",
                    "Size",
                    lambda data: (
                        GLib.format_size(self.sizes[data["id"]]) if self.sizes[data["id"]] else ""
                    ),
                    width=90,
                    align=1,
                    sort_key=lambda data: self.sizes[data["id"]],
                ),
            ],
            title="Components and files",
            search_hint="Search Components and Files",
            identity=lambda data: data["id"],
            check=lambda data: self.checks.state(data["id"]),
            toggled=self.toggle_node,
            tooltip=lambda data: data.get("path") or data.get("detail") or data["title"],
            height=280,
        )
        self.set_rows([root], expanded={root["id"]})

    @staticmethod
    def provider_name(value):
        return {
            "mesa": "Mesa",
            "original": "Original OS/2",
            "off": "Off",
            "2ine": "Provided by 2ine",
        }.get(value, value)

    def provider_action(self, node):
        if node["kind"] != "setting" or not node.get("setting"):
            return None
        return {
            "label": self.provider_name(node.get("selection", "Settings")),
            "tooltip": "Open " + node.get("title", "2ine settings"),
            "activate": lambda: self.setting(node["setting"]),
        }

    def status(self, node):
        if node.get("provider"):
            return self.provider_name(node["provider"].get("selection", "2ine"))
        if node["kind"] == "file":
            return "Installed" if node.get("present") else "Available"
        sources = self.sources.get(node["id"], set())
        count = len(sources & self.selected)
        return f"{count} of {len(sources)} selected" if sources else "No files on this media"

    def toggle_node(self, node):
        self.checks.toggle(node["id"])
        self.changed()

    def focus_group(self, category):
        self.focus(category)

    def configured_components(self):
        return ["som"] if self.checks.complete("som") else []
