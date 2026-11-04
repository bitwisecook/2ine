import AppKit
import BallCore
import Foundation
import SwiftUI
import Testing
@testable import TwoineBall

private func reconciliationPlan(_ sources: [String] = ["a", "b", "c", "d"]) throws -> OS2ExtractionPlan {
    let files = sources.map { source -> [String: Any] in
        ["source": source, "path": "C:\\OS2\\" + source + ".DLL", "category": source == "a" ? "cmd" : "fonts", "provided": false, "present": false, "size": 4]
    }
    let leaves = files.map { file -> [String: Any] in
        var leaf = file
        leaf["id"] = file["source"]; leaf["title"] = file["source"]; leaf["kind"] = "file"
        return leaf
    }
    return try JSONDecoder().decode(OS2ExtractionPlan.self, from: JSONSerialization.data(withJSONObject: [
        "files": files, "unmapped": [], "tree": ["id": "all", "title": "Components", "kind": "group", "category": "full", "children": leaves]
    ]))
}
private func reconciliationState(_ present: Set<String>, statuses: [String: String] = [:], history: String? = nil) throws -> OS2ComponentState {
    let files = ["a", "b", "c", "d"].map { source -> [String: Any] in
        let status = statuses[source] ?? (present.contains(source) ? "managed" : "missing")
        return ["path": "C:\\OS2\\" + source + ".DLL", "source": source, "selectedSource": source,
                "present": present.contains(source), "managed": status == "managed", "status": status]
    }
    var object: [String: Any] = ["installedPaths": [], "managedPaths": [], "protectedPaths": [], "modifiedPaths": [], "unownedPaths": [], "files": files]
    if let history {
        object["progress"] = ["operationID": "previous", "kind": "install", "status": history, "phase": "finished",
                              "activeSources": [], "activePaths": [], "completedPaths": [], "completedCount": 0, "totalCount": 1]
    }
    return try JSONDecoder().decode(OS2ComponentState.self, from: JSONSerialization.data(withJSONObject: object))
}

@MainActor private final class ComponentGate {
    var changes: [ComponentChange] = []
    var results: [CheckedContinuation<ComponentChangeResult, Never>] = []
    var waiter: (Int, CheckedContinuation<Void, Never>)?
    func apply(_ change: ComponentChange) async -> ComponentChangeResult {
        changes.append(change)
        return await withCheckedContinuation { result in
            results.append(result)
            if let (count, continuation) = waiter, changes.count >= count { waiter = nil; continuation.resume() }
        }
    }
    func waitForChange(_ count: Int) async {
        if changes.count < count { await withCheckedContinuation { waiter = (count, $0) } }
    }
    func finish(_ result: ComponentChangeResult) { results.removeFirst().resume(returning: result) }
}

@MainActor @Test func componentDefaultsApplyImmediatelyWithoutSelectingOtherGroups() async throws {
    let plan = try reconciliationPlan()
    var actual: Set<String> = []
    var changes: [ComponentChange] = []
    let controller = ComponentReconciler(ball: "fixture", plan: plan, sources: ["media"], selection: plan.sourceSelection(OS2ExtractionCategory.defaults),
        read: { try reconciliationState(actual) }, apply: { change in
            changes.append(change); actual.formUnion(change.changed)
            return ComponentChangeResult(state: try reconciliationState(actual))
        })
    await controller.start().value
    #expect(changes.count == 1 && changes.first?.kind == "install")
    #expect(changes.first?.files.map(\.source) == ["a"])
    #expect(controller.desired == ["a"] && controller.actual == ["a"])
    #expect(!controller.reconciling && controller.pendingSources.isEmpty)
}

