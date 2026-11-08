import AppKit
import BallCore
import Foundation
import Testing

@testable import TwoineBall

@MainActor private func waitForSessions(
    _ condition: () -> Bool, diagnostic: () -> String = { "" }
) async throws {
    let deadline = ContinuousClock.now + .seconds(12)
    while !condition() {
        guard ContinuousClock.now < deadline else {
            throw BallError(
                "Application sessions did not reach the expected state: " + diagnostic())
        }
        try await Task.sleep(for: .milliseconds(20))
    }
}

@Test @MainActor func applicationOrderingUsesActualIconsAndRespectsExplicitColumns() {
    let icon = NSImage(size: NSSize(width: 16, height: 16))
    let rows = [
        ApplicationRow(name: "License", program: "E.EXE", cwd: ""),
        ApplicationRow(name: "Word Pro", program: "WORDPRO.EXE", cwd: "", icon: icon),
        ApplicationRow(name: "1-2-3", program: "123W.EXE", cwd: "", icon: icon),
    ]
    #expect(ApplicationRow.sorted(rows, using: []).map(\.name) == ["1-2-3", "Word Pro", "License"])
    #expect(
        ApplicationRow.sorted(rows, using: [KeyPathComparator(\.name)]).map(\.name) == [
            "1-2-3", "License", "Word Pro",
        ])
    #expect(
        ApplicationRow.sorted(rows, using: [KeyPathComparator(\.program)]).map(\.name) == [
            "1-2-3", "License", "Word Pro",
        ])
}

@Test @MainActor func concurrentApplicationsKeepNavigationLogsAndStopTargetsIndependent()
    async throws
{
    let root = FileManager.default.temporaryDirectory.appending(
        path: "2ine-session-model-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    let ball = root.appending(path: "Apps.2ineball").path
    let other = root.appending(path: "Other.2ineball").path
    let runtime = root.appending(path: "Runtime")
    try FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
    let loader = runtime.appending(path: "lx_loader")
    try Data(
        #"""
        #!/bin/sh
        echo "$1 ready"
        exec /bin/sleep 20
        """#.utf8
    ).write(to: loader)
    try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: loader.path)
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"), runtimeURL: runtime)
    defer {
        for session in model.sessions where session.running {
            model.selected = session.ball
            model.selectSession(session.id)
            model.stopProcess(force: true)
        }
        try? FileManager.default.removeItem(at: root)
    }
    for path in [ball, other] {
        _ = try NativeBallCore.request(
            DesktopRequest("command", ball: path, arguments: ["init"]), as: BallSnapshot.self)
    }
    for name in ["A.EXE", "B.EXE"] {
        try Data("controlled executable fixture".utf8).write(
            to: URL(filePath: ball).appending(path: "drive_c/OS2/" + name))
    }
    model.paths = [ball, other]
    model.selected = ball
    model.snapshot = try NativeBallCore.request(
        DesktopRequest("snapshot", ball: ball), as: BallSnapshot.self)
    try await model.setRuntime(runtime.path, ball: ball)
    model.execute(["run", #"C:\OS2\A.EXE"#])
    try await waitForSessions(
        { model.sessions.count == 1 && !model.busy && model.sessions[0].output.contains("ready") },
        diagnostic: { model.sessions.map { $0.status + ": " + $0.output }.joined(separator: "\n") })
    model.execute(["run", #"C:\OS2\B.EXE"#])
    try await waitForSessions(
        { model.sessions.count == 2 && !model.busy && model.sessions[1].output.contains("ready") },
        diagnostic: { model.sessions.map { $0.status + ": " + $0.output }.joined(separator: "\n") })
    let first = model.sessions[0]
    let second = model.sessions[1]
    #expect(first.running && second.running && model.processRunning)
    #expect(!model.canManageBall(ball) && model.canManageBall(other))
    model.execute(["install", #"C:\OS2\A.EXE"#])
    #expect(model.sessions.count == 2)
    model.select(other)
    try await waitForSessions { !model.busy && model.snapshot != nil }
    #expect(model.hasRunningSessions && !model.processRunning && model.ballSessions.isEmpty)
    #expect(model.log == nil && model.activity == "No process has run in this session.")
    model.select(ball)
    try await waitForSessions { !model.busy && model.snapshot != nil }
    model.selectSession(first.id)
    model.stopConfirmation = true
    model.selectSession(second.id)
    model.selectSession("")
    model.activity = "Independent audit output"
    model.stopProcess()
    try await waitForSessions { !first.running }
    #expect(second.running && !second.stopRequested)
    #expect(model.selectedSessionID == nil && model.activity == "Independent audit output")
    #expect(
        first.log != nil && first.output.contains("A.EXE ready")
            && !first.output.contains("B.EXE ready"))
    model.selectSession(second.id)
    model.stopProcess()
    try await waitForSessions { !second.running }
    #expect(first.log != second.log)
    #expect(second.output.contains("B.EXE ready") && !second.output.contains("A.EXE ready"))
    #expect(!model.hasRunningSessions && !model.processRunning && !model.busy)
}
