import AppKit
import BallCore
import SwiftUI
import Testing
@testable import TwoineBall

@MainActor private final class BrowserState {
    var selected: String?
    var query = ""
    var focused = false
    var sort: OutlineBrowserSort? = .init(column: "name")
    var actions: [(String?, String)] = []
    var toggles: [(String, Bool)] = []
    var activated: [String] = []
    var drops: [(String?, [URL])] = []
    var writable = true
    var selection: Binding<String?> { Binding(get: { self.selected }, set: { self.selected = $0 }) }
    var search: Binding<String> { Binding(get: { self.query }, set: { self.query = $0 }) }
    var focus: Binding<Bool> { Binding(get: { self.focused }, set: { self.focused = $0 }) }
    var ordering: Binding<OutlineBrowserSort?> { Binding(get: { self.sort }, set: { self.sort = $0 }) }
}

@MainActor private func renderBrowser(_ browser: NativeOutlineBrowser) -> (NSScrollView, NativeOutlineBrowser.Coordinator, NativeOutlineBrowser.BrowserOutline) {
    let coordinator = browser.makeCoordinator()
    let scroll = browser.makeScrollView(coordinator: coordinator)
    scroll.frame = NSRect(x: 0, y: 0, width: 760, height: 360)
    scroll.layoutSubtreeIfNeeded()
    return (scroll, coordinator, coordinator.outline!)
}

