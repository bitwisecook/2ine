import Foundation

public struct SavedApplication: Decodable, Sendable {
    public let program: String
    public let arguments: [String]
    public let cwd: String?
    public let workingDirectories: [String: String]?
    public let environment: [String: String]?
    public let icon: String?

    public var workingDirectorySummary: String {
        var directories = workingDirectories ?? [:]
        if let cwd, let drive = cwd.first { directories[String(drive).uppercased()] = cwd }
        guard !directories.isEmpty else { return "Per-drive defaults" }
        return directories.keys.sorted().compactMap { directories[$0] }.joined(separator: "; ")
    }
}

/// Read-only projection. All manifest writes go through the shared C engine,
/// retaining unknown fields and installer-owned state.
public enum MediaDeviceKind: String, Codable, Sendable, CaseIterable {
    case cdrom, floppy, disk
    public var title: String { switch self { case .cdrom: "CD"; case .floppy: "Floppy"; case .disk: "Disk" } }
    public var symbol: String { switch self { case .cdrom: "opticaldisc"; case .floppy: "floppy"; case .disk: "externaldrive" } }
}
public enum DriveMapping: Decodable, Sendable {
    case directory(String), image(String), removable(MediaDeviceKind, String?, Bool, Bool)
    public init(from decoder: any Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let path = try? container.decode(String.self) { self = .directory(path) }
        else {
            struct ImageSource: Decodable { let image: String?; let device: MediaDeviceKind?; let readonly: Bool?; let locked: Bool? }
            let value = try container.decode(ImageSource.self)
            if let device = value.device { self = .removable(device, value.image, device == .cdrom || value.readonly == true, value.locked == true) }
            else if let image = value.image { self = .image(image) }
            else { throw DecodingError.dataCorruptedError(in: container, debugDescription: "Invalid drive mapping") }
        }
    }
    public var source: String { switch self { case .directory(let path), .image(let path): path; case .removable(_, let path, _, _): path ?? "Ejected" } }
    public var image: String? { switch self { case .image(let path): path; case .removable(_, let path, _, _): path; default: nil } }
    public var device: MediaDeviceKind? { switch self { case .image: .cdrom; case .removable(let kind, _, _, _): kind; default: nil } }
    public var isRemovable: Bool { device != nil }
    public var isReadOnly: Bool { switch self { case .image: true; case .removable(_, _, let readonly, _): readonly; default: false } }
    public var isLocked: Bool { if case .removable(_, _, _, let locked) = self { return locked }; return false }
}

public struct BallManifest: Decodable, Sendable {
    public let version: Int
    public let drives: [String: DriveMapping]
    public let environment: [String: String]
    public let system: [String: SettingValue]
    public let runtime: [String: String]
    public let applications: [String: SavedApplication]
}

public enum SettingValue: Decodable, Sendable {
    case text(String), boolean(Bool), number(Double)
    public init(from decoder: any Decoder) throws {
        let value = try decoder.singleValueContainer()
        if let string = try? value.decode(String.self) { self = .text(string) }
        else if let flag = try? value.decode(Bool.self) { self = .boolean(flag) }
        else { self = .number(try value.decode(Double.self)) }
    }
    public var text: String {
        switch self {
        case .text(let value): value
        case .boolean(let value): value ? "true" : "false"
        case .number(let value): String(value)
        }
    }
}

public struct BallSnapshot: Decodable, Sendable {
    public let os2Support: OS2SupportStatus?
    public let dependencyAudit: DependencyAudit?
    public let unavailableDrives: [String: String]?
    public let manifest: BallManifest
    public let roots: [String: String]
    public let configSys: String
    public let workingDirectories: [String: String]
    public let applicationRecords: [String: String]
    public let driveRecords: [String: String]?
    public let imageSources: [String: String]?
    public let currentDrive: String
    public let environmentDefaults: [String: String]
}

public struct RenamedBall: Decodable, Sendable {
    public let path: String
    public let snapshot: BallSnapshot
}

public struct DesktopRequest: Encodable, Sendable {
    public var application: String?
    public var settings: AudioSettings?
    public var exporter: [String: String]?
    public var includeImages: Bool?
    public var resolve: Bool?
    public var path: String?
    public var sources: [String]?
    public var categories: [String]?
    public var fileSelection: [String]?
    public var paths: [String]?
    public var selection: [OS2ComponentSelection]?
    public var operationID: String?
    public var configureComponents: [String]?
    public var name: String?
    public var originalName: String?
    public var program: String?
    public var workingDirectories: [String: String]?
    public var environment: [String: String]?
    public var iconSource: String?
    public var cwd: String?
    public var key: String?
    public var remove: Bool?
    public var action: String
    public var ball: String?
    public var build: String?
    public var arguments: [String]?
    public var expected: String?
    public var text: String?
    public var receipt: JSONValue?
    public var programHost: String?
    public var exitCode: Int32?
    public init(_ action: String, ball: String? = nil, build: String? = nil,
                arguments: [String]? = nil, expected: String? = nil, text: String? = nil,
                receipt: JSONValue? = nil, programHost: String? = nil, exitCode: Int32? = nil) {
        self.action = action; self.ball = ball; self.build = build
        self.arguments = arguments; self.expected = expected; self.text = text
        self.receipt = receipt; self.programHost = programHost; self.exitCode = exitCode
    }
}