@MainActor @Test func componentChangesCoalesceWhileAJobRunsAndSerializeExactRemoval() async throws {
    let gate = ComponentGate()
    var settled = 0
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: [],
        read: { try reconciliationState([]) }, apply: { await gate.apply($0) }, settled: { settled += 1 })
    await controller.start().value
    controller.setDesired(["a"])
    await gate.waitForChange(1)
    controller.setDesired(["a", "b"])
    controller.setDesired(["a"])
    controller.setDesired(["b"])
    #expect(gate.changes.count == 1 && controller.pendingSources == ["a", "b"])
    gate.finish(ComponentChangeResult(state: try reconciliationState(["a"])))
    await gate.waitForChange(2)
    #expect(gate.changes[1].kind == "remove" && gate.changes[1].files.map(\.source) == ["a"])
    gate.finish(ComponentChangeResult(state: try reconciliationState([])))
    await gate.waitForChange(3)
    #expect(gate.changes[2].kind == "install" && gate.changes[2].changed == ["b"])
    gate.finish(ComponentChangeResult(state: try reconciliationState(["b"])))
    await controller.waitUntilIdle()
    #expect(controller.desired == ["b"] && controller.actual == ["b"])
    #expect(gate.changes.map(\.kind) == ["install", "remove", "install"])
    #expect(settled == 2) // Initialization and the quiescent queue, never its transitions.
}

@MainActor @Test func componentCancelSnapsFailedIntentButPreservesLaterUserEdits() async throws {
    let gate = ComponentGate()
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: [],
        read: { try reconciliationState([]) }, apply: { await gate.apply($0) })
    await controller.start().value
    controller.setDesired(["a"])
    await gate.waitForChange(1)
    controller.setDesired(["a", "b"])
    gate.finish(ComponentChangeResult(state: try reconciliationState([]), interrupted: true))
    await gate.waitForChange(2)
    #expect(controller.desired == ["b"])
    #expect(gate.changes[1].changed == ["b"])
    gate.finish(ComponentChangeResult(state: try reconciliationState(["b"])))
    await controller.waitUntilIdle()
    #expect(gate.changes.count == 2 && controller.desired == ["b"])
    #expect(controller.error != nil)
    controller.observe(try reconciliationState(["b"]))
    #expect(!controller.reconciling && gate.changes.count == 2) // Polling never retries failure.
}

@MainActor @Test func componentUnchecksPreserveUnownedModifiedAndProtectedFiles() async throws {
    let statuses = ["a": "unowned", "b": "modified", "c": "protected"]
    var actual: Set<String> = ["a", "b", "c", "d"]
    var changes: [ComponentChange] = []
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: [],
        read: { try reconciliationState(actual, statuses: statuses) }, apply: { change in
            changes.append(change); actual.subtract(change.changed)
            return ComponentChangeResult(state: try reconciliationState(actual, statuses: statuses))
        })
    await controller.start().value
    controller.setDesired([])
    await controller.waitUntilIdle()
    #expect(changes.count == 1 && changes.first?.kind == "remove")
    #expect(changes.first?.files.map(\.source) == ["d"])
    #expect(controller.desired == ["a", "b", "c"] && controller.notice != nil)
    let rows = OS2ComponentTree.row(try #require(controller.plan.tree), selected: controller.desired, fileStates: controller.fileStates)
    #expect(rows.children.prefix(3).allSatisfy { $0.check == .on && $0.cells["name"]?.enabled == false })
    #expect(rows.check == .mixed && rows.cells["name"]?.enabled == true)
}

