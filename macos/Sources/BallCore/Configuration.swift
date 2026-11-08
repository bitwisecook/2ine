import Foundation

public struct ConfigurationEntry: Decodable, Sendable, Identifiable, Hashable {
    public let application: String
    public let key: String
    public let data: String
    public var id: String { application + "\0" + key }
    public var bytes: [UInt8] {
        stride(from: 0, to: data.count, by: 2).compactMap { at in
            let start = data.index(data.startIndex, offsetBy: at)
            return UInt8(data[start..<data.index(start, offsetBy: 2)], radix: 16)
        }
    }
    public var registryType: UInt32? {
        let value = bytes
        guard value.count >= 4 else { return nil }
        return value.prefix(4).enumerated().reduce(0) { $0 | UInt32($1.element) << ($1.offset * 8) }
    }
}

public struct ConfigurationSnapshot: Decodable, Sendable {
    public let area: String
    public let path: String
    public let revision: String
    public let format: String
    public let entries: [ConfigurationEntry]
    public init(
        area: String, path: String, revision: String, format: String, entries: [ConfigurationEntry]
    ) {
        self.area = area
        self.path = path
        self.revision = revision
        self.format = format
        self.entries = entries
    }
}

public struct ConfigurationFile: Decodable, Sendable, Identifiable {
    public let path: String
    public let label: String
    public let role: String
    public let kind: String
    public let size: UInt64
    public let editable: Bool
    public let reason: String
    public var id: String { path }
}

public struct ConfigurationFiles: Decodable, Sendable {
    public let files: [ConfigurationFile]
    public let truncated: Bool
    public let limit: Int
    public let skipped: Int
}

public struct ConfigurationFileSnapshot: Decodable, Sendable {
    public let path: String
    public let revision: String
    public let kind: String
    public let format: String
    public let editable: Bool
    public let reason: String
    public let text: String?
    public let data: String?
    public let entries: [ConfigurationEntry]?
    public var profile: ConfigurationSnapshot {
        ConfigurationSnapshot(
            area: "file", path: path, revision: revision, format: format, entries: entries ?? [])
    }
}

public enum ConfigurationValue {
    public static func hex(_ bytes: [UInt8]) -> String {
        bytes.map { String(format: "%02x", $0) }.joined()
    }
    public static func decodeHex(_ value: String) throws -> [UInt8] {
        let value = value.filter { !$0.isWhitespace }
        guard value.count.isMultiple(of: 2) else {
            throw BallError("Use pairs of hexadecimal digits.")
        }
        return try stride(from: 0, to: value.count, by: 2).map { at in
            let start = value.index(value.startIndex, offsetBy: at)
            guard let byte = UInt8(value[start..<value.index(start, offsetBy: 2)], radix: 16) else {
                throw BallError("Use hexadecimal digits from 0 to 9 and A to F.")
            }
            return byte
        }
    }
    public static func latin1(_ bytes: [UInt8]) -> String {
        String(String.UnicodeScalarView(bytes.map { UnicodeScalar(UInt32($0))! }))
    }
    public static func encodeText(_ value: String, terminated: Bool) throws -> [UInt8] {
        guard !value.unicodeScalars.contains(where: { $0.value > 255 || $0.value == 0 }) else {
            throw BallError(
                "Text must use Latin-1 characters and cannot contain NUL. Use hexadecimal for other binary values."
            )
        }
        return value.unicodeScalars.map { UInt8($0.value) } + (terminated ? [0] : [])
    }
}
