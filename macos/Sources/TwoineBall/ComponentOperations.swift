import BallCore
import Foundation
import Observation

@MainActor @Observable final class ComponentJob: Identifiable {
    let id = UUID().uuidString
    let ball: String
    let kind: String
    let paths: Set<String>
    let sourcePaths: [String: String]
    let changedSources: Set<String>
    var running = true
    var cancelling = false
    var progress: OS2ComponentProgress?
    var error: String?
    var progressError: String?
    var summary = "Preparing components…"
    var task: Task<Void, Never>?
    init(
        ball: String, kind: String, paths: Set<String>, sourcePaths: [String: String] = [:],
        changedSources: Set<String> = []
    ) {
        self.ball = ball
        self.kind = kind
        self.paths = paths
        self.sourcePaths = sourcePaths
        self.changedSources = changedSources
    }
    var activeSources: Set<String> {
        guard running else { return [] }
        return progress?.activeSources.isEmpty == false
            ? Set(progress?.activeSources ?? []) : changedSources
    }
    var activePaths: Set<String> {
        guard running else { return [] }
        guard let progress, !progress.activePaths.isEmpty else {
            return Set(paths.map { $0.uppercased() })
        }
        return Set(
            (progress.activePaths + progress.activeSources.compactMap { sourcePaths[$0] }).map {
                $0.uppercased()
            })
    }
}

extension AppModel {
    var componentOperationRunning: Bool {
        selected.map {
            componentJobs[$0]?.running == true || componentReconcilers[$0]?.reconciling == true
        } ?? false
    }
    var hasComponentOperations: Bool {
        componentJobs.values.contains { $0.running }
            || componentReconcilers.values.contains { $0.reconciling }
    }

