import AppKit
import BallCore
import Darwin
import Foundation
import Testing

@testable import TwoineBall

@MainActor
private func managementFixture() throws -> (AppModel, URL, String, String) {
    var root = FileManager.default.temporaryDirectory.appending(
        path: "2ine-management-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    let canonical = try #require(realpath(root.path, nil))
    root = URL(filePath: String(cString: canonical))
    free(canonical)
    let first = root.appending(path: "First.2ineball").resolvingSymlinksInPath().path
    let second = root.appending(path: "Second.2ineball").resolvingSymlinksInPath().path
    let snapshot = try NativeBallCore.request(
        DesktopRequest("command", ball: first, arguments: ["init"]), as: BallSnapshot.self)
    _ = try NativeBallCore.request(
        DesktopRequest("command", ball: second, arguments: ["init"]), as: BallSnapshot.self)
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"))
    model.paths = [first, second]
    model.selected = first
    model.snapshot = snapshot
    model.undoManager.groupsByEvent = false
    return (model, root, first, second)
}

@Test @MainActor func renamePreservesDocumentsDraftsAndSelectionAcrossRestart() async throws {
    let (model, root, first, second) = try managementFixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let content = Data("My document".utf8)
    try content.write(to: URL(filePath: first).appending(path: "drive_c/document.txt"))
    model.configText += "\r\nSET UNSAVED=draft\r\n"
    let draft = model.settingsDraft
    draft.envName = "PENDING"
    draft.envValue = "value"
    try await model.runCommand(["config", "system.beep_volume", "0.5"], ball: first)
    #expect(model.undoManager.canUndo)
    try await model.renameBall(first, to: "Renamed")
    let renamed = URL(filePath: first).deletingLastPathComponent().appending(
        path: "Renamed.2ineball"
    ).path
    #expect(model.paths == [renamed, second].sorted())
    #expect(model.selected == renamed)
    #expect(model.snapshot?.roots["C"] == renamed + "/drive_c")
    #expect(!FileManager.default.fileExists(atPath: first))
    #expect(
        try Data(contentsOf: URL(filePath: renamed).appending(path: "drive_c/document.txt"))
            == content)
    #expect(model.settingsDraft === draft)
    #expect(model.configText.contains("UNSAVED=draft"))
    #expect(model.configDirty)
    #expect(!model.undoManager.canUndo)
    let saved = try await PreferenceStore(url: root.appending(path: "prefs.json")).load()
    #expect(saved.selected == renamed)
    #expect(saved.ballPaths == model.paths)
}

@Test @MainActor func renameCollisionPreservesBothBallsAndModel() async throws {
    let (model, root, first, second) = try managementFixture()
    defer { try? FileManager.default.removeItem(at: root) }
    model.configText += "\r\nSET KEEP=draft\r\n"
    await #expect(throws: BallError.self) { try await model.renameBall(first, to: "Second") }
    #expect(model.selected == first)
    #expect(model.paths == [first, second])
    #expect(model.configDirty)
    #expect(FileManager.default.fileExists(atPath: first + "/ball.json"))
    #expect(FileManager.default.fileExists(atPath: second + "/ball.json"))
    #expect(!model.busy)
}

@Test @MainActor func trashRemovesSelectedBallFromPreferencesAndKeepsOtherFiles() async throws {
    let (model, root, first, second) = try managementFixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let external = root.appending(path: "External document.txt")
    try Data("external".utf8).write(to: external)
    try FileManager.default.createSymbolicLink(
        at: URL(filePath: first).appending(path: "external"), withDestinationURL: external)
    model.configText += "\r\nSET DISCARD=draft\r\n"
    let trash = root.appending(path: "Fixture Trash.2ineball")
    try await model.trashBall(first) { try FileManager.default.moveItem(at: $0, to: trash) }
    #expect(model.selected == second)
    #expect(model.paths == [second])
    #expect(!model.hasUnsavedConfigs)
    #expect(model.configDrafts[first] == nil)
    #expect(FileManager.default.fileExists(atPath: trash.appending(path: "ball.json").path))
    #expect(try String(contentsOf: external, encoding: .utf8) == "external")
    #expect(FileManager.default.fileExists(atPath: second + "/ball.json"))
    let saved = try await PreferenceStore(url: root.appending(path: "prefs.json")).load()
    #expect(saved.ballPaths == [second])
    #expect(saved.selected == second)
}

@Test @MainActor func failedTrashAndRunningOperationsPreserveTheBall() async throws {
    let (model, root, first, _) = try managementFixture()
    defer { try? FileManager.default.removeItem(at: root) }
    await #expect(throws: BallError.self) {
        try await model.trashBall(first) { _ in throw BallError("Trash unavailable") }
    }
    #expect(model.selected == first)
    #expect(FileManager.default.fileExists(atPath: first + "/ball.json"))
    for state in 0..<4 {
        model.busy = state == 0
        model.mediaBusy = state == 1
        model.processRunning = state == 2
        model.configSaving = state == 3
        await #expect(throws: BallError.self) { try await model.renameBall(first, to: "Blocked") }
        await #expect(throws: BallError.self) {
            try await model.trashBall(first) { _ in Issue.record("Trash ran while blocked") }
        }
        model.busy = false
        model.mediaBusy = false
        model.processRunning = false
        model.configSaving = false
    }
}

@Test @MainActor func trashRejectsSymlinksAndOrdinaryFolders() throws {
    let (_, root, first, _) = try managementFixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let link = root.appending(path: "Link.2ineball")
    try FileManager.default.createSymbolicLink(atPath: link.path, withDestinationPath: first)
    #expect(throws: BallError.self) { try AppModel.managedBallURL(link.path) }
    #expect(throws: BallError.self) { try AppModel.managedBallURL(root.path) }
}

@Test @MainActor func replacedBallIsNotTrashedAfterConfirmation() async throws {
    let (model, root, first, _) = try managementFixture()
    defer { try? FileManager.default.removeItem(at: root) }
    model.beginTrashBall(first)
    let request = try #require(model.ballToTrash)
    try FileManager.default.moveItem(atPath: first, toPath: first + ".original")
    _ = try NativeBallCore.request(
        DesktopRequest("command", ball: first, arguments: ["init"]), as: BallSnapshot.self)
    await #expect(throws: BallError.self) {
        try await model.trashBall(first, expectedIdentity: request.identity) { _ in
            Issue.record("Trashed a replacement package")
        }
    }
    #expect(FileManager.default.fileExists(atPath: first + "/ball.json"))
    #expect(FileManager.default.fileExists(atPath: first + ".original/ball.json"))
}
