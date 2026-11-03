import SwiftUI
import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    weak var model: AppModel?
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate()
        for window in NSApp.windows where window.canBecomeMain {
            window.delegate = self
            window.contentView?.setAccessibilityLabel("Ball manager")
        }
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if model?.busy == true || model?.mediaBusy == true {
            model?.error = "Stop the running guest from the Ball menu before quitting. Other operations must finish before quitting."
            return .terminateCancel
        }
        guard let model, model.hasUnsavedConfigs else { return .terminateNow }
        switch confirmSave() {
        case .alertFirstButtonReturn:
            Task {
                do { try await model.saveAllConfigDrafts(); sender.reply(toApplicationShouldTerminate: true) }
                catch { model.error = error.localizedDescription; sender.reply(toApplicationShouldTerminate: false) }
            }
            return .terminateLater
        case .alertThirdButtonReturn: model.discardAllConfigDrafts(); return .terminateNow
        default: return .terminateCancel
        }
    }
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if model?.busy == true || model?.mediaBusy == true {
            model?.error = "Stop the running guest from the Ball menu before closing. Other operations must finish before closing."
            return false
        }
        guard let model, model.hasUnsavedConfigs else { return true }
        switch confirmSave() {
        case .alertFirstButtonReturn:
            Task {
                do { try await model.saveAllConfigDrafts(); sender.performClose(nil) }
                catch { model.error = error.localizedDescription }
            }
            return false
        case .alertThirdButtonReturn: model.discardAllConfigDrafts(); return true
        default: return false
        }
    }
    func windowWillReturnUndoManager(_ window: NSWindow) -> UndoManager? { model?.undoManager }
    private func confirmSave() -> NSApplication.ModalResponse {
        let alert = NSAlert()
        alert.messageText = "Save changes to CONFIG.SYS?"
        alert.informativeText = "Your unsaved edits will be lost if you close Ball."
        alert.addButton(withTitle: "Save"); alert.addButton(withTitle: "Cancel"); alert.addButton(withTitle: "Don’t Save")
        return alert.runModal()
    }
}

private struct ManagerWindowAttachment: NSViewRepresentable {
    let delegate: AppDelegate
    func makeNSView(context: Context) -> ManagerWindowView {
        let view = ManagerWindowView(); view.appDelegate = delegate; return view
    }
    func updateNSView(_ view: ManagerWindowView, context: Context) { view.appDelegate = delegate }
}
private final class ManagerWindowView: NSView {
    weak var appDelegate: AppDelegate?
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.delegate = appDelegate
        window?.contentView?.setAccessibilityLabel("Ball manager")
    }
}

@main
struct TwoineBallApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @State private var model = AppModel()
    var body: some Scene {
        // One manager owns one operation. Avoid concurrent edits from duplicate windows.
        WindowGroup("2ine", id: "ball") {
            ContentView(model: model)
                .background(ManagerWindowAttachment(delegate: delegate))
                .onAppear { delegate.model = model; model.start() }
                .onOpenURL { model.open($0, create: false) }
        }
        .defaultSize(width: 1100, height: 740)
        .defaultLaunchBehavior(.presented)
        .windowResizability(.contentMinSize)
        .commands { BallCommands(model: model) }
        Settings { AppSettingsView() }
        Window("About 2ine", id: "about") { ProjectAboutView(model: model) }
            .defaultSize(width: 640, height: 660)
            .defaultLaunchBehavior(.suppressed)
            .windowResizability(.contentMinSize)
    }
}

