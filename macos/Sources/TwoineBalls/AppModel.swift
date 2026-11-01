import AppKit
#if canImport(BallCore)
import BallCore
#endif
import Observation
import UniformTypeIdentifiers

enum Page: String, CaseIterable, Identifiable {
    case applications = "Applications", drives = "Drives", settings = "Settings", config = "CONFIG.SYS", activity = "Activity"
    var id: String { rawValue }
}
enum ProgramAction: String, Identifiable {
    case run = "Run Executable", install = "Install Software", save = "Add Application"
    var id: String { rawValue }
}

@MainActor @Observable
final class AppModel {
    var paths: [String] = []
    var selected: String?
    var snapshot: BallSnapshot?
    var page: Page = .applications
    var busy = false
    var activity = "No process has run in this session."
    var operation = "Ready"
    var log: URL?
    var exitCode: Int32?
    var error: String?
    var programAction: ProgramAction?
    var mapping = false
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
        guard !busy else { throw BallError("Wait for the operation to finish.") }
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
    var title: String { selected.map(Self.displayName) ?? "2ine Balls" }
    var runtimeDescription: String {
        build == bundledRuntime ? "Bundled 2ine Runtime" : "Runtime: " + URL(filePath: build).lastPathComponent
    }

    init() {
        let resources = ProcessInfo.processInfo.environment["TWOINE_MACOS_RESOURCES"].map { URL(filePath: $0) }
            ?? (Bundle.main.object(forInfoDictionaryKey: "TwoineDeveloperResources") as? String).map { URL(filePath: $0) }
            ?? Bundle.main.resourceURL ?? Bundle.main.bundleURL
        bundledRuntime = resources.appending(path: "Runtime").path
        backend = BallBackend()
        let override = ProcessInfo.processInfo.environment["TWOINE_MACOS_PREFERENCES"]
        let path = override.map { URL(filePath: $0) } ?? FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/2ine/balls-macos.json")
        preferences = PreferenceStore(url: path)
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
        guard !busy else { return }
        busy = true
        Task {
            defer { busy = false }
            do { try await work() }
            catch { self.error = error.localizedDescription }
        }
    }

    private func reload(_ ball: String) async throws {
        snapshot = try await backend.request(DesktopRequest("snapshot", ball: ball), as: BallSnapshot.self)
        revision += 1
        prefs.balls = paths; prefs.selected = ball
        try await preferences.save(prefs)
    }

