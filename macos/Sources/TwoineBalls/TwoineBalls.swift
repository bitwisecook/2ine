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
        if model?.busy == true {
            model?.error = "Close the guest application's windows and wait for the operation to finish before quitting Balls."
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
        if model?.busy == true {
            model?.error = "The operation is still running. Close its guest windows and wait for its exit code before closing Balls."
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
    private func confirmSave() -> NSApplication.ModalResponse {
        let alert = NSAlert()
        alert.messageText = "Save changes to CONFIG.SYS?"
        alert.informativeText = "Your unsaved edits will be lost if you close Balls."
        alert.addButton(withTitle: "Save"); alert.addButton(withTitle: "Cancel"); alert.addButton(withTitle: "Don’t Save")
        return alert.runModal()
    }
}

@main
struct TwoineBallsApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @State private var model = AppModel()
    var body: some Scene {
        // One manager owns one operation. Avoid concurrent edits from duplicate windows.
        WindowGroup("2ine Balls", id: "balls") {
            ContentView(model: model)
                .onAppear { delegate.model = model; model.start() }
                .onOpenURL { model.open($0, create: false) }
        }
        .defaultSize(width: 1100, height: 740)
        .defaultLaunchBehavior(.presented)
        .windowResizability(.contentMinSize)
        .commands {
            SidebarCommands()
            CommandGroup(replacing: .newItem) {
                Button("New Ball…") { model.chooseBall(create: true) }.keyboardShortcut("n").disabled(model.busy)
                Button("Open Ball…") { model.chooseBall(create: false) }.keyboardShortcut("o").disabled(model.busy)
                Menu("Open Recent") {
                    ForEach(NSDocumentController.shared.recentDocumentURLs, id: \.self) { url in
                        Button(url.deletingPathExtension().lastPathComponent) { model.open(url, create: false) }
                    }
                    Divider()
                    Button("Clear Menu") { NSDocumentController.shared.clearRecentDocuments(nil) }
                }.disabled(model.busy)
            }
            CommandGroup(after: .sidebar) {
                ForEach(Array(Page.allCases.enumerated()), id: \.element.id) { index, page in
                    Button(page.rawValue) { model.page = page }.keyboardShortcut(KeyEquivalent(Character(String(index + 1))))
                }
            }
            CommandMenu("Ball") {
                Button("Choose Runtime…") { model.chooseRuntime() }.keyboardShortcut("r", modifiers: [.command, .shift])
                    .disabled(model.selected == nil || model.busy)
                Button("Refresh") { model.refresh() }.keyboardShortcut("r").disabled(model.busy)
                Divider()
                Button("Install Software…") { model.programAction = .install }.keyboardShortcut("i").disabled(model.snapshot == nil || model.busy)
                Button("Run Executable…") { model.programAction = .run }.keyboardShortcut("r", modifiers: [.command, .option]).disabled(model.snapshot == nil || model.busy)
                Button("Add Application…") { model.programAction = .save }.keyboardShortcut("a", modifiers: [.command, .shift]).disabled(model.snapshot == nil || model.busy)
                Button("Map Drive…") { model.mapping = true }.keyboardShortcut("d", modifiers: [.command, .shift]).disabled(model.snapshot == nil || model.busy)
                Divider()
                Button("Open Ball in Finder") { if let path = model.selected { model.reveal(path) } }.disabled(model.selected == nil)
            }
            CommandGroup(replacing: .help) {
                Button("2ine Balls Help") { NSWorkspace.shared.open(URL(string: "https://github.com/bitwisecook/2ine/blob/main/docs/balls.md")!) }
            }
        }
        Settings { AppSettingsView() }
    }
}

struct AppSettingsView: View {
    @AppStorage("ballFolder") private var folder = ""
    @AppStorage("logFontSize") private var logFontSize = 12.0
    var body: some View {
        Form {
            Section("New Balls") {
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
