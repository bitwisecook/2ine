// swift-tools-version: 6.4
import PackageDescription

let package = Package(
    name: "TwoineBalls",
    platforms: [.macOS("27.0")],
    products: [.executable(name: "TwoineBalls", targets: ["TwoineBalls"])],
    targets: [
        .target(name: "BallEngine", path: "Sources/BallEngine", exclude: ["CMakeLists.txt", "cli.c", "vendor/cjson/LICENSE", "vendor/sha256/README.md"], publicHeadersPath: "include", cSettings: [.define("CJSON_HIDE_SYMBOLS")]),
        .target(name: "BallCore", dependencies: ["BallEngine"], swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .executableTarget(name: "TwoineBalls", dependencies: ["BallCore"],
                          swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])]),
        .testTarget(name: "BallCoreTests", dependencies: ["BallCore"],
                    swiftSettings: [.unsafeFlags(["-strict-concurrency=complete", "-warnings-as-errors"])])
    ],
    swiftLanguageModes: [.v6]
)