    func select(_ path: String) {
        guard !busy else { return }
        selected = path; snapshot = nil
        perform { try await self.reload(path) }
    }
    func refresh() {
        guard let selected else { start(); return }
        perform { try await self.reload(selected) }
    }
    func chooseBall(create: Bool) {
        guard !busy else { return }
        if create {
            let panel = NSSavePanel()
            panel.title = "Create Ball"; panel.nameFieldLabel = "Ball name:"
            panel.nameFieldStringValue = "My OS2 Ball.2ineball"; panel.canCreateDirectories = true
            panel.allowedContentTypes = [UTType(exportedAs: "io.github.bitwisecook.2ine.ball", conformingTo: .package)]
            panel.prompt = "Create"
            let defaultFolder = UserDefaults.standard.string(forKey: "ballFolder")
            panel.directoryURL = defaultFolder.map { URL(filePath: $0) } ?? FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
            if panel.runModal() == .OK, let url = panel.url { open(url, create: true) }
        } else if let url = Self.pick(directory: true, title: "Open Ball") { open(url, create: false) }
    }
    func open(_ url: URL, create: Bool) {
        guard !busy else { return }
        perform {
            let path = url.resolvingSymlinksInPath().path
            let message = DesktopRequest(create ? "command" : "snapshot", ball: path,
                                         arguments: create ? ["init"] : nil)
            let value = try await self.backend.request(message, as: BallSnapshot.self)
            if !self.paths.contains(path) { self.paths.append(path); self.paths.sort() }
            self.selected = path; self.snapshot = value; self.revision += 1
            NSDocumentController.shared.noteNewRecentDocumentURL(url)
            self.prefs.balls = self.paths; self.prefs.selected = path
            try await self.preferences.save(self.prefs)
        }
    }
    static func pick(directory: Bool, title: String) -> URL? {
        let panel = NSOpenPanel()
        panel.title = title; panel.canChooseDirectories = directory
        panel.canChooseFiles = !directory; panel.allowsMultipleSelection = false
        panel.treatsFilePackagesAsDirectories = directory
        return panel.runModal() == .OK ? panel.url : nil
    }
    func chooseRuntime() {
        guard let selected, !busy, let url = Self.pick(directory: true, title: "Choose 2ine Build (containing lx_loader)") else { return }
        perform {
            self.snapshot = try await self.backend.request(DesktopRequest("runtime-build", ball: selected, build: url.path), as: BallSnapshot.self)
            self.revision += 1
        }
    }
    func command(_ arguments: [String], undo: UndoManager? = nil) {
        guard !busy, let selected else { return }
        applyCommand(arguments, inverse: inverseCommand(arguments), ball: selected, undo: undo)
    }
    private func applyCommand(_ arguments: [String], inverse: [String]?, ball: String, undo: UndoManager?) {
        guard !busy else { return }
        let build = self.build
        if let undo, let inverse {
            // Register synchronously so UndoManager can build its redo group.
            // Capture the ball; changing sidebar selection must not redirect Undo.
            undo.registerUndo(withTarget: self) { [weak undo] model in
                model.applyCommand(inverse, inverse: arguments, ball: ball, undo: undo)
            }
            undo.setActionName("Ball Change")
        }
        perform {
            let value = try await self.backend.request(DesktopRequest("command", ball: ball, build: build, arguments: arguments), as: BallSnapshot.self)
            if self.selected == ball { self.snapshot = value; self.revision += 1 }
        }
    }
    private func inverseCommand(_ args: [String]) -> [String]? {
        guard args.count >= 2, let manifest = snapshot?.manifest else { return nil }
        let action = args[0], key = args[1]
        if action == "map" || action == "unmap" {
            return manifest.drives[key].map { ["map", key, $0.hasPrefix("/") ? $0 : (selected! + "/" + $0)] } ?? ["unmap", key]
        }
        if action == "config" || action == "unset" {
            let parts = key.split(separator: ".", maxSplits: 1).map(String.init)
            guard parts.count == 2 else { return nil }
            let old = parts[0] == "env" ? manifest.environment[parts[1]] : parts[0] == "runtime" ? manifest.runtime[parts[1]] : manifest.system[parts[1]]?.text
            return old.map { ["config", key, $0] } ?? ["unset", key]
        }
        if action == "remove-app", let app = manifest.applications[key] {
            return ["add-app"] + (app.cwd.map { ["--cwd", $0] } ?? []) + [key, app.program] + app.arguments
        }
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
    func program(_ action: ProgramAction, name: String, program: String, arguments: String, cwd: String) {
        do {
            let argv = try Arguments.parse(arguments)
            let working = cwd.isEmpty ? [] : ["--cwd", cwd]
            if action == .save {
                guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { throw BallError("Enter an application name.") }
                if snapshot?.manifest.applications[name] != nil { throw BallError("That name already exists. Remove its launcher or choose a different name.") }
                command(["add-app"] + working + [name, program] + argv)
            } else { execute([action == .install ? "install" : "run"] + working + [program] + argv) }
            programAction = nil
        } catch { self.error = error.localizedDescription }
    }
    func execute(_ arguments: [String]) {
        guard let selected, !busy else { return }
        guard !build.isEmpty else { error = "Choose a runtime build before launching software."; return }
        let build = self.build
        page = .activity; activity = "Starting…"; operation = arguments.first ?? "Run"
        exitCode = nil; log = nil
        perform {
            let result: ProcessResult
            do {
                result = try await self.backend.execute(ball: selected, build: build, arguments: arguments) { [weak self] text in
                    await self?.updateActivity(text)
                }
            } catch {
                self.operation = "Could Not Start"; self.activity = error.localizedDescription
                return
            }
            self.activity = result.output; self.exitCode = result.code; self.log = result.log
            self.operation = result.signalled ? "Terminated by signal \(result.code)" : "Exited \(result.code)"
            if let error = result.receiptError { self.error = "The process exited, but its installer receipt could not be saved: " + error }
            try await self.reload(selected)
        }
    }
    private func updateActivity(_ text: String) { activity = text }
    func reveal(_ path: String) { NSWorkspace.shared.open(URL(filePath: path)) }
}
