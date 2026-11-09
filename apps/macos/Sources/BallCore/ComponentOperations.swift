import Foundation

public struct OS2ComponentSelection: Encodable, Sendable {
    public let source: String
    public let path: String
    public init(source: String, path: String) {
        self.source = source
        self.path = path
    }
}

public struct OS2ComponentProgress: Decodable, Sendable, Equatable {
    public let operationID: String
    public let kind: String
    public let status: String
    public let phase: String
    public let activeSources: [String]
    public let activePaths: [String]
    public let completedPaths: [String]
    public let completedCount: Int
    public let totalCount: Int
    public let logPath: String?
    public let error: String?
}

public struct OS2ComponentState: Decodable, Sendable, Equatable {
    public struct File: Decodable, Sendable, Equatable {
        public let path: String
        public let present: Bool
        public let managed: Bool
        public let status: String
        public let reason: String?
        public let source: String?
        public let selectedSource: String?
    }
    public let installedPaths: [String]
    public let managedPaths: [String]
    public let protectedPaths: [String]
    public let modifiedPaths: [String]
    public let unownedPaths: [String]
    public let files: [File]
    public let progress: OS2ComponentProgress?
}
