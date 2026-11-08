import BallCore
import Foundation
import Observation

/// Requested directory listings only; expanding one folder never scans siblings.
@MainActor @Observable final class GuestBrowserTree {
    private(set) var directories: [String: [GuestEntry]] = [:]
    private(set) var loading: Set<String> = []
    private(set) var errors: [String: String] = [:]
    @ObservationIgnored private var tasks: [String: Task<Void, Never>] = [:]
    @ObservationIgnored private var generation = UUID()

    func reset() {
        generation = UUID()
        for task in tasks.values { task.cancel() }
        tasks.removeAll()
        directories.removeAll()
        loading.removeAll()
        errors.removeAll()
    }

    @discardableResult
    func expand(
        _ path: String, using browse: @escaping @MainActor (String) async throws -> GuestDirectory
    ) -> Task<Void, Never>? {
        guard directories[path] == nil, !loading.contains(path) else { return nil }
        let requestedGeneration = generation
        loading.insert(path)
        errors[path] = nil
        let task = Task { [weak self] in
            do {
                let result = try await browse(path)
                try Task.checkCancellation()
                guard let self, self.generation == requestedGeneration else { return }
                self.directories[path] = result.entries
            } catch is CancellationError {
                // Navigation cancels work; the generation guard owns stale cleanup.
            } catch {
                guard let self, self.generation == requestedGeneration else { return }
                self.errors[path] = error.localizedDescription
            }
            guard let self, self.generation == requestedGeneration else { return }
            self.loading.remove(path)
            self.tasks[path] = nil
        }
        tasks[path] = task
        return task
    }

    func entry(_ path: String?, roots: [GuestEntry]) -> GuestEntry? {
        guard let path else { return nil }
        if let entry = roots.first(where: { $0.path == path }) { return entry }
        for children in directories.values {
            if let entry = children.first(where: { $0.path == path }) { return entry }
        }
        return nil
    }

    func nodes(
        _ entries: [GuestEntry], directoriesOnly: Bool = false, programsOnly: Bool = false,
        relevantOnly: Bool = true
    ) -> [OutlineBrowserNode] {
        let filtered = BrowserRows.filteredGuestEntries(
            entries, directoriesOnly: directoriesOnly,
            programsOnly: programsOnly, relevantOnly: relevantOnly)
        return zip(filtered, BrowserRows.guest(filtered)).map { entry, original in
            var node = original
            if entry.directory {
                node.canExpand = directories[entry.path] == nil
                node.children = nodes(
                    directories[entry.path] ?? [], directoriesOnly: directoriesOnly,
                    programsOnly: programsOnly, relevantOnly: relevantOnly)
                node.busy = loading.contains(entry.path)
                if let error = errors[entry.path] {
                    node.tooltip = entry.path + "\n" + error
                    node.cells["name"]?.warning = true
                }
            }
            return node
        }
    }
}
