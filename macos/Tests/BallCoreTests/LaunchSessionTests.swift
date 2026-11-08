import Darwin
import Foundation
import Testing

@testable import BallCore

private struct LaunchFixture {
    let root: URL
    let ball: URL
    let build: URL
    init(startup: Bool = true) throws {
        root = FileManager.default.temporaryDirectory.appending(
            path: "2ine-sessions-\(UUID().uuidString)")
        ball = root.appending(path: "Shared.2ineball")
        build = root.appending(path: "runtime")
        try FileManager.default.createDirectory(at: build, withIntermediateDirectories: true)
        _ = try NativeBallCore.request(
            DesktopRequest("command", ball: ball.path, arguments: ["init"]), as: BallSnapshot.self)
        if startup {
            let manifest = ball.appending(path: "ball.json")
            var value = try #require(
                JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as? [String: Any])
            value["startupPrograms"] = [
                [
                    "owner": "os2-som", "program": #"C:\OS2\SERVICE.EXE"#, "cwd": #"C:\OS2"#,
                    "arguments": [],
                ]
            ]
            try JSONSerialization.data(withJSONObject: value).write(to: manifest)
        }
        for name in ["ONE.EXE", "TWO.EXE", "SERVICE.EXE", "SETUP.EXE"] {
            try Data("fixture".utf8).write(to: ball.appending(path: "drive_c/OS2/\(name)"))
        }
        // A runtime fixture emits its real inherited namespace and holds until explicitly stopped.
        let script = #"""
            #!/bin/sh
            case "$1" in
              *SERVICE.EXE) echo $$ >> "$TWOINE_BALL/service-pids"; echo "${TWOINE_DESKTOP_AUXILIARY}:${TWOINE_DESKTOP_SESSION}" > "$TWOINE_BALL/service-desktop"; echo service-ready; exec /bin/sleep 30 ;;
              *ONE.EXE) name=one ;;
              *TWO.EXE) name=two ;;
              *SETUP.EXE) name=installer ;;
              *) exit 88 ;;
            esac
            echo "${name}:$TWOINE_BALL:$TWOINE_IPC_DIR"
            echo "${TWOINE_DESKTOP_SESSION}:${TWOINE_DESKTOP_AUXILIARY}" > "$TWOINE_BALL/${name}-desktop"
            echo $$ > "$TWOINE_BALL/${name}-pid"
            exec /bin/sleep 30
            """#
        let loader = build.appending(path: "lx_loader")
        try Data(script.utf8).write(to: loader)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: loader.path)
    }
    func cleanup() { try? FileManager.default.removeItem(at: root) }
    func pid(_ name: String) throws -> Int32 {
        let text = try String(contentsOf: ball.appending(path: name), encoding: .utf8)
        return try #require(Int32(text.trimmingCharacters(in: .whitespacesAndNewlines)))
    }
}

private func awaitCondition(_ condition: @Sendable () async throws -> Bool) async throws {
    let deadline = ContinuousClock.now.advanced(by: .seconds(5))
    while try await !condition() {
        guard ContinuousClock.now < deadline else {
            throw BallError("The controlled fixture did not reach its checkpoint.")
        }
        try await Task.sleep(for: .milliseconds(10))
    }
}

@Test func concurrentAppsLeaseOneStartupAndStopIndependently() async throws {
    let fixture = try LaunchFixture()
    defer { fixture.cleanup() }
    let backend = BallBackend()
    let otherBackend = BallBackend()
    let one = UUID().uuidString
    let two = UUID().uuidString
    let unrelated = Process()
    unrelated.executableURL = URL(filePath: "/bin/sleep")
    unrelated.arguments = ["30"]
    try unrelated.run()
    defer {
        if unrelated.isRunning {
            unrelated.terminate()
            unrelated.waitUntilExit()
        }
    }
    let first = Task {
        try await backend.execute(
            ball: fixture.ball.path, build: fixture.build.path,
            arguments: ["run", #"C:\OS2\ONE.EXE"#], sessionID: one
        ) { _ in }
    }
    var second: Task<ProcessResult, any Error>?
    do {
        try await awaitCondition {
            FileManager.default.fileExists(atPath: fixture.ball.appending(path: "one-pid").path)
        }
        second = Task {
            try await otherBackend.execute(
                ball: fixture.ball.path, build: fixture.build.path,
                arguments: ["run", #"C:\OS2\TWO.EXE"#], sessionID: two
            ) { _ in }
        }
        try await awaitCondition {
            FileManager.default.fileExists(atPath: fixture.ball.appending(path: "two-pid").path)
                && FileManager.default.fileExists(
                    atPath: fixture.ball.appending(path: "service-pids").path)
        }
        let servicePID = try fixture.pid("service-pids")
        let secondPID = try fixture.pid("two-pid")
        #expect(await backend.activeSessionIDs(ball: fixture.ball.path) == [one, two])
        await backend.stop(sessionID: one)
        let firstResult = try await first.value
        #expect(firstResult.sessionID == one && firstResult.signalled)
        let canonical = try #require(realpath(fixture.ball.path, nil))
        let canonicalBall = String(cString: canonical)
        free(canonical)
        #expect(firstResult.output.contains("one:" + canonicalBall + ":" + canonicalBall + "/ipc"))
        #expect(kill(servicePID, 0) == 0 && kill(secondPID, 0) == 0)
        #expect(
            try String(contentsOf: fixture.ball.appending(path: "service-desktop"), encoding: .utf8)
                == "1:\n")
        #expect(
            try String(contentsOf: fixture.ball.appending(path: "two-desktop"), encoding: .utf8)
                == two + ":\n")
        #expect(unrelated.isRunning)
        await otherBackend.stop(sessionID: two)
        let secondResult = try await #require(second).value
        #expect(secondResult.sessionID == two && secondResult.signalled)
        #expect(firstResult.log != secondResult.log)
        #expect(
            firstResult.startupLogs == secondResult.startupLogs
                && firstResult.startupLogs.count == 1)
        #expect(kill(servicePID, 0) == -1 && errno == ESRCH)
        #expect(unrelated.isRunning)
        #expect(await backend.activeSessionIDs(ball: fixture.ball.path).isEmpty)
    } catch {
        await backend.stop(sessionID: one, force: true)
        await otherBackend.stop(sessionID: two, force: true)
        _ = try? await first.value
        if let second { _ = try? await second.value }
        throw error
    }
}

@Test(arguments: [false, true])
func installerIsExclusiveButAnotherBallCanRun(installFirst: Bool) async throws {
    let fixture = try LaunchFixture(startup: false)
    let other = try LaunchFixture(startup: false)
    defer {
        fixture.cleanup()
        other.cleanup()
    }
    let backend = BallBackend()
    let otherBackend = BallBackend()
    let runID = UUID().uuidString
    let otherID = UUID().uuidString
    try Data("fixture".utf8).write(to: fixture.ball.appending(path: "drive_c/OS2/CMD.EXE"))
    let initialArguments =
        installFirst ? ["install", #"C:\OS2\SETUP.EXE"#] : ["run", #"C:\OS2\ONE.EXE"#]
    let rejectedArguments =
        installFirst ? ["run", #"C:\OS2\ONE.EXE"#] : ["install", #"C:\OS2\SETUP.EXE"#]
    let initialMarker = installFirst ? "installer-pid" : "one-pid"
    let running = Task {
        try await backend.execute(
            ball: fixture.ball.path, build: fixture.build.path, arguments: initialArguments,
            sessionID: runID
        ) { _ in }
    }
    var otherTask: Task<ProcessResult, any Error>?
    do {
        try await awaitCondition {
            FileManager.default.fileExists(atPath: fixture.ball.appending(path: initialMarker).path)
        }
        await #expect(throws: BallError.self) {
            try await otherBackend.execute(
                ball: fixture.ball.path, build: fixture.build.path, arguments: rejectedArguments,
                update: { _ in })
        }
        otherTask = Task {
            try await backend.execute(
                ball: other.ball.path, build: other.build.path,
                arguments: ["run", #"C:\OS2\ONE.EXE"#], sessionID: otherID
            ) { _ in }
        }
        try await awaitCondition {
            FileManager.default.fileExists(atPath: other.ball.appending(path: "one-pid").path)
        }
        #expect(await backend.activeSessionIDs(ball: fixture.ball.path) == [runID])
        #expect(await backend.activeSessionIDs(ball: other.ball.path) == [otherID])
        await backend.stop(sessionID: runID)
        await backend.stop(sessionID: otherID)
        _ = try await running.value
        _ = try await #require(otherTask).value
    } catch {
        await backend.stop(sessionID: runID, force: true)
        await backend.stop(sessionID: otherID, force: true)
        _ = try? await running.value
        if let otherTask { _ = try? await otherTask.value }
        throw error
    }
}

@Test func changedSharedStartupConfigurationDoesNotLaunchADuplicate() async throws {
    let fixture = try LaunchFixture()
    defer { fixture.cleanup() }
    let backend = BallBackend()
    let firstID = UUID().uuidString
    let running = Task {
        try await backend.execute(
            ball: fixture.ball.path, build: fixture.build.path,
            arguments: ["run", #"C:\OS2\ONE.EXE"#], sessionID: firstID
        ) { _ in }
    }
    do {
        try await awaitCondition {
            FileManager.default.fileExists(atPath: fixture.ball.appending(path: "one-pid").path)
                && FileManager.default.fileExists(
                    atPath: fixture.ball.appending(path: "service-pids").path)
        }
        let manifest = fixture.ball.appending(path: "ball.json")
        var value = try #require(
            JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as? [String: Any])
        value["startupPrograms"] = [
            [
                "owner": "os2-som", "program": #"C:\OS2\SERVICE.EXE"#, "cwd": #"C:\OS2"#,
                "arguments": ["changed"],
            ]
        ]
        try JSONSerialization.data(withJSONObject: value).write(to: manifest)
        await #expect(throws: BallError.self) {
            try await backend.execute(
                ball: fixture.ball.path, build: fixture.build.path,
                arguments: ["run", #"C:\OS2\TWO.EXE"#], update: { _ in })
        }
        let pids = try String(
            contentsOf: fixture.ball.appending(path: "service-pids"), encoding: .utf8)
        #expect(pids.split(separator: "\n").count == 1)
        #expect(
            !FileManager.default.fileExists(atPath: fixture.ball.appending(path: "two-pid").path))
        await backend.stop(sessionID: firstID)
        _ = try await running.value
    } catch {
        await backend.stop(sessionID: firstID, force: true)
        _ = try? await running.value
        throw error
    }
}

@Test func stopBeforeWorkerStartsIsNotLost() async throws {
    let fixture = try LaunchFixture()
    defer { fixture.cleanup() }
    let backend = BallBackend()
    let id = UUID().uuidString
    await backend.stop(sessionID: id)
    await #expect(throws: CancellationError.self) {
        try await backend.execute(
            ball: fixture.ball.path, build: fixture.build.path,
            arguments: ["run", #"C:\OS2\ONE.EXE"#], sessionID: id, update: { _ in })
    }
    #expect(!FileManager.default.fileExists(atPath: fixture.ball.appending(path: "one-pid").path))
    #expect(
        !FileManager.default.fileExists(atPath: fixture.ball.appending(path: "service-pids").path))
    #expect(await backend.activeSessionIDs(ball: fixture.ball.path).isEmpty)
}

@Test func cancellingObserverTaskDoesNotAbandonItsOwnedProcess() async throws {
    let fixture = try LaunchFixture(startup: false)
    defer { fixture.cleanup() }
    let runner = ProcessRunner()
    let log = fixture.root.appending(path: "cancelled-observer.log")
    let running = Task {
        try await runner.run(
            executable: URL(filePath: "/bin/sh"),
            arguments: ["-c", "echo $$; exec /bin/sleep 30"], log: log)
    }
    do {
        try await awaitCondition {
            (try? String(contentsOf: log, encoding: .utf8).isEmpty) == false
        }
        let pid = try #require(
            Int32(
                String(contentsOf: log, encoding: .utf8).trimmingCharacters(
                    in: .whitespacesAndNewlines)))
        running.cancel()
        #expect(kill(pid, 0) == 0)
        await runner.stop(force: true)
        let result = try await running.value
        #expect(result.signalled && result.code == SIGKILL)
        #expect(kill(pid, 0) == -1 && errno == ESRCH)
    } catch {
        await runner.stop(force: true)
        _ = try? await running.value
        throw error
    }
}

// Each gate still runs multiple guests concurrently; serialize unrelated cold runtime copies.
@Suite(.serialized) struct NativeDesktopRuntimeTests {
    @Test(
        .enabled(
            if: ProcessInfo.processInfo.environment["TWOINE_TEST_RUNTIME"] != nil,
            "Requires an explicitly selected built portable runtime."))
    func independentLXAppsExchangeSharedMemoryInOneBall() async throws {
        let runtime = try #require(ProcessInfo.processInfo.environment["TWOINE_TEST_RUNTIME"])
        let fixture = try LaunchFixture(startup: false)
        defer { fixture.cleanup() }
        let repository = URL(filePath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let generator = try await ProcessRunner().run(
            executable: URL(filePath: "/usr/bin/python3"),
            arguments: [
                repository.appending(path: "tests/integration/session_guests.py").path,
                fixture.ball.appending(path: "drive_c/OS2").path,
            ],
            log: fixture.root.appending(path: "generate.log"))
        #expect(generator.code == 0)
        let backend = BallBackend()
        let firstID = UUID().uuidString
        let secondID = UUID().uuidString
        let firstLog = fixture.ball.appending(path: "logs/macos-\(firstID).log")
        let secondLog = fixture.ball.appending(path: "logs/macos-\(secondID).log")
        let first = Task {
            try await backend.execute(
                ball: fixture.ball.path, build: runtime, arguments: ["run", #"C:\OS2\ONE.EXE"#],
                sessionID: firstID
            ) { _ in }
        }
        var second: Task<ProcessResult, any Error>?
        do {
            try await awaitCondition {
                (try? String(contentsOf: firstLog, encoding: .utf8).contains("writer-ready"))
                    == true
            }
            second = Task {
                try await backend.execute(
                    ball: fixture.ball.path, build: runtime,
                    arguments: ["run", #"C:\OS2\TWO.EXE"#], sessionID: secondID
                ) { _ in }
            }
            try await awaitCondition {
                (try? String(contentsOf: firstLog, encoding: .utf8).contains("writer-shared-ok"))
                    == true
            }
            await backend.stop(sessionID: firstID)
            let firstResult = try await first.value
            #expect(firstResult.signalled && firstResult.sessionID == firstID)
            try await awaitCondition {
                (try? String(contentsOf: secondLog, encoding: .utf8).contains(
                    "reader-after-peer-exit")) == true
            }
            #expect(await backend.activeSessionIDs(ball: fixture.ball.path) == [secondID])
            await backend.stop(sessionID: secondID)
            let secondResult = try await #require(second).value
            #expect(secondResult.signalled && secondResult.output.contains("reader-shared-ok"))
            let firstAudit = try #require(firstResult.dependencyAudit)
            let secondAudit = try #require(secondResult.dependencyAudit)
            #expect(firstAudit.cached == true && secondAudit.cached == true)
            #expect(firstAudit.scope == "launch" && secondAudit.scope == "launch")
            #expect(firstAudit.jsonPath.isEmpty && secondAudit.jsonPath.isEmpty)
            let auditFolders = try FileManager.default.contentsOfDirectory(
                atPath: fixture.ball.appending(path: "logs").path)
            #expect(!auditFolders.contains { $0.hasPrefix("dependency-audit-") })
        } catch {
            await backend.stop(sessionID: firstID, force: true)
            await backend.stop(sessionID: secondID, force: true)
            _ = try? await first.value
            if let second { _ = try? await second.value }
            throw error
        }
    }

    @Test(
        .enabled(
            if: ProcessInfo.processInfo.environment["TWOINE_TEST_RUNTIME"] != nil,
            "Requires an explicitly selected built portable runtime."))
    func execChildUsesItsParentsImmutableDesktopLoader() async throws {
        let runtime = try #require(ProcessInfo.processInfo.environment["TWOINE_TEST_RUNTIME"])
        let fixture = try LaunchFixture(startup: false)
        defer { fixture.cleanup() }
        let repository = URL(filePath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let generator = try await ProcessRunner().run(
            executable: URL(filePath: "/usr/bin/python3"),
            arguments: [
                repository.appending(path: "tests/integration/session_guests.py").path,
                fixture.ball.appending(path: "drive_c/OS2").path,
            ],
            log: fixture.root.appending(path: "generate.log"))
        #expect(generator.code == 0)
        let backend = BallBackend()
        let session = UUID().uuidString
        let log = fixture.ball.appending(path: "logs/macos-\(session).log")
        let running = Task {
            try await backend.execute(
                ball: fixture.ball.path, build: runtime, arguments: ["run", #"C:\OS2\PARENT.EXE"#],
                sessionID: session
            ) { _ in }
        }
        do {
            try await awaitCondition {
                (try? String(contentsOf: log, encoding: .utf8).contains("desktop-child-ready"))
                    == true
            }
            let inventory = try await ProcessRunner().run(
                executable: URL(filePath: "/bin/ps"), arguments: ["-axo", "pid="],
                log: fixture.root.appending(path: "owned-process-paths.log"))
            #expect(inventory.code == 0)
            let marker = "/cache/desktop-sessions/" + session + "/"
            // Child argv[0] is deliberately lx_loader; proc_pidpath reports its actual Mach-O path.
            let owned = inventory.output.split(whereSeparator: \.isWhitespace).compactMap {
                token -> String? in
                guard let pid = Int32(token) else { return nil }
                var bytes = [CChar](repeating: 0, count: 4096)
                let length = proc_pidpath(pid, &bytes, UInt32(bytes.count))
                guard length > 0 else { return nil }
                let path = String(
                    decoding: bytes.prefix(Int(length)).map { UInt8(bitPattern: $0) }, as: UTF8.self
                )
                return path.contains(marker) ? path : nil
            }
            #expect(owned.count == 2)
            #expect(owned.allSatisfy { $0.hasSuffix("/Contents/MacOS/lx_loader") })
            let result = try await running.value
            #expect(result.code == 0 && result.output.contains("desktop-parent-complete"))
        } catch {
            let markerExists = FileManager.default.fileExists(atPath: log.path)
            await backend.stop(sessionID: session, force: true)
            let result = try? await running.value
            let guestLog =
                result?.output
                ?? ((try? String(contentsOf: log, encoding: .utf8)) ?? "(guest log not created)")
            let evidence = repository.appending(
                path: "build-desktop-current/child-wrapper-failure-\(session).log")
            try? FileManager.default.createDirectory(
                at: evidence.deletingLastPathComponent(), withIntermediateDirectories: true)
            let diagnostic =
                "\(error)\nRuntime: \(runtime)\nGuest log existed at failure: \(markerExists)\nExit: \(result?.code.description ?? "unknown")\n\(guestLog)"
            try? Data(diagnostic.utf8).write(to: evidence)
            throw BallError(
                "Generated child-wrapper checkpoint failed; evidence: \(evidence.path)\n\(diagnostic)"
            )
        }
    }
}

@Test func onlyAnUnrequestedSignalDeathNeedsAMessage() {
    let log = URL(filePath: "/ball/logs/session.log")
    #expect(
        abnormalExitMessage(
            program: "App", code: 1, signalled: false, stopRequested: false, log: log) == nil)
    #expect(
        abnormalExitMessage(
            program: "App", code: 15, signalled: true, stopRequested: true, log: log) == nil)
    let killed = abnormalExitMessage(
        program: "App", code: SIGKILL, signalled: true, stopRequested: false, log: log)
    #expect(
        killed?.contains("signal 9") == true && killed?.contains("/ball/logs/session.log") == true)
    let other = abnormalExitMessage(
        program: "App", code: SIGSEGV, signalled: true, stopRequested: false, log: nil)
    #expect(other?.contains("could not record") == true)
}
