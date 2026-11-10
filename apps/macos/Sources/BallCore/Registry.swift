import Foundation

public enum RegistryType: UInt32, CaseIterable, Sendable, Identifiable {
    case none = 0
    case string = 1
    case expandableString = 2
    case binary = 3
    case dword = 4
    case bigEndianDword = 5
    case link = 6
    case multiString = 7
    case resourceList = 8
    case fullResourceDescriptor = 9
    case resourceRequirementsList = 10
    case qword = 11
    public var id: UInt32 { rawValue }
    public var name: String {
        switch self {
        case .none: "REG_NONE"
        case .string: "REG_SZ"
        case .expandableString: "REG_EXPAND_SZ"
        case .binary: "REG_BINARY"
        case .dword: "REG_DWORD"
        case .bigEndianDword: "REG_DWORD_BIG_ENDIAN"
        case .link: "REG_LINK"
        case .multiString: "REG_MULTI_SZ"
        case .resourceList: "REG_RESOURCE_LIST"
        case .fullResourceDescriptor: "REG_FULL_RESOURCE_DESCRIPTOR"
        case .resourceRequirementsList: "REG_RESOURCE_REQUIREMENTS_LIST"
        case .qword: "REG_QWORD"
        }
    }
    public var integerBytes: Int? {
        switch self {
        case .dword, .bigEndianDword: 4
        case .qword: 8
        default: nil
        }
    }
}

