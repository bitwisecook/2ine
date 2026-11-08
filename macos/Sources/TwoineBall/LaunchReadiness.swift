import BallCore
import Foundation

struct LaunchReadiness: Decodable, Sendable {
    let ready: Bool
    let reason: String
    let requiredCategories: [String]
}

private struct BallNavigationState: Decodable, Sendable {
    let page: String
}

struct PendingApplicationLaunch: Identifiable {
    let id = UUID()
    let ball: String
    let sessionID: String
    let arguments: [String]
    let directories: [String: String]?
    let environment: [String: String]?
    let mediaDrive: String?
    let needsComponents: Bool
    var reason: String
    var ready = false
    var autoContinue = true
    var title: String { arguments.dropFirst().first ?? "Application" }
}

extension AppModel {
    var pendingLaunch: PendingApplicationLaunch? { selected.flatMap { pendingLaunches[$0] } }
    func launchReadiness(
        ball: String, arguments: [String], directories: [String: String]? = nil,
        environment: [String: String]? = nil
    ) async throws -> LaunchReadiness {
        var request = DesktopRequest("launch-prerequisites", ball: ball)
        request.arguments = arguments
        request.workingDirectories = directories
        request.environment = environment
        let payload = request
        return try await Task.detached {
            try NativeBallCore.request(payload, as: LaunchReadiness.self)
        }.value
    }
    func selectStartingPage(ball: String) async {
        let currentPage = page
        do {
            let state = try await Task.detached {
                try NativeBallCore.request(
                    DesktopRequest("ball-navigation-state", ball: ball),
                    as: BallNavigationState.self)
            }.value
            guard selected == ball, page == currentPage else { return }
            page =
                [
                    "applications": Page.applications, "application-installation": .installation,
                    "system-components": .systemComponents,
                ][state.page] ?? .drives
        } catch {
            if selected == ball {
                componentNotice =
                    "Could not check this ball’s starting page: " + error.localizedDescription
            }
        }
    }
    func cancelPendingLaunch(ball: String) {
        if let pending = pendingLaunches.removeValue(forKey: ball),
            let session = sessions.first(where: { $0.id == pending.sessionID })
        {
            session.status = "Launch Cancelled"
        }
    }
    func continuePendingLaunch(ball: String) {
        guard selected == ball, !busy, !mediaBusy, !componentOperationRunning, !processRunning,
            let pending = pendingLaunches.removeValue(forKey: ball)
        else { return }
        sessions.removeAll { $0.id == pending.sessionID && !$0.running }
        execute(
            pending.arguments, directories: pending.directories, environment: pending.environment,
            mediaDrive: pending.mediaDrive)
    }
    func componentOperationDidFinish(ball: String) async {
        guard let pending = pendingLaunches[ball] else { return }
        do {
            let status = try await launchReadiness(
                ball: ball, arguments: pending.arguments,
                directories: pending.directories, environment: pending.environment)
            guard pendingLaunches[ball]?.id == pending.id else { return }
            pendingLaunches[ball]?.ready = status.ready
            pendingLaunches[ball]?.reason = status.reason
            guard status.ready, pendingLaunches[ball]?.autoContinue == true, selected == ball else {
                return
            }
            for _ in 0..<50 {
                if !busy && !mediaBusy && !componentOperationRunning { break }
                try await Task.sleep(for: .milliseconds(100))
                guard selected == ball, pendingLaunches[ball]?.id == pending.id,
                    pendingLaunches[ball]?.autoContinue == true
                else { return }
            }
            guard pendingLaunches[ball]?.id == pending.id,
                pendingLaunches[ball]?.autoContinue == true
            else { return }
            continuePendingLaunch(ball: ball)
        } catch {
            if pendingLaunches[ball]?.id == pending.id {
                pendingLaunches[ball]?.reason = error.localizedDescription
            }
        }
    }
}