private func guestEntries() throws -> [GuestEntry] {
    try JSONDecoder().decode([GuestEntry].self, from: Data(#"""
    [{"name":"Folder","path":"C:\\Folder","hostPath":"/private/Folder","directory":true,"size":0},
     {"name":"Alpha.exe","path":"C:\\Alpha.exe","hostPath":"/private/Alpha.exe","directory":false,"size":100,"modified":1730000000},
     {"name":"Beta.cmd","path":"C:\\Beta.cmd","hostPath":"/private/Beta.cmd","directory":false,"size":20,"modified":1731000000},
     {"name":"Readme.txt","path":"C:\\Readme.txt","hostPath":"/private/Readme.txt","directory":false,"size":10}]
    """#.utf8))
}

@MainActor @Test func nativeBrowserRevealsAnExplicitDestinationThroughCollapsedDirectories() throws {
    let state = BrowserState()
    let file = OutlineBrowserNode(id: "source", title: "SOME.DLL")
    let os2 = OutlineBrowserNode(id: "C:\\OS2", title: "OS2", children: [file])
    let drive = OutlineBrowserNode(id: "C:\\", title: "C:\\", children: [os2])
    let component = OutlineBrowserNode(id: "runtime", title: "Runtime", children: [drive])
    let browser = NativeOutlineBrowser(nodes: [component], columns: [OutlineBrowserColumn(id: "name", title: "Name")],
                                       selection: state.selection, query: state.search, searchFocused: state.focus,
                                       focusRequest: OutlineFocusRequest(id: UUID(), row: "source"))
    let (scroll, coordinator, outline) = renderBrowser(browser)
    defer { _ = scroll }
    #expect(outline.numberOfRows == 4)
    #expect(state.selected == "source")
    #expect((outline.item(atRow: outline.selectedRow) as? NativeOutlineBrowser.Item)?.node.id == "source")
    #expect(coordinator.expanded == ["runtime", "C:\\", "C:\\OS2"])
}

@MainActor @Test func nativeBrowserSortFilterAndActivationKeepStableSelection() throws {
    let state = BrowserState()
    state.selected = "C:\\Alpha.exe"
    let browser = NativeOutlineBrowser(nodes: BrowserRows.guest(try guestEntries()), columns: BrowserRows.fileColumns,
                                       selection: state.selection, query: state.search, searchFocused: state.focus, sort: state.ordering,
                                       onActivate: { state.activated.append($0) })
    let (scroll, coordinator, outline) = renderBrowser(browser)
    defer { _ = scroll }
    #expect(outline.selectedRow == 1)
    outline.sortDescriptors = [NSSortDescriptor(key: "size", ascending: true)]
    coordinator.update()
    #expect(state.sort?.column == "size")
    #expect(state.sort?.ascending == true)
    #expect(outline.selectedRow == 3)
    #expect(state.selected == "C:\\Alpha.exe")
    #expect(coordinator.items.first?.node.title == "Folder")
    #expect(coordinator.activate(nil))
    #expect(state.activated == ["C:\\Alpha.exe"])
    state.query = "btcmd"
    coordinator.update()
    #expect(coordinator.items.map(\.node.id) == ["C:\\Beta.cmd"])
    #expect(outline.selectedRow == -1)
    #expect(state.selected == "C:\\Alpha.exe")
    state.query = ""
    coordinator.update()
    #expect(outline.selectedRow == 3)
    let size = try #require(outline.view(atColumn: 1, row: 3, makeIfNecessary: true) as? NSTextField)
    let date = try #require(outline.view(atColumn: 2, row: 3, makeIfNecessary: true) as? NSTextField)
    #expect(size.stringValue == browserSize(100, directory: false))
    #expect(date.stringValue == browserDate(1730000000))
    #expect(outline.window == nil)
}

@MainActor @Test func nativeBrowserKeyboardTogglesOnlyCheckableRowsAndKeepsSpaceForSearch() throws {
    let state = BrowserState()
    state.selected = "plain"
    let browser = NativeOutlineBrowser(nodes: [OutlineBrowserNode(id: "plain", title: "Plain Item"),
                                              OutlineBrowserNode(id: "check", title: "Checked Item", check: .mixed)],
                                       columns: [OutlineBrowserColumn(id: "name", title: "Name")],
                                       selection: state.selection, query: state.search, searchFocused: state.focus,
                                       onToggle: { state.toggles.append(($0, $1)) })
    let (scroll, coordinator, outline) = renderBrowser(browser)
    defer { _ = scroll }
    let space = try #require(NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                                            windowNumber: 0, context: nil, characters: " ", charactersIgnoringModifiers: " ", isARepeat: false, keyCode: 49))
    outline.keyDown(with: space)
    #expect(state.query == " ")
    #expect(state.focused)
    #expect(state.toggles.isEmpty)
    state.query = ""; state.focused = false; state.selected = "check"
    coordinator.update()
    outline.keyDown(with: space)
    #expect(state.query.isEmpty)
    #expect(state.toggles.count == 1)
    #expect(state.toggles.first?.0 == "check" && state.toggles.first?.1 == true)
    coordinator.parent.disabled = true
    #expect(!coordinator.toggle(coordinator.items[1]))
    #expect(state.toggles.count == 1)
    let find = try #require(NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: .command, timestamp: 0,
                                           windowNumber: 0, context: nil, characters: "f", charactersIgnoringModifiers: "f", isARepeat: false, keyCode: 3))
    outline.keyDown(with: find)
    #expect(state.focused && state.query.isEmpty)
}

@MainActor @Test func nativeBrowserContextAndDropHooksRetainRowIdentityAndValidation() throws {
    let state = BrowserState()
    let entries = try guestEntries()
    let browser = NativeOutlineBrowser(nodes: BrowserRows.guest(entries), columns: BrowserRows.fileColumns,
                                       selection: state.selection, query: state.search, searchFocused: state.focus,
                                       onAction: { state.actions.append(($0, $1)) },
                                       contextActions: { id in id == nil ? [] : [OutlineMenuAction(id: "reveal", title: "Show in Finder"), OutlineMenuAction(id: "edit", title: "Edit", enabled: false)] },
                                       dragURL: { id in entries.first(where: { $0.path == id }).map { URL(filePath: $0.hostPath) } },
                                       canDrop: { id in state.writable && (id == nil || id == "C:\\Folder") },
                                       onDrop: { urls, id in state.drops.append((id, urls)); return true })
    let (scroll, coordinator, outline) = renderBrowser(browser)
    defer { _ = scroll }
    let menu = try #require(coordinator.menu(for: "C:\\Alpha.exe"))
    #expect(menu.items.map(\.title) == ["Show in Finder", "Edit"])
    #expect(menu.items[0].isEnabled && !menu.items[1].isEnabled)
    coordinator.menuAction(try #require(menu.items.first as? NativeOutlineBrowser.ActionMenuItem))
    #expect(state.actions.first?.0 == "C:\\Alpha.exe" && state.actions.first?.1 == "reveal")
    coordinator.parent.disabled = true
    coordinator.menuAction(try #require(menu.items.first as? NativeOutlineBrowser.ActionMenuItem))
    #expect(state.actions.count == 1)
    coordinator.parent.disabled = false
    coordinator.menuAction(try #require(menu.items.last as? NativeOutlineBrowser.ActionMenuItem))
    #expect(state.actions.count == 1)
    let folder = try #require(coordinator.find("C:\\Folder", in: coordinator.items))
    let file = try #require(coordinator.find("C:\\Alpha.exe", in: coordinator.items))
    let writer = coordinator.outlineView(outline, pasteboardWriterForItem: file) as? NSURL
    #expect(writer as URL? == URL(filePath: "/private/Alpha.exe"))
    let urls = [URL(filePath: "/private/import.txt")]
    #expect(coordinator.acceptURLs(urls, item: folder))
    #expect(state.drops.first?.0 == "C:\\Folder")
    #expect(coordinator.acceptURLs(urls, item: file))
    #expect(state.drops.last?.0 == nil) // File rows fall back to the current folder.
    state.writable = false
    #expect(!coordinator.acceptURLs(urls, item: folder))
    #expect(state.drops.count == 2)
    #expect(!coordinator.acceptURLs([try #require(URL(string: "https://example.invalid/file"))], item: nil))
}

@Test func nativeBrowserAdaptersPreserveRelevantFiltersAndRegistryProfileIdentity() throws {
    let entries = try guestEntries()
    #expect(BrowserRows.filteredGuestEntries(entries, programsOnly: true).map(\.name) == ["Folder", "Alpha.exe", "Beta.cmd"])
    #expect(BrowserRows.filteredGuestEntries(entries, programsOnly: true, relevantOnly: false).count == 4)
    #expect(BrowserRows.filteredGuestEntries(entries, directoriesOnly: true).map(\.name) == ["Folder"])
    #expect(BrowserRows.filteredGuestEntries(entries, directoriesOnly: true, relevantOnly: false).count == 4)
    let media = try JSONDecoder().decode([MediaDirectory.Entry].self, from: Data(#"""
    [{"name":"Media.zip","path":"opaque-archive","directory":false,"container":true,"size":4096},
     {"name":"Notes.xyz","path":"opaque-notes","directory":false,"container":false,"size":32}]
    """#.utf8))
    #expect(BrowserRows.filteredMediaEntries(media).map(\.id) == ["opaque-archive"])
    #expect(BrowserRows.filteredMediaEntries(media, relevantOnly: false).count == 2)
    #expect(BrowserRows.media(media)[0].cells["name"]?.icon == "archivebox")
    let profile = try JSONDecoder().decode([ConfigurationEntry].self, from: Data(#"""
    [{"application":"MixedÄCase","key":"KeyName","data":"ff00"},{"application":"hkcu\\Software\\ÄCase","key":"v:Unknown","data":"78563412ff"}]
    """#.utf8))
    let ini = BrowserRows.profile(profile) { ConfigurationValue.hex($0.bytes) }
    #expect(ini.flatMap(\.children).map(\.id).sorted() == profile.map(\.id).sorted())
    #expect(ini.flatMap(\.children).first { $0.id == profile[0].id }?.cells["data"]?.text == "ff00")
    let registry = RegistryTree(ConfigurationSnapshot(area: "registry", path: "C:\\REG.INI", revision: "r", format: "profile", entries: [profile[1]]))
    let rows = BrowserRows.registryValues(registry.values)
    #expect(rows.first?.id == profile[1].id)
    #expect(rows.first?.cells["type"]?.text == "Unknown type 305419896")
    #expect(rows.first?.cells["data"]?.text.contains("ff") == true)
    #expect(profile[1].data == "78563412ff")
}

@MainActor @Test func registryProfileConfigAndFileViewsHostTheSameNativeRenderer() throws {
    let root = FileManager.default.temporaryDirectory.appending(path: "2ine-native-browser-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"))
    let snapshot = try NativeBallCore.request(DesktopRequest("command", ball: root.appending(path: "Test.2ineball").path, arguments: ["init"]), as: BallSnapshot.self)
    @MainActor func rendered<V: View>(_ view: V) -> Set<String> {
        let hosting = NSHostingView(rootView: view)
        hosting.frame = NSRect(x: 0, y: 0, width: 1100, height: 700)
        hosting.layoutSubtreeIfNeeded()
        var ids: Set<String> = []
        @MainActor func visit(_ view: NSView) {
            if let outline = view as? NativeOutlineBrowser.BrowserOutline,
               let coordinator = outline.delegate as? NativeOutlineBrowser.Coordinator { ids.insert(coordinator.parent.identifier) }
            for child in view.subviews { visit(child) }
        }
        visit(hosting)
        #expect(hosting.window == nil)
        return ids
    }
    #expect(rendered(RegistrySettingsView(model: model)) == ["registryKeys", "registryValues"])
    #expect(rendered(ProfileConfigurationView(model: model, area: "user")) == ["profileEntries"])
    #expect(rendered(OS2FilesSettingsView(model: model)) == ["configurationFiles"])
    #expect(rendered(GuestBrowserView(model: model, snapshot: snapshot)) == ["guestFilesTable"])
    #expect(rendered(DrivesView(model: model, snapshot: snapshot)) == ["drivesTable", "guestFilesTable"])
}
