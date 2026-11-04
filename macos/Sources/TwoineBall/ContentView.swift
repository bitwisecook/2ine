#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit

struct ContentView: View {
    @Bindable var model: AppModel
    @AppStorage("lastPage") private var lastPage = Page.drives.rawValue
    var body: some View {
        NavigationSplitView { sidebar } detail: { detail }
        .frame(minWidth: 880, minHeight: 590)
        .onAppear {
            switch lastPage {
            case "Configuration": model.page = .settings
            case "Activity": model.page = .activity
            default: model.page = Page(rawValue: lastPage) ?? .drives
            }
        }
        .onChange(of: model.page) { _, page in lastPage = page.rawValue }
        .dropDestination(for: URL.self) { urls, _ in
            guard !model.busy, !model.mediaBusy, let url = urls.first,
                  ["2ineball", "zip"].contains(url.pathExtension.lowercased()) || url.hasDirectoryPath else { return false }
            model.open(url, create: false); return true
        }
        .toolbar {
            ToolbarItemGroup {
                Button("Import Ball…", systemImage: "square.and.arrow.down") { model.chooseImportBall() }
                    .help("Import a ball from a ZIP archive")
                    .disabled(model.busy || model.mediaBusy).accessibilityIdentifier("importBall")
                Button("Export Ball…", systemImage: "square.and.arrow.up") { model.chooseExportBall() }
                    .help("Export the selected ball as a ZIP archive")
                    .disabled(model.snapshot == nil || model.busy || model.mediaBusy).accessibilityIdentifier("exportBall")
                MediaDeviceMenu(model: model, kind: .cdrom)
                MediaDeviceMenu(model: model, kind: .floppy)
                MediaDeviceMenu(model: model, kind: .disk)
                if model.busy || model.mediaBusy { ProgressView().controlSize(.small).accessibilityLabel(model.mediaBusy ? "Updating media" : "Operation in progress") }
                Button("Refresh", systemImage: "arrow.clockwise") { model.refresh() }.disabled(model.busy || model.mediaBusy)
                Button("Runtime…", systemImage: "cpu") { model.chooseRuntime() }.disabled(model.selected == nil || model.busy || model.mediaBusy)
            }
        }
        .sheet(item: $model.supportImport) { request in OS2SupportSheet(model: model, request: request) }
        .sheet(item: $model.programAction) { action in ProgramSheet(model: model, action: action) }
        .sheet(item: $model.ballRename) { request in RenameBallSheet(model: model, request: request) }
        .sheet(isPresented: Binding(get: { model.creatingFloppy != nil }, set: { if !$0 { model.creatingFloppy = nil } })) {
            if let letter = model.creatingFloppy { CreateFloppySheet(model: model, letter: letter) }
        }
        .sheet(isPresented: Binding(get: { model.creatingDisk != nil }, set: { if !$0 { model.creatingDisk = nil } })) {
            if let letter = model.creatingDisk { CreateDiskSheet(model: model, letter: letter) }
        }
        .sheet(item: $model.installCompletion) { completion in InstallCompletionSheet(model: model, completion: completion) }
        .sheet(isPresented: $model.audioEditor) {
            if let selected = model.selected { AudioSheet(model: model, ball: selected) }
        }
        .sheet(isPresented: $model.mapping, onDismiss: { model.mappingDrive = nil }) {
            MapDriveSheet(model: model, initialLetter: model.mappingDrive,
                          initialSource: model.mappingDrive.flatMap { model.snapshot?.manifest.drives[$0]?.source } ?? "")
        }
        .confirmationDialog("Stop the running operation?", isPresented: $model.stopConfirmation) {
            Button("Stop Operation", role: .destructive) { model.stopProcess() }
        } message: { Text("Unsaved work in the guest may be lost. Its output log will be kept.") }
        .confirmationDialog("Move \(model.ballToTrash.map { "“" + AppModel.displayName($0.path) + "”" } ?? "ball") to the Trash?",
                            isPresented: Binding(get: { model.ballToTrash != nil }, set: { if !$0 { model.ballToTrash = nil } }),
                            titleVisibility: .visible) {
            Button("Move to Trash", role: .destructive) {
                guard let request = model.ballToTrash else { return }
                model.ballToTrash = nil
                Task { do { try await model.trashBall(request.path, expectedIdentity: request.identity) } catch { model.error = error.localizedDescription } }
            }
            Button("Cancel", role: .cancel) { model.ballToTrash = nil }
        } message: {
            Text("This moves the entire ball, including its installed applications and documents, to the Trash. You can restore it using Finder.")
            if let request = model.ballToTrash, let draft = model.configDrafts[request.path], draft != model.configBases[request.path] {
                Text("Unsaved configuration edits in this ball will also be discarded.")
            }
        }
        .confirmationDialog("Force stop the running operation?", isPresented: $model.forceStopConfirmation) {
            Button("Force Stop", role: .destructive) { model.stopProcess(force: true) }
        } message: { Text("Use this if the guest does not respond to Stop. Unsaved work may be lost. Its output log will be kept.") }
        .alert("2ine", isPresented: Binding(get: { model.error != nil }, set: { if !$0 { model.error = nil } })) {
            Button("OK", role: .cancel) { model.error = nil }
        } message: { Text(model.error ?? "") }
    }
    private var sidebar: some View {
        List(selection: Binding(get: { model.selected }, set: { if let path = $0 { model.select(path) } })) {
            Section("Ball") {
                ForEach(model.paths, id: \.self) { path in
                    Label {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(AppModel.displayName(path)).font(.headline)
                            Text(DisplayPath.homeAbbreviated(path)).font(.caption).foregroundStyle(.secondary).lineLimit(2)
                        }
                    } icon: { Image(systemName: "shippingbox") }
                    .tag(path).help(DisplayPath.homeAbbreviated(path))
                    .draggable(URL(filePath: path))
                    .contextMenu {
                        Button("Rename Ball…") { model.beginRenameBall(path) }
                            .disabled(!model.canManageBall(path) || !FileManager.default.fileExists(atPath: path))
                        if FileManager.default.fileExists(atPath: path) {
                            Button("Move Ball to Trash…", role: .destructive) { model.beginTrashBall(path) }
                                .disabled(!model.canManageBall(path))
                        } else {
                            Button("Remove Missing Ball from List") { model.removeMissingBall(path) }
                                .disabled(!model.canManageBall(path))
                        }
                        Divider()
                        Button("Open in Finder") { model.reveal(path) }
                        Button("Copy Ball Path") { NSPasteboard.general.clearContents(); NSPasteboard.general.setString(path, forType: .string) }
                    }
                }
            }
        }
        .accessibilityIdentifier("ballList")
        .disabled(model.busy || model.mediaBusy)
        .navigationSplitViewColumnWidth(min: 210, ideal: 250, max: 340)
        .safeAreaInset(edge: .bottom) {
            HStack {
                Button("New Ball", systemImage: "plus") { model.chooseBall(create: true) }
                Button("Open", systemImage: "folder") { model.chooseBall(create: false) }
            }.padding().disabled(model.busy || model.mediaBusy)
        }
    }
    @ViewBuilder private var detail: some View {
        if let snapshot = model.snapshot {
            VStack(spacing: 0) {
                Picker("Ball section", selection: $model.page) {
                    ForEach(Page.allCases) { Text($0.rawValue).tag($0) }
                }.labelsHidden().pickerStyle(.segmented).padding()
                Divider()
                Group {
                    switch model.page {
                    case .applications: ApplicationsView(model: model, snapshot: snapshot)
                    case .drives: DrivesView(model: model, snapshot: snapshot)
                    case .settings: ConfigurationView(model: model).id(model.selected)
                    case .activity: ActivityView(model: model)
                    }
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .navigationTitle(model.title)
            .navigationSubtitle(model.runtimeDescription)
        } else {
            ContentUnavailableView {
                Label(model.selected == nil ? "Your OS/2 applications, at home" : "Cannot Open Ball", systemImage: "shippingbox")
            } description: {
                Text(model.selected == nil ? "Create a ball, map your installation media, and install an application." : "Check the folder and its mapped drives, then refresh or open another ball.")
            } actions: {
                Button("Create Ball…") { model.chooseBall(create: true) }
                Button("Open Ball…") { model.chooseBall(create: false) }
            }
        }
    }

}

struct ApplicationRow: Identifiable {
    let name: String
    let program: String
    let cwd: String
    var icon: NSImage? = nil
    var id: String { name }
}

struct ApplicationsView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @State private var selection: Set<String> = []
    @State private var sortOrder = [KeyPathComparator(\ApplicationRow.name)]
    @SceneStorage("applicationColumns") private var columns: TableColumnCustomization<ApplicationRow>
    var rows: [ApplicationRow] {
        snapshot.manifest.applications.map { ApplicationRow(name: $0.key, program: $0.value.program, cwd: $0.value.workingDirectorySummary, icon: model.applicationIcons[$0.key]) }.sorted(using: sortOrder)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Button("Install Software…", systemImage: "shippingbox.and.arrow.backward") { model.beginProgram(.install) }
                    .accessibilityIdentifier("installSoftware")
                Button("Run Executable…", systemImage: "play") { model.beginProgram(.run) }
                Spacer()
                Button("Add Application…", systemImage: "plus") { model.beginProgram(.save) }
            }.disabled(model.busy || model.mediaBusy)
            if let support = snapshot.os2Support {
                HStack {
                    VStack(alignment: .leading) {
                        Text("OS/2 Installer Support").font(.headline)
                        Text(support.complete ? "CMD support is installed. Add more OS/2 components from your media." : "Missing: " + support.missing).font(.callout).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Button("Install Components…") { model.installComponents() }
                        .disabled(model.busy || model.mediaBusy).accessibilityIdentifier("setupOS2Support")
                }
            }
            Table(rows, selection: $selection, sortOrder: $sortOrder, columnCustomization: $columns) {
                TableColumn("Application", value: \.name) { row in
                    HStack {
                        if let icon = row.icon { Image(nsImage: icon).resizable().interpolation(.none).scaledToFit().frame(width: 28, height: 28).accessibilityHidden(true) }
                        else { Image(systemName: "app.dashed").frame(width: 28, height: 28).accessibilityHidden(true) }
                        Text(row.name)
                    }
                }.customizationID("name")
                TableColumn("Executable", value: \.program)
                    .customizationID("program")
                TableColumn("Working Directory", value: \.cwd)
                    .customizationID("cwd")
            }
            .accessibilityIdentifier("applicationsTable")
            .accessibilityLabel("Saved applications")
            .overlay { if rows.isEmpty { ContentUnavailableView("No Saved Applications", systemImage: "app.dashed", description: Text("Install your software, then add its executable here.")) } }
            .contextMenu(forSelectionType: String.self) { names in
                if let name = names.first, names.count == 1 {
                    Button("Launch \(name)") { model.execute(["launch", name]) }.disabled(model.busy || model.mediaBusy)
                    Button("Edit Application…") { model.editApplication(name) }.disabled(model.busy || model.mediaBusy)
                    Button("Remove Launcher") { model.command(["remove-app", name]) }.disabled(model.busy || model.mediaBusy)
                }
                Button("Copy") { copy(names) }.disabled(names.isEmpty)
            } primaryAction: { names in if let name = names.first { model.execute(["launch", name]) } }
            .onCopyCommand { rows.filter { selection.contains($0.id) }.map { NSItemProvider(object: "\($0.name)\t\($0.program)\t\($0.cwd)" as NSString) } }
            .onDeleteCommand { if selection.count == 1, let name = selection.first { model.command(["remove-app", name]) } }
            HStack {
                Button("Launch", systemImage: "play.fill") { if let name = selection.first { model.execute(["launch", name]) } }
                    .disabled(selection.count != 1 || model.busy || model.mediaBusy)
                Button("Edit…") { if let name = selection.first { model.editApplication(name) } }.disabled(selection.count != 1 || model.busy || model.mediaBusy)
                Button("Remove Launcher", systemImage: "minus") { if let name = selection.first { model.command(["remove-app", name]) } }
                    .disabled(selection.count != 1 || model.busy || model.mediaBusy)
                Spacer()
                Text("Removing a launcher keeps installed files.").font(.caption).foregroundStyle(.secondary)
            }
        }.padding(20)
        .focusedSceneValue(\.ballViewActions, BallViewActions(
            launch: selection.count == 1 ? { launchSelection() } : nil,
            edit: selection.count == 1 ? { if let name = selection.first { model.editApplication(name) } } : nil,
            remove: selection.count == 1 ? { if let name = selection.first { model.command(["remove-app", name]) } } : nil))
    }
    private func launchSelection() { if let name = selection.first { model.execute(["launch", name]) } }
    private func copy(_ names: Set<String>) {
        let text = rows.filter { names.contains($0.id) }.map { "\($0.name)\t\($0.program)\t\($0.cwd)" }.joined(separator: "\n")
        NSPasteboard.general.clearContents(); NSPasteboard.general.setString(text, forType: .string)
    }
}

