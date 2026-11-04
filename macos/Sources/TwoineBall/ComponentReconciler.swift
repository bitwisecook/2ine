import BallCore
import Foundation
import Observation

struct ComponentChange {
    let kind: String
    let sources: [String]
    let files: [OS2ExtractionPlan.File]
    let changed: Set<String>
    let configure: [String]
}
struct ComponentChangeResult {
    let state: OS2ComponentState
    var interrupted = false
    var error: String?
}

/// One ball's desired component state, independent of its currently visible pane.
@MainActor @Observable final class ComponentReconciler {
    let ball: String
    private(set) var desired: Set<String>
    private(set) var actual: Set<String> = []
    private(set) var state: OS2ComponentState?
    private(set) var reconciling = false
    private(set) var activeSources: Set<String> = []
    private(set) var observationRevision: UInt64 = 0
    private(set) var error: String?
    private(set) var notice: String?
    private(set) var plan: OS2ExtractionPlan
    private(set) var sources: [String]
    @ObservationIgnored private var revisions: [String: UInt64] = [:]
    @ObservationIgnored private var sequence: UInt64 = 0
    @ObservationIgnored private var initialized = false
    @ObservationIgnored private var task: Task<Void, Never>?
    @ObservationIgnored private let read: @MainActor () async throws -> OS2ComponentState
    @ObservationIgnored private let apply: @MainActor (ComponentChange) async throws -> ComponentChangeResult
    @ObservationIgnored private let settled: @MainActor () async -> Void

    init(ball: String, plan: OS2ExtractionPlan, sources: [String], selection: Set<String>, explicitIntent: Bool = false,
         read: @escaping @MainActor () async throws -> OS2ComponentState,
         apply: @escaping @MainActor (ComponentChange) async throws -> ComponentChangeResult,
         settled: @escaping @MainActor () async -> Void = {}) {
        self.ball = ball; self.plan = plan; self.sources = sources
        desired = selection.intersection(Self.eligible(plan))
        sequence = explicitIntent ? 1 : 0
        self.read = read; self.apply = apply; self.settled = settled
    }
    private static func eligible(_ plan: OS2ExtractionPlan) -> Set<String> {
        plan.tree?.eligibleSources ?? Set(plan.files.filter { !$0.provided }.map(\.source))
    }
    var isSettled: Bool { !reconciling && pendingSources.isEmpty }
    var pendingSources: Set<String> { desired.symmetricDifference(actual).union(activeSources) }
    var fileStates: [String: OS2ComponentState.File] {
        Dictionary((state?.files ?? []).compactMap { item in item.selectedSource.map { ($0, item) } },
                   uniquingKeysWith: { a, b in
                       let rank = ["missing": 0, "managed": 1, "unowned": 2, "modified": 3, "protected": 4]
                       return (rank[b.status] ?? 5) > (rank[a.status] ?? 5) ? b : a
                   })
    }
    private var immutablePresent: Set<String> {
        Set(fileStates.compactMap { source, file in
            file.present && (!file.managed || file.status != "managed") ? source : nil
        })
    }
    private func constrain() {
        let keep = immutablePresent
        let absentProtected = Set(fileStates.compactMap { source, file in
            file.status == "protected" && !file.present ? source : nil
        })
        if !keep.isSubset(of: desired) {
            notice = "Existing, modified or protected files are kept. Only unchanged files installed by this component manager can be removed."
        }
        desired.formUnion(keep)
        desired.subtract(absentProtected)
        desired.formIntersection(Self.eligible(plan))
    }
    func update(plan: OS2ExtractionPlan, sources: [String], selection: Set<String>, explicitIntent: Bool = false) {
        if self.sources != sources && !reconciling {
            initialized = false; actual = []; state = nil; revisions = [:]
            desired = selection; sequence = explicitIntent ? 1 : 0; error = nil; notice = nil
        }
        self.plan = plan; self.sources = sources
        observationRevision &+= 1
        desired.formIntersection(Self.eligible(plan))
        actual.formIntersection(Self.eligible(plan))
        constrain()
    }
    /// Polling reports actual state but never replaces a queued user choice.
    func observe(_ value: OS2ComponentState, expectedRevision: UInt64? = nil) {
        if let expectedRevision, expectedRevision != observationRevision { return }
        let previous = actual
        let protectedIntent = pendingSources.union(revisions.keys)
        state = value
        actual = Set(value.files.filter(\.present).compactMap(\.selectedSource)).intersection(Self.eligible(plan))
        if initialized {
            desired.formUnion(actual.subtracting(protectedIntent))
            desired.subtract(previous.subtracting(actual).subtracting(protectedIntent))
        } else {
            if value.progress != nil && sequence == 0 { desired = actual }
            else { desired.formUnion(actual) }
            initialized = true
        }
        constrain()
    }
    func setDesired(_ selection: Set<String>) {
        sequence &+= 1
        let next = selection.intersection(Self.eligible(plan))
        for source in next.symmetricDifference(desired) { revisions[source] = sequence }
        desired = next
        error = nil; notice = nil
        constrain()
        start()
    }
    @discardableResult func start() -> Task<Void, Never> {
        if let task { return task }
        reconciling = true
        let work = Task<Void, Never> { [weak self] in
            guard let self else { return }
            await self.drain()
        }
        task = work
        return work
    }
    func waitUntilIdle() async { await task?.value }
    private func drain() async {
        var verified = true
        do {
            if !initialized {
                let value = try await read()
                observationRevision &+= 1
                observe(value)
            }
            while true {
                constrain()
                let states = fileStates
                let remove = actual.subtracting(desired).filter {
                    states[$0]?.managed == true && states[$0]?.status == "managed"
                }
                let install = desired.subtracting(actual)
                let changed = !remove.isEmpty ? Set(remove) : install
                if changed.isEmpty { break }
                let kind = remove.isEmpty ? "install" : "remove"
                let version = revisions
                let selected = kind == "install" ? desired : changed
                let files = plan.files.filter { selected.contains($0.source) && !$0.provided }
                let change = ComponentChange(kind: kind, sources: sources, files: files, changed: changed,
                                             configure: kind == "install" ? plan.configuredComponents(for: desired) : [])
                activeSources = changed
                observationRevision &+= 1
                do {
                    let result = try await apply(change)
                    observationRevision &+= 1
                    observe(result.state)
                    let unfinished = kind == "install" ? changed.subtracting(actual) : changed.intersection(actual)
                    if result.interrupted || result.error != nil || !unfinished.isEmpty {
                        for source in changed where revisions[source] == version[source] {
                            if actual.contains(source) { desired.insert(source) } else { desired.remove(source) }
                        }
                        error = result.error ?? (result.interrupted ? "Component update was cancelled. Completed changes were kept." : "Some component changes could not be applied. Existing files were kept.")
                    }
                } catch {
                    // A worker failure still needs authoritative state before changing ticks.
                    let value = try await read()
                    observationRevision &+= 1
                    observe(value)
                    for source in changed where revisions[source] == version[source] {
                        if actual.contains(source) { desired.insert(source) } else { desired.remove(source) }
                    }
                    self.error = error.localizedDescription
                }
                activeSources = []
            }
        } catch {
            verified = false
            self.error = "Could not verify component files: " + error.localizedDescription
        }
        activeSources = []
        if verified { revisions.removeAll() }
        reconciling = false
        task = nil
        if verified { await settled() }
    }
}
