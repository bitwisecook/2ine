import Foundation
#if canImport(BallEngine)
import BallEngine
#endif

public struct ProcessResult: Sendable {
    public let code: Int32
    public let signalled: Bool
    public let output: String
    public let log: URL
    public var receiptError: String? = nil
}

/// Native process presentation. Shared launch and persistence rules live in C.
public actor ProcessRunner {
    private var process: Process?
    public init() {}

    public func run(executable: URL, arguments: [String],
                    environment: [String: String]? = nil, cwd: URL? = nil,
                    log: URL, update: @Sendable (String) async -> Void = { _ in }) async throws -> ProcessResult {
        guard process == nil else { throw BallError("Wait for the current operation to finish.") }
        let files = FileManager.default
        try files.createDirectory(at: log.deletingLastPathComponent(), withIntermediateDirectories: true)
        guard files.createFile(atPath: log.path, contents: nil) else { throw BallError("Cannot create process log at \(log.path).") }
        let output = try FileHandle(forWritingTo: log)
        let reader = try FileHandle(forReadingFrom: log)
        defer { process = nil; try? output.close(); try? reader.close() }
        let child = Process()
        child.executableURL = executable; child.arguments = arguments
        child.environment = environment ?? ProcessInfo.processInfo.environment
        child.currentDirectoryURL = cwd
        child.standardOutput = output; child.standardError = output
        child.standardInput = FileHandle.nullDevice
        try child.run()
        process = child
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
}

/// All synchronous C filesystem/hash work runs on this actor, off MainActor.
/// C results are copied into Sendable Swift values before freeing C storage.
public actor BallBackend {
    private let runner = ProcessRunner()
    public init() {}

    public func request<T: Decodable & Sendable>(_ message: DesktopRequest, as type: T.Type) throws -> T {
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

    public func execute(ball: String, build: String, arguments: [String],
                        update: @Sendable (String) async -> Void) async throws -> ProcessResult {
        let plan = try request(DesktopRequest("prepare-launch", ball: ball, build: build, arguments: arguments), as: LaunchPlan.self)
        guard let executable = plan.command.first else { throw BallError("The runtime launch command is empty.") }
        var environment = ProcessInfo.processInfo.environment
        for key in plan.unsetEnvironment { environment.removeValue(forKey: key) }
        environment.merge(plan.environment) { _, value in value }
        let log = URL(filePath: ball).appending(path: "logs/macos-\(UUID().uuidString).log")
        var result = try await runner.run(executable: URL(filePath: executable), arguments: Array(plan.command.dropFirst()), environment: environment, cwd: URL(filePath: plan.cwd), log: log, update: update)
        if plan.installer {
            do {
                _ = try request(DesktopRequest("record-install", ball: ball, receipt: plan.receipt, programHost: plan.programHost, exitCode: result.signalled ? -result.code : result.code), as: BallSnapshot.self)
            } catch { result.receiptError = error.localizedDescription }
        }
        return result
    }
}
