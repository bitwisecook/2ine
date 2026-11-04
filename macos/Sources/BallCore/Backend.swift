import Foundation
import Darwin
#if canImport(BallEngine)
import BallEngine
#endif

public struct ProcessResult: Sendable {
    public let code: Int32
    public let signalled: Bool
    public let output: String
    public let log: URL
    public var receiptError: String? = nil
    public var dependencyAudit: DependencyAudit? = nil
}

/// Native process presentation. Shared launch and persistence rules live in C.
public actor ProcessRunner {
    private var process: Process?
    private var operationSupervised = false
    public init() {}

    public func stop(force: Bool = false) {
        guard let process, process.isRunning else { return }
        if force { kill(process.processIdentifier, operationSupervised ? SIGUSR2 : SIGKILL) }
        else { process.terminate() }
    }

    public func run(executable: URL, arguments: [String],
                    environment: [String: String]? = nil, cwd: URL? = nil,
                    log: URL, operationSupervised: Bool = false,
                    update: @Sendable (String) async -> Void = { _ in }) async throws -> ProcessResult {
        guard process == nil else { throw BallError("Wait for the current operation to finish.") }
        let files = FileManager.default
        try files.createDirectory(at: log.deletingLastPathComponent(), withIntermediateDirectories: true)
        guard files.createFile(atPath: log.path, contents: nil) else { throw BallError("Cannot create process log at \(log.path).") }
        let output = try FileHandle(forWritingTo: log)
        let reader = try FileHandle(forReadingFrom: log)
        defer { process = nil; self.operationSupervised = false; try? output.close(); try? reader.close() }
        let child = Process()
        child.executableURL = executable; child.arguments = arguments
        child.environment = environment ?? ProcessInfo.processInfo.environment
        child.currentDirectoryURL = cwd
        child.standardOutput = output; child.standardError = output
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
                try reader.seek(toOffset: size > 65536 ? size - 65536 : 0)
                tail = String(decoding: try reader.read(upToCount: 65536) ?? Data(), as: UTF8.self)
                await update(tail); lastSize = size
            }
            if ended {
                return ProcessResult(code: child.terminationStatus, signalled: child.terminationReason == .uncaughtSignal, output: tail, log: log)
            }
            // The manager owns this operation until actual exit; view/task
            // cancellation never abandons a still-running guest process.
            try? await Task.sleep(for: .milliseconds(100))
        }
    }
}

private struct LaunchPlan: Decodable, Sendable {
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
}

/// All synchronous C filesystem/hash work runs on this actor, off MainActor.
/// C results are copied into Sendable Swift values before freeing C storage.
public actor BallBackend {
    private let runner = ProcessRunner()
    private var executing = false
    public init() {}

    public func stop(force: Bool = false) async { await runner.stop(force: force) }

    public func request<T: Decodable & Sendable>(_ message: DesktopRequest, as type: T.Type) throws -> T {
        try NativeBallCore.request(message, as: type)
    }

    public func execute(ball: String, build: String, arguments: [String],
                        workingDirectories: [String: String]? = nil, environment overrides: [String: String]? = nil,
                        update: @Sendable (String) async -> Void) async throws -> ProcessResult {
        guard !executing else { throw BallError("Wait for the current operation to finish.") }
        executing = true
        defer { executing = false }
        var message = DesktopRequest("prepare-launch", ball: ball, build: build, arguments: arguments)
        message.workingDirectories = workingDirectories; message.environment = overrides
        let plan = try request(message, as: LaunchPlan.self)
        guard let executable = plan.command.first else { throw BallError("The runtime launch command is empty.") }
        var environment = ProcessInfo.processInfo.environment
        for key in plan.unsetEnvironment { environment.removeValue(forKey: key) }
        environment.merge(plan.environment) { _, value in value }
        if environment["TERM"] == nil { environment["TERM"] = "xterm-256color" }
        let log = URL(filePath: ball).appending(path: "logs/macos-\(UUID().uuidString).log")
        var services: [Process] = []
        defer {
            // These services belong to this launch, including on cancellation
            // or a failed main executable. Never leave detached helpers behind.
            for service in services where service.isRunning {
                kill(service.processIdentifier, SIGKILL)
                service.waitUntilExit()
            }
        }
        for (index, startup) in (plan.startup ?? []).enumerated() {
            guard let executable = startup.command.first else { throw BallError("A startup program has an empty command.") }
            let service = Process()
            service.executableURL = URL(filePath: executable)
            service.arguments = Array(startup.command.dropFirst())
            var serviceEnvironment = ProcessInfo.processInfo.environment
            for key in startup.unsetEnvironment { serviceEnvironment.removeValue(forKey: key) }
            serviceEnvironment.merge(startup.environment) { _, value in value }
            service.environment = serviceEnvironment
            service.currentDirectoryURL = URL(filePath: startup.cwd)
            let serviceLog = log.deletingPathExtension().appendingPathExtension("startup-\(index).log")
            try FileManager.default.createDirectory(at: serviceLog.deletingLastPathComponent(), withIntermediateDirectories: true)
            guard FileManager.default.createFile(atPath: serviceLog.path, contents: nil) else { throw BallError("Cannot create startup program log.") }
            let output = try FileHandle(forWritingTo: serviceLog)
            service.standardOutput = output; service.standardError = output
            service.standardInput = FileHandle.nullDevice
            defer { try? output.close() }
            try service.run()
            services.append(service)
        }
        var result = try await runner.run(executable: URL(filePath: executable), arguments: Array(plan.command.dropFirst()), environment: environment, cwd: URL(filePath: plan.cwd), log: log, operationSupervised: plan.operationSupervised == true, update: update)
        result.dependencyAudit = plan.dependencyAudit
        if plan.installer {
            do {
                let installed = try request(DesktopRequest("record-install", ball: ball, build: build, receipt: plan.receipt, programHost: plan.programHost, exitCode: result.signalled ? -result.code : result.code), as: BallSnapshot.self)
                result.dependencyAudit = installed.dependencyAudit
            } catch { result.receiptError = error.localizedDescription }
        }
        return result
    }
}

public enum NativeBallCore {
    public static func request<T: Decodable & Sendable>(_ message: DesktopRequest, as type: T.Type) throws -> T {
        let text = String(decoding: try JSONEncoder().encode(message), as: UTF8.self)
        var result: UnsafeMutablePointer<CChar>?
        var diagnostic: UnsafeMutablePointer<CChar>?
        let status = text.withCString { tb_request($0, &result, &diagnostic) }
        defer { if let result { tb_free(result) }; if let diagnostic { tb_free(diagnostic) } }
        guard status == 0, let result else {
            throw BallError(diagnostic.map { String(cString: $0) } ?? "Ball operation failed.")
        }
        let data = Data(String(cString: result).utf8)
        return try JSONDecoder().decode(type, from: data)
    }

}
