import BallCore
import Foundation
import Testing
@testable import TwoineBall

@Test @MainActor func importingArchiveSelectsNewBallAndPreservesSelectionOnFailure() async throws {
    let files = FileManager.default
    let root = files.temporaryDirectory.appending(path: "2ine-archive-model-" + UUID().uuidString)
    try files.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? files.removeItem(at: root) }
    let source = root.appending(path: "Source.2ineball")
    let archive = root.appending(path: "Source.zip")
    let destination = root.appending(path: "Imported.2ineball")
    _ = try NativeBallCore.request(DesktopRequest("command", ball: source.path, arguments: ["init"]), as: BallSnapshot.self)
    var request = DesktopRequest("export-ball", ball: source.path); request.path = archive.path
    _ = try NativeBallCore.request(request, as: String.self)
    let model = AppModel(preferencesURL: root.appending(path: "preferences.json"))
    model.importBall(archive, to: destination)
    #expect(model.busy)
    var deadline = ContinuousClock.now + .seconds(10)
    while model.busy {
        try #require(ContinuousClock.now < deadline)
        try await Task.sleep(for: .milliseconds(1))
    }
    #expect(model.error == nil)
    #expect(model.snapshot?.roots["C"]?.hasSuffix("Imported.2ineball/drive_c") == true)
    let selected = try #require(model.selected)
    #expect(model.paths == [selected])
    #expect(model.operation == "Ready")
    model.importBall(archive, to: destination)
    deadline = ContinuousClock.now + .seconds(10)
    while model.busy {
        try #require(ContinuousClock.now < deadline)
        try await Task.sleep(for: .milliseconds(1))
    }
    #expect(model.error?.contains("existing items") == true)
    #expect(model.selected == selected)
    #expect(model.snapshot != nil)
    #expect(model.paths == [selected])
}

@Test @MainActor func importedMissingFolderCanBeReconnectedAndUndone() async throws {
    let files = FileManager.default
    let root = files.temporaryDirectory.appending(path: "2ine-reconnect-model-" + UUID().uuidString)
    try files.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? files.removeItem(at: root) }
    let source = root.appending(path: "Source.2ineball")
    let archive = root.appending(path: "Source.zip")
    let destination = root.appending(path: "Imported.2ineball")
    let oldFolder = root.appending(path: "Old Folder")
    let newFolder = root.appending(path: "New Folder")
    try files.createDirectory(at: oldFolder, withIntermediateDirectories: true)
    try files.createDirectory(at: newFolder, withIntermediateDirectories: true)
    _ = try NativeBallCore.request(DesktopRequest("command", ball: source.path, arguments: ["init"]), as: BallSnapshot.self)
    _ = try NativeBallCore.request(DesktopRequest("command", ball: source.path, arguments: ["map", "E", oldFolder.path]), as: BallSnapshot.self)
    var request = DesktopRequest("export-ball", ball: source.path); request.path = archive.path
    _ = try NativeBallCore.request(request, as: String.self)
    try files.removeItem(at: oldFolder)
    let model = AppModel(preferencesURL: root.appending(path: "preferences.json"))
    model.undoManager.groupsByEvent = false
    model.importBall(archive, to: destination)
    var deadline = ContinuousClock.now + .seconds(10)
    while model.busy {
        try #require(ContinuousClock.now < deadline)
        try await Task.sleep(for: .milliseconds(1))
    }
    #expect(model.error == nil)
    #expect(model.snapshot?.unavailableDrives?["E"] != nil)
    #expect(model.snapshot?.roots["E"] == nil)
    #expect(model.page == .systemComponents)
    #expect(model.browseDrive == "E")
    model.chooseMountpoint("E")
    #expect(model.mapping && model.mappingDrive == "E")
    try await model.mapDrive("E", directory: newFolder.path)
    #expect(model.snapshot?.roots["E"]?.hasSuffix("New Folder") == true)
    #expect(model.snapshot?.unavailableDrives?["E"] == nil)
    model.undoManager.undo()
    deadline = ContinuousClock.now + .seconds(10)
    while model.busy {
        try #require(ContinuousClock.now < deadline)
        try await Task.sleep(for: .milliseconds(1))
    }
    #expect(model.error == nil)
    #expect(model.snapshot?.roots["E"] == nil)
    #expect(model.snapshot?.unavailableDrives?["E"] != nil)
}
