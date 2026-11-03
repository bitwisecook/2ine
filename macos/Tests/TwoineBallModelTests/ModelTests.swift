import AppKit
import BallCore
import Foundation
import Darwin
import Testing
@testable import TwoineBall

@MainActor
private func fixture() throws -> (AppModel, URL) {
    var root = FileManager.default.temporaryDirectory.appending(path: "2ine-model-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    let canonical = try #require(realpath(root.path, nil))
    root = URL(filePath: String(cString: canonical)); free(canonical)
    let ball = root.appending(path: "Test.2ineball").path
    let snapshot = try NativeBallCore.request(DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"))
    model.selected = ball; model.snapshot = snapshot
    model.undoManager.groupsByEvent = false
    return (model, root)
}

@MainActor
private func waitForOperation(_ model: AppModel) async throws {
    let deadline = ContinuousClock.now + .seconds(5)
    while model.busy {
        guard ContinuousClock.now < deadline else { throw BallError("Operation did not finish") }
        try await Task.sleep(for: .milliseconds(1))
    }
}

@Test @MainActor func mappingFailurePreservesHistoryAndSuccessfulRemappingSupportsUndoRedo() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let first = root.appending(path: "First"); let second = root.appending(path: "Second")
    try FileManager.default.createDirectory(at: first, withIntermediateDirectories: true)
    try FileManager.default.createDirectory(at: second, withIntermediateDirectories: true)
    await #expect(throws: BallError.self) { try await model.mapDrive("D", directory: root.appending(path: "missing").path) }
    #expect(!model.undoManager.canUndo)
    try await model.mapDrive("D", directory: first.path)
    try await model.mapDrive("D", directory: second.path)
    #expect(model.undoManager.undoActionName == "Map D:")
    model.undoManager.undo()
    #expect(!model.undoManager.canUndo)
    try await waitForOperation(model)
    #expect(model.snapshot?.roots["D"] == first.path)
    #expect(model.undoManager.canRedo)
    model.undoManager.redo(); try await waitForOperation(model)
    #expect(model.snapshot?.roots["D"] == second.path)
    model.undoManager.undo(); try await waitForOperation(model)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.roots["D"] == nil)
    #expect(model.error == nil)
}

@Test @MainActor func undoToUnavailableFolderKeepsDriveEjectedAndCanBeRedone() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let first = root.appending(path: "First"); let second = root.appending(path: "Second")
    try FileManager.default.createDirectory(at: first, withIntermediateDirectories: true)
    try FileManager.default.createDirectory(at: second, withIntermediateDirectories: true)
    try await model.mapDrive("D", directory: first.path)
    try await model.mapDrive("D", directory: second.path)
    try FileManager.default.removeItem(at: first)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.error == nil)
    #expect(model.undoManager.canRedo)
    #expect(model.undoManager.canUndo)
    #expect(model.snapshot?.roots["D"] == nil)
    #expect(model.snapshot?.unavailableDrives?["D"] == first.path)
    model.undoManager.redo(); try await waitForOperation(model)
    #expect(model.snapshot?.roots["D"] == second.path)
}

@Test @MainActor func settingsDraftsSurviveOtherChangesAndStayWithTheirBall() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = try #require(model.selected)
    let draft = model.settingsDraft
    draft.envName = "PENDING"; draft.envValue = "unfinished"
    model.command(["config", "system.beep_volume", "0.5"])
    try await waitForOperation(model)
    #expect(model.settingsDraft === draft)
    #expect(draft.envValue == "unfinished")
    model.selected = root.appending(path: "Other.2ineball").path
    #expect(model.settingsDraft.envName.isEmpty)
    model.selected = ball
    #expect(model.settingsDraft.envName == "PENDING")
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.system["beep_volume"] == nil)
    #expect(draft.envValue == "unfinished")
}

@Test @MainActor func emptyDefaultFolderUsesDocuments() {
    let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
    #expect(AppModel.defaultBallFolder(nil) == documents)
    #expect(AppModel.defaultBallFolder("") == documents)
    #expect(AppModel.defaultBallFolder("/tmp/Ball") == URL(filePath: "/tmp/Ball"))
}