@MainActor @Test func nativeComponentCheckboxStartsWorkAndAnotherCheckboxRemainsUsable() async throws {
    let plan = try reconciliationPlan()
    let gate = ComponentGate()
    let controller = ComponentReconciler(ball: "fixture", plan: plan, sources: ["media"], selection: [],
        read: { try reconciliationState([]) }, apply: { await gate.apply($0) })
    await controller.start().value
    let view = OS2ComponentTree(root: try #require(plan.tree), selection: .constant([]), query: .constant(""),
                               searchFocused: .constant(false), disabled: false,
                               toggle: { node, enabled in controller.setDesired(plan.selecting(node, in: controller.desired, enabled: enabled)) },
                               openSetting: { _ in })
    let coordinator = view.browser.makeCoordinator()
    let scroll = view.browser.makeScrollView(coordinator: coordinator)
    scroll.frame = NSRect(x: 0, y: 0, width: 760, height: 360); scroll.layoutSubtreeIfNeeded()
    let outline = try #require(coordinator.outline)
    let first = try #require(coordinator.find("a", in: coordinator.items))
    #expect(coordinator.toggle(first))
    await gate.waitForChange(1)
    let second = try #require(coordinator.find("b", in: coordinator.items))
    #expect(coordinator.toggle(second))
    #expect(controller.desired == ["a", "b"] && gate.changes.count == 1)
    let rows = OS2ComponentTree.row(try #require(plan.tree), selected: controller.desired,
                                  activeSources: ["a"], queuedSources: controller.pendingSources)
    #expect(rows.children[0].busy && rows.children[0].cells["status"]?.text == "Updating…")
    #expect(rows.children[1].busy && rows.children[1].cells["status"]?.text == "Queued…")
    #expect(rows.children[1].cells["name"]?.enabled == true)
    gate.finish(ComponentChangeResult(state: try reconciliationState(["a"])))
    await gate.waitForChange(2)
    gate.finish(ComponentChangeResult(state: try reconciliationState(["a", "b"])))
    await controller.waitUntilIdle()
    #expect(outline.window == nil)
}

@MainActor @Test func componentCompletionRejectsAnOlderInFlightPoll() async throws {
    let gate = ComponentGate()
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: [],
        read: { try reconciliationState([]) }, apply: { await gate.apply($0) })
    await controller.start().value
    controller.setDesired(["a"])
    await gate.waitForChange(1)
    let pollRevision = controller.observationRevision
    gate.finish(ComponentChangeResult(state: try reconciliationState(["a"])))
    await controller.waitUntilIdle()
    controller.observe(try reconciliationState([]), expectedRevision: pollRevision)
    #expect(controller.actual == ["a"] && controller.pendingSources.isEmpty)
}

