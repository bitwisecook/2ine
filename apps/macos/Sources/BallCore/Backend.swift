import Darwin
import Foundation

#if canImport(BallEngine)
import BallEngine
#endif

public struct ProcessResult: Sendable {
    public let code: Int32
    public let signalled: Bool
    public let output: String
    public let log: URL
    public var sessionID: String? = nil
    public var startupLogs: [URL] = []
    public var receiptError: String? = nil
    public var dependencyAudit: DependencyAudit? = nil
}

/// Native process presentation. Shared launch and persistence rules live in C.
public actor ProcessRunner {
    private var process: Process?
    private var operationSupervised = false
    private var pendingStop = false
    public init() {}

    func stopLaunch(force: Bool = false) {
        pendingStop = true
        stop(force: force)
    }

    public func stop(force: Bool = false) {
        guard let process, process.isRunning else { return }
        if force {
            kill(process.processIdentifier, operationSupervised ? SIGUSR2 : SIGKILL)
        } else {
            process.terminate()
        }
    }

    public func run(
        executable: URL, arguments: [String],
        environment: [String: String]? = nil, cwd: URL? = nil,
        log: URL, operationSupervised: Bool = false,
        update: @Sendable (String) async -> Void = { _ in }
    ) async throws -> ProcessResult {
        guard process == nil else { throw BallError("Wait for the current operation to finish.") }
        guard !pendingStop else {
            pendingStop = false
            throw CancellationError()
        }
        let files = FileManager.default
        try files.createDirectory(
            at: log.deletingLastPathComponent(), withIntermediateDirectories: true)
        guard files.createFile(atPath: log.path, contents: nil) else {
            throw BallError("Cannot create process log at \(log.path).")
        }
        let output = try FileHandle(forWritingTo: log)
        let reader = try FileHandle(forReadingFrom: log)
        defer {
            process = nil
            self.operationSupervised = false
            pendingStop = false
            try? output.close()
            try? reader.close()
        }
        let child = Process()
        child.executableURL = executable
        child.arguments = arguments
        child.environment = environment ?? ProcessInfo.processInfo.environment
        child.currentDirectoryURL = cwd
        child.standardOutput = output
        child.standardError = output
        child.standardInput = FileHandle.nullDevice
        try child.run()
        process = child
        self.operationSupervised = operationSupervised
        var lastSize: UInt64 = .max
        var tail = ""
        while true {
            let ended = !child.isRunning
            let size = try reader.seekToEnd()
            if size != lastSize || ended {
                try reader.seek(
                    toOffset: size > SessionLimits.logTailBytes
                        ? size - SessionLimits.logTailBytes : 0)
                tail = String(
                    decoding: try reader.read(upToCount: Int(SessionLimits.logTailBytes)) ?? Data(),
                    as: UTF8.self)
                await update(tail)
                lastSize = size
            }
            if ended {
                return ProcessResult(
                    code: child.terminationStatus,
                    signalled: child.terminationReason == .uncaughtSignal, output: tail, log: log)
            }
            // The manager owns this operation until actual exit; view/task
            // cancellation never abandons a still-running guest process.
            await processPollingPause(SessionLimits.logPollInterval)
        }
    }
}

private enum SessionLimits {
    /// The activity view shows only the tail of a log; the ball keeps all of it.
    static let logTailBytes: UInt64 = 65536
    static let logPollInterval: Duration = .milliseconds(100)
    /// Finished and early-stopped session IDs remembered so a late Stop is ignored.
    static let rememberedSessions = 256
}

struct LaunchPlan: Decodable, Sendable {
    let command: [String]
    let cwd: String
    let environment: [String: String]
    let unsetEnvironment: [String]
    let programHost: String
    let receipt: JSONValue
    let installer: Bool
    let operationSupervised: Bool?
    let dependencyAudit: DependencyAudit?
    let startup: [LaunchPlan]?
    let program: String?
    let owner: String?
    let desktopIdentity: DesktopIdentity?
}