@Test @MainActor func runtimeSelectionAndResetSupportUndoRedo() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let external = root.appending(path: "External")
    try FileManager.default.createDirectory(at: external, withIntermediateDirectories: true)
    try Data("runtime fixture".utf8).write(to: external.appending(path: "lx_loader"))
    let ball = try #require(model.selected)
    try await model.setRuntime(external.path, ball: ball)
    #expect(model.snapshot?.manifest.runtime["build"] == external.path)
    model.useBundledRuntime(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.runtime["build"] == nil)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.runtime["build"] == external.path)
    model.undoManager.redo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.runtime["build"] == nil)
    #expect(model.error == nil)
}

@Test @MainActor func imageMappingsPersistAndUndoRestoresThePreviousSourceType() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    var iso = Data(count: 24 * 2048)
    func bytes(_ offset: Int, _ values: [UInt8]) { iso.replaceSubrange(offset..<(offset + values.count), with: values) }
    let descriptor = 16 * 2048
    bytes(descriptor, [1, 67, 68, 48, 48, 49, 1])
    bytes(descriptor + 80, [24, 0, 0, 0, 0, 0, 0, 24])
    bytes(descriptor + 128, [0, 8, 8, 0])
    bytes(descriptor + 156, [34])
    bytes(descriptor + 158, [20, 0, 0, 0, 0, 0, 0, 20])
    bytes(descriptor + 166, [0, 8, 0, 0, 0, 0, 8, 0])
    bytes(descriptor + 181, [2])
    bytes(descriptor + 188, [1, 0])
    bytes(17 * 2048, [255, 67, 68, 48, 48, 49, 1])
    let image = root.appending(path: "Media.iso")
    try iso.write(to: image)
    try await model.mapDrive("D", directory: root.path)
    try await model.mapDrive("D", directory: image.path, image: true)
    #expect(model.snapshot?.manifest.drives["D"]?.image == image.path)
    #expect(model.snapshot?.roots["D"]?.hasPrefix("/@2ine-device/") == true)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.drives["D"]?.image == nil)
    #expect(model.snapshot?.roots["D"] == root.path)
    model.undoManager.redo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.drives["D"]?.image == image.path)
    model.command(["unmap", "D"]); try await waitForOperation(model)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.drives["D"]?.image == image.path)
    #expect(model.error == nil)
}

@Test @MainActor func ejectedDevicesKeepTheirLetterAndUndoRestoresMediaState() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    try await model.changeMedia(["device", "D", "cdrom"])
    let deviceRoot = try #require(model.snapshot?.roots["D"])
    #expect(model.snapshot?.manifest.drives["D"]?.device == .cdrom)
    #expect(model.snapshot?.manifest.drives["D"]?.image == nil)
    #expect(model.availableMediaLetter(.cdrom) == "E")
    try await model.changeMedia(["eject", "D"])
    #expect(model.snapshot?.roots["D"] == deviceRoot)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.drives["D"]?.device == .cdrom)
    model.busy = true; model.processRunning = true
    #expect(model.canChangeMedia)
    try await model.changeMedia(["eject", "D"])
    #expect(model.busy && model.undoManager.blocked)
    model.processRunning = false
    #expect(!model.canChangeMedia)
    model.busy = false
}
@Test @MainActor func installerSuggestionsIncludeCommandFilesWithoutRestrictingBrowsing() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let media = root.appending(path: "Media")
    try FileManager.default.createDirectory(at: media.appending(path: "ENGLISH"), withIntermediateDirectories: true)
    for name in ["setup.cmd", "ENGLISH/install.exe", "unusual-tool.cmd", "notes.txt"] {
        try Data("file".utf8).write(to: media.appending(path: name))
    }
    try await model.mapDrive("D", directory: media.path)
    let candidates = try await model.installerCandidates(letter: "D")
    #expect(candidates.contains("D:\\setup.cmd"))
    #expect(candidates.first == "D:\\ENGLISH\\install.exe")
    let listing = try await model.browse("D:\\", ball: try #require(model.selected))
    #expect(listing.entries.contains { $0.name == "unusual-tool.cmd" && !$0.directory })
}