@MainActor @Test func componentQueueInstallsAndRemovesGeneratedFilesAfterNavigatingToAnotherBall() async throws {
    let root = FileManager.default.temporaryDirectory.appending(path: "2ine-component-queue-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = root.appending(path: "First.2ineball").path
    let other = root.appending(path: "Other.2ineball").path
    let snapshot = try NativeBallCore.request(DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    _ = try NativeBallCore.request(DesktopRequest("command", ball: other, arguments: ["init"]), as: BallSnapshot.self)
    let media = root.appending(path: "Media/OS2/HELP")
    try FileManager.default.createDirectory(at: media, withIntermediateDirectories: true)
    try Data("one".utf8).write(to: media.appending(path: "ONE.HLP"))
    try Data("two".utf8).write(to: media.appending(path: "TWO.HLP"))
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"))
    model.selected = ball; model.snapshot = snapshot; model.paths = [ball, other]
    let sources = [root.appending(path: "Media").path]
    let plan = try await model.planOS2Extraction(sources, ball: ball)
    model.componentPlans[ball] = plan
    let one = try #require(plan.files.first { $0.path.hasSuffix("ONE.HLP") })
    let two = try #require(plan.files.first { $0.path.hasSuffix("TWO.HLP") })
    let controller = model.componentReconciler(ball: ball, plan: plan, sources: sources, selection: [])
    await controller.start().value
    controller.setDesired([one.source, two.source])
    model.select(other)
    await controller.waitUntilIdle()
    #expect(model.selected == other)
    #expect(controller.error == nil && controller.actual == [one.source, two.source])
    // Unmount the original source. Removing an owned source needs only its ledger.
    try FileManager.default.moveItem(at: root.appending(path: "Media"), to: root.appending(path: "OfflineMedia"))
    controller.setDesired([two.source])
    await controller.waitUntilIdle()
    #expect(model.selected == other && controller.actual == [two.source])
    let state = try await model.componentState(ball: ball, selection: [.init(source: one.source, path: one.path), .init(source: two.source, path: two.path)])
    #expect(state.files.first { $0.selectedSource == one.source }?.present == false)
    #expect(state.files.first { $0.selectedSource == two.source }?.managed == true)
    // Change a file after the last verified state. The removal worker must keep it.
    let folder = String(two.path.prefix(through: try #require(two.path.lastIndex(of: "\\"))))
    let listing = try await model.browse(folder, ball: ball)
    let host = try #require(listing.entries.first { $0.path == two.path }?.hostPath)
    try Data("externally modified".utf8).write(to: URL(filePath: host))
    controller.setDesired([])
    await controller.waitUntilIdle()
    #expect(controller.actual == [two.source] && controller.desired == [two.source])
    #expect(controller.error != nil)
    #expect(try Data(contentsOf: URL(filePath: host)) == Data("externally modified".utf8))
    let deadline = ContinuousClock.now + .seconds(10)
    while model.busy {
        guard ContinuousClock.now < deadline else { throw BallError("Navigation did not finish") }
        try await Task.sleep(for: .milliseconds(5))
    }
}

@MainActor @Test func componentFailureRequiresANewUserRequestInsteadOfRetryingFromPolling() async throws {
    var changes = 0
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: ["a"],
        read: { try reconciliationState([]) }, apply: { _ in
            changes += 1
            return ComponentChangeResult(state: try reconciliationState(changes == 1 ? [] : ["a"]), interrupted: changes == 1)
        })
    await controller.start().value
    #expect(changes == 1 && controller.desired.isEmpty && controller.error != nil)
    controller.observe(try reconciliationState([]))
    #expect(changes == 1 && !controller.reconciling)
    controller.setDesired(["a"])
    await controller.waitUntilIdle()
    #expect(changes == 2 && controller.actual == ["a"] && controller.error == nil)
}

@MainActor @Test func editingTheSameSourceDuringCancellationCountsAsAFreshRequest() async throws {
    let gate = ComponentGate()
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: ["a"],
        read: { try reconciliationState([]) }, apply: { await gate.apply($0) })
    controller.start()
    await gate.waitForChange(1)
    controller.setDesired([])
    controller.setDesired(["a"])
    gate.finish(ComponentChangeResult(state: try reconciliationState([]), interrupted: true))
    await gate.waitForChange(2)
    #expect(controller.desired == ["a"] && gate.changes[1].changed == ["a"])
    gate.finish(ComponentChangeResult(state: try reconciliationState(["a"])))
    await controller.waitUntilIdle()
    #expect(controller.isSettled && controller.actual == ["a"])
}

@MainActor @Test func changingComponentMediaReinitializesDefaultsWithoutExpandingInstalledGroups() async throws {
    let plan = try reconciliationPlan()
    var actual: Set<String> = ["b"] // One installed font does not select other fonts.
    var changed: [Set<String>] = []
    let controller = ComponentReconciler(ball: "fixture", plan: plan, sources: ["old"], selection: [],
        read: { try reconciliationState(actual) }, apply: { change in
            changed.append(change.changed); actual.formUnion(change.changed)
            return ComponentChangeResult(state: try reconciliationState(actual))
        })
    await controller.start().value
    #expect(controller.desired == ["b"] && changed.isEmpty)
    controller.update(plan: plan, sources: ["new"], selection: ["a"])
    await controller.start().value
    #expect(controller.desired == ["a", "b"])
    #expect(changed == [["a"]])
}

@MainActor @Test func settledComponentPollingReflectsExternalChangesWithoutLeavingAnIdleQueue() async throws {
    var actual: Set<String> = []
    let controller = ComponentReconciler(ball: "fixture", plan: try reconciliationPlan(), sources: ["media"], selection: [],
        read: { try reconciliationState(actual) }, apply: { change in
            actual.formUnion(change.changed)
            return ComponentChangeResult(state: try reconciliationState(actual))
        })
    await controller.start().value
    controller.setDesired(["a"])
    await controller.waitUntilIdle()
    #expect(controller.isSettled && controller.desired == ["a"])
    controller.observe(try reconciliationState([]))
    #expect(controller.desired.isEmpty && controller.actual.isEmpty && controller.isSettled)
}

@MainActor @Test func componentHistorySuppressesDefaultsButAnUnchangedExplicitRequestStillApplies() async throws {
    let plan = try reconciliationPlan()
    for history in ["complete", "cancelled", "failed"] {
        var changes = 0
        let read = { @MainActor in try reconciliationState([], history: history) }
        let apply = { @MainActor (_: ComponentChange) throws -> ComponentChangeResult in
            changes += 1
            return ComponentChangeResult(state: try reconciliationState(["a"], history: "complete"))
        }
        let automatic = ComponentReconciler(ball: "fixture", plan: plan, sources: ["media"], selection: ["a"], read: read, apply: apply)
        await automatic.start().value
        #expect(automatic.isSettled && automatic.desired.isEmpty && changes == 0)
        let explicit = ComponentReconciler(ball: "fixture", plan: plan, sources: ["media"], selection: ["a"], read: read, apply: apply)
        explicit.setDesired(explicit.desired) // Selecting navigation can already match default ticks.
        await explicit.waitUntilIdle()
        #expect(explicit.actual == ["a"] && changes == 1)
        let draft = ComponentReconciler(ball: "fixture", plan: plan, sources: ["media"], selection: ["a"], explicitIntent: true, read: read, apply: apply)
        await draft.start().value
        #expect(draft.actual == ["a"] && changes == 2)
    }
}

@MainActor @Test func recreatedManagerDoesNotReinstallCommandSupportAfterItWasUnticked() async throws {
    let root = FileManager.default.temporaryDirectory.appending(path: "2ine-component-restart-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = root.appending(path: "Test.2ineball").path
    let snapshot = try NativeBallCore.request(DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    let media = root.appending(path: "Media/OS2")
    try FileManager.default.createDirectory(at: media, withIntermediateDirectories: true)
    var executable = Data(repeating: 0, count: 100)
    executable[0] = 77; executable[1] = 90; executable[60] = 64; executable[64] = 76; executable[65] = 88
    try executable.write(to: media.appending(path: "CMD.EXE"))
    let sources = [root.appending(path: "Media").path]
    let original = AppModel(preferencesURL: root.appending(path: "prefs-a.json"))
    original.selected = ball; original.snapshot = snapshot
    let plan = try await original.planOS2Extraction(sources, ball: ball)
    original.componentPlans[ball] = plan
    let controller = original.componentReconciler(ball: ball, plan: plan, sources: sources, selection: plan.sourceSelection(OS2ExtractionCategory.defaults))
    await controller.start().value
    #expect(controller.error == nil && controller.actual.count == 1)
    controller.setDesired([])
    await controller.waitUntilIdle()
    #expect(controller.error == nil && controller.actual.isEmpty)
    let previous = try #require(controller.state?.progress?.operationID)
    let recreated = AppModel(preferencesURL: root.appending(path: "prefs-b.json"))
    recreated.selected = ball; recreated.snapshot = snapshot
    let nextPlan = try await recreated.planOS2Extraction(sources, ball: ball)
    recreated.componentPlans[ball] = nextPlan
    let next = recreated.componentReconciler(ball: ball, plan: nextPlan, sources: sources, selection: nextPlan.sourceSelection(OS2ExtractionCategory.defaults))
    await next.start().value
    #expect(next.isSettled && next.actual.isEmpty && next.desired.isEmpty)
    #expect(next.state?.progress?.operationID == previous)
    #expect(recreated.componentJobs[ball] == nil)
    #expect(!FileManager.default.fileExists(atPath: ball + "/drive_c/OS2/CMD.EXE"))
    let deadline = ContinuousClock.now + .seconds(10)
    while original.busy {
        guard ContinuousClock.now < deadline else { throw BallError("Snapshot refresh did not finish") }
        try await Task.sleep(for: .milliseconds(5))
    }
}
