import AppKit
#if canImport(BallCore)
import BallCore
#endif
import Observation
import UniformTypeIdentifiers
import SwiftUI

enum Page: String, CaseIterable, Identifiable {
    case drives = "Drives", applications = "Applications"
    case systemComponents = "OS/2 System Components", installation = "Application Installation"
    case settings = "Settings", activity = "Logs"
    var id: String { rawValue }
}
struct OS2ImportRequest: Identifiable {
    let id = UUID()
    let ball: String
    let sources: [String]
    let categories: Set<OS2ExtractionCategory>
    init(ball: String, sources: [String] = [], categories: Set<OS2ExtractionCategory> = OS2ExtractionCategory.defaults) {
        self.ball = ball; self.sources = sources; self.categories = categories
    }
}
enum SettingsSection: String {
    case general, graphics, environment, printing, files, registry
}
struct ComponentNavigation: Equatable {
    let id = UUID()
    let ball: String
    let category: OS2ExtractionCategory
}
struct ComponentDraft {
    let sources: [String]
    let selection: Set<String>
}
struct InstallCompletion: Identifiable {
    let id = UUID()
    let ball: String
    let letter: String
    let code: Int32
    let signalled: Bool
}
struct BallRenameRequest: Identifiable {
    let id = UUID()
    let path: String
    let identity: BallFileIdentity
}
struct BallFileIdentity: Equatable {
    let device: UInt64
    let inode: UInt64
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
    var canChangeMedia: Bool { selected != nil && !componentOperationRunning && !mediaBusy && (!busy || processRunning) }
    var installCompletion: InstallCompletion?
    var processRunning = false { didSet { if oldValue != processRunning { updateConfigObservation() } } }
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
    var settingsSection: SettingsSection = .general
    var highlightedSetting: String?
    var componentNavigation: ComponentNavigation?
    private var appliedComponentNavigation: UUID?
    var componentDrafts: [String: ComponentDraft] = [:]
    var componentJobs: [String: ComponentJob] = [:]
    var componentPlans: [String: OS2ExtractionPlan] = [:]
    var componentPlanSources: [String: [String]] = [:]
    var componentMedia: [String: OS2MediaSource] = [:]
    var componentNotice: String?
    func openComponentSetting(_ token: String) {
        guard ["graphics.opengl", "graphics", "printing"].contains(token) else { return }
        settingsSection = token == "printing" ? .printing : .graphics
        highlightedSetting = token
        page = .settings
    }
    func openOS2ComponentGroup(_ category: OS2ExtractionCategory) {
        guard selected != nil, !busy, !mediaBusy, !processRunning else { return }
        showOS2ComponentGroup(category)
    }
    private func showOS2ComponentGroup(_ category: OS2ExtractionCategory) {
        guard let selected else { return }
        componentNavigation = ComponentNavigation(ball: selected, category: category)
        page = .systemComponents
    }
    func setOpenGLProvider(_ provider: String) {
        guard let ball = selected, !busy, !mediaBusy else { return }
        perform {
            try await self.runCommand(provider == "automatic" ? ["unset", "system.opengl"] : ["config", "system.opengl", provider], ball: ball)
            if provider == "original", !self.processRunning { self.showOS2ComponentGroup(.opengl) }
        }
    }
    func installOriginalOpenGL() { setOpenGLProvider("original") }
    func componentSelection(in plan: OS2ExtractionPlan, preserving selection: Set<String>, ball: String) -> Set<String> {
        guard let navigation = componentNavigation, navigation.ball == ball,
              navigation.id != appliedComponentNavigation,
              let node = plan.tree?.find(navigation.category.rawValue) else { return selection }
        guard !node.eligibleSources.isEmpty else { return selection }
        appliedComponentNavigation = navigation.id
        return plan.selecting(node, in: selection, enabled: true)
    }
    var lastOS2MediaSources: [String] { prefs.lastOS2MediaSources }
    func suggestedOS2MediaSources(_ ballSources: [String]?) -> [String] {
        if let ballSources, !ballSources.isEmpty { return ballSources }
        return prefs.lastOS2MediaSources.isEmpty ? recentMediaCandidates : prefs.lastOS2MediaSources
    }
    private struct ValidatedOS2Media: Decodable, Sendable {
        let complete: Bool
        let sources: [String]
        let physicalSources: [String]?
    }
    private func cacheOS2Media(_ sources: [String], ball: String) async {
        guard !sources.isEmpty else { return }
        do {
            var request = DesktopRequest("validate-os2-media", ball: ball, build: build)
            request.sources = sources
            let payload = request
            let validated = try await Task.detached { try NativeBallCore.request(payload, as: ValidatedOS2Media.self) }.value
            guard validated.complete, !validated.sources.isEmpty else { return }
            prefs.lastOS2MediaSources = validated.sources
            prefs.lastOS2MediaFiles = validated.physicalSources ?? []
            try await preferences.save(prefs)
        } catch { componentNotice = "Could not remember OS/2 media for new balls: " + error.localizedDescription }
    }
    var audioEditor = false
    var operation = "Ready"
    var log: URL?
    var exitCode: Int32?
    var error: String?
    var ballRename: BallRenameRequest?
    var ballToTrash: BallRenameRequest?
    var canManageBalls: Bool { !busy && !mediaBusy && !processRunning && !configSaving }
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
    @ObservationIgnored private var configObservation: Task<Void, Never>?
    var liveConfigTexts: [String: String] = [:]
    var configurationNotice: String?
    var configSaving = false
    var configDrafts: [String: String] = [:]
    var configBases: [String: String] = [:]
    var configText: String {
        get { selected.flatMap { configDrafts[$0] } ?? selected.flatMap { liveConfigTexts[$0] } ?? snapshot?.configSys ?? "" }
        set {
            guard let selected else { return }
            if configBases[selected] == nil { configBases[selected] = liveConfigTexts[selected] ?? snapshot?.configSys ?? "" }
            configDrafts[selected] = newValue
        }
    }
    var configBase: String { selected.flatMap { configBases[$0] ?? liveConfigTexts[$0] } ?? snapshot?.configSys ?? "" }
    var configConflict: Bool {
        guard let selected, let base = configBases[selected], configDirty else { return false }
        return base != (liveConfigTexts[selected] ?? snapshot?.configSys ?? "")
    }
    var configDirty: Bool { configText != configBase }
    var hasUnsavedConfigs: Bool { configDrafts.contains { $0.value != configBases[$0.key] } }
    func discardAllConfigDrafts() { configDrafts.removeAll(); configBases.removeAll() }
    func saveAllConfigDrafts() async throws {
        guard !busy, !mediaBusy else { throw BallError("Wait for the operation to finish.") }
        busy = true
        defer { busy = false }
        for (path, text) in configDrafts where text != configBases[path] {
            let value = try await backend.request(DesktopRequest("save-config-sys", ball: path, expected: configBases[path], text: text), as: BallSnapshot.self)
            liveConfigTexts[path] = value.configSys
            if path == selected { snapshot = value; revision += 1 }
            configDrafts.removeValue(forKey: path); configBases.removeValue(forKey: path)
        }
    }
    private let backend: BallBackend
    private let preferences: PreferenceStore
    private var prefs = Preferences()
    private var recentMediaCandidates: [String] = []
    private let bundledRuntime: String
    var build: String {
        let fallback = ProcessInfo.processInfo.environment["TWOINE_BUILD"] ?? bundledRuntime
        var request = DesktopRequest("effective-runtime", build: fallback)
        request.path = snapshot?.manifest.runtime["build"]
        return (try? NativeBallCore.request(request, as: String.self)) ?? fallback
    }
    static func displayName(_ path: String) -> String {
        let url = URL(filePath: path)
        return (url.pathExtension.lowercased() == "2ineball" ? url.deletingPathExtension() : url).lastPathComponent
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
            .appending(path: "Library/Application Support/2ine/ball-macos.json")
        let legacy = preferencesURL == nil && override == nil ? FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/2ine/balls-macos.json") : nil
        preferences = PreferenceStore(url: preferencesURL ?? path, legacyURL: legacy)
    }

