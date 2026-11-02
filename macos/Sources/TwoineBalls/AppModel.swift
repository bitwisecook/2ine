import AppKit
#if canImport(BallCore)
import BallCore
#endif
import Observation
import UniformTypeIdentifiers
import SwiftUI

enum Page: String, CaseIterable, Identifiable {
    case drives = "Drives", applications = "Applications", settings = "Settings", config = "CONFIG.SYS", activity = "Activity"
    var id: String { rawValue }
}
struct InstallCompletion: Identifiable {
    let id = UUID()
    let ball: String
    let letter: String
    let code: Int32
    let signalled: Bool
}
enum ProgramAction: String, Identifiable {
    case run = "Run Executable", install = "Install Software", save = "Add Application"
    var id: String { rawValue }
}

@MainActor
final class OperationUndoManager: UndoManager {
    var blocked = false
    override var canUndo: Bool { !blocked && super.canUndo }
    override var canRedo: Bool { !blocked && super.canRedo }
    override func undo() { if !blocked { super.undo() } }
    override func redo() { if !blocked { super.redo() } }
}

@MainActor
private final class BallUndoEntry {
    let request: DesktopRequest
    let inverse: DesktopRequest
    let name: String
    init(request: DesktopRequest, inverse: DesktopRequest, name: String) {
        self.request = request; self.inverse = inverse; self.name = name
    }
}

@Observable
final class SettingsDraft {
    var key = "system.beep_volume"
    var value = ""
    var envName = ""
    var envValue = ""
}

struct BallViewActions {
    var launch: (() -> Void)?
    var edit: (() -> Void)?
    var remove: (() -> Void)?
    var importFiles: (() -> Void)?
    var unmap: (() -> Void)?
    var setDefaultFolder: (() -> Void)?
    var changeMountpoint: (() -> Void)?
}
struct BallViewActionsKey: FocusedValueKey { typealias Value = BallViewActions }
extension FocusedValues {
    var ballViewActions: BallViewActions? {
        get { self[BallViewActionsKey.self] }
        set { self[BallViewActionsKey.self] = newValue }
    }
}

@MainActor @Observable
final class AppModel {
    var paths: [String] = []
    var selected: String? {
        didSet {
            if let selected, settingsDrafts[selected] == nil { settingsDrafts[selected] = SettingsDraft() }
        }
    }
    var snapshot: BallSnapshot?
    var page: Page = .drives
    let undoManager = OperationUndoManager()
    var busy = false { didSet { undoManager.blocked = busy || mediaBusy } }
    var mediaBusy = false { didSet { undoManager.blocked = busy || mediaBusy } }
    var canChangeMedia: Bool { selected != nil && !mediaBusy && (!busy || processRunning) }
    var installCompletion: InstallCompletion?
    var processRunning = false
    var stopRequested = false
    var stopConfirmation = false
    var forceStopConfirmation = false
    var settingsDrafts: [String: SettingsDraft] = [:]
    var settingsDraft: SettingsDraft {
        guard let selected else { return SettingsDraft() }
        if let draft = settingsDrafts[selected] { return draft }
        let draft = SettingsDraft(); settingsDrafts[selected] = draft
        return draft
    }
    var activity = "No process has run in this session."
    var audioEditor = false
    var operation = "Ready"
    var log: URL?
    var exitCode: Int32?
    var error: String?
    var programAction: ProgramAction?
    var editingApplication: String?
    var applicationIcons: [String: NSImage] = [:]
    var driveNotice = ""
    var browseDrive: String?
    var mapping = false
    var mappingDrive: String?
    func chooseMountpoint(_ letter: String) {
        guard !busy, !mediaBusy, letter != "C" else { return }
        mappingDrive = letter; mapping = true
    }
    var creatingFloppy: String?
    var creatingDisk: String?
    var revision = 0
    var configDrafts: [String: String] = [:]
    var configBases: [String: String] = [:]
    var configText: String {
        get { selected.flatMap { configDrafts[$0] } ?? snapshot?.configSys ?? "" }
        set {
            guard let selected else { return }
            if configBases[selected] == nil { configBases[selected] = snapshot?.configSys ?? "" }
            configDrafts[selected] = newValue
        }
    }
    var configBase: String { selected.flatMap { configBases[$0] } ?? snapshot?.configSys ?? "" }
    var configDirty: Bool { configText != configBase }
    var hasUnsavedConfigs: Bool { configDrafts.contains { $0.value != configBases[$0.key] } }
    func discardAllConfigDrafts() { configDrafts.removeAll(); configBases.removeAll() }
    func saveAllConfigDrafts() async throws {
        guard !busy, !mediaBusy else { throw BallError("Wait for the operation to finish.") }
        busy = true
        defer { busy = false }
        for (path, text) in configDrafts where text != configBases[path] {
            let value = try await backend.request(DesktopRequest("save-config-sys", ball: path, expected: configBases[path], text: text), as: BallSnapshot.self)
            if path == selected { snapshot = value; revision += 1 }
            configDrafts.removeValue(forKey: path); configBases.removeValue(forKey: path)
        }
    }
    private let backend: BallBackend
    private let preferences: PreferenceStore
    private var prefs = Preferences()
    private let bundledRuntime: String
    var build: String { snapshot?.manifest.runtime["build"] ?? ProcessInfo.processInfo.environment["TWOINE_BUILD"] ?? bundledRuntime }
    static func displayName(_ path: String) -> String {
        let url = URL(filePath: path)
        return (url.pathExtension == "2ineball" ? url.deletingPathExtension() : url).lastPathComponent
    }
    var title: String { selected.map(Self.displayName) ?? "2ine" }
    var runtimeDescription: String {
        build == bundledRuntime ? "Bundled 2ine Runtime" : "Runtime: " + URL(filePath: build).lastPathComponent
    }

