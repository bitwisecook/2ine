import Foundation
import Darwin

func canonicalLaunchBall(_ path: String) -> String {
    if let canonical = realpath(path, nil) {
        defer { free(canonical) }
        return String(cString: canonical)
    }
    return URL(filePath: path).standardizedFileURL.resolvingSymlinksInPath().path
}

/// Explains a death the worker could not report itself; nil for a normal or requested end.
public func abnormalExitMessage(program: String, code: Int32, signalled: Bool, stopRequested: Bool,
                                log: URL?) -> String? {
    guard signalled, !stopRequested else { return nil }
    let cause = code == SIGKILL ? "The system or another program killed it, for example because memory ran out or a code signature check failed."
                                : "2ine could not record what happened."
    let location = log.map { "\n\nThe session log is at \($0.path)." } ?? ""
    return "\(program) was terminated by signal \(code). \(cause)\(location)"
}

func validLaunchSessionID(_ value: String) -> Bool {
    !value.isEmpty && value.utf8.count <= 64 && value.utf8.allSatisfy {
        (65...90).contains($0) || (97...122).contains($0) || (48...57).contains($0) || $0 == 45 || $0 == 95
    }
}

/// A view's cancellation does not abandon a child. Avoid a cancelled task's sleep spinning.
func processPollingPause(_ duration: Duration) async {
    if Task.isCancelled {
        await Task.detached { try? await Task.sleep(for: duration) }.value
    } else {
        try? await Task.sleep(for: duration)
    }
}

struct StartupServiceKey: Hashable, Sendable {
    let ball: String
    let program: String
}
struct StartupLease: Sendable {
    let sessionID: String
    let keys: [StartupServiceKey]
    let logs: [URL]
}

/// One manager process owns this registry. External daemon PIDs are never adopted or signalled.
actor LaunchCoordinator {
    static let shared = LaunchCoordinator()
    private struct Reservation {
        let ball: String
        let installer: Bool
    }
    private struct Configuration: Equatable {
        let command: [String]
        let cwd: String
        let environment: [String: String]
        let unsetEnvironment: [String]
        let owner: String?
        init(_ plan: LaunchPlan) {
            command = plan.command; cwd = plan.cwd; environment = plan.environment
            unsetEnvironment = plan.unsetEnvironment.sorted(); owner = plan.owner
        }
    }
    private final class Service {
        let process: Process
        let configuration: Configuration
        let supervised: Bool
        let log: URL
        var holders: Set<String>
        var stopping = false
        init(process: Process, plan: LaunchPlan, log: URL, sessionID: String) {
            self.process = process; configuration = Configuration(plan)
            supervised = plan.operationSupervised == true; self.log = log; holders = [sessionID]
        }
    }
    private var reservations: [String: Reservation] = [:]
    private var services: [StartupServiceKey: Service] = [:]

    func prepare(sessionID: String, ball: String, installer: Bool, message: DesktopRequest) throws -> LaunchPlan {
        guard reservations[sessionID] == nil else { throw BallError("This application session ID is already in use.") }
        let peers = reservations.values.filter { $0.ball == ball }
        guard !peers.contains(where: \.installer) && (!installer || peers.isEmpty) else {
            throw BallError("Close the applications running in this ball before installing software, and wait for its installer to finish before launching another application.")
        }
        reservations[sessionID] = Reservation(ball: ball, installer: installer)
        do {
            var request = message
            if !peers.isEmpty { request.resolve = false }
            let plan = try NativeBallCore.request(request, as: LaunchPlan.self)
            return plan
        } catch {
            reservations.removeValue(forKey: sessionID)
            throw error
        }
    }

    func finish(sessionID: String) { reservations.removeValue(forKey: sessionID) }

    func activeSessionIDs(ball: String?) -> Set<String> {
        let canonical = ball.map(canonicalLaunchBall)
        return Set(reservations.compactMap { id, value in canonical == nil || value.ball == canonical ? id : nil })
    }

    func acquireStartup(sessionID: String, ball: String, plans: [LaunchPlan], log: URL) async throws -> StartupLease {
        var keys: [StartupServiceKey] = [], logs: [URL] = []
        do {
            for (index, plan) in plans.enumerated() {
                let program = (plan.program ?? plan.programHost).replacingOccurrences(of: "/", with: "\\").uppercased()
                let key = StartupServiceKey(ball: ball, program: program)
                while services[key]?.stopping == true { await processPollingPause(.milliseconds(20)) }
                if let service = services[key] {
                    guard service.configuration == Configuration(plan) else {
                        throw BallError("Startup program settings changed while another application is using it. Close applications in this ball before using different startup settings.")
                    }
                    guard service.process.isRunning else {
                        throw BallError("A shared startup program exited while applications were using it. Close those applications before restarting it. Its log is at \(service.log.path).")
                    }
                    service.holders.insert(sessionID)
                    keys.append(key); logs.append(service.log)
                    continue
                }
                guard let executable = plan.command.first else { throw BallError("A startup program has an empty command.") }
                let service = Process()
                service.executableURL = URL(filePath: executable); service.arguments = Array(plan.command.dropFirst())
                var environment = ProcessInfo.processInfo.environment
                for name in plan.unsetEnvironment { environment.removeValue(forKey: name) }
                environment.merge(plan.environment) { _, value in value }
                environment["TWOINE_DESKTOP_AUXILIARY"] = "1"
                service.environment = environment; service.currentDirectoryURL = URL(filePath: plan.cwd)
                let serviceLog = log.deletingPathExtension().appendingPathExtension("startup-\(index).log")
                try FileManager.default.createDirectory(at: serviceLog.deletingLastPathComponent(), withIntermediateDirectories: true)
                guard FileManager.default.createFile(atPath: serviceLog.path, contents: nil) else {
                    throw BallError("Cannot create startup program log at \(serviceLog.path).")
                }
                let output = try FileHandle(forWritingTo: serviceLog)
                defer { try? output.close() }
                service.standardOutput = output; service.standardError = output; service.standardInput = FileHandle.nullDevice
                try service.run()
                services[key] = Service(process: service, plan: plan, log: serviceLog, sessionID: sessionID)
                keys.append(key); logs.append(serviceLog)
            }
            return StartupLease(sessionID: sessionID, keys: keys, logs: logs)
        } catch {
            await releaseStartup(StartupLease(sessionID: sessionID, keys: keys, logs: logs))
            throw error
        }
    }

    func releaseStartup(_ lease: StartupLease) async {
        for key in lease.keys {
            guard let service = services[key], service.holders.remove(lease.sessionID) != nil else { continue }
            guard service.holders.isEmpty else { continue }
            service.stopping = true
            if service.process.isRunning { service.process.terminate() }
            let graceful = ContinuousClock.now.advanced(by: .seconds(2))
            while service.process.isRunning && ContinuousClock.now < graceful {
                await processPollingPause(.milliseconds(20))
            }
            if service.process.isRunning {
                kill(service.process.processIdentifier, service.supervised ? SIGUSR2 : SIGKILL)
                let forced = ContinuousClock.now.advanced(by: .seconds(5))
                while service.process.isRunning && ContinuousClock.now < forced {
                    await processPollingPause(.milliseconds(20))
                }
                if service.process.isRunning { kill(service.process.processIdentifier, SIGKILL) }
            }
            while service.process.isRunning { await processPollingPause(.milliseconds(20)) }
            // Foundation owns wait/reap; never signal a PID found by name or supplied by a record.
            if services[key] === service { services.removeValue(forKey: key) }
        }
    }
}