public enum JSONValue: Codable, Sendable {
    case string(String), number(Double), boolean(Bool), array([JSONValue]), object([String: JSONValue]), null
    public init(from decoder: any Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() { self = .null }
        else if let value = try? c.decode(String.self) { self = .string(value) }
        else if let value = try? c.decode(Bool.self) { self = .boolean(value) }
        else if let value = try? c.decode(Double.self) { self = .number(value) }
        else if let value = try? c.decode([JSONValue].self) { self = .array(value) }
        else { self = .object(try c.decode([String: JSONValue].self)) }
    }
    public func encode(to encoder: any Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .string(let v): try c.encode(v)
        case .number(let v): try c.encode(v)
        case .boolean(let v): try c.encode(v)
        case .array(let v): try c.encode(v)
        case .object(let v): try c.encode(v)
        case .null: try c.encodeNil()
        }
    }
}

public struct BallError: LocalizedError, Sendable {
    public let message: String
    public init(_ message: String) { self.message = message }
    public var errorDescription: String? { message }
}

/// Quotes group whitespace; backslashes are always literal OS/2 separators.
public enum Arguments {
    public static func parse(_ text: String) throws -> [String] {
        try NativeBallCore.request(DesktopRequest("parse-arguments", text: text), as: [String].self)
    }
    public static func format(_ arguments: [String]) throws -> String {
        try NativeBallCore.request(DesktopRequest("format-arguments", arguments: arguments), as: String.self)
    }
}

public struct GuestEntry: Decodable, Sendable, Identifiable {
    public let name: String
    public let path: String
    public let hostPath: String
    public let directory: Bool
    public let size: UInt64?
    public let modified: Double?
    public var id: String { path }
}
public struct GuestDirectory: Decodable, Sendable {
    public let parent: String?
    public let path: String
    public let hostPath: String
    public let entries: [GuestEntry]
}
public struct ExecutableIcon: Decodable, Sendable {
    public let source: String
    public let displayName: String?
    public let rgba: Data?
    public let image: Data?
    public let width: Int?
    public let height: Int?
}

public struct DependencyAudit: Decodable, Sendable {
    public struct ComponentFile: Decodable, Sendable {
        public let path: String
        public let status: String
    }
    public struct ComponentGroup: Decodable, Sendable, Identifiable {
        public let id: String
        public let title: String
        public let modules: [String]?
        public let requiredBy: [String]?
    }
    public struct Media: Decodable, Sendable {
        public let path: String
        public let status: String?
    }
    public struct Imported: Decodable, Sendable {
        public let path: String
        public let source: String
        public let sha256: String?
    }
    public let summary: String
    public let logPath: String
    public let jsonPath: String
    public let missing: [String]
    public let imported: [Imported]
    public let complete: Bool?
    public let staticOnly: Bool?
    public let media: [Media]?
    public let stale: Bool?
    public let componentGroups: [ComponentGroup]?
    public let componentFiles: [ComponentFile]?
    public var needsMediaSelection: Bool {
        (!missing.isEmpty || (componentFiles ?? []).contains { $0.status == "missing" }) && !(media ?? []).contains {
            $0.status == "verified-os2-media" || $0.status == "verified-os2-set"
        }
    }
}

public struct OS2SupportStatus: Decodable, Sendable {
    public let selectedCategories: [OS2ExtractionCategory]?
    public struct SkippedFile: Decodable, Sendable, Identifiable {
        public let path: String
        public let source: String
        public let reason: String
        public var id: String { path + source }
    }
    public struct File: Decodable, Sendable, Identifiable {
        public let name: String
        public let path: String
        public let present: Bool
        public let required: Bool?
        public var id: String { name }
    }
    public struct Imported: Decodable, Sendable {
        public let path: String
        public let source: String
    }
    public let files: [File]
    public let complete: Bool
    public let rexxComplete: Bool?
    public let imported: [Imported]?
    public let mediaSources: [String]?
    public let skippedFiles: [SkippedFile]?
    public var missing: String { files.filter { !$0.present && $0.required != false }.map(\.name).joined(separator: ", ") }
}
public struct MediaDirectory: Decodable, Sendable {
    public struct Entry: Decodable, Sendable, Identifiable {
        public let name: String
        public let path: String
        public let directory: Bool
        public let container: Bool
        public let size: UInt64
        public let modified: Double?
        public var id: String { path }
    }
    public let path: String
    public let label: String
    public let parent: String?
    public let entries: [Entry]
}

