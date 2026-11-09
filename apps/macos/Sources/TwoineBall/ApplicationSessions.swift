import BallCore
import Foundation
import Observation

@MainActor @Observable final class ApplicationSession: Identifiable {
    let id = UUID().uuidString
    let ball: String
    let title: String
    let installer: Bool
    let started = Date()
    var label: String { title + " · " + started.formatted(date: .omitted, time: .standard) }
    var running = true
    var preparing = true
    var stopRequested = false
    var output = "Starting…"
    var status = "Checking installed components…"
    var log: URL?
    var exitCode: Int32?
    init(ball: String, arguments: [String]) {
        self.ball = ball
        title = arguments.dropFirst().first ?? "Application"
        installer = arguments.first == "install"
    }
}

extension AppModel {
    var hasRunningSessions: Bool { sessions.contains { $0.running } }
    var ballSessions: [ApplicationSession] { sessions.filter { $0.ball == selected } }
    var launchPreparing: Bool { ballSessions.contains { $0.running && $0.preparing } }
    var currentSession: ApplicationSession? { sessions.first { $0.id == selectedSessionID } }
    var canStopCurrentSession: Bool { currentSession.map { $0.running && !$0.preparing } ?? false }
    func hasRunningSession(in ball: String) -> Bool {
        sessions.contains { $0.ball == ball && $0.running }
    }
    func selectSession(_ id: String) {
        if id.isEmpty {
            selectedSessionID = nil
            return
        }
        guard let session = sessions.first(where: { $0.id == id && $0.ball == selected }) else {
            return
        }
        selectedSessionID = id
        presentSession(session)
    }
    func syncSessionPresentation(selectDefault: Bool = false) {
        processRunning = selected.map { hasRunningSession(in: $0) } ?? false
        if let currentSession, currentSession.ball == selected {
            presentSession(currentSession)
        } else if selectDefault,
            let latest = ballSessions.last(where: \.running) ?? ballSessions.last
        {
            selectedSessionID = latest.id
            presentSession(latest)
        } else if selectDefault {
            selectedSessionID = nil
            activity = "No process has run in this session."
            operation = "Ready"
            log = nil
            exitCode = nil
            stopRequested = false
        }
    }
    func presentSession(_ session: ApplicationSession) {
        guard session.ball == selected, selectedSessionID == session.id else { return }
        stopRequested = session.stopRequested
    }
}
