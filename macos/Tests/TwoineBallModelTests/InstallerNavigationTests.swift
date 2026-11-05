import AppKit
import BallCore
import Foundation
import SwiftUI
import Testing
@testable import TwoineBall

@MainActor private func settle(_ model: AppModel, until predicate: () -> Bool = { true }) async throws {
    let deadline = ContinuousClock.now + .seconds(15)
    while model.busy || !predicate() {
        guard ContinuousClock.now < deadline else { throw BallError("Navigation/installer operation did not settle") }
        try await Task.sleep(for: .milliseconds(20))
    }
}

@Test @MainActor func allNavigationDestinationsRemainVisibleAndOrdered() throws {
    #expect(Page.allCases.map(\.rawValue) == ["Drives", "OS/2 Components", "Application Installation", "Applications", "Settings", "Logs"])
    let host = NSHostingView(rootView: BallNavigation(page: .constant(.drives)))
    host.frame = NSRect(x: 0, y: 0, width: 760, height: 64)
    host.layoutSubtreeIfNeeded()
    func controls(_ view: NSView) -> [NSView] { [view] + view.subviews.flatMap(controls) }
    let all = controls(host)
    let segments = try #require(all.compactMap { $0 as? NSSegmentedControl }.first)
    #expect(segments.segmentCount == 6)
    #expect((0..<6).map { segments.label(forSegment: $0) } == Page.allCases.map { Optional($0.rawValue) })
    #expect(segments.fittingSize.width <= 760)
    #expect(!all.contains { $0 is NSPopUpButton })
}

@Test @MainActor func installerDropdownShowsSuggestionsAndKeepsCustomPaths() throws {
    let paths = InstallerSuggestions.sorted([#"D:\ENGLISH\INSTALL.EXE"#, #"D:\SETUP.CMD"#, #"D:\SETUP.EXE"#, #"d:\setup.exe"#])
    #expect(paths == [#"D:\SETUP.EXE"#, #"D:\SETUP.CMD"#, #"D:\ENGLISH\INSTALL.EXE"#])
    #expect(InstallerSuggestions.selectedProgram(current: "", previousSuggestion: nil, suggestions: paths, reset: false) == paths[0])
    #expect(InstallerSuggestions.selectedProgram(current: #"D:\OTHER\CUSTOM.CMD"#, previousSuggestion: paths[0], suggestions: paths, reset: false) == #"D:\OTHER\CUSTOM.CMD"#)
    #expect(InstallerSuggestions.selectedProgram(current: #"D:\OLD.EXE"#, previousSuggestion: nil, suggestions: paths, reset: true) == paths[0])
    var selected = ""
    let view = InstallerProgramPicker(program: Binding(get: { selected }, set: { selected = $0 }), suggestions: paths)
    let coordinator = view.makeCoordinator()
    coordinator.options = paths
    let combo = NSComboBox()
    combo.addItems(withObjectValues: paths); combo.selectItem(at: 1)
    coordinator.comboBoxSelectionDidChange(Notification(name: NSComboBox.selectionDidChangeNotification, object: combo))
    #expect(selected == paths[1])
    combo.stringValue = #"D:\CUSTOM.CMD"#
    coordinator.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: combo))
    #expect(selected == #"D:\CUSTOM.CMD"#)
}