public enum RegistryCodec {
    public static let maximumPayload = 16 * 1024 * 1024
    /// Longest key path; value names leave room for the two-byte "v:" key prefix.
    public static let maximumPathBytes = 32767
    public static let maximumValueNameBytes = 32765
    /// How many payload bytes a summary previews in hex.
    static let previewBytes = 24
    private static let asciiUppercase: ClosedRange<UInt32> = 65...90
    private static let asciiCaseOffset: UInt32 = 32
    public static let roots = ["hkcr", "hkcu", "hklm", "hku"]
    public static func foldASCII(_ value: String) -> String {
        String(
            String.UnicodeScalarView(
                value.unicodeScalars.map {
                    asciiUppercase.contains($0.value)
                        ? UnicodeScalar($0.value + asciiCaseOffset)! : $0
                }))
    }
    public static func rootTitle(_ value: String) -> String {
        switch value {
        case "hkcr": "HKEY_CLASSES_ROOT"
        case "hkcu": "HKEY_CURRENT_USER"
        case "hklm": "HKEY_LOCAL_MACHINE"
        case "hku": "HKEY_USERS"
        default: value
        }
    }
    public static func path(_ value: String) throws -> String {
        let components = value.split(separator: "\\", omittingEmptySubsequences: false)
        guard let first = components.first else { throw BallError("Choose a registry hive.") }
        let root = roots.first {
            foldASCII(String(first)) == $0 || String(first).uppercased() == rootTitle($0)
        }
        guard let root else {
            throw BallError(
                "Open32 supports HKEY_CLASSES_ROOT, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE and HKEY_USERS."
            )
        }
        let result = foldASCII(
            ([root] + components.dropFirst().map(String.init)).joined(separator: "\\"))
        guard !components.contains(where: \.isEmpty), !result.contains("\0"),
            result.utf8.count <= maximumPathBytes,
            !result.unicodeScalars.contains(where: { $0.value > 255 })
        else {
            throw BallError(
                "A registry path must use Latin-1 characters, have nonempty components, no NUL characters and at most 32,767 bytes."
            )
        }
        return result
    }
    public static func valueName(_ value: String) throws -> String {
        guard !value.contains("\0"), value.utf8.count <= maximumValueNameBytes,
            !value.unicodeScalars.contains(where: { $0.value > 255 })
        else {
            throw BallError(
                "A value name must use Latin-1 characters, have no NUL characters and at most 32,765 bytes. Leave it empty for the default value."
            )
        }
        return foldASCII(value)
    }
    public static func typeName(_ type: UInt32?) -> String {
        guard let type else { return "Malformed record" }
        return RegistryType(rawValue: type)?.name ?? "Unknown type \(type)"
    }
    public static func wire(type: UInt32, payload: [UInt8]) throws -> [UInt8] {
        guard payload.count <= maximumPayload else {
            throw BallError("Open32 values support at most 16 MiB of data.")
        }
        if type == RegistryType.dword.rawValue && payload.count != 4 {
            throw BallError("REG_DWORD requires exactly four bytes.")
        }
        return (0..<4).map { UInt8(truncatingIfNeeded: type >> ($0 * 8)) } + payload
    }
    public static func validateWire(_ bytes: [UInt8]) throws {
        guard bytes.count >= 4 else {
            throw BallError(
                "The raw record needs a four-byte little-endian type header. Repair it explicitly before saving."
            )
        }
        let type = bytes.prefix(4).enumerated().reduce(UInt32(0)) {
            $0 | UInt32($1.element) << ($1.offset * 8)
        }
        guard bytes.count - 4 <= maximumPayload else {
            throw BallError("Open32 values support at most 16 MiB of data.")
        }
        if type == RegistryType.dword.rawValue && bytes.count != 8 {
            throw BallError("REG_DWORD requires exactly four bytes.")
        }
    }
    public static func string(_ payload: [UInt8]) throws -> String {
        guard payload.last == 0, !payload.dropLast().contains(0) else {
            throw BallError(
                "A string requires one terminating NUL and no embedded NUL bytes. Use raw hexadecimal to preserve malformed data."
            )
        }
        return ConfigurationValue.latin1(Array(payload.dropLast()))
    }
    public static func multiString(_ payload: [UInt8]) throws -> [String] {
        guard payload.count >= 2, payload.suffix(2).allSatisfy({ $0 == 0 }) else {
            throw BallError("REG_MULTI_SZ requires a double NUL terminator.")
        }
        let body = payload.dropLast(2)
        if body.isEmpty { return [] }
        let rows = body.split(separator: 0, omittingEmptySubsequences: false)
        guard !rows.contains(where: \.isEmpty) else {
            throw BallError("REG_MULTI_SZ cannot contain an empty entry before its terminator.")
        }
        return rows.map { ConfigurationValue.latin1(Array($0)) }
    }
    public static func encodeMultiString(_ rows: [String]) throws -> [UInt8] {
        guard !rows.contains(where: \.isEmpty) else {
            throw BallError("Multi-string entries cannot be empty.")
        }
        return try rows.flatMap { try ConfigurationValue.encodeText($0, terminated: true) }
            + (rows.isEmpty ? [0, 0] : [0])
    }
    public static func integer(_ payload: [UInt8], type: RegistryType) throws -> UInt64 {
        guard let count = type.integerBytes, payload.count == count else {
            throw BallError("\(type.name) requires exactly \(type.integerBytes ?? 0) bytes.")
        }
        let bytes = type == .bigEndianDword ? Array(payload.reversed()) : payload
        return bytes.enumerated().reduce(UInt64(0)) { $0 | UInt64($1.element) << ($1.offset * 8) }
    }
    public static func encodeInteger(_ text: String, type: RegistryType) throws -> [UInt8] {
        let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let hexadecimal = text.hasPrefix("0x") || text.hasPrefix("0X")
        guard let count = type.integerBytes,
            let value = UInt64(
                hexadecimal ? String(text.dropFirst(2)) : text, radix: hexadecimal ? 16 : 10),
            count == 8 || value <= UInt64(UInt32.max)
        else {
            throw BallError(
                "Enter an unsigned \(type == .qword ? "64" : "32")-bit decimal number or hexadecimal number starting with 0x."
            )
        }
        let bytes = (0..<count).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) }
        return type == .bigEndianDword ? Array(bytes.reversed()) : bytes
    }
}

