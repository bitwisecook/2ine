import Foundation

public struct RuntimeFeature: Decodable, Sendable {
    public let requested: Bool?
    public let `default`: Bool
    public let effective: Bool
    public let available: Bool
    public let reason: String
}

public struct OpenGLFeatures: Decodable, Sendable {
    public struct Provider: Decodable, Sendable {
        public let available: Bool
        public let reason: String
    }
    public let requested: String?
    public let `default`: String
    public let effective: String
    public let available: Bool
    public let providers: [String: Provider]
    public let reason: String
}

public struct RuntimeFeatures: Decodable, Sendable {
    public let opengl: OpenGLFeatures
    public let video: RuntimeFeature
    public let pcm: RuntimeFeature
    public let midi: RuntimeFeature
    public let dive: RuntimeFeature
}
