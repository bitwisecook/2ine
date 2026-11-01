import Foundation

public struct Preferences: Codable, Sendable {
    public var balls: [String] = []
    public var selected: String?
    public init() {}
}

public actor PreferenceStore {
    private let url: URL
    public init(url: URL) { self.url = url }
    public func load() throws -> Preferences {
        guard FileManager.default.fileExists(atPath: url.path) else { return Preferences() }
        return try JSONDecoder().decode(Preferences.self, from: Data(contentsOf: url))
    }
    public func save(_ preferences: Preferences) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(preferences).write(to: url, options: .atomic)
    }
}