public struct RegistryValueRecord: Identifiable, Hashable, Sendable {
    public let entry: ConfigurationEntry
    public init(_ entry: ConfigurationEntry) { self.entry = entry }
    public var id: String { entry.id }
    public var name: String { String(entry.key.dropFirst(2)) }
    public var displayName: String { name.isEmpty ? "(Default)" : name }
    public var type: UInt32? { entry.registryType }
    public var typeName: String { RegistryCodec.typeName(type) }
    public var payload: [UInt8] { Array(entry.bytes.dropFirst(4)) }
    public var editable: Bool {
        entry.key.hasPrefix("v:")
            && (try? RegistryCodec.path(entry.application)) == entry.application
            && RegistryCodec.foldASCII(entry.key) == entry.key
    }
    public var warning: String? {
        guard let type else { return "Missing or truncated type header. Raw bytes are preserved." }
        do {
            switch RegistryType(rawValue: type) {
            case .string, .expandableString: _ = try RegistryCodec.string(payload)
            case .multiString: _ = try RegistryCodec.multiString(payload)
            case .dword, .bigEndianDword, .qword:
                _ = try RegistryCodec.integer(payload, type: RegistryType(rawValue: type)!)
            default: break
            }
            return nil
        } catch { return error.localizedDescription }
    }
    public var summary: String {
        if warning == nil, let kind = type.flatMap(RegistryType.init(rawValue:)) {
            switch kind {
            case .string, .expandableString:
                if let text = try? RegistryCodec.string(payload) {
                    return text.replacingOccurrences(of: "\n", with: " ↵ ")
                }
            case .multiString:
                if let rows = try? RegistryCodec.multiString(payload) {
                    return rows.joined(separator: " · ")
                }
            case .dword, .bigEndianDword, .qword:
                if let value = try? RegistryCodec.integer(payload, type: kind) {
                    return "\(value) (0x\(String(value, radix: 16).uppercased()))"
                }
            default: break
            }
        }
        let bytes = type == nil ? entry.bytes : payload
        return "\(bytes.count) bytes: "
            + ConfigurationValue.hex(Array(bytes.prefix(RegistryCodec.previewBytes)))
            + (bytes.count > RegistryCodec.previewBytes ? "…" : "")
    }
}

public struct RegistryKeyNode: Identifiable, Hashable, Sendable {
    public let path: String
    public let title: String
    public let children: [RegistryKeyNode]?
    public var id: String { path }
}

public struct RegistryTree: Sendable {
    public let roots: [RegistryKeyNode]
    public let values: [RegistryValueRecord]
    public init(_ snapshot: ConfigurationSnapshot) {
        values = snapshot.entries.filter { $0.key.hasPrefix("v:") }.map(RegistryValueRecord.init)
        var paths = Set(RegistryCodec.roots)
        for entry in snapshot.entries {
            let components = entry.application.split(
                separator: "\\", omittingEmptySubsequences: false)
            for count in 1...max(1, components.count) {
                paths.insert(components.prefix(count).joined(separator: "\\"))
            }
        }
        var children: [String: [String]] = [:]
        for path in paths {
            if let separator = path.lastIndex(of: "\\") {
                children[String(path[..<separator]), default: []].append(path)
            }
        }
        var nodes: [String: RegistryKeyNode] = [:]
        for path in paths.sorted(by: { $0.count > $1.count }) {
            let nested = (children[path] ?? []).sorted().compactMap { nodes[$0] }
            nodes[path] = RegistryKeyNode(
                path: path,
                title: path.contains("\\")
                    ? String(
                        path.split(separator: "\\", omittingEmptySubsequences: false).last ?? "")
                    : RegistryCodec.rootTitle(path), children: nested.isEmpty ? nil : nested)
        }
        roots = paths.filter { !$0.contains("\\") }.sorted().compactMap { nodes[$0] }
    }
    public func values(at path: String) -> [RegistryValueRecord] {
        values.filter { $0.entry.application == path }.sorted {
            $0.name.localizedStandardCompare($1.name) == .orderedAscending
        }
    }
}

public enum RegistryRepresentation: String, CaseIterable, Identifiable, Sendable {
    case string, multiString, integer, hex, rawRecord
    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .string: "Latin-1 Text"
        case .multiString: "String List"
        case .integer: "Unsigned Integer"
        case .hex: "Hexadecimal Data"
        case .rawRecord: "Complete Raw Record"
        }
    }
}