    init(preferencesURL: URL? = nil) {
        let resources = ProcessInfo.processInfo.environment["TWOINE_MACOS_RESOURCES"].map { URL(filePath: $0) }
            ?? (Bundle.main.object(forInfoDictionaryKey: "TwoineDeveloperResources") as? String).map { URL(filePath: $0) }
            ?? Bundle.main.resourceURL ?? Bundle.main.bundleURL
        bundledRuntime = resources.appending(path: "Runtime").path
        backend = BallBackend()
        let override = ProcessInfo.processInfo.environment["TWOINE_MACOS_PREFERENCES"]
        let path = override.map { URL(filePath: $0) } ?? FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/2ine/balls-macos.json")
        preferences = PreferenceStore(url: preferencesURL ?? path)
    }

    func start() {
        perform {
            self.prefs = try await self.preferences.load()
            let discovered = try await self.backend.request(DesktopRequest("discover"), as: [String].self)
            self.paths = Array(Set(self.prefs.balls + discovered)).sorted()
            let initial = ProcessInfo.processInfo.environment["TWOINE_MACOS_BALL"]
            self.selected = initial ?? self.prefs.selected ?? self.paths.first
            if let selected = self.selected {
                if !self.paths.contains(selected) { self.paths.append(selected) }
                try await self.reload(selected)
            }
        }
    }

    func perform(_ work: @escaping @MainActor () async throws -> Void) {
        guard !busy, !mediaBusy else { return }
        busy = true
        Task {
            defer { busy = false }
            do { try await work() }
            catch { self.error = error.localizedDescription }
        }
    }

    private func reload(_ ball: String) async throws {
        snapshot = try await backend.request(DesktopRequest("snapshot", ball: ball), as: BallSnapshot.self)
        await loadIcons(ball)
        revision += 1
        prefs.balls = paths; prefs.selected = ball
        try await preferences.save(prefs)
    }

