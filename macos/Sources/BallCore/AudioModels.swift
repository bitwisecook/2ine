// Public domain. No rights reserved. Wholly AI-authored.
import Foundation

public struct MIDIChannel: Codable, Sendable, Equatable {
    public var target: Int
    public var bank: String
    public var program: Int
    public var bankNumber: Int
    public var percussion: Int
    public var muted: Bool
    public init(channel: Int) {
        target = channel
        bank = ""
        program = -1
        bankNumber = -1
        percussion = -1
        muted = false
    }
}

public struct AudioSettings: Codable, Sendable, Equatable {
    public var profile: String
    public var bank: String
    public var channels: [MIDIChannel]
    public mutating func applyDefaults(_ preset: MIDIProfile) {
        profile = preset.id
        channels = preset.channels
    }
}

public struct SoundBank: Decodable, Sendable, Identifiable {
    public let id: String
    public let name: String
    public let format: String
    public let license: String?
    public let sha256: String?
}

public struct MIDIProfile: Decodable, Sendable, Identifiable {
    public let id: String
    public let name: String
    public let channels: [MIDIChannel]
}

public struct AudioSnapshot: Decodable, Sendable {
    public let settings: AudioSettings
    public let banks: [SoundBank]
    public let profiles: [MIDIProfile]
}

public struct ProjectSource: Decodable, Sendable, Identifiable {
    public let id: String
    public let name: String
    public let version: String
    public let license: String
    public let authors: [String]
    public let role: String?
    public let homepage: String
    public let source: String
    public let download: String
}

public struct ProjectCatalogue: Decodable, Sendable {
    public let catalogue: String
    public let originCredit: String
    public let projects: [ProjectSource]
}