public struct OS2MediaSource: Decodable, Sendable {
    public struct SkippedSource: Decodable, Sendable {
        public let path: String
        public let reason: String
    }
    public let sources: [String]
    public let directory: MediaDirectory
    public let detectedISO: Bool
    public let installationDirectory: Bool?
    public let skippedSources: [SkippedSource]?
    public var warningSummary: String {
        (skippedSources ?? []).map { "Skipped \($0.path): \($0.reason)" }.joined(separator: "\n")
    }
}
public enum OS2ExtractionCategory: String, Codable, CaseIterable, Sendable, Identifiable {
    case cmd, fonts, assets, printers, help, som, runtime, multimedia, network
    case utilities, languages, diagnostics, legacy, opengl, full
    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .cmd: "CMD and REXX Support"
        case .fonts: "Fonts"
        case .assets: "Sounds, Images and Video"
        case .printers: "Printer Support"
        case .help: "Full Help"
        case .som: "SOM/DSOM Object Runtime"
        case .runtime: "Unicode and C Libraries"
        case .multimedia: "Multimedia Files"
        case .network: "Networking Files"
        case .utilities: "System Utilities"
        case .languages: "Languages and Input"
        case .diagnostics: "Diagnostics"
        case .legacy: "DOS and Windows Compatibility"
        case .opengl: "Original OpenGL Files"
        case .full: "Full OS/2 File Set"
        }
    }
    public var detail: String {
        switch self {
        case .cmd: "CMD.EXE, installer messages, system version, and the original REXX interpreter and utilities."
        case .fonts: "Original bitmap, TrueType, and PostScript fonts."
        case .assets: "Desktop backgrounds, icons, sounds, MIDI files, and videos."
        case .printers: "Original printer drivers and their support files."
        case .help: "Original help manager, viewer, and help books."
        case .som: "Object libraries, DSOM services, repositories and configuration. Includes supporting language libraries."
        case .runtime: "Original Unicode conversion and shared C runtime libraries."
        case .multimedia: "Original multimedia tools and support files. Components supplied by 2ine are excluded."
        case .network: "Original network tools and configuration files. Components supplied by 2ine are excluded."
        case .utilities: "Original command-line and desktop utilities."
        case .languages: "Language resources, keyboard layouts and input support."
        case .diagnostics: "Original diagnostic and troubleshooting tools."
        case .legacy: "Original DOS and Windows compatibility files."
        case .opengl: "Original OpenGL files. Copying these files does not change the selected graphics provider."
        case .full: "Includes all options above and remaining OS/2 files. Components supplied by 2ine are excluded."
        }
    }
    public static let defaults: Set<Self> = [.cmd]
    public var requires: Set<Self> { self == .som ? [.runtime] : [] }
    public static func expanded(_ categories: Set<Self>) -> Set<Self> {
        categories.union(categories.flatMap(\.requires))
    }
}
public struct OS2ExtractionPlan: Decodable, Sendable {
    public struct Provider: Decodable, Sendable, Equatable {
        public let title: String
        public let selection: String
        public let setting: String
        public let group: String
        public let nativeSelected: Bool?
        public let originalAvailable: Bool?
        public let choiceSupported: Bool?
    }
    public struct Node: Decodable, Sendable, Identifiable, Equatable {
        public enum SelectionState { case off, mixed, on }
        public let id: String
        public let title: String
        public let kind: String
        public let detail: String?
        public let category: OS2ExtractionCategory?
        public let source: String?
        public let path: String?
        public let size: UInt64?
        public let provided: Bool?
        public var present: Bool?
        public let disabled: Bool?
        public let priority: Int?
        public let preferred: Bool?
        public let provider: Provider?
        public let setting: String?
        public let selection: String?
        public let group: String?
        public var children: [Node]?
        public func updatingPresence(_ installed: Set<String>) -> Self {
            var copy = self
            if kind == "file", let path { copy.present = installed.contains(path.uppercased()) }
            copy.children = children?.map { $0.updatingPresence(installed) }
            return copy
        }
        public func updatingSourcePresence(_ installed: Set<String>) -> Self {
            var copy = self
            if kind == "file", let source { copy.present = installed.contains(source) }
            copy.children = children?.map { $0.updatingSourcePresence(installed) }
            return copy
        }
        public var eligibleSources: Set<String> {
            if disabled == true { return [] }
            if kind == "file", provided != true, disabled != true, let source { return [source] }
            return Set((children ?? []).flatMap { $0.eligibleSources })
        }
        public var files: [Node] {
            kind == "file" ? [self] : (children ?? []).flatMap(\.files)
        }
        public func selectionState(_ selected: Set<String>) -> SelectionState {
            let sources = eligibleSources
            let count = sources.intersection(selected).count
            return count == 0 ? .off : count == sources.count ? .on : .mixed
        }
        public func find(_ identity: String) -> Node? {
            if id == identity { return self }
            for child in children ?? [] {
                if let found = child.find(identity) { return found }
            }
            return nil
        }
        public func filtered(query: String) -> Node? {
            guard !query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return self }
            if FileSearch.matches(title + " " + (path ?? ""), query: query) { return self }
            let matches = (children ?? []).compactMap { $0.filtered(query: query) }
            guard !matches.isEmpty else { return nil }
            return Node(id: id, title: title, kind: kind, detail: detail, category: category, source: source,
                        path: path, size: size, provided: provided, present: present,
                        disabled: disabled, priority: priority, preferred: preferred,
                        provider: provider, setting: setting,
                        selection: selection, group: group, children: matches)
        }
    }
    public struct Group: Decodable, Sendable, Identifiable {
        public let id: OS2ExtractionCategory
        public let title: String
        public let detail: String
        public let defaultSelected: Bool
        public let requires: [OS2ExtractionCategory]
        public let provider: Provider?
    }
    public struct File: Decodable, Sendable, Identifiable {
        public let path: String
        public let source: String
        public let category: OS2ExtractionCategory
        public let provided: Bool
        public var present: Bool
        public let size: UInt64
        public let priority: Int?
        public var id: String { source }
        public func selected(by categories: Set<OS2ExtractionCategory>) -> Bool {
            !provided && !present && (categories.contains(.full) || categories.contains(category))
        }
    }
    public var files: [File]
    public let unmapped: [String]
    public let groups: [Group]?
    public var tree: Node?
    public func updatingPresence(_ paths: Set<String>) -> Self {
        let installed = Set(paths.map { $0.uppercased() })
        var copy = self
        copy.files = files.map { file in var updated = file; updated.present = installed.contains(file.path.uppercased()); return updated }
        copy.tree = tree?.updatingPresence(installed)
        return copy
    }
    public func updatingSourcePresence(_ sources: Set<String>) -> Self {
        var copy = self
        copy.files = files.map { file in var updated = file; updated.present = sources.contains(file.source); return updated }
        copy.tree = tree?.updatingSourcePresence(sources)
        return copy
    }
    public func sourceSelection(_ categories: Set<OS2ExtractionCategory>) -> Set<String> {
        let expanded = expanded(categories)
        return Set((tree?.files ?? []).filter {
            !$0.eligibleSources.isEmpty && (expanded.contains(.full) || $0.category.map(expanded.contains) == true)
        }.compactMap(\.source))
    }
    public func selecting(_ node: Node, in selected: Set<String>, enabled: Bool) -> Set<String> {
        guard node.disabled != true else { return selected }
        if !enabled { return selected.subtracting(node.eligibleSources) }
        var result = selected.union(node.eligibleSources)
        if node.kind == "group", let category = node.category,
           node.id == category.rawValue || category == .full {
            result.formUnion(sourceSelection(expanded([category])))
        }
        return result
    }
    public func configuredComponents(for selected: Set<String>) -> [String] {
        let som = Set((tree?.files ?? []).filter { $0.category == .som }.flatMap(\.eligibleSources))
        return !som.isEmpty && som.isSubset(of: selected) ? ["som"] : []
    }
    public func expanded(_ categories: Set<OS2ExtractionCategory>) -> Set<OS2ExtractionCategory> {
        var result = OS2ExtractionCategory.expanded(categories)
        for group in groups ?? [] where result.contains(group.id) {
            result.formUnion(group.requires)
        }
        return result
    }
    public var installedCategories: Set<OS2ExtractionCategory> {
        let present = Set(files.filter { !$0.provided && $0.present && $0.category != .full }.map(\.category))
        return Set(present.filter { selectedFiles([$0]).isEmpty })
    }
    public func selectedFiles(_ categories: Set<OS2ExtractionCategory>) -> [File] {
        let eligible = files.filter { $0.selected(by: expanded(categories)) }
        let priorities = Dictionary(grouping: eligible, by: { $0.path.uppercased() }).mapValues { $0.map { $0.priority ?? 1 }.max() ?? 1 }
        var paths: Set<String> = []
        return eligible.filter { ($0.priority ?? 1) == priorities[$0.path.uppercased()] && paths.insert($0.path.uppercased()).inserted }
    }
}
