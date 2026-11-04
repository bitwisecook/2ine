import AppKit
import BallCore
import SwiftUI

enum OutlineCheck: Equatable, Sendable { case off, mixed, on }
enum OutlineSortValue: Equatable, Sendable {
    case text(String), number(Double), bytes(UInt64)
    func compare(_ other: Self) -> ComparisonResult {
        switch (self, other) {
        case let (.number(a), .number(b)): a < b ? .orderedAscending : a > b ? .orderedDescending : .orderedSame
        case let (.bytes(a), .bytes(b)): a < b ? .orderedAscending : a > b ? .orderedDescending : .orderedSame
        case let (.text(a), .text(b)): a.localizedStandardCompare(b)
        default: .orderedSame
        }
    }
}
struct OutlineCell: Equatable, Sendable {
    var text: String
    var secondary = false
    var monospaced = false
    var icon: String? = nil
    var warning = false
    var tooltip: String? = nil
    var action: String? = nil
    var enabled = true
    var sortValue: OutlineSortValue? = nil
}
struct OutlineBrowserNode: Equatable, Sendable, Identifiable {
    let id: String
    var title: String
    var cells: [String: OutlineCell] = [:]
    var children: [Self] = []
    var canExpand = false
    var check: OutlineCheck? = nil
    var enabled = true
    var emphasized = false
    var tooltip: String? = nil
    var searchText: String? = nil
    var sortRank = 0
    var busy = false
    func filtered(_ query: String) -> Self? {
        let needle = query.trimmingCharacters(in: .whitespacesAndNewlines)
        if needle.isEmpty || FileSearch.matches(searchText ?? title, query: needle) { return self }
        let matches = children.compactMap { $0.filtered(needle) }
        guard !matches.isEmpty else { return nil }
        var copy = self
        copy.children = matches
        return copy
    }
}
struct OutlineBrowserColumn: Equatable, Sendable {
    let id: String
    let title: String
    var width: Double = 220
    var minimum: Double = 80
    var maximum: Double = 10000
    var sortable = false
    var rightAligned = false
}
struct OutlineBrowserSort: Equatable, Sendable {
    var column: String
    var ascending = true
}
struct OutlineMenuAction {
    let id: String
    let title: String
    var enabled = true
}
struct OutlineFocusRequest: Equatable {
    let id: UUID
    let row: String
}

/// A single native renderer for hierarchical and flat browser rows. Domain
/// adapters supply immutable metadata and handle edits, navigation and copying.
struct NativeOutlineBrowser: NSViewRepresentable {
    let nodes: [OutlineBrowserNode]
    let columns: [OutlineBrowserColumn]
    @Binding var selection: String?
    @Binding var query: String
    @Binding var searchFocused: Bool
    var sort: Binding<OutlineBrowserSort?> = .constant(nil)
    var disabled = false
    var identifier = "nativeOutlineBrowser"
    var accessibilityLabel = "Items"
    var defaultExpanded: Set<String> = []
    var focusRequest: OutlineFocusRequest? = nil
    var onToggle: ((String, Bool) -> Void)? = nil
    var onAction: ((String?, String) -> Void)? = nil
    var onActivate: ((String) -> Void)? = nil
    var onActivateBranch: ((String) -> Void)? = nil
    var onExpand: ((String) -> Void)? = nil
    var contextActions: ((String?) -> [OutlineMenuAction])? = nil
    var dragURL: ((String) -> URL?)? = nil
    var canDrop: ((String?) -> Bool)? = nil
    var onDrop: (([URL], String?) -> Bool)? = nil
    var dropTargetChanged: ((Bool) -> Void)? = nil

    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeNSView(context: Context) -> NSScrollView { makeScrollView(coordinator: context.coordinator) }
    func makeScrollView(coordinator: Coordinator) -> NSScrollView {
        let outline = BrowserOutline()
        outline.delegate = coordinator
        outline.dataSource = coordinator
        outline.usesAlternatingRowBackgroundColors = true
        outline.rowSizeStyle = .medium
        outline.rowHeight = 28
        outline.allowsMultipleSelection = false
        outline.allowsEmptySelection = true
        outline.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle
        // Expanding deeper rows must not widen the flexible column behind the viewport.
        outline.autoresizesOutlineColumn = false
        outline.indentationPerLevel = 16
        outline.target = coordinator
        outline.doubleAction = #selector(Coordinator.activateRow(_:))
        outline.registerForDraggedTypes([.fileURL])
        outline.setDraggingSourceOperationMask(.copy, forLocal: false)
        outline.setDraggingSourceOperationMask(.copy, forLocal: true)
        outline.typeSearch = { [weak coordinator] characters in
            coordinator?.parent.query += characters
            coordinator?.parent.searchFocused = true
        }
        outline.focusSearch = { [weak coordinator] in coordinator?.parent.searchFocused = true }
        outline.toggleRow = { [weak coordinator, weak outline] in
            guard let outline, outline.selectedRow >= 0,
                  let item = outline.item(atRow: outline.selectedRow) as? Item else { return false }
            return coordinator?.toggle(item) == true
        }
        outline.activateRow = { [weak coordinator] in coordinator?.activate(nil) == true }
        outline.makeMenu = { [weak coordinator] event in coordinator?.menu(event) }
        outline.dropEnded = { [weak coordinator] in coordinator?.parent.dropTargetChanged?(false) }
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.hasHorizontalScroller = true
        scroll.autohidesScrollers = true
        scroll.borderType = .bezelBorder
        scroll.documentView = outline
        coordinator.outline = outline
        coordinator.update()
        return scroll
    }
    func updateNSView(_ view: NSScrollView, context: Context) {
        context.coordinator.parent = self
        context.coordinator.update()
    }