    func componentState(
        ball: String, paths: [String] = [], selection: [OS2ComponentSelection]? = nil
    ) async throws -> OS2ComponentState {
        var request = DesktopRequest("os2-component-state", ball: ball)
        request.paths = paths
        request.selection = selection
        request.operationID = componentJobs[ball]?.id
        return try await Self.readComponentState(request)
    }
    private static func readComponentState(
        _ request: DesktopRequest
    ) async throws -> OS2ComponentState {
        try await Task.detached(priority: .utility) {
            try NativeBallCore.request(request, as: OS2ComponentState.self)
        }.value
    }
    func startComponentOperation(
        ball: String, kind: String, sources: [String], files: [OS2ExtractionPlan.File],
        configure: [String] = []
    ) {
        guard selected == ball, !busy, !mediaBusy, !processRunning,
            componentJobs[ball]?.running != true, componentReconcilers[ball]?.reconciling != true
        else { return }
        _ = runComponentOperation(
            ball: ball, kind: kind, sources: sources, files: files, configure: configure,
            receivingBuild: build)
    }
    private func runComponentOperation(
        ball: String, kind: String, sources: [String], files: [OS2ExtractionPlan.File],
        configure: [String], receivingBuild: String, changed: Set<String>? = nil
    ) -> ComponentJob {
        let activity = files.filter { changed?.contains($0.source) ?? true }
        let job = ComponentJob(
            ball: ball, kind: kind, paths: Set(activity.map(\.path)),
            sourcePaths: Dictionary(
                files.map { ($0.source, $0.path) }, uniquingKeysWith: { first, _ in first }),
            changedSources: Set(activity.map(\.source)))
        componentJobs[ball] = job
        var request = DesktopRequest(
            kind == "remove" ? "remove-os2-components" : "import-os2-support", ball: ball,
            build: receivingBuild)
        request.operationID = job.id
        if kind == "remove" {
            request.selection = files.map {
                OS2ComponentSelection(source: $0.source, path: $0.path)
            }
        } else {
            request.sources = sources
            request.fileSelection = files.map(\.source)
            request.configureComponents = configure
        }
        let payload = request
        job.task = Task {
            let worker = Task.detached(priority: .userInitiated) {
                try NativeBallCore.request(payload, as: JSONValue.self)
            }
            let monitor = Task {
                while job.running && !Task.isCancelled {
                    var poll = DesktopRequest("os2-component-state", ball: ball)
                    poll.paths = []
                    poll.operationID = job.id
                    do {
                        let state = try await Self.readComponentState(poll)
                        if let progress = state.progress {
                            if progress != job.progress {
                                job.progress = progress
                                job.summary =
                                    "\(progress.phase.capitalized) · \(progress.completedCount)/\(progress.totalCount) files"
                            }
                            job.progressError = nil
                        }
                    } catch {
                        job.progressError = "Could not read progress: " + error.localizedDescription
                    }
                    try? await Task.sleep(for: .milliseconds(500))
                }
            }
            var outcome: String?
            do {
                let result = try await worker.value
                if case .object(let fields) = result {
                    if kind == "remove", case .array(let removed) = fields["removedPaths"] {
                        var kept = 0
                        if case .array(let preserved) = fields["preserved"] {
                            kept = preserved.count
                        }
                        outcome = "Removed \(removed.count) files; kept \(kept)."
                    } else if case .array(let imported) = fields["imported"] {
                        outcome = "Installed \(imported.count) files."
                        if case .array(let skipped) = fields["skippedFiles"], !skipped.isEmpty {
                            outcome =
                                "Installed \(imported.count) files; skipped \(skipped.count). See the log for details."
                        }
                    }
                }
            } catch { job.error = error.localizedDescription }
            job.running = false
            monitor.cancel()
            var final = DesktopRequest("os2-component-state", ball: ball)
            final.paths = []
            final.operationID = job.id
            if let state = try? await Self.readComponentState(final), let progress = state.progress
            {
                job.progress = progress
            }
            if job.progress?.status == "cancelled" {
                job.summary = "Cancelled. Completed files were kept."
            } else if job.error != nil {
                job.summary = "Component operation failed. Completed files were kept."
            } else {
                job.summary = outcome ?? job.progress?.status.capitalized ?? "Components updated"
            }
            if selected == ball { refresh() }
            job.task = nil
        }
        return job
    }
    func componentReconciler(
        ball: String, plan: OS2ExtractionPlan, sources: [String], selection: Set<String>,
        explicitIntent: Bool = false
    ) -> ComponentReconciler {
        if let current = componentReconcilers[ball] {
            current.update(
                plan: plan, sources: sources, selection: selection, explicitIntent: explicitIntent)
            return current
        }
        let receivingBuild = build
        let reconciler = ComponentReconciler(
            ball: ball, plan: plan, sources: sources, selection: selection,
            explicitIntent: explicitIntent,
            read: { [weak self] in
                guard let self, let plan = self.componentPlans[ball] else {
                    throw BallError("The component plan is no longer available.")
                }
                return try await self.componentState(
                    ball: ball,
                    selection: plan.files.filter { !$0.provided }.map {
                        .init(source: $0.source, path: $0.path)
                    })
            },
            apply: { [weak self] change in
                guard let self, !self.hasRunningSession(in: ball),
                    self.componentJobs[ball]?.running != true,
                    FileManager.default.fileExists(atPath: ball)
                else {
                    throw BallError(
                        "Finish this ball’s applications before changing its components.")
                }
                let job = self.runComponentOperation(
                    ball: ball, kind: change.kind, sources: change.sources,
                    files: change.files, configure: change.configure,
                    receivingBuild: receivingBuild, changed: change.changed)
                await job.task?.value
                guard let plan = self.componentPlans[ball] else {
                    throw BallError("The component plan is no longer available.")
                }
                let state = try await self.componentState(
                    ball: ball,
                    selection: plan.files.filter { !$0.provided }.map {
                        .init(source: $0.source, path: $0.path)
                    })
                return ComponentChangeResult(
                    state: state,
                    interrupted: job.cancelling || job.progress?.status == "cancelled",
                    error: job.error)
            }, settled: { [weak self] in await self?.componentOperationDidFinish(ball: ball) })
        componentReconcilers[ball] = reconciler
        return reconciler
    }
    func cancelComponentOperation(_ job: ComponentJob) {
        guard job.running, !job.cancelling else { return }
        pendingLaunches[job.ball]?.autoContinue = false
        job.cancelling = true
        Task {
            var request = DesktopRequest("cancel-os2-component-operation", ball: job.ball)
            request.operationID = job.id
            do {
                _ = try await Task.detached {
                    try NativeBallCore.request(request, as: JSONValue.self)
                }.value
            } catch {
                job.error = error.localizedDescription
                job.cancelling = false
            }
        }
    }
}
