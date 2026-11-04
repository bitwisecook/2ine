@testable import BallCore
import Foundation
import Testing

@Test func desktopBundlePreservesIdentityAndPrivateArtifacts() async throws {
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
    let result = try await DesktopLaunchBundle.create(ball: root.path, sessionID: "fixture-session", loader: loader, identity: identity)
    #expect(try Data(contentsOf: result.executable) == bytes)
    #expect(result.name == "Lotus Word Pro")
    let contents = result.executable.deletingLastPathComponent().deletingLastPathComponent()
    let info = try #require(PropertyListSerialization.propertyList(from: Data(contentsOf: contents.appending(path: "Info.plist")), format: nil) as? [String: Any])
    #expect(info["CFBundleDisplayName"] as? String == "Lotus Word Pro")
    #expect(info["CFBundleName"] as? String == "Lotus Word Pro")
    #expect(info["LSUIElement"] as? Bool == true)
    let link = contents.appending(path: "Resources/Runtime")
    #expect(!files.fileExists(atPath: link.path))
    let image = try Data(contentsOf: #require(result.icon))
    #expect(image.prefix(8) == Data("2INERGBA".utf8))
    #expect(image.dropFirst(16) == pixels)
    await #expect(throws: BallError.self) {
        try await DesktopLaunchBundle.create(ball: root.path, sessionID: "fixture-session", loader: loader, identity: identity)
    }
    #expect(try Data(contentsOf: result.executable) == bytes)
}

@Test func desktopBundleRejectsEscapesAndDoesNotFollowCacheLinks() async throws {
    let files = FileManager.default
    let root = files.temporaryDirectory.appending(path: "2ine-desktop-\(UUID().uuidString)")
    defer { try? files.removeItem(at: root) }
    let ball = root.appending(path: "Fixture.2ineball"), outside = root.appending(path: "outside")
    try files.createDirectory(at: ball, withIntermediateDirectories: true)
    try files.createDirectory(at: outside, withIntermediateDirectories: true)
    let loader = root.appending(path: "lx_loader")
    try Data("fixture".utf8).write(to: loader)
    await #expect(throws: BallError.self) {
        try await DesktopLaunchBundle.create(ball: ball.path, sessionID: "../escape", loader: loader, identity: nil)
    }
    try files.createSymbolicLink(at: ball.appending(path: "cache"), withDestinationURL: outside)
    await #expect(throws: BallError.self) {
        try await DesktopLaunchBundle.create(ball: ball.path, sessionID: "safe", loader: loader, identity: nil)
    }
    #expect(try files.contentsOfDirectory(atPath: outside.path).isEmpty)
}

@Test func desktopMachOBundleSealsIdentityAndPreservesEntitlements() async throws {
    let files = FileManager.default
    let root = files.temporaryDirectory.appending(path: "2ine desktop signed \(UUID().uuidString).2ineball")
    defer { try? files.removeItem(at: root) }
    let runtime = root.appending(path: "cache/runtime/pinned")
    try files.createDirectory(at: runtime, withIntermediateDirectories: true)
    let loader = runtime.appending(path: "lx_loader")
    try files.copyItem(at: URL(filePath: "/usr/bin/true"), to: loader)
    let entitlements = root.appending(path: "fixture-entitlements.plist")
    try PropertyListSerialization.data(fromPropertyList: ["com.apple.security.cs.allow-jit": true], format: .xml, options: 0).write(to: entitlements)
    let originalSign = try await ProcessRunner().run(executable: URL(filePath: "/usr/bin/codesign"),
        arguments: ["--force", "--sign", "-", "--entitlements", entitlements.path, "--", loader.path], log: root.appending(path: "original-sign.log"))
    #expect(originalSign.code == 0)
    let original = try Data(contentsOf: loader)
    let icon = ExecutableIcon(source: "fixture", displayName: nil, rgba: Data([255, 0, 0, 255]), image: nil, width: 1, height: 1)
    let result = try await DesktopLaunchBundle.create(ball: root.path, sessionID: "signed-fixture", loader: loader,
        identity: DesktopIdentity(name: "Original Application Name With Spaces", nameSource: "launcher", icon: icon))
    let bundle = result.executable.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    let verify = try await ProcessRunner().run(executable: URL(filePath: "/usr/bin/codesign"),
        arguments: ["--verify", "--strict", "--", bundle.path], log: root.appending(path: "verify.log"))
    #expect(verify.code == 0)
    let queried = try await ProcessRunner().run(executable: URL(filePath: "/usr/bin/codesign"),
        arguments: ["--display", "--entitlements", ":-", "--", bundle.path], log: root.appending(path: "entitlements.log"))
    #expect(queried.code == 0 && queried.output.contains("com.apple.security.cs.allow-jit"))
    let xmlStart = try #require(queried.output.range(of: "<?xml")?.lowerBound)
    let xmlEnd = try #require(queried.output.range(of: "</plist>")?.upperBound)
    let preserved = try #require(PropertyListSerialization.propertyList(from: Data(queried.output[xmlStart..<xmlEnd].utf8), format: nil) as? [String: Any])
    #expect(preserved["com.apple.security.cs.allow-jit"] as? Bool == true)
    #expect(try Data(contentsOf: loader) == original)
    // Both plist identity and the copied icon must be covered by the completed bundle seal.
    let info = bundle.appending(path: "Contents/Info.plist")
    let originalInfo = try Data(contentsOf: info)
    var changed = try #require(PropertyListSerialization.propertyList(from: Data(contentsOf: info), format: nil) as? [String: Any])
    changed["CFBundleDisplayName"] = "Tampered Name"
    try PropertyListSerialization.data(fromPropertyList: changed, format: .xml, options: 0).write(to: info)
    let tampered = try await ProcessRunner().run(executable: URL(filePath: "/usr/bin/codesign"),
        arguments: ["--verify", "--strict", "--", bundle.path], log: root.appending(path: "tampered.log"))
    #expect(tampered.code != 0)
    try originalInfo.write(to: info)
    try Data("tampered icon".utf8).write(to: #require(result.icon))
    let tamperedIcon = try await ProcessRunner().run(executable: URL(filePath: "/usr/bin/codesign"),
        arguments: ["--verify", "--strict", "--", bundle.path], log: root.appending(path: "tampered-icon.log"))
    #expect(tamperedIcon.code != 0)
}