struct BallCommands: Commands {
    @Bindable var model: AppModel
    @FocusedValue(\.ballViewActions) private var actions
    @Environment(\.openWindow) private var openWindow
    var body: some Commands {
        SidebarCommands()
        CommandGroup(replacing: .appInfo) {
            Button("About 2ine") { openWindow(id: "about") }
        }
        CommandGroup(replacing: .undoRedo) { BallUndoMenu(model: model) }
        CommandGroup(replacing: .newItem) {
            Button("New Ball…") { model.chooseBall(create: true) }.keyboardShortcut("n").disabled(model.busy || model.mediaBusy)
            Button("Open Ball…") { model.chooseBall(create: false) }.keyboardShortcut("o").disabled(model.busy || model.mediaBusy)
            Menu("Open Recent") {
                ForEach(NSDocumentController.shared.recentDocumentURLs, id: \.self) { url in
                    Button(url.deletingPathExtension().lastPathComponent) { model.open(url, create: false) }
                }
                Divider()
                Button("Clear Menu") { NSDocumentController.shared.clearRecentDocuments(nil) }
            }.disabled(model.busy || model.mediaBusy)
        }
        CommandGroup(after: .sidebar) {
            ForEach(Array(Page.allCases.enumerated()), id: \.element.id) { index, page in
                Button(page.rawValue) { model.page = page }.keyboardShortcut(KeyEquivalent(Character(String(index + 1))))
            }
        }
        CommandGroup(replacing: .saveItem) {
            Button("Save CONFIG.SYS") { model.configSys(model.configText, expected: model.configBase) }
                .keyboardShortcut("s").disabled(model.busy || !model.configDirty || model.mediaBusy)
            Divider()
            Button("Import Ball from ZIP…") { model.chooseImportBall() }
                .disabled(model.busy || model.mediaBusy)
            Button("Export Ball as ZIP…") { model.chooseExportBall() }
                .keyboardShortcut("e", modifiers: [.command, .option])
                .disabled(model.snapshot == nil || model.busy || model.mediaBusy)
        }
        CommandMenu("Ball") {
            Button("Rename Ball…") { if let path = model.selected { model.beginRenameBall(path) } }
                .disabled(model.selected == nil || !model.canManageBalls || model.snapshot == nil)
            if let path = model.selected, !FileManager.default.fileExists(atPath: path) {
                Button("Remove Missing Ball from List") { model.removeMissingBall(path) }.disabled(!model.canManageBalls)
            } else {
                Button("Move Ball to Trash…", role: .destructive) { if let path = model.selected { model.beginTrashBall(path) } }
                    .disabled(model.selected == nil || !model.canManageBalls)
            }
            Divider()
            Button("Choose Runtime…") { model.chooseRuntime() }.keyboardShortcut("r", modifiers: [.command, .shift])
                .disabled(model.selected == nil || model.busy || model.mediaBusy)
            Button("Use Bundled Runtime") { model.useBundledRuntime() }
                .disabled(model.snapshot?.manifest.runtime["build"] == nil || model.busy || model.mediaBusy)
            Button("Refresh") { model.refresh() }.keyboardShortcut("r").disabled(model.busy || model.mediaBusy)
            Divider()
            Button("Install Components…") { model.installComponents() }.disabled(model.snapshot == nil || model.busy || model.mediaBusy)
            Button("Check and Restore System Libraries…") { model.auditDependencies() }.disabled(model.snapshot == nil || model.busy || model.mediaBusy)
            Button("Install Software…") { model.beginProgram(.install) }.keyboardShortcut("i").disabled(model.snapshot == nil || model.busy || model.mediaBusy)
            Button("Run Executable…") { model.beginProgram(.run) }.keyboardShortcut("r", modifiers: [.command, .option]).disabled(model.snapshot == nil || model.busy || model.mediaBusy)
            Button("Add Application…") { model.beginProgram(.save) }.keyboardShortcut("a", modifiers: [.command, .shift]).disabled(model.snapshot == nil || model.busy || model.mediaBusy)
            Button("Launch Application") { actions?.launch?() }.keyboardShortcut(.return, modifiers: .command)
                .disabled(actions?.launch == nil || model.busy || model.mediaBusy)
            Button("Edit Application…") { actions?.edit?() }.keyboardShortcut("e", modifiers: [.command, .shift])
                .disabled(actions?.edit == nil || model.busy || model.mediaBusy)
            Button("Remove Launcher") { actions?.remove?() }.disabled(actions?.remove == nil || model.busy || model.mediaBusy)
            Divider()
            Button("Import Files…") { actions?.importFiles?() }.keyboardShortcut("i", modifiers: [.command, .shift])
                .disabled(actions?.importFiles == nil || model.busy || model.mediaBusy)
            Button("Map Drive…") { model.mapping = true }.keyboardShortcut("d", modifiers: [.command, .shift]).disabled(model.snapshot == nil || model.busy || model.mediaBusy)
            Button("Change Mountpoint…") { actions?.changeMountpoint?() }
                .disabled(actions?.changeMountpoint == nil || model.busy || model.mediaBusy)
            Divider()
            MediaDeviceMenu(model: model, kind: .cdrom)
            MediaDeviceMenu(model: model, kind: .floppy)
            MediaDeviceMenu(model: model, kind: .disk)
            Divider()
            Button("Unmap Drive") { actions?.unmap?() }.disabled(actions?.unmap == nil || model.busy || model.mediaBusy)
            Button("Set Default Folder…") { actions?.setDefaultFolder?() }.disabled(actions?.setDefaultFolder == nil || model.busy || model.mediaBusy)
            Divider()
            Button("Stop Operation…") { model.stopConfirmation = true }.keyboardShortcut(".")
                .disabled(!model.processRunning || model.stopRequested)
            Button("Force Stop Operation…") { model.forceStopConfirmation = true }
                .disabled(!model.processRunning || !model.stopRequested)
            Divider()
            Button("Show Ball in Finder") { if let path = model.selected { model.reveal(path) } }.disabled(model.selected == nil)
        }
        CommandGroup(replacing: .help) {
            Button("2ine Help") { NSWorkspace.shared.open(URL(string: "https://github.com/bitwisecook/2ine/blob/main/docs/ball.md")!) }
        }
    }
}