/// All synchronous C filesystem/hash work runs on this actor, off MainActor.
/// C results are copied into Sendable Swift values before freeing C storage.
public actor BallBackend {
    private struct Session {
        let runner: ProcessRunner
        let ball: String
    }
    private var sessions: [String: Session] = [:]
    private var pendingStops: Set<String> = []
    private var completedSessions: Set<String> = []
    public init() {}

    private func retire(_ id: String) {
        sessions.removeValue(forKey: id)
        if completedSessions.count >= SessionLimits.rememberedSessions,
            let oldest = completedSessions.first
        {
            completedSessions.remove(oldest)
        }
        completedSessions.insert(id)
    }

    /// Compatibility for callers with one launch. Ambiguous Stop never affects other sessions.
    public func stop(force: Bool = false) async {
        guard sessions.count == 1, let id = sessions.keys.first else { return }
        await stop(sessionID: id, force: force)
    }

    public func stop(sessionID: String, force: Bool = false) async {
        guard let session = sessions[sessionID] else {
            if validLaunchSessionID(sessionID), !completedSessions.contains(sessionID) {
                if pendingStops.count >= SessionLimits.rememberedSessions,
                    let oldest = pendingStops.first
                {
                    pendingStops.remove(oldest)
                }
                pendingStops.insert(sessionID)
            }
            return
        }
        await session.runner.stopLaunch(force: force)
    }

    public func activeSessionIDs(ball: String? = nil) async -> Set<String> {
        await LaunchCoordinator.shared.activeSessionIDs(ball: ball)
    }

    public func request<T: Decodable & Sendable>(
        _ message: DesktopRequest, as type: T.Type
    ) throws -> T {
        try NativeBallCore.request(message, as: type)
    }

    public func execute(
        ball: String, build: String, arguments: [String],
        workingDirectories: [String: String]? = nil, environment overrides: [String: String]? = nil,
        sessionID: String = UUID().uuidString,
        update: @Sendable (String) async -> Void
    ) async throws -> ProcessResult {
        guard validLaunchSessionID(sessionID), sessions[sessionID] == nil,
            !completedSessions.contains(sessionID)
        else {
            throw BallError(
                "Choose a unique application session ID containing letters, digits, hyphens or underscores."
            )
        }
        if pendingStops.remove(sessionID) != nil {
            retire(sessionID)
            throw CancellationError()
        }
        let canonicalBall = canonicalLaunchBall(ball)
        let runner = ProcessRunner()
        sessions[sessionID] = Session(runner: runner, ball: canonicalBall)
        let coordinator = LaunchCoordinator.shared
        var reserved = false
        var lease: StartupLease?
        do {
            var message = DesktopRequest(
                "prepare-launch", ball: canonicalBall, build: build, arguments: arguments)
            message.workingDirectories = workingDirectories
            message.environment = overrides
            let plan = try await coordinator.prepare(
                sessionID: sessionID, ball: canonicalBall,
                installer: arguments.first == "install", message: message)
            reserved = true
            guard let executable = plan.command.first else {
                throw BallError("The runtime launch command is empty.")
            }
            let log = URL(filePath: canonicalBall).appending(path: "logs/macos-\(sessionID).log")
            lease = try await coordinator.acquireStartup(
                sessionID: sessionID, ball: canonicalBall,
                plans: plan.startup ?? [], log: log)
            var environment = ProcessInfo.processInfo.environment
            for key in plan.unsetEnvironment { environment.removeValue(forKey: key) }
            environment.merge(plan.environment) { _, value in value }
            if environment["TERM"] == nil { environment["TERM"] = "xterm-256color" }
            let desktop = try await DesktopLaunchBundle.create(
                ball: canonicalBall, sessionID: sessionID,
                loader: URL(filePath: executable), identity: plan.desktopIdentity,
                fallbackIcon: Bundle.main.url(forResource: "Twoine", withExtension: "icns"))
            environment["TWOINE_DESKTOP_SESSION"] = sessionID
            environment["TWOINE_DESKTOP_NAME"] = desktop.name
            if let icon = desktop.icon { environment["TWOINE_DESKTOP_ICON"] = icon.path }
            var result = try await runner.run(
                executable: desktop.executable,
                arguments: Array(plan.command.dropFirst()), environment: environment,
                cwd: URL(filePath: plan.cwd), log: log,
                operationSupervised: plan.operationSupervised == true, update: update)
            result.sessionID = sessionID
            result.startupLogs = lease?.logs ?? []
            result.dependencyAudit = plan.dependencyAudit
            if plan.installer {
                do {
                    let installed = try request(
                        DesktopRequest(
                            "record-install", ball: canonicalBall, build: build,
                            receipt: plan.receipt, programHost: plan.programHost,
                            exitCode: result.signalled ? -result.code : result.code),
                        as: BallSnapshot.self)
                    result.dependencyAudit = installed.dependencyAudit
                } catch { result.receiptError = error.localizedDescription }
            }
            if let lease { await coordinator.releaseStartup(lease) }
            await coordinator.finish(sessionID: sessionID)
            retire(sessionID)
            return result
        } catch {
            if let lease { await coordinator.releaseStartup(lease) }
            if reserved { await coordinator.finish(sessionID: sessionID) }
            retire(sessionID)
            throw error
        }
    }
}

public enum NativeBallCore {
    public static func request<T: Decodable & Sendable>(
        _ message: DesktopRequest, as type: T.Type
    ) throws -> T {
        let text = String(decoding: try JSONEncoder().encode(message), as: UTF8.self)
        var result: UnsafeMutablePointer<CChar>?
        var diagnostic: UnsafeMutablePointer<CChar>?
        let status = text.withCString { tb_request($0, &result, &diagnostic) }
        defer {
            if let result { tb_free(result) }
            if let diagnostic { tb_free(diagnostic) }
        }
        guard status == 0, let result else {
            throw BallError(diagnostic.map { String(cString: $0) } ?? "Ball operation failed.")
        }
        let data = Data(String(cString: result).utf8)
        return try JSONDecoder().decode(type, from: data)
    }

}
