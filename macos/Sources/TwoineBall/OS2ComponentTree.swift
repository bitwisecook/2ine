#if canImport(BallCore)
import BallCore
#endif
import SwiftUI

struct OS2ComponentTree: View {
    let root: OS2ExtractionPlan.Node
    @Binding var selection: Set<String>
    @Binding var query: String
    @Binding var searchFocused: Bool
    let disabled: Bool
    let toggle: (OS2ExtractionPlan.Node, Bool) -> Void
    let openSetting: (String) -> Void
    var focusRequest: OutlineFocusRequest? = nil
    var activePaths: Set<String> = []
    var fileStates: [String: OS2ComponentState.File] = [:]
    var mutating = false
    @State private var focusedRow: String?

    var body: some View { browser }
    var browser: NativeOutlineBrowser {
        NativeOutlineBrowser(nodes: [Self.row(root, selected: selection, activePaths: activePaths, fileStates: fileStates, mutating: mutating)], columns: [
            OutlineBrowserColumn(id: "name", title: "Component or File", width: 360, minimum: 180),
            OutlineBrowserColumn(id: "status", title: "Status", width: 205, minimum: 150),
            OutlineBrowserColumn(id: "size", title: "Size", width: 85, minimum: 70, rightAligned: true)
        ], selection: $focusedRow, query: $query, searchFocused: $searchFocused,
        disabled: disabled, identifier: "os2ComponentTree", accessibilityLabel: "OS/2 components and files",
        defaultExpanded: ["all", "cmd"], focusRequest: focusRequest, onToggle: { id, enabled in
            if let node = root.find(id) { toggle(node, enabled) }
        }, onAction: { _, setting in openSetting(setting) })
    }
    static func row(_ node: OS2ExtractionPlan.Node, selected: Set<String>, activePaths: Set<String> = [],
                    fileStates: [String: OS2ComponentState.File] = [:], mutating: Bool = false) -> OutlineBrowserNode {
        let children = (node.children ?? []).filter { node.provider == nil || $0.kind != "setting" }.map {
            row($0, selected: selected, activePaths: activePaths, fileStates: fileStates, mutating: mutating)
        }
        let busy = node.kind == "file" && node.path.map { activePaths.contains($0.uppercased()) } == true || children.contains { $0.busy }
        let files = node.files.filter { $0.provided != true }
        let sources = node.eligibleSources
        let count = sources.intersection(selected).count
        let installed = files.filter { $0.present == true && $0.disabled != true }.count
        let bytes = files.reduce(UInt64(0)) { $0 &+ ($1.size ?? 0) }
        var status: OutlineCell
        if node.setting != nil || node.provider != nil {
            let selection = node.selection ?? node.provider?.selection ?? ""
            let title = ["mesa": "2ine (Mesa)", "original": "Original OS/2", "off": "Off", "2ine": "2ine"][selection] ?? selection
            status = OutlineCell(text: title, tooltip: "Open and highlight the provider setting.", action: node.setting ?? node.provider?.setting)
        } else if node.disabled == true {
            status = OutlineCell(text: "2ine selected", secondary: true)
        } else if busy && node.kind == "file" {
            status = OutlineCell(text: "Updating…", secondary: true)
        } else if node.kind == "file" {
            let state = node.source.flatMap { fileStates[$0] }
            let anotherVersion = state?.source != nil && state?.source != node.source
            let label = state?.status == "protected" ? "Protected · kept" : state?.status == "modified" ? "Modified · kept" : state?.status == "unowned" ? "Existing · not managed" : anotherVersion ? "Another version at destination" : node.present == true ? "Installed" : node.preferred == false ? "Alternative version" : "To add"
            let location = state.flatMap { $0.present && $0.path.caseInsensitiveCompare(node.path ?? "") != .orderedSame ? "Installed at " + $0.path : nil }
            let detail = [state?.reason, location].compactMap { $0 }.joined(separator: "\n")
            status = OutlineCell(text: label, secondary: true, tooltip: detail.isEmpty ? nil : detail)
        } else {
            status = OutlineCell(text: "\(count)/\(sources.count) selected · \(installed) installed", secondary: true)
        }
        let check: OutlineCheck? = sources.isEmpty || node.kind == "setting" ? nil : count == 0 ? .off : count == sources.count ? .on : .mixed
        return OutlineBrowserNode(id: node.id, title: node.title, cells: [
            "name": OutlineCell(text: node.title, secondary: node.kind == "setting" || node.disabled == true, icon: node.kind == "directory" ? "folder" : nil, enabled: node.disabled != true && !mutating),
            "status": status,
            "size": OutlineCell(text: node.kind == "setting" ? "" : ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file), secondary: true)
        ], children: children, check: check,
        enabled: node.disabled != true || node.provider != nil, emphasized: node.kind == "group", tooltip: node.path ?? node.detail,
        searchText: node.title + " " + (node.path ?? ""), busy: busy)
    }
}