@Test @MainActor func startingPageFollowsActualBallContentsAndRefreshPreservesNavigation() async throws {
    let root = URL(filePath: "/private/tmp/2ine-start-page-" + UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = root.appending(path: "New.2ineball")
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"))
    model.open(ball, create: true); try await settle(model)
    #expect(model.page == .drives)
    model.open(ball, create: false); try await settle(model)
    #expect(model.page == .systemComponents)
    try Data("original command fixture".utf8).write(to: ball.appending(path: "drive_c/OS2/CMD.EXE"))
    model.open(ball, create: false); try await settle(model)
    #expect(model.page == .installation)
    var executable = Data(count: 84)
    executable[0] = 77; executable[1] = 90; executable[60] = 64
    executable[64] = 76; executable[65] = 88
    try executable.write(to: ball.appending(path: "drive_c/APP.EXE"))
    var request = DesktopRequest("save-application", ball: ball.path, arguments: [])
    request.name = "Valid"; request.program = #"C:\APP.EXE"#
    _ = try NativeBallCore.request(request, as: BallSnapshot.self)
    model.open(ball, create: false); try await settle(model)
    #expect(model.page == .applications)
    model.page = .settings; model.refresh(); try await settle(model)
    #expect(model.page == .settings)
    try FileManager.default.removeItem(at: ball.appending(path: "drive_c/APP.EXE"))
    model.open(ball, create: false); try await settle(model)
    #expect(model.page == .installation)
}

@Test(arguments: [false, true]) @MainActor func commandFileWaitsForComponentsThenResumesTheRequestedInstall(cancelled: Bool) async throws {
    let root = URL(filePath: "/private/tmp/2ine-command-ready-" + UUID().uuidString)
    let runtime = root.appending(path: "Runtime")
    try FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }
    let loader = runtime.appending(path: "lx_loader")
    try Data("#!/bin/sh\nprintf started > \"$TWOINE_BALL/started\"\n".utf8).write(to: loader)
    try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: loader.path)
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"), runtimeURL: runtime)
    let ball = root.appending(path: "Command.2ineball")
    model.open(ball, create: true); try await settle(model)
    try Data("echo fixture".utf8).write(to: ball.appending(path: "drive_c/INSTALL.CMD"))
    model.execute(["install", #"C:\INSTALL.CMD"#])
    try await settle(model, until: { model.pendingLaunch != nil && !model.processRunning })
    #expect(model.page == .systemComponents && model.componentNavigation?.category == .cmd)
    #expect(!FileManager.default.fileExists(atPath: ball.appending(path: "started").path))
    if cancelled {
        model.cancelComponentOperation(ComponentJob(ball: ball.path, kind: "install", paths: []))
    }
    try Data("command interpreter fixture".utf8).write(to: ball.appending(path: "drive_c/OS2/CMD.EXE"))
    await model.componentOperationDidFinish(ball: ball.path)
    if cancelled {
        #expect(model.pendingLaunch?.ready == true)
        #expect(!FileManager.default.fileExists(atPath: ball.appending(path: "started").path))
        model.continuePendingLaunch(ball: ball.path)
    }
    try await settle(model, until: { !model.hasRunningSessions })
    #expect(model.pendingLaunch == nil)
    #expect(try String(contentsOf: ball.appending(path: "started"), encoding: .utf8) == "started")
}

@Test @MainActor func cachedMissingLibraryAdviceDoesNotScanOrRedirectALaunch() async throws {
    let root = URL(filePath: "/private/tmp/2ine-cached-launch-" + UUID().uuidString)
    let runtime = root.appending(path: "Runtime")
    try FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: root) }
    let loader = runtime.appending(path: "lx_loader")
    try Data("#!/bin/sh\nprintf started > \"$TWOINE_BALL/started\"\n".utf8).write(to: loader)
    try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: loader.path)
    let model = AppModel(preferencesURL: root.appending(path: "prefs.json"), runtimeURL: runtime)
    let ball = root.appending(path: "Cached.2ineball")
    model.open(ball, create: true); try await settle(model)
    var executable = Data(count: 84)
    executable[0] = 77; executable[1] = 90; executable[60] = 64
    executable[64] = 76; executable[65] = 88
    try executable.write(to: ball.appending(path: "drive_c/APP.EXE"))
    let index = ball.appending(path: "cache/dependency-index.json")
    try FileManager.default.createDirectory(at: index.deletingLastPathComponent(), withIntermediateDirectories: true)
    let stored = Data(#"{"schema":1,"generated":"fixture","binaries":[{"path":"C:\\APP.EXE","format":"LX","valid":true,"modules":["NETAPI"]}]}"#.utf8)
    try stored.write(to: index)
    model.page = .applications
    model.execute(["run", #"C:\APP.EXE"#])
    try await settle(model, until: { !model.hasRunningSessions })
    #expect(model.page == .activity)
    #expect(model.pendingLaunch == nil)
    #expect(FileManager.default.fileExists(atPath: ball.appending(path: "started").path))
    #expect(try Data(contentsOf: index) == stored)
    #expect(model.currentSession?.output.contains("NETAPI") == true)
}
