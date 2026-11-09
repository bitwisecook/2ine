import BallCore
import Foundation

enum BrowserRows {
    static func filteredGuestEntries(
        _ entries: [GuestEntry], directoriesOnly: Bool = false,
        programsOnly: Bool = false, relevantOnly: Bool = true,
        query: String = ""
    ) -> [GuestEntry] {
        entries.filter { entry in
            if entry.directory { return true }
            if directoriesOnly && relevantOnly { return false }
            return !programsOnly || !relevantOnly || FileSearch.isProgram(entry.name)
        }.filter { FileSearch.matches($0.name, query: query) }
    }
    static func filteredMediaEntries(
        _ entries: [MediaDirectory.Entry], relevantOnly: Bool = true,
        query: String = ""
    ) -> [MediaDirectory.Entry] {
        entries.filter {
            (!relevantOnly || $0.directory
                || FileSearch.isOS2MediaFile($0.name, container: $0.container))
                && FileSearch.matches($0.name, query: query)
        }
    }
    static let fileColumns = [
        OutlineBrowserColumn(id: "name", title: "Name", width: 320, minimum: 160, sortable: true),
        OutlineBrowserColumn(
            id: "size", title: "Size", width: 90, minimum: 75, sortable: true, rightAligned: true),
        OutlineBrowserColumn(
            id: "modified", title: "Date Modified", width: 165, minimum: 135, sortable: true),
    ]
    static func guest(_ entries: [GuestEntry]) -> [OutlineBrowserNode] {
        entries.map { entry in
            file(
                id: entry.path, name: entry.name, directory: entry.directory,
                icon: entry.directory ? "folder" : "doc", size: entry.size,
                modified: entry.modified,
                tooltip: entry.path)
        }
    }
    static func media(_ entries: [MediaDirectory.Entry]) -> [OutlineBrowserNode] {
        entries.map { entry in
            file(
                id: entry.path, name: entry.name, directory: entry.directory,
                icon: entry.directory ? "folder" : entry.container ? "archivebox" : "doc",
                size: entry.size, modified: entry.modified, tooltip: entry.name)
        }
    }
    private static func file(
        id: String, name: String, directory: Bool, icon: String,
        size: UInt64?, modified: Double?, tooltip: String
    ) -> OutlineBrowserNode {
        OutlineBrowserNode(
            id: id, title: name,
            cells: [
                "name": OutlineCell(text: name, icon: icon),
                "size": OutlineCell(
                    text: browserSize(size, directory: directory), sortValue: .bytes(size ?? 0)),
                "modified": OutlineCell(
                    text: browserDate(modified),
                    sortValue: .number(modified ?? -.greatestFiniteMagnitude)),
            ], tooltip: tooltip, searchText: name, sortRank: directory ? 0 : 1)
    }
    static func registryKeys(_ keys: [RegistryKeyNode]) -> [OutlineBrowserNode] {
        keys.map { key in
            OutlineBrowserNode(
                id: key.path, title: key.title.isEmpty ? "(Empty component)" : key.title,
                cells: [
                    "name": OutlineCell(
                        text: key.title.isEmpty ? "(Empty component)" : key.title, icon: "folder")
                ],
                children: registryKeys(key.children ?? []), tooltip: key.path, searchText: key.path)
        }
    }
    static func registryValues(_ values: [RegistryValueRecord]) -> [OutlineBrowserNode] {
        values.map { value in
            OutlineBrowserNode(
                id: value.id, title: value.displayName,
                cells: [
                    "name": OutlineCell(text: value.displayName),
                    "type": OutlineCell(text: value.typeName),
                    "data": OutlineCell(
                        text: value.summary, monospaced: true,
                        icon: value.warning == nil ? nil : "exclamationmark.triangle",
                        warning: value.warning != nil, tooltip: value.warning ?? value.summary),
                ], searchText: value.displayName + " " + value.typeName + " " + value.summary)
        }
    }
    static func profile(
        _ entries: [ConfigurationEntry], summary: (ConfigurationEntry) -> String
    ) -> [OutlineBrowserNode] {
        let applications = Dictionary(grouping: entries, by: \.application)
        return applications.keys.sorted().map { application in
            let children = (applications[application] ?? []).map { entry in
                OutlineBrowserNode(
                    id: entry.id, title: entry.key,
                    cells: [
                        "name": OutlineCell(text: entry.key, icon: "doc.text"),
                        "data": OutlineCell(text: summary(entry), monospaced: true),
                    ], tooltip: application + " / " + entry.key,
                    searchText: application + " " + entry.key)
            }
            return OutlineBrowserNode(
                id: "application:" + Data(application.utf8).base64EncodedString(),
                title: application.isEmpty ? "(Empty application)" : application,
                cells: [
                    "name": OutlineCell(
                        text: application.isEmpty ? "(Empty application)" : application,
                        icon: "folder"),
                    "data": OutlineCell(
                        text: children.count == 1 ? "1 value" : "\(children.count) values",
                        secondary: true),
                ],
                children: children, emphasized: true, searchText: application)
        }
    }
    static func configurationFiles(_ files: [ConfigurationFile]) -> [OutlineBrowserNode] {
        files.map { file in
            OutlineBrowserNode(
                id: file.path, title: file.label,
                cells: [
                    "name": OutlineCell(text: file.label, icon: "doc.text"),
                    "size": OutlineCell(
                        text: browserSize(file.size, directory: false), sortValue: .bytes(file.size)
                    ),
                ],
                tooltip: file.path + (file.reason.isEmpty ? "" : "\n" + file.reason),
                searchText: file.label + " " + file.path)
        }
    }
}
