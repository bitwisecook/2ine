// swift-tools-version: 6.4
import PackageDescription
import Foundation

func sourceIdentity(_ arguments: [String], fallback: String) -> String {
    let process = Process(); let pipe = Pipe()
    process.executableURL = URL(filePath: "/usr/bin/git")
    process.arguments = arguments
    process.currentDirectoryURL = URL(filePath: #filePath).deletingLastPathComponent()
    process.standardOutput = pipe; process.standardError = FileHandle.nullDevice
    do { try process.run() } catch { return fallback }
    let output = pipe.fileHandleForReading.readDataToEndOfFile()
    process.waitUntilExit()
    let value = String(decoding: output, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
    return process.terminationStatus == 0 && !value.isEmpty ? value : fallback
}

let ballBuildSettings: [CSetting] = [
    .define("CJSON_HIDE_SYMBOLS"),
    .define("TB_SOURCE_VERSION", to: "\"" + sourceIdentity(["describe", "--tags", "--always", "--dirty"], fallback: "unversioned") + "\""),
    .define("TB_SOURCE_REVISION", to: "\"" + sourceIdentity(["rev-parse", "HEAD"], fallback: "unknown") + "\"")
]

let package = Package(
    name: "TwoineBalls",
    platforms: [.macOS("27.0")],
    products: [.executable(name: "TwoineBalls", targets: ["TwoineBalls"])],
    targets: [
        .target(name: "BallEngine", path: "Sources/BallEngine", exclude: ["CMakeLists.txt", "cli.c", "vendor/cjson/LICENSE", "vendor/sha256/README.md", "vendor/libarchive/COPYING", "vendor/fatfs/README.md", "vendor/fatfs/LICENSE.txt"], publicHeadersPath: "include", cSettings: ballBuildSettings, linkerSettings: [.linkedLibrary("archive"), .linkedLibrary("z")]),
        .target(name: "BallCore", dependencies: ["BallEngine"], swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .executableTarget(name: "TwoineBalls", dependencies: ["BallCore"],
                          swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .testTarget(name: "TwoineBallsModelTests", dependencies: ["TwoineBalls", "BallCore"],
                    swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .testTarget(name: "BallCoreTests", dependencies: ["BallCore"],
                    swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])])
    ],
    swiftLanguageModes: [.v6]
)
