#if canImport(BallCore)
import BallCore
#else
@testable import TwoineBalls
#endif
import Foundation
import Testing

private func temporary() throws -> URL {
    let url = FileManager.default.temporaryDirectory.appending(path: "2ine-tests-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
}

@Test func processUsesExactArgvAndKeepsFullLog() async throws {
    let root = try temporary()
    defer { try? FileManager.default.removeItem(at: root) }
    let runner = ProcessRunner()
    let long = String(repeating: "x", count: 80000)
    let result = try await runner.run(executable: URL(filePath: "/usr/bin/printf"),
                                      arguments: ["%s|%s|%s|%s", long, "a b", #"C:\DATA\file"#, "$(touch injected)"],
                                      log: root.appending(path: "output.log"))
    #expect(result.code == 0)
    #expect(!result.signalled)
    #expect(result.output.utf8.count == 65536)
    #expect(result.output.hasSuffix(#"a b|C:\DATA\file|$(touch injected)"#))
    let full = try String(contentsOf: result.log, encoding: .utf8)
    #expect(full == long + #"|a b|C:\DATA\file|$(touch injected)"#)
    let failure = try await runner.run(executable: URL(filePath: "/usr/bin/false"), arguments: [], log: root.appending(path: "failure.log"))
    #expect(failure.code == 1)
}

@Test func preferencesPersistAndReportCorruption() async throws {
    let root = try temporary()
    defer { try? FileManager.default.removeItem(at: root) }
    let path = root.appending(path: "state/preferences.json")
    let store = PreferenceStore(url: path)
    #expect(try await store.load().balls.isEmpty)
    var prefs = Preferences()
    prefs.balls = ["/a path/with spaces", #"/OS2\data"#]
    prefs.selected = prefs.balls[0]
    try await store.save(prefs)
    #expect(try await store.load().balls == prefs.balls)
    #expect(try await store.load().selected == prefs.selected)
    try Data("broken".utf8).write(to: path)
    await #expect(throws: (any Error).self) { try await store.load() }
}

@Test func nativeCEngineInteroperatesAndProtectsConfigSys() async throws {
    let root = try temporary()
    defer { try? FileManager.default.removeItem(at: root) }
    let backend = BallBackend()
    let ball = root.appending(path: "Ball with spaces").path
    let created = try await backend.request(DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    #expect(created.manifest.drives["C"] == "drive_c")
    let changed = created.configSys + "SET DESKTOP=roundtrip\r\n"
    let saved = try await backend.request(DesktopRequest("save-config-sys", ball: ball, expected: created.configSys, text: changed), as: BallSnapshot.self)
    #expect(saved.configSys == changed)
    await #expect(throws: BallError.self) {
        try await backend.request(DesktopRequest("save-config-sys", ball: ball, expected: created.configSys, text: "stale"), as: BallSnapshot.self)
    }
    let current = try await backend.request(DesktopRequest("snapshot", ball: ball), as: BallSnapshot.self)
    #expect(current.configSys == changed)
}