    func start() {
        perform {
            self.prefs = try await self.preferences.load()
            let discovered = try await self.backend.request(DesktopRequest("discover"), as: [String].self)
            self.paths = Array(Set(self.prefs.ballPaths + discovered)).sorted()
            let initial = ProcessInfo.processInfo.environment["TWOINE_MACOS_BALL"]
            self.selected = initial ?? self.prefs.selected ?? self.paths.first
            if let selected = self.selected {
                if !self.paths.contains(selected) { self.paths.append(selected) }
                try await self.reload(selected)
            }
        }
    }

    func perform(allowComponentRead: Bool = false, _ work: @escaping @MainActor () async throws -> Void) {
        guard !busy, !mediaBusy, allowComponentRead || !componentOperationRunning else { return }
        busy = true
        Task {
            defer { busy = false }
            do { try await work() }
            catch { self.error = error.localizedDescription }
        }
    }

    private func reload(_ ball: String) async throws {
        snapshot = try await backend.request(DesktopRequest("snapshot", ball: ball), as: BallSnapshot.self)
        if let sources = snapshot?.os2Support?.mediaSources, !sources.isEmpty { recentMediaCandidates = sources }
        if let text = snapshot?.configSys { applyLiveConfig(text, ball: ball) }
        await loadIcons(ball)
        revision += 1
        prefs.ballPaths = paths; prefs.selected = ball
        try await preferences.save(prefs)
    }

