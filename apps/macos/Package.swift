import Foundation
// swift-tools-version: 6.4
import PackageDescription

func sourceIdentity(_ arguments: [String], fallback: String) -> String {
    let process = Process()
    let pipe = Pipe()
    process.executableURL = URL(filePath: "/usr/bin/git")
    process.arguments = arguments
    process.currentDirectoryURL = URL(filePath: #filePath).deletingLastPathComponent()
    process.standardOutput = pipe
    process.standardError = FileHandle.nullDevice
    do { try process.run() } catch { return fallback }
    let output = pipe.fileHandleForReading.readDataToEndOfFile()
    process.waitUntilExit()
    let value = String(decoding: output, as: UTF8.self).trimmingCharacters(
        in: .whitespacesAndNewlines)
    return process.terminationStatus == 0 && !value.isEmpty ? value : fallback
}

// C sources include project headers by their path from the repository root
// ("ball/...", "formats/..."); this package sits two levels below it, in apps/macos.
let projectRoot = URL(filePath: #filePath).deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent().path(percentEncoded: false)

let ballBuildSettings: [CSetting] = [
    .unsafeFlags(["-I", projectRoot]),
    .define("CJSON_HIDE_SYMBOLS"),
    .define(
        "TB_SOURCE_VERSION",
        to: "\""
            + sourceIdentity(
                ["describe", "--tags", "--always", "--dirty"], fallback: "unversioned") + "\""),
    .define(
        "TB_SOURCE_REVISION",
        to: "\"" + sourceIdentity(["rev-parse", "HEAD"], fallback: "unknown") + "\""),
]

let package = Package(
    name: "TwoineBall",
    platforms: [.macOS("27.0")],
    products: [.executable(name: "TwoineBall", targets: ["TwoineBall"])],
    targets: [
        .target(
            name: "TwoineFormats", path: "Sources/TwoineFormats", exclude: ["CMakeLists.txt"],
            cSettings: [.unsafeFlags(["-I", projectRoot])]),
        // Vendored libraries compiled into the engine: cJSON, SHA-256, FatFs and
        // HWZIP. Deark's decoder is included by storage, and libarchive supplies
        // only API headers.
        .target(
            name: "TwoineThirdParty", path: "Sources/TwoineThirdParty",
            exclude: [
                "cjson/LICENSE", "sha256/README.md", "fatfs/README.md", "fatfs/LICENSE.txt",
                "hwzip/LICENSE", "hwzip/README.md", "deark", "libarchive",
            ],
            cSettings: [.define("CJSON_HIDE_SYMBOLS")]),
        .target(
            name: "TwoineStorage", dependencies: ["TwoineThirdParty"],
            path: "Sources/TwoineStorage",
            exclude: ["CMakeLists.txt"], publicHeadersPath: "include",
            cSettings: [.unsafeFlags(["-I", projectRoot]), .define("TB_MEDIA_TIME_CACHE")],
            linkerSettings: [.linkedLibrary("archive"), .linkedLibrary("z")]),
        .target(
            name: "TwoineOS2Config", path: "Sources/TwoineOS2Config", exclude: ["CMakeLists.txt"],
            cSettings: [.unsafeFlags(["-I", projectRoot])]),
        .target(
            name: "TwoineEnvironment",
            dependencies: ["TwoineFormats", "TwoineStorage", "TwoineThirdParty"],
            path: "Sources/TwoineEnvironment", exclude: ["CMakeLists.txt"],
            cSettings: [.unsafeFlags(["-I", projectRoot]), .define("CJSON_HIDE_SYMBOLS")]),
        .target(
            name: "TwoineLaunch",
            dependencies: ["TwoineFormats", "TwoineStorage", "TwoineThirdParty"],
            path: "Sources/TwoineLaunch", exclude: ["CMakeLists.txt"],
            cSettings: [.unsafeFlags(["-I", projectRoot]), .define("CJSON_HIDE_SYMBOLS")]),
        .target(
            name: "BallEngine",
            dependencies: [
                "TwoineFormats", "TwoineOS2Config", "TwoineStorage", "TwoineEnvironment",
                "TwoineLaunch", "TwoineThirdParty",
            ],
            path: "Sources/BallEngine",
            exclude: ["CMakeLists.txt"],
            publicHeadersPath: "include", cSettings: ballBuildSettings,
            linkerSettings: [.linkedLibrary("archive"), .linkedLibrary("z")]),
        .target(
            name: "BallCore", dependencies: ["BallEngine"],
            swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .executableTarget(
            name: "TwoineBall", dependencies: ["BallCore"],
            swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .testTarget(
            name: "TwoineBallModelTests", dependencies: ["TwoineBall", "BallCore"],
            swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .testTarget(
            name: "BallCoreTests", dependencies: ["BallCore"],
            swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
    ],
    swiftLanguageModes: [.v6]
)
