import Foundation

public struct Preferences: Codable, Sendable {
    public var ballPaths: [String] = []
    public var selected: String?
    public var lastOS2MediaSources: [String] = []
    public var lastOS2MediaFiles: [String] = []
    public init() {}
    private enum CodingKeys: String, CodingKey { case ballPaths, balls, selected, lastOS2MediaSources, lastOS2MediaFiles }
    public init(from decoder: any Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        ballPaths = try values.decodeIfPresent([String].self, forKey: .ballPaths)
            ?? values.decodeIfPresent([String].self, forKey: .balls) ?? []
        selected = try values.decodeIfPresent(String.self, forKey: .selected)
        lastOS2MediaSources = try values.decodeIfPresent([String].self, forKey: .lastOS2MediaSources) ?? []
        lastOS2MediaFiles = try values.decodeIfPresent([String].self, forKey: .lastOS2MediaFiles) ?? []
    }
    public func encode(to encoder: any Encoder) throws {
        var values = encoder.container(keyedBy: CodingKeys.self)
        try values.encode(ballPaths, forKey: .ballPaths)
        try values.encodeIfPresent(selected, forKey: .selected)
        try values.encode(lastOS2MediaSources, forKey: .lastOS2MediaSources)
        try values.encode(lastOS2MediaFiles, forKey: .lastOS2MediaFiles)
    }
}

public actor PreferenceStore {
    private let url: URL
    private let legacyURL: URL?
    public init(url: URL, legacyURL: URL? = nil) { self.url = url; self.legacyURL = legacyURL }
    public func load() throws -> Preferences {
        let source = FileManager.default.fileExists(atPath: url.path) ? url : legacyURL
        guard let source, FileManager.default.fileExists(atPath: source.path) else { return Preferences() }
        return try JSONDecoder().decode(Preferences.self, from: Data(contentsOf: source))
    }
    public func save(_ preferences: Preferences) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(preferences).write(to: url, options: .atomic)
    }
}