    func select(_ path: String) {
        guard !busy, !mediaBusy else { return }
        selected = path; snapshot = nil
        perform(allowComponentRead: true) { try await self.reload(path) }
    }

    func canManageBall(_ path: String) -> Bool {
        canManageBalls && componentJobs[path]?.running != true && (paths.contains(path) || selected == path)
    }
    func beginRenameBall(_ path: String) {
        guard canManageBall(path) else { return }
        do { ballRename = BallRenameRequest(path: path, identity: try Self.ballIdentity(path)) }
        catch { self.error = error.localizedDescription }
    }
    func beginTrashBall(_ path: String) {
        guard canManageBall(path) else { return }
        do { ballToTrash = BallRenameRequest(path: path, identity: try Self.ballIdentity(path)) }
        catch { self.error = error.localizedDescription }
    }
    func renameBall(_ path: String, to name: String, expectedIdentity: BallFileIdentity? = nil) async throws {
        guard canManageBall(path) else { throw BallError("Stop the running operation before renaming a ball.") }
        if let expectedIdentity, try Self.ballIdentity(path) != expectedIdentity {
            throw BallError("The ball changed after the rename dialog opened. Choose Rename Ball again.")
        }
        busy = true
        defer { busy = false; operation = "Ready" }
        operation = "Renaming ball…"
        var request = DesktopRequest("rename-ball", ball: path)
        request.text = name
        let renamed = try await backend.request(request, as: RenamedBall.self)
        paths = Array(Set(paths.map { $0 == path ? renamed.path : $0 })).sorted()
        componentDrafts.removeValue(forKey: path)
        componentPlans.removeValue(forKey: path); componentMedia.removeValue(forKey: path)
        componentPlanSources.removeValue(forKey: path); componentJobs.removeValue(forKey: path)
        if componentNavigation?.ball == path { componentNavigation = nil }
        if let value = settingsDrafts.removeValue(forKey: path) { settingsDrafts[renamed.path] = value }
        if let value = configDrafts.removeValue(forKey: path) { configDrafts[renamed.path] = value }
        if let value = configBases.removeValue(forKey: path) { configBases[renamed.path] = value }
        if let value = liveConfigTexts.removeValue(forKey: path) { liveConfigTexts[renamed.path] = value }
        if let log, log.path.hasPrefix(path + "/") {
            self.log = URL(filePath: renamed.path + log.path.dropFirst(path.count))
        }
        undoManager.removeAllActions()
        if selected == path {
            selected = renamed.path; snapshot = renamed.snapshot
            browseDrive = nil
            await loadIcons(renamed.path)
        }
        revision += 1
        prefs.ballPaths = paths; prefs.selected = selected
        try await preferences.save(prefs)
        updateRecentBallPath(path, to: renamed.path)
    }
    func trashBall(_ path: String, expectedIdentity: BallFileIdentity? = nil, using trash: (URL) throws -> Void = {
        try FileManager.default.trashItem(at: $0, resultingItemURL: nil)
    }) async throws {
        guard canManageBall(path) else { throw BallError("Stop the running operation before moving a ball to the Trash.") }
        let url = try Self.managedBallURL(path)
        if let expectedIdentity, try Self.ballIdentity(path) != expectedIdentity {
            throw BallError("The ball changed after the Trash confirmation opened. Choose Move Ball to Trash again.")
        }
        busy = true
        defer { busy = false; operation = "Ready" }
        operation = "Moving ball to the Trash…"
        try trash(url)
        try await forgetBallPath(path)
    }
    func removeMissingBall(_ path: String) {
        guard canManageBall(path), !FileManager.default.fileExists(atPath: path) else { return }
        perform { try await self.forgetBallPath(path) }
    }
    static func managedBallURL(_ path: String) throws -> URL {
        let url = URL(filePath: path).standardizedFileURL
        let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard url.pathExtension.lowercased() == "2ineball", values.isDirectory == true,
              values.isSymbolicLink != true else { throw BallError("Choose a .2ineball package, rather than an ordinary folder or symbolic link.") }
        let manifest = try url.appending(path: "ball.json").resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard manifest.isRegularFile == true, manifest.isSymbolicLink != true else {
            throw BallError("The selected package does not contain a regular ball.json file.")
        }
        return url
    }
    static func ballIdentity(_ path: String) throws -> BallFileIdentity {
        let url = try managedBallURL(path)
        let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
        guard let device = attributes[.systemNumber] as? NSNumber, let inode = attributes[.systemFileNumber] as? NSNumber else {
            throw BallError("Cannot identify the selected ball package.")
        }
        return BallFileIdentity(device: device.uint64Value, inode: inode.uint64Value)
    }
    private func updateRecentBallPath(_ path: String, to replacement: String?) {
        let controller = NSDocumentController.shared
        let recent = controller.recentDocumentURLs
        controller.clearRecentDocuments(nil)
        for url in recent.reversed() where url.standardizedFileURL.path != path {
            controller.noteNewRecentDocumentURL(url)
        }
        if let replacement { controller.noteNewRecentDocumentURL(URL(filePath: replacement)) }
    }
    private func forgetBallPath(_ path: String) async throws {
        paths.removeAll { $0 == path }
        componentDrafts.removeValue(forKey: path)
        componentPlans.removeValue(forKey: path); componentMedia.removeValue(forKey: path)
        componentPlanSources.removeValue(forKey: path); componentJobs.removeValue(forKey: path)
        if componentNavigation?.ball == path { componentNavigation = nil }
        settingsDrafts.removeValue(forKey: path); configDrafts.removeValue(forKey: path)
        configBases.removeValue(forKey: path); liveConfigTexts.removeValue(forKey: path)
        if log?.path.hasPrefix(path + "/") == true { log = nil }
        undoManager.removeAllActions()
        updateRecentBallPath(path, to: nil)
        if selected == path {
            selected = paths.first { FileManager.default.fileExists(atPath: $0 + "/ball.json") }
            snapshot = nil; applicationIcons = [:]; browseDrive = nil
            activity = "No process has run in this session."; exitCode = nil
            if let selected { try await reload(selected) }
        }
        revision += 1
        prefs.ballPaths = paths; prefs.selected = selected
        try await preferences.save(prefs)
    }
    func refresh() {
        guard let selected else { start(); return }
        perform(allowComponentRead: true) { try await self.reload(selected) }
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
        if create, componentJobs[url.resolvingSymlinksInPath().path]?.running == true { return }
        if !create && url.pathExtension.lowercased() == "zip" { chooseImportBall(url); return }
        perform(allowComponentRead: true) {
            let path = url.resolvingSymlinksInPath().path
            if let sources = self.snapshot?.os2Support?.mediaSources, !sources.isEmpty { self.recentMediaCandidates = sources }
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
            self.prefs.ballPaths = self.paths; self.prefs.selected = path
            try await self.preferences.save(self.prefs)
            if create { self.page = .systemComponents }
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
        panel.message = "Choose a new ball location. An existing ball will not be replaced."
        panel.nameFieldStringValue = source.deletingPathExtension().lastPathComponent + ".2ineball"
        panel.allowedContentTypes = [UTType(exportedAs: "io.github.bitwisecook.2ine.ball", conformingTo: .package)]
        panel.directoryURL = Self.defaultBallFolder(UserDefaults.standard.string(forKey: "ballFolder"))
        if panel.runModal() == .OK, let destination = panel.url { importBall(source, to: destination) }
    }
    func importBall(_ source: URL, to destination: URL) {
        guard componentJobs[destination.resolvingSymlinksInPath().path]?.running != true else { return }
        perform(allowComponentRead: true) {
            self.operation = "Importing ball…"
            defer { self.operation = "Ready" }
            let path = destination.resolvingSymlinksInPath().path
            var request = DesktopRequest("import-ball", ball: path); request.path = source.path
            let value = try await self.backend.request(request, as: BallSnapshot.self)
            if !self.paths.contains(path) { self.paths.append(path); self.paths.sort() }
            self.selected = path; self.snapshot = value
            await self.loadIcons(path); self.revision += 1
            NSDocumentController.shared.noteNewRecentDocumentURL(destination)
            self.prefs.ballPaths = self.paths; self.prefs.selected = path
            try await self.preferences.save(self.prefs)
            self.activity = "Imported ball from " + source.path
            if let letter = value.unavailableDrives?.keys.sorted().first {
                self.page = .drives; self.browseDrive = letter
                self.driveNotice = "Unavailable paths are ejected. Choose a replacement folder or attach an image to reconnect a drive."
            }
        }
    }
    func chooseExportBall() {
        guard let selected, snapshot != nil, !busy, !mediaBusy, !componentOperationRunning else { return }
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
        let inverse = DesktopRequest("restore-runtime", ball: ball, build: snapshot?.manifest.runtime["build"])
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
        invalidateComponentPlan(after: request)
        if selected == request.ball { snapshot = value; revision += 1 }
        if let inverse { register(BallUndoEntry(request: request, inverse: inverse, name: name), reverse: true) }
    }
    private func invalidateComponentPlan(after request: DesktopRequest) {
        guard let ball = request.ball else { return }
        if ["runtime-build", "restore-runtime"].contains(request.action) ||
            (request.action == "command" && ["config", "unset"].contains(request.arguments?.first ?? "") && request.arguments?.dropFirst().first == "system.opengl") {
            componentPlans.removeValue(forKey: ball)
        }
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
                    self.invalidateComponentPlan(after: request)
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
    func projectCatalogue() async throws -> ProjectCatalogue {
        try await backend.request(DesktopRequest("project-sources", build: build), as: ProjectCatalogue.self)
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
        guard !busy, !mediaBusy, componentJobs[ball]?.running != true, selected == ball else { throw BallError("Wait for the operation to finish and keep this ball selected.") }
        busy = true; defer { busy = false }
        var request = DesktopRequest("save-audio", ball: ball); request.settings = settings
        _ = try await backend.request(request, as: AudioSnapshot.self)
        try await reload(ball)
    }
    func pickOS2Media(directory: Bool = false) async -> [String]? {
        guard !busy, !mediaBusy else { return nil }
        let panel = NSOpenPanel()
        panel.title = directory ? "Choose OS/2 Media Folder" : "Choose OS/2 ISO or Archive"
        panel.message = "Choose your OS/2 installation ISO. Verified OS/2 media will also be suggested for new balls. An ISO inside a ZIP or another archive is supported."
        panel.prompt = "Use This Media"
        panel.canChooseDirectories = directory; panel.canChooseFiles = !directory
        panel.allowsMultipleSelection = !directory
        if let path = prefs.lastOS2MediaFiles.first, FileManager.default.fileExists(atPath: path) {
            panel.directoryURL = URL(filePath: path).deletingLastPathComponent()
            if !directory { panel.nameFieldStringValue = URL(filePath: path).lastPathComponent }
        }
        return await withCheckedContinuation { continuation in
            let completion: (NSApplication.ModalResponse) -> Void = { response in
                continuation.resume(returning: response == .OK ? panel.urls.map(\.path) : nil)
            }
            if let window = NSApp.keyWindow { panel.beginSheetModal(for: window, completionHandler: completion) }
            else { panel.begin(completionHandler: completion) }
        }
    }
    func installComponents() {
        guard selected != nil, !busy, !mediaBusy else { return }
        page = .systemComponents
    }
    func rememberOS2Media(_ sources: [String], ball: String) async throws {
        var request = DesktopRequest("remember-os2-media", ball: ball); request.sources = sources
        _ = try await backend.request(request, as: OS2SupportStatus.self)
        if selected == ball { await cacheOS2Media(sources, ball: ball) }
    }
    func openOS2Source(_ sources: [String], ball: String) async throws -> OS2MediaSource {
        guard selected == ball, !busy, !mediaBusy else { throw BallError("Wait for the current operation to finish.") }
        var request = DesktopRequest("open-os2-source", ball: ball, build: build); request.sources = sources
        let payload = request
        return try await Task.detached { try NativeBallCore.request(payload, as: OS2MediaSource.self) }.value
    }
    func planOS2Extraction(_ sources: [String], ball: String) async throws -> OS2ExtractionPlan {
        guard selected == ball, !busy, !mediaBusy else { throw BallError("Wait for the current operation to finish.") }
        var request = DesktopRequest("plan-os2-extraction", ball: ball, build: build); request.sources = sources
        let payload = request
        return try await Task.detached { try NativeBallCore.request(payload, as: OS2ExtractionPlan.self) }.value
    }
    func os2Support() async throws -> OS2SupportStatus {
        guard let selected else { throw BallError("Choose a ball first.") }
        return try await backend.request(DesktopRequest("os2-support", ball: selected), as: OS2SupportStatus.self)
    }
    var dependencyAuditLog: URL? {
        guard let selected, let path = snapshot?.dependencyAudit?.logPath else { return nil }
        let logs = URL(filePath: selected).appending(path: "logs").resolvingSymlinksInPath()
        let file = URL(filePath: path).resolvingSymlinksInPath()
        return file.path.hasPrefix(logs.path + "/") && FileManager.default.fileExists(atPath: file.path) ? file : nil
    }
    var applicationRegistrationLog: URL? {
        guard let selected else { return nil }
        let logs = URL(filePath: selected).appending(path: "logs").resolvingSymlinksInPath()
        let file = logs.appending(path: "wps-registration-latest.json").resolvingSymlinksInPath()
        return file.path.hasPrefix(logs.path + "/") && FileManager.default.fileExists(atPath: file.path) ? file : nil
    }
    private func requestDependencyComponents(_ audit: DependencyAudit, ball: String) -> DependencyAudit? {
        guard audit.needsMediaSelection else { return audit }
        guard selected == ball else { return nil }
        let groups = (audit.componentGroups ?? []).map(\.title).joined(separator: ", ")
        componentNotice = (groups.isEmpty ? "Additional OS/2 components are required." : "Required OS/2 components: " + groups + ".")
            + " Choose media and install them here, then start the application again."
        let category = (audit.componentGroups ?? []).compactMap { OS2ExtractionCategory(rawValue: $0.id) }.first ?? .cmd
        showOS2ComponentGroup(category)
        return nil
    }
    func dependencyRequirements() async throws -> DependencyAudit {
        guard let selected else { throw BallError("Choose a ball first.") }
        var request = DesktopRequest("audit-dependencies", ball: selected, build: build)
        request.resolve = false
        let payload = request
        return try await Task.detached { try NativeBallCore.request(payload, as: DependencyAudit.self) }.value
    }
    func auditDependencies() {
        guard let selected, !busy, !mediaBusy, !processRunning else { return }
        perform {
            self.operation = "Checking installed components…"
            let audit = try await self.backend.request(DesktopRequest("audit-dependencies", ball: selected, build: self.build), as: DependencyAudit.self)
            self.page = .activity; self.activity = audit.summary
            self.exitCode = nil
            _ = self.requestDependencyComponents(audit, ball: selected)
            self.operation = "Dependency audit finished"
            try await self.reload(selected)
        }
    }
    func browseMedia(_ path: String) async throws -> MediaDirectory {
        var request = DesktopRequest("browse-media", ball: selected); request.path = path
        return try await backend.request(request, as: MediaDirectory.self)
    }
    func installOS2Support(_ sources: [String], categories: Set<OS2ExtractionCategory> = OS2ExtractionCategory.defaults, ball: String? = nil,
                           fileSelection: [String]? = nil, configureComponents: [String]? = nil) async throws -> OS2SupportStatus {
        guard let selected, !busy, !mediaBusy else { throw BallError("Wait for the current operation to finish.") }
        busy = true; defer { busy = false }
        guard ball == nil || selected == ball else { throw BallError("The selected ball changed. Open OS/2 extraction again.") }
        var request = DesktopRequest("import-os2-support", ball: selected, build: build); request.sources = sources
        request.fileSelection = fileSelection
        request.configureComponents = configureComponents
        request.categories = categories.map(\.rawValue).sorted()
        let result = try await backend.request(request, as: OS2SupportStatus.self)
        await cacheOS2Media(result.mediaSources ?? sources, ball: selected)
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
        guard !busy, !mediaBusy, !componentOperationRunning, let selected else { throw BallError("Wait for this ball’s component operation to finish.") }
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
        guard let selected, !mediaBusy, !configSaving, !busy || processRunning else { return }
        configSaving = true
        let ownsBusy = !busy
        if ownsBusy { busy = true }
        Task {
            defer { configSaving = false; if ownsBusy { busy = false } }
            do {
                let value = try await backend.request(DesktopRequest("save-config-sys", ball: selected, expected: expected, text: text), as: BallSnapshot.self)
                liveConfigTexts[selected] = value.configSys
                if self.selected == selected { snapshot = value; revision += 1 }
                configDrafts.removeValue(forKey: selected); configBases.removeValue(forKey: selected)
            } catch { self.error = error.localizedDescription }
        }
    }
    func discardConfigDraft() {
        if let selected { configDrafts.removeValue(forKey: selected); configBases.removeValue(forKey: selected) }
        guard let selected else { return }
        Task { await refreshConfigFromDisk(selected) }
    }
    func refreshConfigFromDisk(_ ball: String) async {
        do {
            let value = try await backend.request(DesktopRequest("config-sys", ball: ball), as: String.self)
            applyLiveConfig(value, ball: ball)
            configurationNotice = nil
        } catch { configurationNotice = "Could not refresh CONFIG.SYS: " + error.localizedDescription }
    }
    private func applyLiveConfig(_ text: String, ball: String) {
        if let draft = configDrafts[ball], draft == configBases[ball] {
            configDrafts.removeValue(forKey: ball); configBases.removeValue(forKey: ball)
        }
        liveConfigTexts[ball] = text
    }
    private func updateConfigObservation() {
        configObservation?.cancel(); configObservation = nil
        guard processRunning, let ball = selected else { return }
        configObservation = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refreshConfigFromDisk(ball)
                do { try await Task.sleep(for: .seconds(1)) } catch { break }
            }
        }
    }
    func configuration(_ area: String, ball: String) async throws -> ConfigurationSnapshot {
        var request = DesktopRequest("configuration-read", ball: ball); request.name = area
        return try await backend.request(request, as: ConfigurationSnapshot.self)
    }
    func runtimeFeatures(ball: String) async throws -> RuntimeFeatures {
        try await backend.request(DesktopRequest("runtime-features", ball: ball, build: build), as: RuntimeFeatures.self)
    }
    func configurationFiles(ball: String) async throws -> ConfigurationFiles {
        try await backend.request(DesktopRequest("configuration-files", ball: ball), as: ConfigurationFiles.self)
    }
    func settingsSnapshot(ball: String) async throws -> BallSnapshot {
        try await backend.request(DesktopRequest("snapshot", ball: ball), as: BallSnapshot.self)
    }
    func configurationFile(_ path: String, ball: String) async throws -> ConfigurationFileSnapshot {
        var request = DesktopRequest("configuration-file-read", ball: ball); request.path = path
        return try await backend.request(request, as: ConfigurationFileSnapshot.self)
    }
    func saveConfigurationFile(_ path: String, ball: String, revision: String, text: String,
                               application: String? = nil, key: String? = nil, remove: Bool = false) async throws -> ConfigurationFileSnapshot {
        guard !busy, !mediaBusy, !processRunning, !componentOperationRunning, selected == ball else { throw BallError("Finish this ball’s operations before editing OS/2 files.") }
        busy = true; defer { busy = false }
        var request = DesktopRequest("configuration-file-write", ball: ball, expected: revision, text: text)
        request.path = path; request.application = application; request.key = key; request.remove = remove
        return try await backend.request(request, as: ConfigurationFileSnapshot.self)
    }
    func saveConfiguration(_ area: String, ball: String, revision: String, application: String, key: String, data: String, remove: Bool = false) async throws -> ConfigurationSnapshot {
        guard !busy, !mediaBusy, !processRunning, !componentOperationRunning, selected == ball else { throw BallError("Finish this ball’s operations before editing profiles or the registry.") }
        busy = true; defer { busy = false }
        var request = DesktopRequest("configuration-write", ball: ball, expected: revision, text: data)
        request.name = area; request.path = application; request.key = key; request.remove = remove
        return try await backend.request(request, as: ConfigurationSnapshot.self)
    }
    func beginProgram(_ action: ProgramAction) {
        if componentOperationRunning, action != .install {
            componentNotice = "Finish or cancel this ball’s component job before starting or changing applications."
            return
        }
        editingApplication = nil
        if action == .install { page = .installation }
        else { programAction = action }
    }
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
                self.operation = "Checking installed components…"
                let audit = try await self.backend.request(DesktopRequest("ensure-dependencies", ball: selected, build: build), as: DependencyAudit.self)
                self.activity = audit.summary
                guard self.requestDependencyComponents(audit, ball: selected) != nil else {
                    self.operation = "Launch Cancelled"
                    try await self.reload(selected)
                    return
                }
                self.operation = "Starting…"
                result = try await self.backend.execute(ball: selected, build: build, arguments: arguments, workingDirectories: directories, environment: environment) { [weak self] text in
                    await self?.updateActivity(text)
                }
            } catch {
                self.operation = "Could Not Start"; self.activity = error.localizedDescription
                return
            }
            self.activity = result.output; self.exitCode = result.code; self.log = result.log
            if let audit = result.dependencyAudit { self.activity += "\n\n" + audit.summary }
            if let media {
                self.installCompletion = InstallCompletion(ball: selected, letter: media, code: result.code, signalled: result.signalled)
            }
            self.operation = result.signalled ? "Terminated by signal \(result.code)" : "Exited \(result.code)"
            if let error = result.receiptError { self.error = "The process exited, but its installer receipt could not be saved: " + error }
            if let audit = result.dependencyAudit, audit.needsMediaSelection {
                _ = self.requestDependencyComponents(audit, ball: selected)
            }
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
