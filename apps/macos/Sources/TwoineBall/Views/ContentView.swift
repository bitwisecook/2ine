import AppKit
import SwiftUI

#if canImport(BallCore)
import BallCore
#endif

struct ContentView: View {
    @Bindable var model: AppModel
    var body: some View {
        NavigationSplitView {
            sidebar
        } detail: {
            detail
        }
        .frame(minWidth: 880, minHeight: 590)
        .dropDestination(for: URL.self) { urls, _ in
            guard !model.busy, !model.mediaBusy, let url = urls.first,
                ["2ineball", "zip"].contains(url.pathExtension.lowercased()) || url.hasDirectoryPath
            else { return false }
            model.open(url, create: false)
            return true
        }
        .toolbar {
            ToolbarItemGroup {
                Button("Import Ball…", systemImage: "square.and.arrow.down") {
                    model.chooseImportBall()
                }
                .help("Import a ball from a ZIP archive")
                .disabled(model.busy || model.mediaBusy).accessibilityIdentifier("importBall")
                Button("Export Ball…", systemImage: "square.and.arrow.up") {
                    model.chooseExportBall()
                }
                .help("Export the selected ball as a ZIP archive")
                .disabled(
                    model.snapshot == nil || model.busy || model.mediaBusy || model.processRunning
                        || model.componentOperationRunning
                ).accessibilityIdentifier("exportBall")
                MediaDeviceMenu(model: model, kind: .cdrom)
                MediaDeviceMenu(model: model, kind: .floppy)
                MediaDeviceMenu(model: model, kind: .disk)
                if model.busy || model.mediaBusy {
                    ProgressView().controlSize(.small).accessibilityLabel(
                        model.mediaBusy ? "Updating media" : "Operation in progress")
                }
                Button("Refresh", systemImage: "arrow.clockwise") { model.refresh() }.disabled(
                    model.busy || model.mediaBusy)
                Button("Runtime…", systemImage: "cpu") { model.chooseRuntime() }.disabled(
                    model.selected == nil || model.busy || model.mediaBusy)
            }
        }
        .sheet(item: $model.programAction) { action in ProgramSheet(model: model, action: action) }
        .sheet(item: $model.ballRename) { request in RenameBallSheet(model: model, request: request)
        }
        .sheet(
            isPresented: Binding(
                get: { model.creatingFloppy != nil }, set: { if !$0 { model.creatingFloppy = nil } }
            )
        ) {
            if let letter = model.creatingFloppy { CreateFloppySheet(model: model, letter: letter) }
        }
        .sheet(
            isPresented: Binding(
                get: { model.creatingDisk != nil }, set: { if !$0 { model.creatingDisk = nil } })
        ) {
            if let letter = model.creatingDisk { CreateDiskSheet(model: model, letter: letter) }
        }
        .sheet(item: $model.installCompletion) { completion in
            InstallCompletionSheet(model: model, completion: completion)
        }
        .sheet(isPresented: $model.audioEditor) {
            if let selected = model.selected { AudioSheet(model: model, ball: selected) }
        }
        .sheet(isPresented: $model.mapping, onDismiss: { model.mappingDrive = nil }) {
            MapDriveSheet(
                model: model, initialLetter: model.mappingDrive,
                initialSource: model.mappingDrive.flatMap {
                    model.snapshot?.manifest.drives[$0]?.source
                } ?? "")
        }
        .confirmationDialog("Stop the selected application?", isPresented: $model.stopConfirmation)
        {
            Button("Stop Application", role: .destructive) { model.stopProcess() }
            Button("Cancel", role: .cancel) { model.cancelStopRequest() }
        } message: {
            Text("Unsaved work in the guest may be lost. Its output log will be kept.")
        }
        .confirmationDialog(
            "Move \(model.ballToTrash.map { "“" + AppModel.displayName($0.path) + "”" } ?? "ball") to the Trash?",
            isPresented: Binding(
                get: { model.ballToTrash != nil }, set: { if !$0 { model.ballToTrash = nil } }),
            titleVisibility: .visible
        ) {
            Button("Move to Trash", role: .destructive) {
                guard let request = model.ballToTrash else { return }
                model.ballToTrash = nil
                Task {
                    do {
                        try await model.trashBall(request.path, expectedIdentity: request.identity)
                    } catch { model.error = error.localizedDescription }
                }
            }
            Button("Cancel", role: .cancel) { model.ballToTrash = nil }
        } message: {
            Text(
                "This moves the entire ball, including its installed applications and documents, to the Trash. You can restore it using Finder."
            )
            if let request = model.ballToTrash, let draft = model.configDrafts[request.path],
                draft != model.configBases[request.path]
            {
                Text("Unsaved configuration edits in this ball will also be discarded.")
            }
        }
        .confirmationDialog(
            "Force stop the selected application?", isPresented: $model.forceStopConfirmation
        ) {
            Button("Force Stop", role: .destructive) { model.stopProcess(force: true) }
            Button("Cancel", role: .cancel) { model.cancelStopRequest() }
        } message: {
            Text(
                "Use this if the guest does not respond to Stop. Unsaved work may be lost. Its output log will be kept."
            )
        }
        .alert(
            "2ine",
            isPresented: Binding(get: { model.error != nil }, set: { if !$0 { model.error = nil } })
        ) {
            Button("OK", role: .cancel) { model.error = nil }
        } message: {
            Text(model.error ?? "")
        }
    }
    private var sidebar: some View {
        List(
            selection: Binding(
                get: { model.selected }, set: { if let path = $0 { model.select(path) } })
        ) {
            Section("Ball") {
                ForEach(model.paths, id: \.self) { path in
                    Label {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(AppModel.displayName(path)).font(.headline)
                            Text(DisplayPath.homeAbbreviated(path)).font(.caption).foregroundStyle(
                                .secondary
                            ).lineLimit(2)
                            let count = model.sessions.filter { $0.ball == path && $0.running }
                                .count
                            if count > 0 {
                                Text("\(count) running").font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    } icon: {
                        Image(systemName: "shippingbox")
                    }
                    .tag(path).help(DisplayPath.homeAbbreviated(path))
                    .draggable(URL(filePath: path))
                    .contextMenu {
                        Button("Rename Ball…") { model.beginRenameBall(path) }
                            .disabled(
                                !model.canManageBall(path)
                                    || !FileManager.default.fileExists(atPath: path))
                        if FileManager.default.fileExists(atPath: path) {
                            Button("Move Ball to Trash…", role: .destructive) {
                                model.beginTrashBall(path)
                            }
                            .disabled(!model.canManageBall(path))
                        } else {
                            Button("Remove Missing Ball from List") {
                                model.removeMissingBall(path)
                            }
                            .disabled(!model.canManageBall(path))
                        }
                        Divider()
                        Button("Open in Finder") { model.reveal(path) }
                        Button("Copy Ball Path") {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(path, forType: .string)
                        }
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
                BallNavigation(page: $model.page).padding()
                if let pending = model.pendingLaunch {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(
                            pending.ready
                                ? "Ready to continue \(pending.title)"
                                : pending.needsComponents
                                    ? "\(pending.title) is waiting for OS/2 components."
                                    : "Review the configuration for \(pending.title)."
                        ).font(.headline)
                        Text(pending.reason).foregroundStyle(.secondary).textSelection(.enabled)
                        HStack {
                            if pending.needsComponents {
                                Button("Show CMD + REXX") { model.showOS2ComponentGroup(.cmd) }
                            } else {
                                Button("Review Program Options") { model.page = .installation }
                            }
                            Button(pending.ready ? "Continue" : "Retry Launch") {
                                model.continuePendingLaunch(ball: pending.ball)
                            }
                            .disabled(
                                model.busy || model.mediaBusy || model.componentOperationRunning
                                    || model.processRunning)
                            Button("Cancel Pending Launch") {
                                model.cancelPendingLaunch(ball: pending.ball)
                            }
                        }
                    }.padding(.horizontal).padding(.bottom, 10)
                }
                ForEach(model.componentJobs.values.filter(\.running).sorted { $0.ball < $1.ball }) {
                    job in
                    HStack {
                        ProgressView().controlSize(.small)
                        Button(AppModel.displayName(job.ball) + ": " + job.summary) {
                            model.select(job.ball)
                            model.page = .systemComponents
                        }.buttonStyle(.link)
                        Spacer()
                        Button(job.cancelling ? "Cancelling…" : "Cancel") {
                            model.cancelComponentOperation(job)
                        }.disabled(job.cancelling)
                    }.padding(.horizontal).padding(.bottom, 8)
                }
                if let notice = model.componentNotice {
                    HStack {
                        Text(notice).foregroundStyle(.secondary)
                        Spacer()
                        Button("Dismiss") { model.componentNotice = nil }
                    }.padding(.horizontal).padding(.bottom, 8)
                }
                Divider()
                Group {
                    switch model.page {
                    case .applications: ApplicationsView(model: model, snapshot: snapshot)
                    case .drives: DrivesView(model: model, snapshot: snapshot)
                    case .systemComponents:
                        if let ball = model.selected {
                            OS2ComponentsPane(model: model, request: OS2ImportRequest(ball: ball))
                                .id(ball)
                        }
                    case .installation:
                        ProgramSheet(model: model, action: .install, embedded: true).id(
                            model.selected)
                    case .settings: ConfigurationView(model: model).id(model.selected)
                    case .activity: ActivityView(model: model)
                    }
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .frame(minWidth: 640)
            .navigationTitle(model.title)
            .navigationSubtitle(model.runtimeDescription)
        } else {
            ContentUnavailableView {
                Label(
                    model.selected == nil ? "Your OS/2 applications, at home" : "Cannot Open Ball",
                    systemImage: "shippingbox")
            } description: {
                Text(
                    model.selected == nil
                        ? "Create a ball, map your installation media, and install an application."
                        : "Check the folder and its mapped drives, then refresh or open another ball."
                )
            } actions: {
                Button("Create Ball…") { model.chooseBall(create: true) }
                Button("Open Ball…") { model.chooseBall(create: false) }
            }
        }
    }

}

struct BallNavigation: NSViewRepresentable {
    @Binding var page: Page
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeNSView(context: Context) -> NSSegmentedControl {
        let control = NSSegmentedControl(
            labels: Page.allCases.map(\.rawValue), trackingMode: .selectOne,
            target: context.coordinator, action: #selector(Coordinator.choose(_:)))
        control.segmentDistribution = .fit
        control.setAccessibilityLabel("Ball Navigation")
        control.setAccessibilityIdentifier("ballNavigationTabs")
        control.selectedSegment = Page.allCases.firstIndex(of: page) ?? 0
        return control
    }
    func updateNSView(_ control: NSSegmentedControl, context: Context) {
        context.coordinator.parent = self
        control.selectedSegment = Page.allCases.firstIndex(of: page) ?? 0
        control.isEnabled = context.environment.isEnabled
    }
    @MainActor final class Coordinator: NSObject {
        var parent: BallNavigation
        init(_ parent: BallNavigation) { self.parent = parent }
        @objc func choose(_ sender: NSSegmentedControl) {
            guard Page.allCases.indices.contains(sender.selectedSegment) else { return }
            parent.page = Page.allCases[sender.selectedSegment]
        }
    }
}

struct ApplicationRow: Identifiable {
    let name: String
    let program: String
    let cwd: String
    var icon: NSImage? = nil
    var id: String { name }
    static func sorted(_ rows: [Self], using order: [KeyPathComparator<Self>]) -> [Self] {
        if !order.isEmpty { return rows.sorted(using: order) }
        return rows.sorted {
            if ($0.icon != nil) != ($1.icon != nil) { return $0.icon != nil }
            return $0.name.localizedStandardCompare($1.name) == .orderedAscending
        }
    }
}

struct ApplicationsView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @State private var selection: Set<String> = []
    @State private var sortOrder: [KeyPathComparator<ApplicationRow>] = []
    @SceneStorage("applicationColumns") private var columns:
        TableColumnCustomization<ApplicationRow>
    var rows: [ApplicationRow] {
        ApplicationRow.sorted(
            snapshot.manifest.applications.map {
                ApplicationRow(
                    name: $0.key, program: $0.value.program, cwd: $0.value.workingDirectorySummary,
                    icon: model.applicationIcons[$0.key])
            }, using: sortOrder)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Button("Install Software…", systemImage: "shippingbox.and.arrow.backward") {
                    model.beginProgram(.install)
                }
                .accessibilityIdentifier("installSoftware")
                Button("Run Executable…", systemImage: "play") { model.beginProgram(.run) }
                Spacer()
                Menu("Sort", systemImage: "arrow.up.arrow.down") {
                    Button("Icons First") { sortOrder = [] }
                    Button("Name") { sortOrder = [KeyPathComparator(\ApplicationRow.name)] }
                }.help("Icons first by default; click a column to choose another order.")
                Button("Add Application…", systemImage: "plus") { model.beginProgram(.save) }
            }.disabled(model.busy || model.mediaBusy)
            DependencyAdviceView(model: model, audit: snapshot.dependencyAudit)
            Table(rows, selection: $selection, sortOrder: $sortOrder, columnCustomization: $columns)
            {
                TableColumn("Application", value: \.name) { row in
                    HStack {
                        if let icon = row.icon {
                            Image(nsImage: icon).resizable().interpolation(.none).scaledToFit()
                                .frame(width: 28, height: 28).accessibilityHidden(true)
                        } else {
                            Image(systemName: "app.dashed").frame(width: 28, height: 28)
                                .accessibilityHidden(true)
                        }
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
            .overlay {
                if rows.isEmpty {
                    ContentUnavailableView(
                        "No Saved Applications", systemImage: "app.dashed",
                        description: Text("Install your software, then add its executable here."))
                }
            }
            .contextMenu(forSelectionType: String.self) { names in
                if let name = names.first, names.count == 1 {
                    Button("Launch \(name)") { model.execute(["launch", name]) }.disabled(
                        model.busy || model.mediaBusy)
                    Button("Edit Application…") { model.editApplication(name) }.disabled(
                        model.busy || model.mediaBusy)
                    Button("Remove Launcher") { model.command(["remove-app", name]) }.disabled(
                        model.busy || model.mediaBusy)
                }
                Button("Copy") { copy(names) }.disabled(names.isEmpty)
            } primaryAction: { names in
                if let name = names.first { model.execute(["launch", name]) }
            }
            .onCopyCommand {
                rows.filter { selection.contains($0.id) }.map {
                    NSItemProvider(object: "\($0.name)\t\($0.program)\t\($0.cwd)" as NSString)
                }
            }
            .onDeleteCommand {
                if selection.count == 1, let name = selection.first {
                    model.command(["remove-app", name])
                }
            }
            HStack {
                Button("Launch", systemImage: "play.fill") {
                    if let name = selection.first { model.execute(["launch", name]) }
                }
                .disabled(selection.count != 1 || model.busy || model.mediaBusy)
                Button("Edit…") { if let name = selection.first { model.editApplication(name) } }
                    .disabled(selection.count != 1 || model.busy || model.mediaBusy)
                Button("Remove Launcher", systemImage: "minus") {
                    if let name = selection.first { model.command(["remove-app", name]) }
                }
                .disabled(selection.count != 1 || model.busy || model.mediaBusy)
                Spacer()
                Text("Removing a launcher keeps installed files.").font(.caption).foregroundStyle(
                    .secondary)
            }
        }.padding(20)
            .focusedSceneValue(
                \.ballViewActions,
                BallViewActions(
                    launch: selection.count == 1 ? { launchSelection() } : nil,
                    edit: selection.count == 1
                        ? { if let name = selection.first { model.editApplication(name) } } : nil,
                    remove: selection.count == 1
                        ? { if let name = selection.first { model.command(["remove-app", name]) } }
                        : nil))
    }
    private func launchSelection() {
        if let name = selection.first { model.execute(["launch", name]) }
    }
    private func copy(_ names: Set<String>) {
        let text = rows.filter { names.contains($0.id) }.map {
            "\($0.name)\t\($0.program)\t\($0.cwd)"
        }.joined(separator: "\n")
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

struct ActivityView: View {
    @Bindable var model: AppModel
    @AppStorage("logFontSize") private var logFontSize = 12.0
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if !model.ballSessions.isEmpty {
                Picker(
                    "Application",
                    selection: Binding(
                        get: { model.selectedSessionID ?? "" }, set: { model.selectSession($0) })
                ) {
                    Text("Activity and Audits").tag("")
                    ForEach(model.ballSessions) { session in
                        Text(session.label + " · " + session.status).tag(session.id)
                    }
                }.accessibilityIdentifier("applicationLogSession")
            }
            HStack {
                Label(
                    model.currentSession?.status ?? model.operation,
                    systemImage: model.busy ? "clock" : "terminal")
                Spacer()
                if model.canStopCurrentSession {
                    Button("Stop Operation", systemImage: "stop.fill") {
                        model.stopConfirmation = true
                    }.disabled(model.stopRequested)
                    if model.stopRequested {
                        Button("Force Stop…") { model.forceStopConfirmation = true }
                    }
                }
                if let log = model.currentSession == nil ? model.log : model.currentSession?.log {
                    Button("Open Full Log") { model.openFile(log.path) }
                }
                Button("Rescan Dependencies") { model.auditDependencies() }
                    .disabled(model.busy || model.mediaBusy || model.processRunning)
                    .accessibilityIdentifier("rescanDependencies")
                if let audit = model.dependencyAuditLog {
                    Button("Open Dependency Audit") { model.openFile(audit.path) }
                        .accessibilityIdentifier("openDependencyAudit")
                }
                if let registrations = model.applicationRegistrationLog {
                    Button("Open Application Registrations") { model.openFile(registrations.path) }
                }
            }
            ScrollView([.horizontal, .vertical]) {
                Text(model.currentSession?.output ?? model.activity).font(
                    .system(size: logFontSize, design: .monospaced)
                )
                .textSelection(.enabled).frame(maxWidth: .infinity, alignment: .topLeading).padding(
                    12)
            }.background(.background.secondary).border(.separator)
                .accessibilityIdentifier("processOutput")
            Text(
                "The last 64 KiB appears here. Full output is saved in the ball’s logs folder. An installer’s exit code does not verify application functionality."
            )
            .font(.caption).foregroundStyle(.secondary)
        }.padding(20)
    }
}