struct ActivityView: View {
    @Bindable var model: AppModel
    @AppStorage("logFontSize") private var logFontSize = 12.0
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Label(model.operation, systemImage: model.busy ? "clock" : "terminal")
                Spacer()
                if model.processRunning {
                    Button("Stop Operation", systemImage: "stop.fill") { model.stopConfirmation = true }.disabled(model.stopRequested)
                    if model.stopRequested { Button("Force Stop…") { model.forceStopConfirmation = true } }
                }
                if let log = model.log { Button("Open Full Log") { model.openFile(log.path) } }
                if let audit = model.dependencyAuditLog { Button("Open Dependency Audit") { model.openFile(audit.path) }.accessibilityIdentifier("openDependencyAudit") }
                if let registrations = model.applicationRegistrationLog { Button("Open Application Registrations") { model.openFile(registrations.path) } }
            }
            ScrollView([.horizontal, .vertical]) {
                Text(model.activity).font(.system(size: logFontSize, design: .monospaced))
                    .textSelection(.enabled).frame(maxWidth: .infinity, alignment: .topLeading).padding(12)
            }.background(.background.secondary).border(.separator)
                .accessibilityIdentifier("processOutput")
            Text("The last 64 KiB appears here. Full output is saved in the ball’s logs folder. An installer’s exit code does not verify application functionality.")
                .font(.caption).foregroundStyle(.secondary)
        }.padding(20)
    }
}
