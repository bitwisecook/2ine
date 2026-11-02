import Foundation

public struct SavedApplication: Decodable, Sendable {
    public let program: String
    public let arguments: [String]
    public let cwd: String?
}

/// Read-only projection. All manifest writes go through the shared C engine,
/// retaining unknown fields and installer-owned state.
public struct BallManifest: Decodable, Sendable {
    public let version: Int
    public let drives: [String: String]
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
    public let manifest: BallManifest
    public let roots: [String: String]
    public let configSys: String
}

public struct DesktopRequest: Encodable, Sendable {
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
        var result: [String] = [], word = "", quote: Character?, started = false
        for character in text {
            if let current = quote {
                if character == current { quote = nil } else { word.append(character) }
            } else if character == "\"" || character == "'" {
                quote = character; started = true
            } else if character.isWhitespace {
                if started { result.append(word); word = ""; started = false }
            } else { word.append(character); started = true }
        }
        guard quote == nil else { throw BallError("Close the quote in the arguments field.") }
        if started { result.append(word) }
        return result
    }
}
