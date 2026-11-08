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
// ("ball/...", "formats/..."); that root lies outside the package directory.
let projectRoot = URL(filePath: #filePath).deletingLastPathComponent()
    .deletingLastPathComponent().path(percentEncoded: false)

let ballBuildSettings: [CSetting] = [
    .unsafeFlags(["-I", projectRoot]),
    .define("TB_MEDIA_TIME_CACHE"),
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
        .target(
            name: "BallEngine", dependencies: ["TwoineFormats"], path: "Sources/BallEngine",
            exclude: [
                "CMakeLists.txt", "cli.c", "vendor/cjson/LICENSE", "vendor/sha256/README.md",
                "vendor/libarchive/COPYING", "vendor/fatfs/README.md", "vendor/fatfs/LICENSE.txt",
                "vendor/hwzip/LICENSE", "vendor/hwzip/README.md",
            ], publicHeadersPath: "include", cSettings: ballBuildSettings,
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