    @MainActor final class Item: NSObject {
        let node: OutlineBrowserNode
        let children: [Item]
        init(_ node: OutlineBrowserNode) {
            self.node = node
            children = node.children.map(Item.init)
        }
    }
    final class CheckButton: NSButton { var rowID = "" }
    final class ActionButton: NSButton { var rowID = ""; var actionID = "" }
    final class ActionMenuItem: NSMenuItem { var rowID: String?; var actionID = "" }
    final class BrowserOutline: NSOutlineView {
        var typeSearch: ((String) -> Void)?
        var focusSearch: (() -> Void)?
        var toggleRow: (() -> Bool)?
        var activateRow: (() -> Bool)?
        var makeMenu: ((NSEvent) -> NSMenu?)?
        var dropEnded: (() -> Void)?
        override func menu(for event: NSEvent) -> NSMenu? { makeMenu?(event) }
        override func draggingExited(_ sender: (any NSDraggingInfo)?) { super.draggingExited(sender); dropEnded?() }
        override func concludeDragOperation(_ sender: (any NSDraggingInfo)?) { super.concludeDragOperation(sender); dropEnded?() }
        override func keyDown(with event: NSEvent) {
            let modifiers = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
            if modifiers == .command, event.charactersIgnoringModifiers?.lowercased() == "f" { focusSearch?(); return }
            if modifiers.intersection([.command, .control, .option]).isEmpty {
                if event.keyCode == 49, toggleRow?() == true { return }
                if (event.keyCode == 36 || event.keyCode == 76), activateRow?() == true { return }
                if let characters = event.characters, !characters.isEmpty,
                   characters.unicodeScalars.allSatisfy({ !CharacterSet.controlCharacters.contains($0) }) {
                    typeSearch?(characters); return
                }
            }
            super.keyDown(with: event)
        }
    }
    @MainActor final class Coordinator: NSObject, NSOutlineViewDataSource, NSOutlineViewDelegate {
        var parent: NativeOutlineBrowser
        weak var outline: BrowserOutline?
        var items: [Item] = []
        var source: [OutlineBrowserNode] = []
        var displayed: [OutlineBrowserNode] = []
        var originals: [String: OutlineBrowserNode] = [:]
        var expanded: Set<String>
        var columns: [OutlineBrowserColumn] = []
        var updating = false
        var revealedFocus: UUID?
        var appliedDefaults = false
        init(_ parent: NativeOutlineBrowser) { self.parent = parent; expanded = parent.defaultExpanded }
        func update() {
            guard let outline else { return }
            updating = true
            defer { updating = false }
            outline.setAccessibilityLabel(parent.accessibilityLabel)
            outline.setAccessibilityIdentifier(parent.identifier)
            if !appliedDefaults, !parent.nodes.isEmpty { expanded.formUnion(parent.defaultExpanded); appliedDefaults = true }
            if columns != parent.columns {
                for column in outline.tableColumns { outline.removeTableColumn(column) }
                for metadata in parent.columns {
                    let column = NSTableColumn(identifier: .init(metadata.id))
                    column.title = metadata.title
                    column.width = metadata.width
                    column.minWidth = metadata.minimum
                    column.maxWidth = metadata.maximum
                    if metadata.sortable { column.sortDescriptorPrototype = NSSortDescriptor(key: metadata.id, ascending: true) }
                    outline.addTableColumn(column)
                    if metadata.id == parent.columns.first?.id { outline.outlineTableColumn = column }
                }
                columns = parent.columns
            }
            if source != parent.nodes {
                source = parent.nodes
                originals.removeAll(keepingCapacity: true)
                func index(_ nodes: [OutlineBrowserNode]) {
                    for node in nodes { originals[node.id] = node; index(node.children) }
                }
                index(source)
            }
            let filtered = sorted(source.compactMap { $0.filtered(parent.query) })
            if displayed != filtered { displayed = filtered; items = filtered.map(Item.init) }
            var didReveal = false
            if let focus = parent.focusRequest, revealedFocus != focus.id {
                func reveal(_ nodes: [Item], ancestors: [String]) -> Bool {
                    for item in nodes {
                        if item.node.id == focus.row {
                            expanded.formUnion(ancestors)
                            parent.selection = focus.row
                            return true
                        }
                        if reveal(item.children, ancestors: ancestors + [item.node.id]) { return true }
                    }
                    return false
                }
                if reveal(items, ancestors: []) { revealedFocus = focus.id; didReveal = true }
            }
            let descriptors = parent.sort.wrappedValue.map { [NSSortDescriptor(key: $0.column, ascending: $0.ascending)] } ?? []
            if outline.sortDescriptors != descriptors { outline.sortDescriptors = descriptors }
            outline.reloadData()
            @MainActor func restore(_ item: Item) {
                if !parent.query.isEmpty || expanded.contains(item.node.id) {
                    outline.expandItem(item)
                    for child in item.children { restore(child) }
                }
            }
            for item in items { restore(item) }
            if let identity = parent.selection,
               let item = find(identity, in: items), outline.row(forItem: item) >= 0 {
                outline.selectRowIndexes(IndexSet(integer: outline.row(forItem: item)), byExtendingSelection: false)
            } else { outline.deselectAll(nil) }
            if didReveal, outline.selectedRow >= 0 {
                outline.scrollRowToVisible(outline.selectedRow)
            }
        }
        func find(_ id: String, in items: [Item]) -> Item? {
            for item in items { if item.node.id == id { return item }; if let found = find(id, in: item.children) { return found } }
            return nil
        }
        func sorted(_ nodes: [OutlineBrowserNode]) -> [OutlineBrowserNode] {
            let order = parent.sort.wrappedValue
            let children = nodes.map { node in var copy = node; copy.children = sorted(node.children); return copy }
            guard let order else { return children }
            return children.sorted { a, b in
                if a.sortRank != b.sortRank { return a.sortRank < b.sortRank }
                let left = a.cells[order.column]?.sortValue ?? .text(a.cells[order.column]?.text ?? a.title)
                let right = b.cells[order.column]?.sortValue ?? .text(b.cells[order.column]?.text ?? b.title)
                let compared = left.compare(right)
                if compared == .orderedSame { return a.id < b.id }
                return compared == (order.ascending ? .orderedAscending : .orderedDescending)
            }
        }
        func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int { (item as? Item)?.children.count ?? items.count }
        func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any { (item as? Item)?.children[index] ?? items[index] }
        func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool { (item as? Item).map { $0.node.canExpand || !$0.children.isEmpty } ?? false }
        func outlineView(_ outlineView: NSOutlineView, shouldExpandItem item: Any) -> Bool {
            if !updating, !parent.disabled, let item = item as? Item {
                expanded.insert(item.node.id)
                parent.onExpand?(item.node.id)
            }
            return true
        }
        func outlineViewSelectionDidChange(_ notification: Notification) {
            guard !updating, let outline else { return }
            parent.selection = outline.selectedRow < 0 ? nil : (outline.item(atRow: outline.selectedRow) as? Item)?.node.id
        }
        func outlineView(_ outlineView: NSOutlineView, sortDescriptorsDidChange oldDescriptors: [NSSortDescriptor]) {
            guard !updating else { return }
            parent.sort.wrappedValue = outlineView.sortDescriptors.first.flatMap { descriptor in descriptor.key.map { OutlineBrowserSort(column: $0, ascending: descriptor.ascending) } }
        }
        func outlineViewItemDidExpand(_ notification: Notification) { if !updating, let item = notification.userInfo?["NSObject"] as? Item { expanded.insert(item.node.id) } }
        func outlineViewItemDidCollapse(_ notification: Notification) { if !updating, let item = notification.userInfo?["NSObject"] as? Item { expanded.remove(item.node.id) } }
        func original(_ item: Item) -> OutlineBrowserNode { originals[item.node.id] ?? item.node }
        @discardableResult func toggle(_ item: Item) -> Bool {
            let node = original(item)
            guard !parent.disabled, node.enabled, node.cells[parent.columns.first?.id ?? ""]?.enabled != false,
                  let state = node.check, let toggle = parent.onToggle else { return false }
            toggle(node.id, state != .on)
            return true
        }
        @objc func check(_ sender: CheckButton) { if let item = find(sender.rowID, in: items) { toggle(item) } }
        @objc func action(_ sender: ActionButton) {
            guard !parent.disabled, sender.isEnabled, let node = originals[sender.rowID], node.enabled,
                  node.cells.values.contains(where: { $0.action == sender.actionID && $0.enabled }) else { return }
            parent.onAction?(sender.rowID, sender.actionID)
        }
        @objc func menuAction(_ sender: ActionMenuItem) {
            guard !parent.disabled, sender.isEnabled,
                  sender.rowID.map({ originals[$0]?.enabled == true }) ?? true,
                  parent.contextActions?(sender.rowID).contains(where: { $0.id == sender.actionID && $0.enabled }) == true else { return }
            parent.onAction?(sender.rowID, sender.actionID)
        }
        @objc func activateRow(_ sender: Any?) { activate(outline?.clickedRow) }
        @discardableResult func activate(_ clicked: Int?) -> Bool {
            guard !parent.disabled, let outline else { return false }
            let row = clicked.flatMap { $0 >= 0 ? $0 : nil } ?? outline.selectedRow
            guard row >= 0, let item = outline.item(atRow: row) as? Item, item.node.enabled else { return false }
            if item.node.canExpand || !item.children.isEmpty {
                if let activate = parent.onActivateBranch { activate(item.node.id) }
                else if outline.isItemExpanded(item) { outline.collapseItem(item) }
                else { outline.expandItem(item) }
                return true
            }
            guard let activate = parent.onActivate else { return false }
            activate(item.node.id)
            return true
        }
        func menu(_ event: NSEvent) -> NSMenu? {
            guard let outline else { return nil }
            let row = outline.row(at: outline.convert(event.locationInWindow, from: nil))
            let id = row >= 0 ? (outline.item(atRow: row) as? Item)?.node.id : nil
            if row >= 0 { outline.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false) }
            return menu(for: id)
        }
        func menu(for id: String?) -> NSMenu? {
            let actions = parent.contextActions?(id) ?? []
            guard !actions.isEmpty else { return nil }
            let menu = NSMenu()
            for action in actions {
                let item = ActionMenuItem(title: action.title, action: #selector(menuAction(_:)), keyEquivalent: "")
                item.target = self; item.rowID = id; item.actionID = action.id
                item.isEnabled = action.enabled && !parent.disabled
                menu.addItem(item)
            }
            menu.autoenablesItems = false
            return menu
        }
        func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> (any NSPasteboardWriting)? {
            guard let item = item as? Item, let url = parent.dragURL?(item.node.id) else { return nil }
            return url as NSURL
        }
        func dropTarget(_ item: Any?) -> (accepted: Bool, id: String?) {
            guard !parent.disabled, parent.onDrop != nil else { return (false, nil) }
            let id = (item as? Item)?.node.id
            if let id, parent.canDrop?(id) == true { return (true, id) }
            return (parent.canDrop?(nil) == true, nil)
        }
        func outlineView(_ outlineView: NSOutlineView, validateDrop info: any NSDraggingInfo, proposedItem item: Any?, proposedChildIndex index: Int) -> NSDragOperation {
            let target = dropTarget(item)
            parent.dropTargetChanged?(target.accepted)
            guard target.accepted else { return [] }
            outlineView.setDropItem(target.id == nil ? nil : item, dropChildIndex: NSOutlineViewDropOnItemIndex)
            return .copy
        }
        func outlineView(_ outlineView: NSOutlineView, acceptDrop info: any NSDraggingInfo, item: Any?, childIndex index: Int) -> Bool {
            defer { parent.dropTargetChanged?(false) }
            guard let urls = info.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] else { return false }
            return acceptURLs(urls, item: item)
        }
        func acceptURLs(_ urls: [URL], item: Any?) -> Bool {
            let target = dropTarget(item)
            guard target.accepted, !urls.isEmpty, urls.allSatisfy(\.isFileURL) else { return false }
            return parent.onDrop?(urls, target.id) == true
        }
        func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
            guard let item = item as? Item, let column = tableColumn else { return nil }
            let node = original(item)
            let cell = node.cells[column.identifier.rawValue] ?? OutlineCell(text: column.identifier.rawValue == parent.columns.first?.id ? node.title : "")
            func activity(_ view: NSView) -> NSView {
                guard node.busy, column.identifier.rawValue == parent.columns.first?.id else { return view }
                let spinner = NSProgressIndicator()
                spinner.style = .spinning; spinner.controlSize = .small
                spinner.isIndeterminate = true
                spinner.setAccessibilityLabel("Updating " + node.title)
                spinner.translatesAutoresizingMaskIntoConstraints = false
                spinner.widthAnchor.constraint(equalToConstant: 14).isActive = true
                spinner.heightAnchor.constraint(equalToConstant: 14).isActive = true
                spinner.startAnimation(nil)
                let stack = NSStackView(views: [spinner, view])
                stack.orientation = .horizontal; stack.spacing = 5; stack.alignment = .centerY
                return stack
            }
            if column.identifier.rawValue == parent.columns.first?.id, let check = node.check {
                let button = CheckButton(checkboxWithTitle: cell.text, target: self, action: #selector(check(_:)))
                button.rowID = node.id
                button.allowsMixedState = true
                button.state = check == .off ? .off : check == .on ? .on : .mixed
                button.isEnabled = !parent.disabled && node.enabled && cell.enabled
                button.font = .systemFont(ofSize: 13, weight: node.emphasized ? .semibold : .regular)
                button.toolTip = cell.tooltip ?? node.tooltip
                button.setAccessibilityIdentifier(parent.identifier + ".check." + node.id)
                if let icon = cell.icon {
                    button.title = ""
                    button.setAccessibilityLabel(cell.text)
                    let image = NSImageView(image: NSImage(systemSymbolName: icon, accessibilityDescription: nil) ?? NSImage())
                    image.contentTintColor = .secondaryLabelColor
                    image.translatesAutoresizingMaskIntoConstraints = false
                    image.widthAnchor.constraint(equalToConstant: 16).isActive = true
                    image.heightAnchor.constraint(equalToConstant: 16).isActive = true
                    let label = NSTextField(labelWithString: cell.text)
                    label.font = button.font
                    label.textColor = !node.enabled || !cell.enabled ? .disabledControlTextColor : cell.secondary ? .secondaryLabelColor : .labelColor
                    label.lineBreakMode = .byTruncatingMiddle
                    let stack = NSStackView(views: [button, image, label])
                    stack.orientation = .horizontal; stack.spacing = 5; stack.alignment = .centerY
                    stack.toolTip = button.toolTip
                    return activity(stack)
                }
                return activity(button)
            }
            if let action = cell.action {
                let button = ActionButton(title: cell.text, target: self, action: #selector(Coordinator.action(_:)))
                button.rowID = node.id; button.actionID = action
                button.bezelStyle = .rounded; button.controlSize = .small
                button.isEnabled = !parent.disabled && node.enabled && cell.enabled
                button.toolTip = cell.tooltip ?? node.tooltip
                button.setAccessibilityLabel(node.title + ". " + cell.text)
                return button
            }
            let field = NSTextField(labelWithString: cell.text)
            field.font = cell.monospaced ? .monospacedSystemFont(ofSize: 12, weight: .regular) : .systemFont(ofSize: 12, weight: node.emphasized && column.identifier.rawValue == parent.columns.first?.id ? .semibold : .regular)
            field.textColor = !node.enabled || !cell.enabled ? .disabledControlTextColor : cell.secondary ? .secondaryLabelColor : .labelColor
            field.lineBreakMode = .byTruncatingMiddle
            field.toolTip = cell.tooltip ?? node.tooltip ?? cell.text
            if parent.columns.first(where: { $0.id == column.identifier.rawValue })?.rightAligned == true { field.alignment = .right }
            guard let icon = cell.icon else { return activity(field) }
            let image = NSImageView(image: NSImage(systemSymbolName: icon, accessibilityDescription: cell.warning ? "Warning" : nil) ?? NSImage())
            image.contentTintColor = cell.warning ? .systemOrange : .secondaryLabelColor
            image.translatesAutoresizingMaskIntoConstraints = false
            image.widthAnchor.constraint(equalToConstant: 16).isActive = true
            image.heightAnchor.constraint(equalToConstant: 16).isActive = true
            let stack = NSStackView(views: [image, field])
            stack.orientation = .horizontal; stack.spacing = 5; stack.alignment = .centerY
            return activity(stack)
        }
    }
}