@Test @MainActor func floppyCreationProtectionAndLockingPersistAndSupportUndo() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let image = root.appending(path: "Blank.img")
    try await model.createFloppy(letter: "A", path: image.path, kib: 1440)
    #expect(try Data(contentsOf: image).count == 1440 * 1024)
    #expect(model.snapshot?.manifest.drives["A"]?.device == .floppy)
    #expect(model.snapshot?.manifest.drives["A"]?.isReadOnly == false)
    try await model.changeMedia(["media-readonly", "A", "on"])
    #expect(model.snapshot?.manifest.drives["A"]?.isReadOnly == true)
    try await model.changeMedia(["device-lock", "A", "on"])
    await #expect(throws: BallError.self) { try await model.changeMedia(["eject", "A"]) }
    #expect(model.snapshot?.manifest.drives["A"]?.image == image.path)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.drives["A"]?.isLocked == false)
    model.undoManager.undo(); try await waitForOperation(model)
    #expect(model.snapshot?.manifest.drives["A"]?.isReadOnly == false)
    try await model.changeMedia(["eject", "A"])
    #expect(model.snapshot?.manifest.drives["A"]?.image == nil)
    #expect(FileManager.default.fileExists(atPath: image.path))
}

@Test @MainActor func newFilesystemImagesAreDiskDevicesWithPersistentAccessModes() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    for (filesystem, mib) in [("fat32", 64), ("exfat", 8), ("hpfs", 8)] {
        let image = root.appending(path: filesystem + ".img")
        try await model.createDisk(letter: "F", path: image.path, filesystem: filesystem, mib: mib)
        #expect(try image.resourceValues(forKeys: [.fileSizeKey]).fileSize == mib * 1024 * 1024)
        #expect(model.snapshot?.manifest.drives["F"]?.device == .disk)
        #expect(model.snapshot?.manifest.drives["F"]?.isReadOnly == false)
        #expect(model.imageSource("F") == image.path)
        try await model.changeMedia(["media-readonly", "F", "on"])
        #expect(model.snapshot?.manifest.drives["F"]?.isReadOnly == true)
        model.undoManager.undo(); try await waitForOperation(model)
        #expect(model.snapshot?.manifest.drives["F"]?.isReadOnly == false)
    }
}

@Test @MainActor func supportImportUpdatesTheSelectedBallAndPreservesApplicationMedia() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let media = root.appending(path: "OS2 Media")
    try FileManager.default.createDirectory(at: media, withIntermediateDirectories: true)
    var command = Data(count: 100)
    command.replaceSubrange(0..<2, with: [77, 90])
    command.replaceSubrange(60..<64, with: [64, 0, 0, 0])
    command.replaceSubrange(64..<66, with: [76, 88])
    try command.write(to: media.appending(path: "CMD.EXE"))
    try Data([255, 255] + Array("SYSLEVEL".utf8)).write(to: media.appending(path: "SYSLEVEL.OS2"))
    try Data([255] + Array("MKMSGF".utf8) + [0]).write(to: media.appending(path: "OSO001.MSG"))
    try await model.mapDrive("D", directory: media.path)
    let before = try #require(model.snapshot?.manifest.drives["D"]?.source)
    #expect(try await model.os2Support().complete == false)
    let status = try await model.installOS2Support([media.path])
    #expect(status.complete)
    #expect(status.imported?.count == 3)
    #expect(model.snapshot?.os2Support?.complete == true)
    #expect(model.snapshot?.manifest.drives["D"]?.source == before)
    #expect(model.busy == false)
}