public struct RegistryValueDraft: Sendable {
    public var path: String
    public var name: String
    public var typeText: String
    public var representation: RegistryRepresentation
    public var text: String
    public init(record: RegistryValueRecord?, path: String) {
        self.path = record?.entry.application ?? path
        name = record?.name ?? ""
        typeText = String(record?.type ?? RegistryType.string.rawValue)
        representation = .hex
        text = ConfigurationValue.hex(record?.payload ?? [0])
        if let record, record.type == nil {
            representation = .rawRecord
            text = record.entry.data
        } else {
            let preferred = typedRepresentation
            if let preferred { try? selectRepresentation(preferred) }
        }
    }
    public var type: UInt32? {
        if representation == .rawRecord {
            let header = String(text.lazy.filter { !$0.isWhitespace }.prefix(8))
            guard let bytes = try? ConfigurationValue.decodeHex(header), bytes.count == 4 else {
                return nil
            }
            return bytes.enumerated().reduce(UInt32(0)) {
                $0 | UInt32($1.element) << ($1.offset * 8)
            }
        }
        let value = typeText.trimmingCharacters(in: .whitespacesAndNewlines)
        return value.hasPrefix("0x") || value.hasPrefix("0X")
            ? UInt32(value.dropFirst(2), radix: 16) : UInt32(value)
    }
    public var typedRepresentation: RegistryRepresentation? {
        switch type.flatMap(RegistryType.init(rawValue:)) {
        case .string, .expandableString: .string
        case .multiString: .multiString
        case .dword, .bigEndianDword, .qword: .integer
        default: nil
        }
    }
    public mutating func newType(_ value: UInt32) {
        typeText = String(value)
        representation = .hex
        text = ""
        representation = typedRepresentation ?? .hex
        text = representation == .integer ? "0" : ""
    }
    public func bytes(validate: Bool = true) throws -> [UInt8] {
        let result: [UInt8]
        if representation == .rawRecord {
            result = try ConfigurationValue.decodeHex(text)
        } else {
            guard let type else {
                throw BallError("Enter a registry type between 0 and 4,294,967,295.")
            }
            let payload: [UInt8]
            switch representation {
            case .string:
                guard type == 1 || type == 2 else {
                    throw BallError("Choose a string type before using the text editor.")
                }
                payload = try ConfigurationValue.encodeText(text, terminated: true)
            case .multiString:
                guard type == 7 else {
                    throw BallError("Choose REG_MULTI_SZ before using the string list editor.")
                }
                payload = try RegistryCodec.encodeMultiString(
                    text.isEmpty ? [] : text.components(separatedBy: "\n"))
            case .integer:
                guard let kind = RegistryType(rawValue: type), kind.integerBytes != nil else {
                    throw BallError("Choose DWORD or QWORD before using the integer editor.")
                }
                payload = try RegistryCodec.encodeInteger(text, type: kind)
            case .hex: payload = try ConfigurationValue.decodeHex(text)
            case .rawRecord: payload = []
            }
            result = (0..<4).map { UInt8(truncatingIfNeeded: type >> ($0 * 8)) } + payload
        }
        if validate { try RegistryCodec.validateWire(result) }
        return result
    }
    public mutating func selectRepresentation(_ next: RegistryRepresentation) throws {
        guard next != representation else { return }
        let wire = try bytes(validate: false)
        let header =
            wire.count >= 4
            ? wire.prefix(4).enumerated().reduce(UInt32(0)) {
                $0 | UInt32($1.element) << ($1.offset * 8)
            } : nil
        let payload = Array(wire.dropFirst(4))
        let value: String
        switch next {
        case .rawRecord: value = ConfigurationValue.hex(wire)
        case .hex:
            guard header != nil else {
                throw BallError("Repair the type header in the complete raw record first.")
            }
            value = ConfigurationValue.hex(payload)
        case .string: value = try RegistryCodec.string(payload)
        case .multiString:
            let rows = try RegistryCodec.multiString(payload)
            guard !rows.contains(where: { $0.contains("\n") || $0.contains("\r") }) else {
                throw BallError(
                    "An entry contains a newline; use hexadecimal to preserve its exact bytes.")
            }
            value = rows.joined(separator: "\n")
        case .integer:
            guard let header, let kind = RegistryType(rawValue: header), kind.integerBytes != nil
            else { throw BallError("The record does not contain a supported integer type.") }
            value = String(try RegistryCodec.integer(payload, type: kind))
        }
        representation = next
        text = value
        if let header { typeText = String(header) }
    }
}