private struct UndoMenuState: Equatable {
    let manager: ObjectIdentifier
    let undo: String
    let redo: String
    let canUndo: Bool
    let canRedo: Bool
}

private struct BallUndoMenu: View {
    @Bindable var model: AppModel
    @State private var state: UndoMenuState?
    private var manager: UndoManager {
        if let editor = NSApp.keyWindow?.firstResponder as? NSTextView, editor.isEditable,
           let undo = editor.undoManager { return undo }
        return model.undoManager
    }
    var body: some View {
        let _ = state
        let undo = manager
        Group {
            Button(undo.undoMenuItemTitle) { manager.undo(); refresh() }
                .keyboardShortcut("z").disabled(model.busy || !undo.canUndo || model.mediaBusy)
            Button(undo.redoMenuItemTitle) { manager.redo(); refresh() }
                .keyboardShortcut("z", modifiers: [.command, .shift]).disabled(model.busy || !undo.canRedo || model.mediaBusy)
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didUpdateNotification)) { _ in refresh() }
        .onReceive(NotificationCenter.default.publisher(for: .NSUndoManagerDidCloseUndoGroup)) { _ in refresh() }
        .onReceive(NotificationCenter.default.publisher(for: .NSUndoManagerDidUndoChange)) { _ in refresh() }
        .onReceive(NotificationCenter.default.publisher(for: .NSUndoManagerDidRedoChange)) { _ in refresh() }
    }
    private func refresh() {
        let undo = manager
        let next = UndoMenuState(manager: ObjectIdentifier(undo), undo: undo.undoMenuItemTitle,
                                 redo: undo.redoMenuItemTitle, canUndo: undo.canUndo, canRedo: undo.canRedo)
        if state != next { state = next }
    }
}

struct AppSettingsView: View {
    @AppStorage("ballFolder") private var folder = ""
    @AppStorage("logFontSize") private var logFontSize = 12.0
    var body: some View {
        Form {
            Section("New Ball") {
                LabeledContent("Default folder", value: folder.isEmpty ? "Documents" : folder)
                Button("Choose Folder…") { if let url = AppModel.pick(directory: true, title: "Default Ball Folder") { folder = url.path } }
                Button("Reset to Documents") { folder = "" }
            }
            Section("Activity") {
                Slider(value: $logFontSize, in: 10...24, step: 1) { Text("Log text size") }
                Text("\(Int(logFontSize)) pt").foregroundStyle(.secondary)
            }
        }.formStyle(.grouped).frame(width: 500, height: 300)
    }
}