    func select(_ path: String) {
        guard !busy, !mediaBusy else { return }
        selected = path; snapshot = nil
        perform { try await self.reload(path) }
    }
    func refresh() {
        guard let selected else { start(); return }
        perform { try await self.reload(selected) }
    }
    func chooseBall(create: Bool) {
        guard !busy, !mediaBusy else { return }
        if create {
            let panel = NSSavePanel()
            panel.title = "Create Ball"; panel.nameFieldLabel = "Ball name:"
            panel.nameFieldStringValue = "My OS2 Ball.2ineball"; panel.canCreateDirectories = true
            panel.allowedContentTypes = [UTType(exportedAs: "io.github.bitwisecook.2ine.ball", conformingTo: .package)]
            panel.prompt = "Create"
            let defaultFolder = UserDefaults.standard.string(forKey: "ballFolder")
            panel.directoryURL = Self.defaultBallFolder(defaultFolder)
            if panel.runModal() == .OK, let url = panel.url { open(url, create: true) }
        } else if let url = Self.pick(directory: true, title: "Open Ball") { open(url, create: false) }
    }
    func open(_ url: URL, create: Bool) {
        guard !busy, !mediaBusy else { return }
        if !create && url.pathExtension.lowercased() == "zip" { chooseImportBall(url); return }
        perform {
            let path = url.resolvingSymlinksInPath().path
            let message = DesktopRequest(create ? "command" : "snapshot", ball: path,
                                         arguments: create ? ["init"] : nil)
            var value = try await self.backend.request(message, as: BallSnapshot.self)
            if create {
                for (letter, kind) in [("A", "floppy"), ("D", "cdrom")] {
                    value = try await self.backend.request(DesktopRequest("command", ball: path, arguments: ["device", letter, kind]), as: BallSnapshot.self)
                }
            }
            if !self.paths.contains(path) { self.paths.append(path); self.paths.sort() }
            self.selected = path; self.snapshot = value
            if let letter = value.unavailableDrives?.keys.sorted().first {
                self.page = .drives; self.browseDrive = letter
                self.driveNotice = "Unavailable paths are ejected. Choose a replacement folder or attach an image to reconnect a drive."
            }
            await self.loadIcons(path); self.revision += 1
            NSDocumentController.shared.noteNewRecentDocumentURL(url)
            self.prefs.balls = self.paths; self.prefs.selected = path
            try await self.preferences.save(self.prefs)
        }
    }
    func chooseImportBall(_ archive: URL? = nil) {
        guard !busy, !mediaBusy else { return }
        var source = archive
        if source == nil {
            let panel = NSOpenPanel()
            panel.title = "Import Ball from ZIP"; panel.prompt = "Choose ZIP"
            panel.allowedContentTypes = [.zip]; panel.allowsMultipleSelection = false
            panel.canChooseDirectories = false
            guard panel.runModal() == .OK else { return }
            source = panel.url
        }
        guard let source else { return }
        let panel = NSSavePanel()
        panel.title = "Import Ball"; panel.prompt = "Import"; panel.canCreateDirectories = true
        panel.message = "Choose a new ball location. Existing balls will not be replaced."
        panel.nameFieldStringValue = source.deletingPathExtension().lastPathComponent + ".2ineball"
        panel.allowedContentTypes = [UTType(exportedAs: "io.github.bitwisecook.2ine.ball", conformingTo: .package)]
        panel.directoryURL = Self.defaultBallFolder(UserDefaults.standard.string(forKey: "ballFolder"))
        if panel.runModal() == .OK, let destination = panel.url { importBall(source, to: destination) }
    }
    func importBall(_ source: URL, to destination: URL) {
        perform {
            self.operation = "Importing ball…"
            defer { self.operation = "Ready" }
            let path = destination.resolvingSymlinksInPath().path
            var request = DesktopRequest("import-ball", ball: path); request.path = source.path
            let value = try await self.backend.request(request, as: BallSnapshot.self)
            if !self.paths.contains(path) { self.paths.append(path); self.paths.sort() }
            self.selected = path; self.snapshot = value
            await self.loadIcons(path); self.revision += 1
            NSDocumentController.shared.noteNewRecentDocumentURL(destination)
            self.prefs.balls = self.paths; self.prefs.selected = path
            try await self.preferences.save(self.prefs)
            self.activity = "Imported ball from " + source.path
            if let letter = value.unavailableDrives?.keys.sorted().first {
                self.page = .drives; self.browseDrive = letter
                self.driveNotice = "Unavailable paths are ejected. Choose a replacement folder or attach an image to reconnect a drive."
            }
        }
    }
    func chooseExportBall() {
        guard let selected, snapshot != nil, !busy, !mediaBusy else { return }
        let panel = NSSavePanel()
        panel.title = "Export Ball as ZIP"; panel.prompt = "Export"
        panel.allowedContentTypes = [.zip]; panel.canCreateDirectories = true
        panel.nameFieldStringValue = Self.displayName(selected) + ".zip"
        let images = NSButton(checkboxWithTitle: "Include disk images", target: nil, action: nil)
        images.setAccessibilityIdentifier("includeDiskImages")
        let note = NSTextField(wrappingLabelWithString: "Includes attached images and images stored in the ball. Without images, media drives import as ejected. Unavailable folders can be reconnected after import.")
        note.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        note.textColor = .secondaryLabelColor
        note.preferredMaxLayoutWidth = 400
        let accessory = NSStackView(views: [images, note])
        accessory.orientation = .vertical; accessory.alignment = .leading; accessory.spacing = 8
        accessory.edgeInsets = NSEdgeInsets(top: 12, left: 20, bottom: 16, right: 20)
        accessory.widthAnchor.constraint(equalToConstant: 440).isActive = true
        accessory.setFrameSize(accessory.fittingSize)
        panel.accessoryView = accessory
        if panel.runModal() == .OK, let destination = panel.url {
            exportBall(to: destination, includeImages: images.state == .on)
        }
    }
    func exportBall(to destination: URL, includeImages: Bool) {
        guard let selected else { return }
        perform {
            self.operation = "Exporting ball…"
            defer { self.operation = "Ready" }
            if self.configDirty {
                let text = self.configText
                self.snapshot = try await self.backend.request(DesktopRequest("save-config-sys", ball: selected, expected: self.configBase, text: text), as: BallSnapshot.self)
                self.configDrafts.removeValue(forKey: selected); self.configBases.removeValue(forKey: selected)
                self.revision += 1
            }
            var request = DesktopRequest("export-ball", ball: selected, build: self.build)
            request.exporter = ["name": "2ine for macOS",
                                "version": Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "unversioned",
                                "build": Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "unknown"]
            request.path = destination.path; request.includeImages = includeImages
            let path = try await self.backend.request(request, as: String.self)
            self.activity = "Exported ball to " + path
            NSWorkspace.shared.activateFileViewerSelecting([URL(filePath: path)])
        }
    }
    static func pick(directory: Bool, title: String) -> URL? {
        let panel = NSOpenPanel()
        panel.title = title; panel.canChooseDirectories = directory
        panel.canChooseFiles = !directory; panel.allowsMultipleSelection = false
        panel.treatsFilePackagesAsDirectories = directory
        return panel.runModal() == .OK ? panel.url : nil
    }
    static func defaultBallFolder(_ value: String?) -> URL? {
        guard let value, !value.isEmpty else {
            return FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
        }
        return URL(filePath: value)
    }
    func chooseRuntime() {
        guard let selected, !busy, !mediaBusy, let url = Self.pick(directory: true, title: "Choose 2ine Build (containing lx_loader)") else { return }
        perform { try await self.setRuntime(url.path, ball: selected) }
    }
    func useBundledRuntime() {
        guard let selected else { return }
        perform { try await self.setRuntime(nil, ball: selected) }
    }
    func setRuntime(_ build: String?, ball: String) async throws {
        let inverse = DesktopRequest("runtime-build", ball: ball, build: snapshot?.manifest.runtime["build"])
        try await mutate(DesktopRequest("runtime-build", ball: ball, build: build), inverse: inverse, name: "Change Runtime")
    }
    func command(_ arguments: [String]) {
        guard !busy, !mediaBusy, let selected else { return }
        perform { try await self.runCommand(arguments, ball: selected) }
    }
    func runCommand(_ arguments: [String], ball: String) async throws {
        let inverse = inverseCommand(arguments).map { DesktopRequest("command", ball: ball, build: build, arguments: $0) }
        let name: String
        switch arguments.first {
        case "map", "map-image": name = "Map " + (arguments.dropFirst().first ?? "Drive") + ":"
        case "eject": name = "Eject " + (arguments.dropFirst().first ?? "Drive") + ":"
        case "device": name = "Add Media Drive"
        case "device-lock": name = "Change Device Lock"
        case "media-readonly": name = "Change Floppy Access"
        case "create-floppy": name = "Create Floppy Image"
        case "create-disk": name = "Create Disk Image"
        case "unmap": name = "Unmap " + (arguments.dropFirst().first ?? "Drive") + ":"
        case "remove-app": name = "Remove Launcher"
        default: name = "Change " + (arguments.dropFirst().first ?? "Setting")
        }
        try await mutate(DesktopRequest("command", ball: ball, build: build, arguments: arguments), inverse: inverse, name: name)
    }
    private func mutate(_ request: DesktopRequest, inverse: DesktopRequest?, name: String) async throws {
        let value = try await backend.request(request, as: BallSnapshot.self)
        if selected == request.ball { snapshot = value; revision += 1 }
        if let inverse { register(BallUndoEntry(request: request, inverse: inverse, name: name), reverse: true) }
    }
    private func register(_ entry: BallUndoEntry, reverse: Bool) {
        let ownGroup = !undoManager.isUndoing && !undoManager.isRedoing && undoManager.groupingLevel == 0
        if ownGroup { undoManager.beginUndoGrouping() }
        defer { if ownGroup { undoManager.endUndoGrouping() } }
        // UndoManager does not retain its target; the callback owns this entry.
        undoManager.registerUndo(withTarget: entry) { [weak self, entry] _ in
            guard let self else { return }
            // Register the counterpart while UndoManager is still undoing/redoing.
            self.register(entry, reverse: !reverse)
            self.perform {
                do {
                    let request = reverse ? entry.inverse : entry.request
                    let value = try await self.backend.request(request, as: BallSnapshot.self)
                    if let ball = request.ball {
                        self.selected = ball; self.snapshot = value; self.revision += 1
                        await self.loadIcons(ball)
                    }
                } catch {
                    self.undoManager.removeAllActions(withTarget: entry)
                    throw error
                }
            }
        }
        undoManager.setActionName(entry.name)
    }
    func imageSource(_ letter: String) -> String? {
        snapshot?.imageSources?[letter] ?? snapshot?.manifest.drives[letter]?.image
    }
    func availableMediaLetter(_ kind: MediaDeviceKind) -> String? {
        let order = kind == .floppy ? "ABDEFGHIJKLMNOPQRSTUVWXYZ" : "DEFGHIJKLMNOPQRSTUVWXYZAB"
        return order.map(String.init).first { snapshot?.manifest.drives[$0] == nil }
    }
    func changeMedia(_ arguments: [String]) async throws {
        guard canChangeMedia, let selected else { throw BallError("Wait for the current operation to finish.") }
        mediaBusy = true; defer { mediaBusy = false }
        try await runCommand(arguments, ball: selected)
    }
    func mediaCommand(_ arguments: [String]) {
        Task { do { try await changeMedia(arguments) } catch { self.error = error.localizedDescription } }
    }
    func chooseMedia(_ letter: String) {
        guard canChangeMedia, let kind = snapshot?.manifest.drives[letter]?.device,
              let url = Self.pick(directory: false, title: "Attach \(kind.title) Image to \(letter):") else { return }
        mediaCommand(["map-image", letter, url.path])
    }
    func createDisk(letter: String, path: String, filesystem: String, mib: Int) async throws {
        try await changeMedia(["create-disk", letter, path, filesystem, String(mib)])
    }
    func createFloppy(letter: String, path: String, kib: Int) async throws {
        try await changeMedia(["create-floppy", letter, path, String(kib)])
    }
    func audioSnapshot(ball: String) async throws -> AudioSnapshot {
        try await backend.request(DesktopRequest("audio-snapshot", ball: ball), as: AudioSnapshot.self)
    }
    func importSoundBank(_ path: String, ball: String) async throws -> AudioSnapshot {
        guard !busy, !mediaBusy, selected == ball else { throw BallError("Wait for the operation to finish and keep this ball selected.") }
        busy = true; defer { busy = false }
        var request = DesktopRequest("import-soundfont", ball: ball); request.path = path
        let value = try await backend.request(request, as: AudioSnapshot.self)
        try await reload(ball)
        return value
    }
    func saveAudio(_ settings: AudioSettings, ball: String) async throws {
        guard !busy, !mediaBusy, selected == ball else { throw BallError("Wait for the operation to finish and keep this ball selected.") }
        busy = true; defer { busy = false }
        var request = DesktopRequest("save-audio", ball: ball); request.settings = settings
        _ = try await backend.request(request, as: AudioSnapshot.self)
        try await reload(ball)
    }
    func chooseOS2Support() async throws -> OS2SupportStatus? {
        guard !busy, !mediaBusy else { throw BallError("Wait for the current operation to finish.") }
        let panel = NSOpenPanel()
        panel.title = "Choose OS/2 Installation ISO or Floppy Images"
        panel.message = "CMD.EXE, installer support files and the original REXX interpreter will be extracted into this ball’s C: drive."
        panel.prompt = "Extract Support Files"
        panel.allowsMultipleSelection = true
        guard panel.runModal() == .OK else { return nil }
        return try await installOS2Support(panel.urls.map(\.path))
    }
    func installOS2SupportFromPicker() {
        Task {
            do {
                if let result = try await chooseOS2Support(), !result.complete {
                    error = "Some support files were installed. Still missing: " + result.missing + ". Choose more OS/2 installation media to continue."
                }
            } catch { self.error = error.localizedDescription }
        }
    }
    func os2Support() async throws -> OS2SupportStatus {
        guard let selected else { throw BallError("Choose a ball first.") }
        return try await backend.request(DesktopRequest("os2-support", ball: selected), as: OS2SupportStatus.self)
    }
    func browseMedia(_ path: String) async throws -> MediaDirectory {
        var request = DesktopRequest("browse-media", ball: selected); request.path = path
        return try await backend.request(request, as: MediaDirectory.self)
    }
    func installOS2Support(_ sources: [String]) async throws -> OS2SupportStatus {
        guard let selected, !busy, !mediaBusy else { throw BallError("Wait for the current operation to finish.") }
        busy = true; defer { busy = false }
        var request = DesktopRequest("import-os2-support", ball: selected); request.sources = sources
        let result = try await backend.request(request, as: OS2SupportStatus.self)
        try await reload(selected)
        revision += 1
        return result
    }
    func attachInstallerMedia(_ url: URL, kind: MediaDeviceKind, letter: String) async throws -> [String] {
        let expectedBall = selected
        if snapshot?.manifest.drives[letter] == nil { try await changeMedia(["device", letter, kind.rawValue]) }
        guard selected == expectedBall else { throw BallError("The selected ball changed. Open Install Software again.") }
        try await changeMedia(["map-image", letter, url.path])
        return try await installerCandidates(letter: letter)
    }
    /// Bounded discovery keeps large installation CDs responsive. The picker accepts every file.
    func installerCandidates(letter: String) async throws -> [String] {
        guard let selected else { return [] }
        var pending = [(letter + ":\\", 0)], candidates: [(String, Int)] = []
        var scanned = 0
        while !pending.isEmpty && scanned < 96 {
            let (path, depth) = pending.removeFirst(); scanned += 1
            let listing = try await browse(path, ball: selected)
            for entry in listing.entries {
                let name = entry.name.lowercased()
                if entry.directory && depth < 2 {
                    if name == "english" || name == "os2" || name.contains("install") || name.contains("setup") || depth == 0 {
                        pending.append((entry.path, depth + 1))
                    }
                } else if !entry.directory {
                    let stems = ["install", "setup", "inst", "os2inst", "autorun"]
                    let url = URL(filePath: name)
                    let stem = url.deletingPathExtension().lastPathComponent
                    if let rank = stems.firstIndex(of: stem), ["exe", "cmd", "bat", "com"].contains(url.pathExtension) {
                        let languageBonus = entry.path.uppercased().contains("\\ENGLISH\\") ? -2 : 0
                        candidates.append((entry.path, rank * 10 + depth * 3 + languageBonus))
                    }
                }
            }
        }
        return candidates.sorted { $0.1 == $1.1 ? $0.0 < $1.0 : $0.1 < $1.1 }.prefix(32).map(\.0)
    }
    func mapDrive(_ letter: String, directory: String, image: Bool = false) async throws {
        guard !busy, !mediaBusy, let selected else { throw BallError("Wait for the current operation to finish.") }
        busy = true; defer { busy = false }
        try await runCommand([image ? "map-image" : "map", letter, directory], ball: selected)
    }
    func inverseCommand(_ args: [String]) -> [String]? {
        guard args.count >= 2, let manifest = snapshot?.manifest else { return nil }
        let action = args[0], key = args[1]
        if ["map", "map-image", "unmap", "device", "eject", "restore-drive", "create-floppy", "create-disk"].contains(action) {
            if let record = snapshot?.driveRecords?[key] { return ["restore-drive", key, record] }
            guard let old = manifest.drives[key] else { return ["unmap", key] }
            if let image = old.image { return ["map-image", key, image] }
            return ["map", key, old.source.hasPrefix("/") ? old.source : (selected! + "/" + old.source)]
        }
        if action == "device-lock" { return [action, key, manifest.drives[key]?.isLocked == true ? "on" : "off"] }
        if action == "media-readonly" { return [action, key, manifest.drives[key]?.isReadOnly == true ? "on" : "off"] }
        if action == "config" || action == "unset" {
            let parts = key.split(separator: ".", maxSplits: 1).map(String.init)
            guard parts.count == 2 else { return nil }
            let old = parts[0] == "env" ? manifest.environment[parts[1]] : parts[0] == "runtime" ? manifest.runtime[parts[1]] : manifest.system[parts[1]]?.text
            return old.map { ["config", key, $0] } ?? ["unset", key]
        }
        if action == "remove-app", let record = snapshot?.applicationRecords[key] { return ["restore-app", key, record] }
        if action == "restore-app" { return ["remove-app", key] }
        if action == "add-app" {
            let index = key == "--cwd" ? 3 : 1
            return args.count > index ? ["remove-app", args[index]] : nil
        }
        return nil
    }
    func configSys(_ text: String, expected: String) {
        guard let selected else { return }
        perform {
            self.snapshot = try await self.backend.request(DesktopRequest("save-config-sys", ball: selected, expected: expected, text: text), as: BallSnapshot.self)
            self.configDrafts.removeValue(forKey: selected); self.configBases.removeValue(forKey: selected)
            self.revision += 1
        }
    }
    func discardConfigDraft() {
        if let selected { configDrafts.removeValue(forKey: selected); configBases.removeValue(forKey: selected) }
        refresh()
    }
    func beginProgram(_ action: ProgramAction) { editingApplication = nil; programAction = action }
    func editApplication(_ name: String) { editingApplication = name; programAction = .save }
    func browse(_ path: String, ball: String) async throws -> GuestDirectory {
        var request = DesktopRequest("browse", ball: ball); request.path = path
        return try await backend.request(request, as: GuestDirectory.self)
    }
    func setWorkingDirectory(_ path: String) {
        guard let selected else { return }
        perform {
            var request = DesktopRequest("set-working-directory", ball: selected); request.path = path
            self.snapshot = try await self.backend.request(request, as: BallSnapshot.self)
            self.revision += 1
        }
    }
    @discardableResult func importFiles(_ urls: [URL], into path: String) -> Bool {
        guard !busy, !mediaBusy, let selected, !urls.isEmpty, urls.allSatisfy(\.isFileURL) else { return false }
        perform {
            self.driveNotice = "Copying files…"
            var request = DesktopRequest("import-files", ball: selected)
            request.path = path; request.sources = urls.map(\.path)
            do {
                _ = try await self.backend.request(request, as: GuestDirectory.self)
                self.driveNotice = "Copied \(urls.count) item(s) to \(path)."
                self.revision += 1
            } catch { self.driveNotice = "Copy did not complete."; throw error }
        }
        return true
    }
    func program(_ action: ProgramAction, name: String, program: String, arguments: String,
                 directories: [String: String], environment: [String: String], iconSource: String?, mediaDrive: String? = nil) async throws {
        guard !busy, !mediaBusy, let selected else { throw BallError("Wait for the current operation to finish.") }
        let argv = try Arguments.parse(arguments)
        if action == .save {
            guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { throw BallError("Enter an application name.") }
            if editingApplication == nil && snapshot?.manifest.applications[name] != nil { throw BallError("That application name already exists.") }
            busy = true; defer { busy = false }
            var request = DesktopRequest("save-application", ball: selected, arguments: argv)
            request.name = name; request.originalName = editingApplication; request.program = program
            request.workingDirectories = directories; request.environment = environment; request.iconSource = iconSource
            snapshot = try await backend.request(request, as: BallSnapshot.self)
            await loadIcons(selected); revision += 1
            programAction = nil; editingApplication = nil
        } else {
            programAction = nil; editingApplication = nil
            execute([action == .install ? "install" : "run", program] + argv, directories: directories, environment: environment, mediaDrive: mediaDrive)
        }
    }
    func executableIcon(program: String, name: String? = nil, ball: String) async throws -> ExecutableIcon {
        var request = DesktopRequest("application-icon", ball: ball); request.program = program; request.name = name
        return try await backend.request(request, as: ExecutableIcon.self)
    }
    func previewImage(_ path: String) async throws -> ExecutableIcon {
        var request = DesktopRequest("image-preview"); request.path = path
        return try await backend.request(request, as: ExecutableIcon.self)
    }
    static func image(_ icon: ExecutableIcon) -> NSImage? {
        if let data = icon.image { return NSImage(data: data) }
        guard let data = icon.rgba, let width = icon.width, let height = icon.height,
              width > 0, height > 0, width <= 1024, height <= 1024, data.count == width * height * 4,
              let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
                 bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                 colorSpaceName: .deviceRGB, bitmapFormat: .alphaNonpremultiplied, bytesPerRow: width * 4, bitsPerPixel: 32),
              let destination = bitmap.bitmapData else { return nil }
        data.copyBytes(to: destination, count: data.count)
        let image = NSImage(size: NSSize(width: width, height: height)); image.addRepresentation(bitmap)
        return image
    }
    private func loadIcons(_ ball: String) async {
        applicationIcons = [:]
        for (name, app) in snapshot?.manifest.applications ?? [:] {
            if let icon = try? await executableIcon(program: app.program, name: name, ball: ball), let image = Self.image(icon) {
                applicationIcons[name] = image
            }
        }
    }
    func execute(_ arguments: [String], directories: [String: String]? = nil, environment: [String: String]? = nil, mediaDrive: String? = nil) {
        guard let selected, !busy, !mediaBusy else { return }
        guard !build.isEmpty else { error = "Choose a runtime build before launching software."; return }
        let build = self.build
        let media = arguments.first == "install" ? mediaDrive ?? arguments.dropFirst().first.flatMap { value -> String? in
            let letter = String(value.prefix(1)).uppercased()
            return snapshot?.manifest.drives[letter]?.image != nil ? letter : nil
        } : nil
        installCompletion = nil
        page = .activity; activity = "Starting…"; operation = arguments.first ?? "Run"
        exitCode = nil; log = nil; stopRequested = false
        perform {
            defer { self.processRunning = false }
            let result: ProcessResult
            do {
                result = try await self.backend.execute(ball: selected, build: build, arguments: arguments, workingDirectories: directories, environment: environment) { [weak self] text in
                    await self?.updateActivity(text)
                }
            } catch {
                self.operation = "Could Not Start"; self.activity = error.localizedDescription
                return
            }
            self.activity = result.output; self.exitCode = result.code; self.log = result.log
            if let media {
                self.installCompletion = InstallCompletion(ball: selected, letter: media, code: result.code, signalled: result.signalled)
            }
            self.operation = result.signalled ? "Terminated by signal \(result.code)" : "Exited \(result.code)"
            if let error = result.receiptError { self.error = "The process exited, but its installer receipt could not be saved: " + error }
            try await self.reload(selected)
        }
    }
    private func updateActivity(_ text: String) { processRunning = true; activity = text }
    func stopProcess(force: Bool = false) {
        guard processRunning else { return }
        stopRequested = true
        operation = force ? "Force stopping…" : "Stopping…"
        Task { await backend.stop(force: force) }
    }
    func chooseImport(into path: String) {
        guard !busy, !mediaBusy else { return }
        let panel = NSOpenPanel()
        panel.title = "Import Files into " + path; panel.prompt = "Import"
        panel.canChooseFiles = true; panel.canChooseDirectories = true; panel.allowsMultipleSelection = true
        if panel.runModal() == .OK { _ = importFiles(panel.urls, into: path) }
    }
    func reveal(_ path: String) { NSWorkspace.shared.activateFileViewerSelecting([URL(filePath: path)]) }
    func openFile(_ path: String) { NSWorkspace.shared.open(URL(filePath: path)) }

}
