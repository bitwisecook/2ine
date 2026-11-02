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

@Test func driveBrowserAndPerApplicationSettingsRoundTripThroughC() async throws {
    let root = try temporary()
    defer { try? FileManager.default.removeItem(at: root) }
    let backend = BallBackend()
    let ball = root.appending(path: "Ball").path
    _ = try await backend.request(DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    let source = root.appending(path: "Game Files")
    try FileManager.default.createDirectory(at: source, withIntermediateDirectories: true)
    try Data("guest".utf8).write(to: source.appending(path: "OS2DOOM.EXE"))
    var copy = DesktopRequest("import-files", ball: ball)
    copy.path = "C:\\"; copy.sources = [source.path]
    let copied = try await backend.request(copy, as: GuestDirectory.self)
    #expect(copied.entries.contains { $0.name == "Game Files" && $0.directory })
    var directory = DesktopRequest("set-working-directory", ball: ball); directory.path = "C:\\Game Files"
    let defaults = try await backend.request(directory, as: BallSnapshot.self)
    #expect(defaults.workingDirectories["C"] == "C:\\Game Files")
    #expect(defaults.environmentDefaults["PATH"] != nil)
    var save = DesktopRequest("save-application", ball: ball, arguments: ["a b", "", #"C:\DATA\FILE"#])
    save.name = "Doom"; save.program = "C:\\Game Files\\OS2DOOM.EXE"
    save.workingDirectories = ["C": "C:\\TEMP"]
    save.environment = ["PATH": "%PATH%;C:\\Game Files", "DATA": "%TEMP%"]
    let saved = try await backend.request(save, as: BallSnapshot.self)
    #expect(saved.manifest.applications["Doom"]?.environment?["PATH"] == "%PATH%;C:\\Game Files")
    #expect(saved.manifest.applications["Doom"]?.workingDirectories?["C"] == "C:\\TEMP")
    let text = try Arguments.format(saved.manifest.applications["Doom"]!.arguments)
    #expect(try Arguments.parse(text) == ["a b", "", #"C:\DATA\FILE"#])
    var browse = DesktopRequest("browse", ball: ball); browse.path = "C:"
    #expect(try await backend.request(browse, as: GuestDirectory.self).path == "C:\\Game Files")
}
