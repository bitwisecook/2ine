# Public domain. No rights reserved. Wholly AI-authored.
"""Search, hierarchy pruning and optional check state for native browsers."""

import unicodedata
from dataclasses import dataclass


def folded(value):
    return "".join(
        char
        for char in unicodedata.normalize("NFKD", value.casefold())
        if not unicodedata.combining(char)
    )


def matches(text, query):
    remaining = iter(folded(text))
    return all(any(char == candidate for candidate in remaining) for char in folded(query))


@dataclass
class VisibleNode:
    data: dict
    children: list


def visible_tree(roots, query, *, text, children, accept=None):
    def visit(data, inherited=False):
        matching = inherited or not query or matches(text(data), query)
        descendants = [
            node for child in children(data) if (node := visit(child, matching)) is not None
        ]
        allowed = accept is None or accept(data)
        if descendants or (matching and allowed):
            return VisibleNode(data, descendants)
        return None

    return [node for root in roots if (node := visit(root)) is not None]


@dataclass(frozen=True)
class CheckState:
    active: bool
    mixed: bool
    enabled: bool
    visible: bool = True


def check_state(values, selected, *, visible=True):
    count = len(values & selected)
    return CheckState(
        bool(values) and count == len(values), 0 < count < len(values), bool(values), visible
    )


class CheckedTree:
    """Adapter-owned values; filtering never changes the underlying selection."""

    def __init__(self, roots, selected, *, value, children=lambda data: data.get("children", [])):
        self.nodes, self.values = {}, {}

        self.disabled = set()

        def index(data, inherited=False):
            key = data["id"]
            self.nodes[key] = data
            blocked = inherited or bool(data.get("disabled"))
            if blocked:
                self.disabled.add(key)
            own = None if blocked else value(data)
            values = {own} if own is not None else set()
            for child in children(data):
                values.update(index(child, blocked))
            self.values[key] = values
            return values

        available = set()
        for root in roots:
            available.update(index(root))
        self.selected = set(selected) & available

    def state(self, key):
        return check_state(
            self.values[key], self.selected, visible=self.nodes[key].get("kind") != "setting"
        )

    def toggle(self, key):
        values = self.values[key]
        if not values:
            return
        if values <= self.selected:
            self.selected.difference_update(values)
            return
        self.selected.update(values)
        pending = list(self.nodes[key].get("requires", []))
        seen = set()
        while pending:
            requirement = pending.pop()
            if requirement in seen or requirement not in self.nodes:
                continue
            seen.add(requirement)
            self.selected.update(self.values[requirement])
            pending.extend(self.nodes[requirement].get("requires", []))

    def complete(self, key):
        values = self.values.get(key, set())
        return bool(values) and values <= self.selected
