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

public struct DesktopRequest: Encodable, Sendable {
    public var application: String?
    public var settings: AudioSettings?
    public var exporter: [String: String]?
    public var includeImages: Bool?
    public var path: String?
    public var sources: [String]?
    public var categories: [String]?
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
    public var needsMediaSelection: Bool {
        !missing.isEmpty && !(media ?? []).contains {
            $0.status == "verified-os2-media" || $0.status == "verified-os2-set"
        }
    }
}

public struct OS2SupportStatus: Decodable, Sendable {
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
    case cmd, fonts, assets, printers, help, full
    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .cmd: "CMD and REXX Support"
        case .fonts: "Fonts"
        case .assets: "Sounds, Images and Video"
        case .printers: "Printer Support"
        case .help: "Full Help"
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
        case .full: "Includes all options above and remaining OS/2 files. Components supplied by 2ine are excluded."
        }
    }
    public static let defaults: Set<Self> = [.cmd]
}
public struct OS2ExtractionPlan: Decodable, Sendable {
    public struct File: Decodable, Sendable, Identifiable {
        public let path: String
        public let source: String
        public let category: OS2ExtractionCategory
        public let provided: Bool
        public let present: Bool
        public let size: UInt64
        public let priority: Int?
        public var id: String { source }
        public func selected(by categories: Set<OS2ExtractionCategory>) -> Bool {
            !provided && !present && (categories.contains(.full) || categories.contains(category))
        }
    }
    public let files: [File]
    public let unmapped: [String]
    public func selectedFiles(_ categories: Set<OS2ExtractionCategory>) -> [File] {
        let eligible = files.filter { $0.selected(by: categories) }
        let priorities = Dictionary(grouping: eligible, by: { $0.path.uppercased() }).mapValues { $0.map { $0.priority ?? 1 }.max() ?? 1 }
        var paths: Set<String> = []
        return eligible.filter { ($0.priority ?? 1) == priorities[$0.path.uppercased()] && paths.insert($0.path.uppercased()).inserted }
    }
}