@Test @MainActor func os2ImportReviewDefaultsToCommandSupportAndPreservesOptionalFiles() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = try #require(model.selected)
    let media = root.appending(path: "Media")
    try FileManager.default.createDirectory(at: media.appending(path: "OS2"), withIntermediateDirectories: true)
    try FileManager.default.createDirectory(at: media.appending(path: "PSFONTS"), withIntermediateDirectories: true)
    var executable = Data(repeating: 0, count: 100)
    executable[0] = 77; executable[1] = 90; executable[60] = 64
    executable[64] = 76; executable[65] = 88
    try executable.write(to: media.appending(path: "OS2/CMD.EXE"))
    try Data("font".utf8).write(to: media.appending(path: "PSFONTS/TEST.PFB"))
    let source = try await model.openOS2Source([media.path], ball: ball)
    #expect(source.sources == [media.path])
    let plan = try await model.planOS2Extraction(source.sources, ball: ball)
    #expect(OS2ExtractionCategory.defaults == [.cmd])
    #expect(plan.selectedFiles(OS2ExtractionCategory.defaults).map(\.path) == ["C:\\OS2\\CMD.EXE"])
    #expect(plan.selectedFiles([.full]).count == 2)
    #expect(!FileManager.default.fileExists(atPath: ball + "/drive_c/OS2/CMD.EXE"))
    let imported = try await model.installOS2Support(source.sources, ball: ball)
    #expect(imported.imported?.count == 1)
    #expect(FileManager.default.fileExists(atPath: ball + "/drive_c/OS2/CMD.EXE"))
    #expect(!FileManager.default.fileExists(atPath: ball + "/drive_c/PSFONTS/TEST.PFB"))
    #expect(!model.busy)
    let fonts = try await model.installOS2Support(source.sources, categories: [.fonts], ball: ball)
    #expect(fonts.imported?.count == 1)
}

@Test @MainActor func liveConfigUpdatesFollowDiskAndPreserveConflictingDraft() async throws {
    let (model, root) = try fixture()
    defer { model.processRunning = false; try? FileManager.default.removeItem(at: root) }
    let ball = try #require(model.selected)
    let path = URL(filePath: ball).appending(path: "drive_c/CONFIG.SYS")
    let original = model.configText
    try Data((original + "REM café\r\n").unicodeScalars.map { UInt8($0.value) }).write(to: path)
    model.processRunning = true
    let deadline = ContinuousClock.now + .seconds(4)
    while !model.configText.contains("café") && ContinuousClock.now < deadline { try await Task.sleep(for: .milliseconds(30)) }
    #expect(model.configText == original + "REM café\r\n")
    model.configText += "SET DRAFT=kept\r\n"
    let draft = model.configText
    let installer = original + "SET INSTALLER=changed\r\n"
    try Data(installer.utf8).write(to: path)
    await model.refreshConfigFromDisk(ball)
    #expect(model.configText == draft)
    #expect(model.configConflict)
    model.configSys(draft, expected: model.configBase)
    while model.configSaving { try await Task.sleep(for: .milliseconds(1)) }
    #expect(model.error?.contains("changed on disk") == true)
    #expect(model.configText == draft)
    #expect(try String(contentsOf: path, encoding: .utf8) == installer)
    model.discardConfigDraft()
    await model.refreshConfigFromDisk(ball)
    #expect(model.configText == installer)
    #expect(!model.configConflict)
}

@Test @MainActor func cleanConfigDraftFollowsExternalEditsAndProfileEditingRequiresStoppedGuest() async throws {
    let (model, root) = try fixture()
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = try #require(model.selected)
    let original = model.configText
    model.configText = original + "REM temporary\r\n"
    model.configText = original
    let updated = original + "SET EXTERNAL=visible\r\n"
    try Data(updated.utf8).write(to: URL(filePath: ball).appending(path: "drive_c/CONFIG.SYS"))
    await model.refreshConfigFromDisk(ball)
    #expect(model.configText == updated)
    #expect(!model.configDirty)
    let profile = try await model.configuration("registry", ball: ball)
    model.processRunning = true
    await #expect(throws: BallError.self) {
        try await model.saveConfiguration("registry", ball: ball, revision: profile.revision, application: "hkcu\\software\\fixture", key: "v:test", data: "0400000001000000")
    }
    model.processRunning = false
    let saved = try await model.saveConfiguration("registry", ball: ball, revision: profile.revision, application: "hkcu\\software\\fixture", key: "v:test", data: "0400000001000000")
    #expect(saved.entries.contains { $0.key == "v:test" && $0.registryType == 4 })
}
