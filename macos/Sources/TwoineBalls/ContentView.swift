#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit

struct ContentView: View {
    @Bindable var model: AppModel
    @AppStorage("lastPage") private var lastPage = Page.applications.rawValue
    var body: some View {
        NavigationSplitView {
            List(selection: Binding(get: { model.selected }, set: { if let path = $0 { model.select(path) } })) {
                Section("Balls") {
                    ForEach(model.paths, id: \.self) { path in
                        Label {
                            VStack(alignment: .leading, spacing: 3) {
                                Text(AppModel.displayName(path)).font(.headline)
                                Text(path).font(.caption).foregroundStyle(.secondary).lineLimit(2)
                            }
                        } icon: { Image(systemName: "shippingbox") }
                        .tag(path).help(path)
                        .draggable(URL(filePath: path))
                        .contextMenu {
                            Button("Open in Finder") { model.reveal(path) }
                            Button("Copy Ball Path") { NSPasteboard.general.clearContents(); NSPasteboard.general.setString(path, forType: .string) }
                        }
                    }
                }
            }
            .accessibilityIdentifier("ballList")
            .disabled(model.busy)
            .navigationSplitViewColumnWidth(min: 210, ideal: 250, max: 340)
            .safeAreaInset(edge: .bottom) {
                HStack {
                    Button("New Ball", systemImage: "plus") { model.chooseBall(create: true) }
                    Button("Open", systemImage: "folder") { model.chooseBall(create: false) }
                }.padding().disabled(model.busy)
            }
        } detail: {
            if let snapshot = model.snapshot {
                VStack(spacing: 0) {
                    Picker("Ball section", selection: $model.page) {
                        ForEach(Page.allCases) { Text($0.rawValue).tag($0) }
                    }.pickerStyle(.segmented).padding()
                    Divider()
                    Group {
                        switch model.page {
                        case .applications: ApplicationsView(model: model, snapshot: snapshot)
                        case .drives: DrivesView(model: model, snapshot: snapshot)
                        case .settings: BallSettingsView(model: model, snapshot: snapshot).id(model.revision)
                        case .config: ConfigSysView(model: model)
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
        .frame(minWidth: 880, minHeight: 590)
        .onAppear { model.page = Page(rawValue: lastPage) ?? .applications }
        .onChange(of: model.page) { _, page in lastPage = page.rawValue }
        .dropDestination(for: URL.self) { urls, _ in
            guard !model.busy, let url = urls.first else { return false }
            model.open(url, create: false); return true
        }
        .toolbar {
            ToolbarItemGroup {
                if model.busy { ProgressView().controlSize(.small).accessibilityLabel("Operation in progress") }
                Button("Refresh", systemImage: "arrow.clockwise") { model.refresh() }.disabled(model.busy)
                Button("Runtime…", systemImage: "cpu") { model.chooseRuntime() }.disabled(model.selected == nil || model.busy)
            }
        }
        .sheet(item: $model.programAction) { action in ProgramSheet(model: model, action: action) }
        .sheet(isPresented: $model.mapping) { MapDriveSheet(model: model) }
        .alert("2ine Balls", isPresented: Binding(get: { model.error != nil }, set: { if !$0 { model.error = nil } })) {
            Button("OK", role: .cancel) { model.error = nil }
        } message: { Text(model.error ?? "") }
    }
}

struct ApplicationRow: Identifiable {
    let name: String
    let program: String
    let cwd: String
    var id: String { name }
}

struct ApplicationsView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @Environment(\.undoManager) private var undo
    @State private var selection: Set<String> = []
    @State private var sortOrder = [KeyPathComparator(\ApplicationRow.name)]
    @SceneStorage("applicationColumns") private var columns: TableColumnCustomization<ApplicationRow>
    var rows: [ApplicationRow] {
        snapshot.manifest.applications.map { ApplicationRow(name: $0.key, program: $0.value.program, cwd: $0.value.cwd ?? "Executable folder") }.sorted(using: sortOrder)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Button("Install Software…", systemImage: "shippingbox.and.arrow.backward") { model.programAction = .install }
                    .accessibilityIdentifier("installSoftware")
                Button("Run Executable…", systemImage: "play") { model.programAction = .run }
                Spacer()
                Button("Add Application…", systemImage: "plus") { model.programAction = .save }
            }.disabled(model.busy)
            Table(rows, selection: $selection, sortOrder: $sortOrder, columnCustomization: $columns) {
                TableColumn("Application", value: \.name)
                    .customizationID("name")
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
                    Button("Launch \(name)") { model.execute(["launch", name]) }.disabled(model.busy)
                    Button("Remove Launcher") { model.command(["remove-app", name], undo: undo) }.disabled(model.busy)
                }
                Button("Copy") { copy(names) }.disabled(names.isEmpty)
            } primaryAction: { names in if let name = names.first { model.execute(["launch", name]) } }
            .onCopyCommand { rows.filter { selection.contains($0.id) }.map { NSItemProvider(object: "\($0.name)\t\($0.program)\t\($0.cwd)" as NSString) } }
            .onDeleteCommand { if selection.count == 1, let name = selection.first { model.command(["remove-app", name], undo: undo) } }
            HStack {
                Button("Launch", systemImage: "play.fill") { if let name = selection.first { model.execute(["launch", name]) } }
                    .keyboardShortcut(.return, modifiers: [.command]).disabled(selection.count != 1 || model.busy)
                Button("Remove Launcher", systemImage: "minus") { if let name = selection.first { model.command(["remove-app", name], undo: undo) } }
                    .disabled(selection.count != 1 || model.busy)
                Spacer()
                Text("Removing a launcher keeps installed files.").font(.caption).foregroundStyle(.secondary)
            }
        }.padding(20)
    }
    private func copy(_ names: Set<String>) {
        let text = rows.filter { names.contains($0.id) }.map { "\($0.name)\t\($0.program)\t\($0.cwd)" }.joined(separator: "\n")
        NSPasteboard.general.clearContents(); NSPasteboard.general.setString(text, forType: .string)
    }
}

struct DrivesView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @Environment(\.undoManager) private var undo
    var body: some View {
        Form {
            Section("Mapped Drives") {
                ForEach(snapshot.roots.keys.sorted(), id: \.self) { letter in
                    LabeledContent(letter + ":") {
                        Text(snapshot.roots[letter] ?? "").textSelection(.enabled)
                        Spacer()
                        Button("Open") { model.reveal(snapshot.roots[letter] ?? "") }
                        if letter != "C" { Button("Unmap") { model.command(["unmap", letter], undo: undo) }.disabled(model.busy) }
                    }
                }
                Button("Map Drive…") { model.mapping = true }.disabled(model.busy)
            }
            Section {
                Text("C: is private to this ball. Map a folder or extracted installation CD to another drive. Mapped folders are writable host directories.")
                    .foregroundStyle(.secondary)
            }
        }.formStyle(.grouped)
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
                if let log = model.log { Button("Open Full Log") { model.reveal(log.path) } }
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
