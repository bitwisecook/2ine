import AppKit
import BallCore
import SwiftUI
import Testing
@testable import TwoineBall

@MainActor private func hostComponents(_ view: OS2ComponentTree) throws -> (NSHostingView<OS2ComponentTree>, NativeOutlineBrowser.Coordinator, NativeOutlineBrowser.BrowserOutline) {
    let hosting = NSHostingView(rootView: view)
    hosting.frame = NSRect(x: 0, y: 0, width: 780, height: 400)
    hosting.layoutSubtreeIfNeeded()
    @MainActor func find(_ view: NSView) -> NativeOutlineBrowser.BrowserOutline? {
        if let outline = view as? NativeOutlineBrowser.BrowserOutline { return outline }
        for child in view.subviews { if let found = find(child) { return found } }
        return nil
    }
    let outline = try #require(find(hosting))
    let coordinator = try #require(outline.delegate as? NativeOutlineBrowser.Coordinator)
    return (hosting, coordinator, outline)
}

@MainActor @Test func OS2ComponentOutlineUsesNativeMixedCheckboxesAndWholeFilteredParents() throws {
    let data = Data(#"""
    {"files":[],"unmapped":[],"tree":{"id":"all","kind":"group","title":"Full OS/2 File Set","category":"full","children":[
      {"id":"fonts","kind":"group","title":"Fonts","category":"fonts","children":[
        {"id":"font1","source":"font1","kind":"file","title":"Helvetica.ttf","category":"fonts","size":32},
        {"id":"font2","source":"font2","kind":"file","title":"System.fon","category":"fonts","size":64}]}]}}
    """#.utf8)
    let plan = try JSONDecoder().decode(OS2ExtractionPlan.self, from: data)
    let root = try #require(plan.tree)
    var checked: Set<String> = ["font1"]
    let view = OS2ComponentTree(root: root, selection: .constant(checked), query: .constant("helv"),
                               searchFocused: .constant(false), disabled: false,
                               toggle: { node, enabled in checked = plan.selecting(node, in: checked, enabled: enabled) },
                               openSetting: { _ in })
    let (hosting, coordinator, outline) = try hostComponents(view)
    defer { _ = hosting }
    let name = try #require(outline.tableColumns.first)
    let group = try #require(coordinator.items.first?.children.first)
    #expect(group.children.count == 1) // Search shows the matching file.
    #expect(coordinator.original(group).check == .mixed) // Parent still owns its whole group.
    let checkbox = try #require(coordinator.outlineView(outline, viewFor: name, item: group) as? NSButton)
    #expect(checkbox.allowsMixedState)
    #expect(checkbox.state == .mixed)
    #expect(checkbox.isEnabled)
    coordinator.toggle(group)
    #expect(checked == ["font1", "font2"])
}

@MainActor @Test func OS2ComponentOutlineProviderIsASettingsButtonNotAFileCheckbox() throws {
    let data = Data(#"""
    {"id":"opengl.provider","title":"Active OpenGL Provider","kind":"setting","selection":"mesa","setting":"graphics.opengl","group":"opengl"}
    """#.utf8)
    let node = try JSONDecoder().decode(OS2ExtractionPlan.Node.self, from: data)
    var opened: String?
    let view = OS2ComponentTree(root: node, selection: .constant([]), query: .constant(""),
                               searchFocused: .constant(false), disabled: false,
                               toggle: { _, _ in Issue.record("Provider rows cannot select files") },
                               openSetting: { opened = $0 })
    let (hosting, coordinator, outline) = try hostComponents(view)
    defer { _ = hosting }
    let item = try #require(coordinator.items.first)
    let column = NSTableColumn(identifier: .init("status"))
    let button = try #require(coordinator.outlineView(outline, viewFor: column, item: item) as? NativeOutlineBrowser.ActionButton)
    #expect(button.title == "Mesa · Settings…")
    #expect(button.isEnabled)
    #expect(button.actionID == "graphics.opengl")
    #expect(item.node.check == nil)
    coordinator.action(button)
    #expect(opened == "graphics.opengl")
}

@MainActor @Test func OS2ComponentOutlineKeepsLargeFileSetsCollapsed() throws {
    let leaves = (0..<11_000).map { index in
        ["id": "source\(index)", "source": "source\(index)", "title": "Font\(index).ttf",
         "kind": "file", "category": "fonts", "size": 1024] as [String: Any]
    }
    let object: [String: Any] = ["id": "all", "title": "Full OS/2 File Set", "kind": "group", "category": "full",
                                "children": [["id": "fonts", "title": "Fonts", "kind": "group", "category": "fonts", "children": leaves]]]
    let root = try JSONDecoder().decode(OS2ExtractionPlan.Node.self, from: JSONSerialization.data(withJSONObject: object))
    let view = OS2ComponentTree(root: root, selection: .constant([]), query: .constant(""),
                               searchFocused: .constant(false), disabled: false,
                               toggle: { _, _ in }, openSetting: { _ in })
    let (hosting, coordinator, outline) = try hostComponents(view)
    defer { _ = hosting }
    #expect(coordinator.items.first?.children.first?.children.count == 11_000)
    #expect(outline.numberOfRows == 2)
}

@MainActor @Test func OS2ComponentOutlineLayoutPreservesCheckboxStatusAndSize() throws {
    let data = Data(#"""
    {"id":"all","title":"Full OS/2 File Set","kind":"group","category":"full","children":[
      {"id":"font1","source":"font1","kind":"file","title":"Helvetica.ttf","category":"fonts","size":32},
      {"id":"font2","source":"font2","kind":"file","title":"System.fon","category":"fonts","size":64}]}
    """#.utf8)
    let root = try JSONDecoder().decode(OS2ExtractionPlan.Node.self, from: data)
    let tree = OS2ComponentTree(root: root, selection: .constant(["font1"]), query: .constant(""),
                               searchFocused: .constant(false), disabled: false,
                               toggle: { _, _ in }, openSetting: { _ in })
    let hosting = NSHostingView(rootView: tree)
    hosting.frame = NSRect(x: 0, y: 0, width: 780, height: 240)
    hosting.layoutSubtreeIfNeeded()
    @MainActor func findOutline(_ view: NSView) -> NSOutlineView? {
        if let outline = view as? NSOutlineView { return outline }
        for child in view.subviews { if let found = findOutline(child) { return found } }
        return nil
    }
    let outline = try #require(findOutline(hosting))
    hosting.layoutSubtreeIfNeeded()
    let check = try #require(outline.view(atColumn: 0, row: 0, makeIfNecessary: true) as? NSButton)
    let status = try #require(outline.view(atColumn: 1, row: 0, makeIfNecessary: true) as? NSTextField)
    let size = try #require(outline.view(atColumn: 2, row: 0, makeIfNecessary: true) as? NSTextField)
    #expect(hosting.window == nil)
    #expect(check.state == .mixed)
    #expect(status.stringValue == "1/2 selected · 0 installed")
    #expect(size.stringValue == ByteCountFormatter.string(fromByteCount: 96, countStyle: .file))
}
