@testable import BallCore
import Foundation
import Testing

@Test func desktopBundlePreservesIdentityAndPinnedRuntime() throws {
    let files = FileManager.default
    let root = files.temporaryDirectory.appending(path: "2ine-desktop-\(UUID().uuidString).2ineball")
    defer { try? files.removeItem(at: root) }
    let runtime = root.appending(path: "cache/runtime/pinned")
    try files.createDirectory(at: runtime, withIntermediateDirectories: true)
    let loader = runtime.appending(path: "lx_loader"), bytes = Data("controlled loader".utf8)
    try bytes.write(to: loader)
    let pixels = Data([255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 0, 0])
    let icon = ExecutableIcon(source: "fixture", displayName: nil, rgba: pixels, image: nil, width: 2, height: 2)
    let identity = DesktopIdentity(name: "Lotus Word Pro", nameSource: "launcher", icon: icon)
    let result = try DesktopLaunchBundle.create(ball: root.path, sessionID: "fixture-session", loader: loader, identity: identity)
    #expect(try Data(contentsOf: result.executable) == bytes)
    #expect(result.name == "Lotus Word Pro")
    let contents = result.executable.deletingLastPathComponent().deletingLastPathComponent()
    let info = try #require(PropertyListSerialization.propertyList(from: Data(contentsOf: contents.appending(path: "Info.plist")), format: nil) as? [String: Any])
    #expect(info["CFBundleDisplayName"] as? String == "Lotus Word Pro")
    #expect(info["CFBundleName"] as? String == "Lotus Word Pro")
    #expect(info["LSUIElement"] as? Bool == true)
    let link = contents.appending(path: "Resources/Runtime")
    #expect(try !files.destinationOfSymbolicLink(atPath: link.path).hasPrefix("/"))
    #expect(canonicalLaunchBall(link.path) == canonicalLaunchBall(runtime.path))
    let image = try Data(contentsOf: #require(result.icon))
    #expect(image.prefix(8) == Data("2INERGBA".utf8))
    #expect(image.dropFirst(16) == pixels)
    #expect(throws: BallError.self) {
        try DesktopLaunchBundle.create(ball: root.path, sessionID: "fixture-session", loader: loader, identity: identity)
    }
    #expect(try Data(contentsOf: result.executable) == bytes)
}

@Test func desktopBundleRejectsEscapesAndDoesNotFollowCacheLinks() throws {
    let files = FileManager.default
    let root = files.temporaryDirectory.appending(path: "2ine-desktop-\(UUID().uuidString)")
    defer { try? files.removeItem(at: root) }
    let ball = root.appending(path: "Fixture.2ineball"), outside = root.appending(path: "outside")
    try files.createDirectory(at: ball, withIntermediateDirectories: true)
    try files.createDirectory(at: outside, withIntermediateDirectories: true)
    let loader = root.appending(path: "lx_loader")
    try Data("fixture".utf8).write(to: loader)
    #expect(throws: BallError.self) {
        try DesktopLaunchBundle.create(ball: ball.path, sessionID: "../escape", loader: loader, identity: nil)
    }
    try files.createSymbolicLink(at: ball.appending(path: "cache"), withDestinationURL: outside)
    #expect(throws: BallError.self) {
        try DesktopLaunchBundle.create(ball: ball.path, sessionID: "safe", loader: loader, identity: nil)
    }
    #expect(try files.contentsOfDirectory(atPath: outside.path).isEmpty)
}
