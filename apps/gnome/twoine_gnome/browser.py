# Public domain. No rights reserved. Wholly AI-authored.
"""Shared native GTK tree/table browser with adapter-owned rows and actions."""

from dataclasses import dataclass

import gi

from .browser_model import visible_tree

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Adw", "1")
try:
    from gi.repository import Adw, Gdk, Gio, GObject, Gtk, Pango
except ImportError as error:
    raise SystemExit("GTK 4 is required: " + str(error)) from error


@dataclass
class Column:
    key: str
    title: str
    text: object
    width: int = 160
    expand: bool = False
    align: float = 0
    sort_key: object = None
    action: object = None
    compact: bool = False


class BrowserItem(GObject.Object):
    def __init__(self, node):
        super().__init__()
        self.data = node.data
        self.children = Gio.ListStore.new(BrowserItem)
        for child in node.children:
            self.children.append(BrowserItem(child))


class NativeBrowser(Gtk.Box):
    styled_displays = set()

    def __init__(
        self,
        columns,
        *,
        title="Items",
        search_hint="Search",
        identity=None,
        children=None,
        expanded_changed=None,
        searchable=None,
        accept=None,
        selected=None,
        activated=None,
        check=None,
        toggled=None,
        icon=None,
        tooltip=None,
        muted=None,
        activity=None,
        dropped=None,
        context=None,
        leading_sort=None,
        single_click=False,
        height=180,
    ):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=8, vexpand=True)
        self.columns = columns
        self.identity = identity or (
            lambda data: data.get("id", data.get("path", data.get("name", "")))
        )
        self.on_expanded = expanded_changed
        self.watched_rows = {}
        self.rebuilding = False
        self.children = children or (lambda data: data.get("children", []))
        self.searchable = searchable or (
            lambda data: " ".join(str(data.get(key, "")) for key in ("title", "name", "path"))
        )
        self.accept, self.on_selected, self.on_activated = accept, selected, activated
        self.check, self.on_toggled, self.icon = check, toggled, icon
        self.tooltip, self.on_dropped, self.on_context = tooltip, dropped, context
        self.muted, self.activity = muted, activity
        self.leading_sort = leading_sort
        self.roots, self.expanded = [], set()
        self.last_query = ""
        self.bound_checks, self.bound_cells = {}, {}
        self.updating = False
        self.search = Gtk.SearchEntry(placeholder_text=search_hint, hexpand=True)
        self.search.update_property([Gtk.AccessibleProperty.LABEL], [search_hint])
        self.append(self.search)
        self.table = Gtk.ColumnView()
        self.table.set_single_click_activate(single_click)
        self.table.set_show_row_separators(True)
        self.table.add_css_class("data-table")
        self.table.add_css_class("twoine-files")
        self.table.update_property([Gtk.AccessibleProperty.LABEL], [title])
        display = self.get_display()
        if display not in self.styled_displays:
            provider = Gtk.CssProvider()
            provider.load_from_string(
                "columnview.twoine-files row:nth-child(even):not(:selected) { background-color: alpha(currentColor, 0.035); }"
            )
            Gtk.StyleContext.add_provider_for_display(
                display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
            )
            self.styled_displays.add(display)
        self.search.set_key_capture_widget(self.table)
        self.search.connect("search-changed", lambda *_: self.refresh())
        self.table.connect("activate", self.activate)
        self.view_columns = []
        for index, definition in enumerate(columns):
            factory = Gtk.SignalListItemFactory()
            factory.connect("setup", self.setup_cell, index)
            factory.connect("bind", self.bind_cell, index)
            factory.connect("unbind", self.unbind_cell, index)
            column = Gtk.ColumnViewColumn.new(definition.title, factory)
            column.set_resizable(True)
            column.set_expand(definition.expand)
            column.set_fixed_width(definition.width)
            if definition.sort_key:
                column.set_sorter(
                    Gtk.CustomSorter.new(
                        lambda a, b, *_, definition=definition: self.compare(a, b, definition)
                    )
                )
            self.table.append_column(column)
            self.view_columns.append(column)
        self.sorter = Gtk.MultiSorter.new()
        if leading_sort:
            self.sorter.append(Gtk.CustomSorter.new(self.compare_leading))
        self.sorter.append(self.table.get_sorter())
        if self.view_columns and columns[0].sort_key:
            self.table.sort_by_column(self.view_columns[0], Gtk.SortType.ASCENDING)
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=height)
        scroll.set_child(self.table)
        self.layout = Adw.BreakpointBin(
            child=scroll, vexpand=True, width_request=180, height_request=height
        )
        breakpoint = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 600sp"))
        for column, definition in zip(self.view_columns[1:], columns[1:], strict=True):
            if not definition.compact:
                breakpoint.add_setter(column, "visible", False)
        if self.view_columns:
            breakpoint.add_setter(self.view_columns[0], "fixed-width", -1)
        self.layout.add_breakpoint(breakpoint)
        self.append(self.layout)
        self.refresh()

    @staticmethod
    def ordering(left, right):
        return (
            Gtk.Ordering.SMALLER
            if left < right
            else Gtk.Ordering.LARGER
            if left > right
            else Gtk.Ordering.EQUAL
        )

    def compare(self, first, second, definition):
        return self.ordering(definition.sort_key(first.data), definition.sort_key(second.data))

    def compare_leading(self, first, second, *_):
        return self.ordering(self.leading_sort(first.data), self.leading_sort(second.data))

    def set_rows(self, rows, *, reset_search=False, expanded=None):
        self.roots = list(rows)
        if expanded is not None:
            self.expanded = set(expanded)
        if reset_search:
            self.search.set_text("")
        self.refresh()

    def refresh(self):
        selected = self.selected_data()
        if hasattr(self, "model") and not self.last_query:
            for i in range(self.model.get_n_items()):
                row = self.model.get_item(i)
                key = self.identity(row.get_item().data)
                if row.get_expanded():
                    self.expanded.add(key)
                else:
                    self.expanded.discard(key)
        query = self.search.get_text()
        self.last_query = query
        roots = Gio.ListStore.new(BrowserItem)
        for node in visible_tree(
            self.roots, query, text=self.searchable, children=self.children, accept=self.accept
        ):
            roots.append(BrowserItem(node))
        self.store = roots
        self.sorted = Gtk.SortListModel.new(roots, self.sorter)
        self.model = Gtk.TreeListModel.new(
            self.sorted,
            False,
            False,
            lambda node: (
                Gtk.SortListModel.new(node.children, self.sorter)
                if node.children.get_n_items()
                else None
            ),
        )
        self.selection = Gtk.SingleSelection.new(self.model)
        self.selection.set_autoselect(False)
        self.selection.set_can_unselect(True)
        self.selection.connect("notify::selected-item", self.selection_changed)
        self.table.set_model(self.selection)
        for row, handler in self.watched_rows.items():
            row.disconnect(handler)
        self.watched_rows = {}
        self.rebuilding = True
        if self.on_expanded:
            self.model.connect("items-changed", lambda *_: self.watch_expansion())
            self.watch_expansion()
        index = 0
        while index < self.model.get_n_items():
            row = self.model.get_item(index)
            if query or self.identity(row.get_item().data) in self.expanded:
                row.set_expanded(True)
            index += 1
        restored = self.focus(self.identity(selected)) if selected else False
        if selected and not restored and self.on_selected:
            self.on_selected(None)
        self.rebuilding = False

    def watch_expansion(self):
        for index in range(self.model.get_n_items()):
            row = self.model.get_item(index)
            if row not in self.watched_rows:
                self.watched_rows[row] = row.connect("notify::expanded", self.expansion_changed)

    def expansion_changed(self, row, _property):
        if self.rebuilding:
            return
        data = row.get_item().data
        key = self.identity(data)
        if row.get_expanded():
            self.expanded.add(key)
        else:
            self.expanded.discard(key)
        if self.on_expanded:
            self.on_expanded(data, row.get_expanded())

    def selected_data(self):
        row = self.selection.get_selected_item() if hasattr(self, "selection") else None
        return row.get_item().data if row else None

    def selection_changed(self, *_):
        if self.on_selected:
            self.on_selected(self.selected_data())

    def focus(self, key):
        def ancestors(nodes, path=()):
            for data in nodes:
                own = self.identity(data)
                if own == key:
                    return path
                found = ancestors(self.children(data), (*path, own))
                if found is not None:
                    return found
            return None

        parents = ancestors(self.roots)
        if parents is None:
            return False
        index = 0
        while index < self.model.get_n_items():
            row = self.model.get_item(index)
            own = self.identity(row.get_item().data)
            if own in parents:
                row.set_expanded(True)
            if own == key:
                self.selection.set_selected(index)
                self.table.scroll_to(index, None, Gtk.ListScrollFlags.FOCUS, None)
                return True
            index += 1
        return False

    def setup_cell(self, _factory, item, index):
        definition = self.columns[index]
        if index == 0:
            expander = Gtk.TreeExpander()
            box = Gtk.Box(spacing=8)
            if self.check:
                check = Gtk.CheckButton()
                check.connect("toggled", self.toggled)
                box.append(check)
            if self.icon:
                box.append(Gtk.Image())
            if self.activity:
                box.append(Gtk.Spinner())
            box.append(Gtk.Label(xalign=0, ellipsize=Pango.EllipsizeMode.END, hexpand=True))
            if self.on_dropped:
                target = Gtk.DropTarget.new(Gdk.FileList, Gdk.DragAction.COPY)
                target.connect("drop", lambda _target, files, _x, _y: self.drop(item, files))
                box.add_controller(target)
            if self.on_context:
                click = Gtk.GestureClick(button=Gdk.BUTTON_SECONDARY)
                click.connect(
                    "pressed", lambda gesture, _count, x, y: self.context(item, gesture, x, y)
                )
                box.add_controller(click)
            expander.set_child(box)
            item.set_child(expander)
        elif definition.action:
            item.set_child(Gtk.Box(spacing=4))
        else:
            item.set_child(Gtk.Label(xalign=definition.align, ellipsize=Pango.EllipsizeMode.END))

    def bind_cell(self, _factory, item, index):
        data = item.get_item().get_item().data
        widget, definition = item.get_child(), self.columns[index]
        self.bound_cells[item] = (data, index)
        if index == 0:
            widget.set_list_row(item.get_item())
            box = widget.get_child()
            child = box.get_first_child()
            if self.check:
                self.bound_checks[child] = data
                self.update_check(child, data)
                child = child.get_next_sibling()
            if self.icon:
                child.set_from_icon_name(self.icon(data))
                child = child.get_next_sibling()
            if self.activity:
                running = bool(self.activity(data))
                child.set_spinning(running)
                child.set_visible(running)
                child = child.get_next_sibling()
            child.set_text(str(definition.text(data)))
            if self.muted and self.muted(data):
                child.add_css_class("dim-label")
            else:
                child.remove_css_class("dim-label")
            widget.set_tooltip_text(
                self.tooltip(data) if self.tooltip else str(definition.text(data))
            )
        elif definition.action:
            while (child := widget.get_first_child()) is not None:
                widget.remove(child)
            action = definition.action(data)
            if action:
                child = Gtk.Button(label=action["label"])
                child.add_css_class("flat")
                child.set_tooltip_text(action.get("tooltip"))
                child.connect("clicked", lambda *_: action["activate"]())
            else:
                child = Gtk.Label(label=str(definition.text(data)), xalign=definition.align)
            widget.append(child)
        else:
            widget.set_text(str(definition.text(data)))
            widget.set_tooltip_text(str(definition.text(data)))

    def unbind_cell(self, _factory, item, index):
        self.bound_cells.pop(item, None)
        if index == 0:
            if self.check:
                self.bound_checks.pop(item.get_child().get_child().get_first_child(), None)
            item.get_child().set_list_row(None)

    def update_check(self, button, data):
        state = self.check(data)
        self.updating = True
        try:
            button.set_active(state.active)
            button.set_inconsistent(state.mixed)
            button.set_sensitive(state.enabled)
            button.set_visible(state.visible)
            button.update_property(
                [Gtk.AccessibleProperty.LABEL], ["Select " + str(self.columns[0].text(data))]
            )
        finally:
            self.updating = False

    def refresh_cells(self):
        for item, (_data, index) in list(self.bound_cells.items()):
            if item.get_item():
                self.bind_cell(None, item, index)

    def toggled(self, button):
        if not self.updating and button in self.bound_checks and self.on_toggled:
            self.on_toggled(self.bound_checks[button])
            self.refresh_cells()

    def activate(self, _view, position):
        row = self.model.get_item(position)
        if row and self.on_activated:
            self.on_activated(row.get_item().data)

    def drop(self, item, files):
        row = item.get_item()
        return bool(row and self.on_dropped and self.on_dropped(files, row.get_item().data))

    def context(self, item, gesture, x, y):
        row = item.get_item()
        if row and self.on_context:
            self.on_context(row.get_item().data, gesture.get_widget(), x, y)
