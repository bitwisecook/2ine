import AppKit
import SwiftUI
import UniformTypeIdentifiers

#if canImport(BallCore)
import BallCore
#endif

private struct FolderChoice: Identifiable {
    let letter: String
    let path: String
    var id: String { letter }
}

struct ProgramSheet: View {
    private enum Panel: String, CaseIterable {
        case general = "General"
        case directories = "Working Directories"
        case environment = "Environment"
    }
    @Bindable var model: AppModel
    let action: ProgramAction
    var embedded = false
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var suggestedName = ""
    @State private var program = ""
    @State private var arguments = ""
    @State private var directories: [String: String] = [:]
    @State private var environment: [String: String] = [:]
    @State private var iconSource: String?
    @State private var icon: NSImage?
    @State private var iconRequest = UUID()
    @State private var iconDescription = "Uses the icon embedded in the executable."
    @State private var executablePicker = false
    @State private var mediaLetter = "D"
    @State private var mediaKind = MediaDeviceKind.cdrom
    @State private var candidates: [String] = []
    @State private var previousSuggestion: String?
    @State private var discoveryTask: Task<Void, Never>?
    @State private var discoveryID = UUID()
    @State private var attachingMedia = false
    @State private var discovering = false
    @State private var mediaNotice = ""
    private var mediaLetters: [String] {
        let existing = (model.snapshot?.manifest.drives ?? [:]).filter {
            $0.value.device == mediaKind
        }.keys
        return Array(Set(existing).union(model.availableMediaLetter(mediaKind).map { [$0] } ?? []))
            .sorted()
    }
    @State private var folderChoice: FolderChoice?
    @State private var error: String?
    @State private var panel = Panel.general
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(
                embedded
                    ? "Application Installation"
                    : model.editingApplication == nil ? action.rawValue : "Edit Application"
            ).font(.title2.bold())
            if let snapshot = model.snapshot {
                if embedded && action == .install {
                    DependencyAdviceView(model: model, audit: snapshot.dependencyAudit)
                }
                VStack(spacing: 12) {
                    Picker("Program Options", selection: $panel) {
                        ForEach(Panel.allCases, id: \.self) { Text($0.rawValue).tag($0) }
                    }
                    .labelsHidden().pickerStyle(.segmented).controlSize(.regular)
                    // Preserve the full segment labels in a sheet’s sizing proposal.
                    .fixedSize(horizontal: true, vertical: false)
                    .frame(maxWidth: .infinity)
                    .accessibilityIdentifier("programOptions")
                    Group {
                        switch panel {
                        case .general:
                            Form {
                                if action == .install {
                                    Section("Installer") {
                                        HStack {
                                            Picker(
                                                "Device",
                                                selection: Binding(
                                                    get: { mediaKind },
                                                    set: {
                                                        mediaKind = $0
                                                        selectMediaLetter()
                                                        discoverInstallers(reset: true)
                                                    })
                                            ) {
                                                Text("CD").tag(MediaDeviceKind.cdrom)
                                                Text("Floppy").tag(MediaDeviceKind.floppy)
                                            }.frame(width: 150).disabled(attachingMedia)
                                            Picker(
                                                "Drive",
                                                selection: Binding(
                                                    get: { mediaLetter },
                                                    set: {
                                                        mediaLetter = $0
                                                        discoverInstallers(reset: true)
                                                    })
                                            ) {
                                                ForEach(mediaLetters, id: \.self) {
                                                    Text($0 + ":").tag($0)
                                                }
                                            }.frame(width: 130).disabled(attachingMedia)
                                            Button(
                                                snapshot.manifest.drives[mediaLetter]?.image == nil
                                                    ? "Attach Image…" : "Change Image…"
                                            ) { chooseInstallerMedia() }
                                            .disabled(
                                                attachingMedia || mediaLetters.isEmpty
                                                    || snapshot.manifest.drives[mediaLetter]?
                                                        .isLocked == true
                                                    || !model.canChangeMedia)
                                            if snapshot.manifest.drives[mediaLetter]?.image != nil {
                                                Button("Eject Image", systemImage: "eject") {
                                                    ejectInstallerMedia()
                                                }
                                                .labelStyle(.iconOnly).help("Eject \(mediaLetter):")
                                                .accessibilityIdentifier("ejectInstallerMedia")
                                                .disabled(
                                                    attachingMedia
                                                        || snapshot.manifest.drives[mediaLetter]?
                                                            .isLocked == true
                                                        || !model.canChangeMedia)
                                            }
                                        }
                                        Text(
                                            model.snapshot?.manifest.drives[mediaLetter]?.image.map
                                            { URL(filePath: $0).lastPathComponent }
                                                ?? "No image attached"
                                        ).foregroundStyle(.secondary)
                                        if discovering { ProgressView("Looking for installers…") }
                                        if !mediaNotice.isEmpty {
                                            Text(mediaNotice).font(.callout).foregroundStyle(
                                                .secondary)
                                        }
                                        programRow
                                        argumentRow
                                    }
                                }
                                if action == .save {
                                    TextField("Application name", text: $name)
                                        .accessibilityIdentifier("applicationName")
                                }
                                if action != .install {
                                    programRow
                                    argumentRow
                                }
                                Text(
                                    action == .install
                                        ? "Quotes group arguments; backslashes remain literal. Installers start in their own folder. Use the other tabs to set overrides."
                                        : "Quotes group arguments; backslashes remain literal. Working directories and environment inherit from the ball unless overridden below."
                                )
                                .font(.callout).foregroundStyle(.secondary)
                                if action == .save {
                                    HStack(spacing: 16) {
                                        if let icon {
                                            Image(nsImage: icon).resizable().interpolation(.none)
                                                .scaledToFit().frame(width: 48, height: 48)
                                                .accessibilityLabel("Application icon")
                                        } else {
                                            Image(systemName: "app.dashed").font(.largeTitle).frame(
                                                width: 48, height: 48
                                            ).accessibilityLabel("No embedded icon found")
                                        }
                                        VStack(alignment: .leading) {
                                            Text(iconDescription).font(.callout)
                                            HStack {
                                                Button("Choose Icon…") { chooseIcon() }
                                                Button("Use Executable Icon") {
                                                    iconSource = ""
                                                    icon = nil
                                                    iconDescription =
                                                        "Uses the icon embedded in the executable."
                                                    refreshIcon()
                                                }
                                            }
                                        }
                                    }
                                }
                                if action == .install {
                                    Text(
                                        "Choose any program or command file on your original media. CMD and BAT files require the original OS/2 CMD.EXE. After the installer exits, you can keep or eject the media."
                                    ).font(.callout)
                                }
                            }.formStyle(.grouped)
                        case .directories:
                            Form {
                                Section("Working Directory Overrides") {
                                    Text(
                                        "Each drive has its own directory. Leave an override off to use the ball’s default for that drive."
                                    ).foregroundStyle(.secondary)
                                    ForEach(snapshot.roots.keys.sorted(), id: \.self) { letter in
                                        VStack(alignment: .leading, spacing: 8) {
                                            Toggle(
                                                "Override \(letter):",
                                                isOn: Binding(
                                                    get: { directories[letter] != nil },
                                                    set: { enabled in
                                                        directories[letter] =
                                                            enabled
                                                            ? snapshot.workingDirectories[letter]
                                                            : nil
                                                    })
                                            ).toggleStyle(.checkbox).accessibilityIdentifier(
                                                "overrideDirectory" + letter)
                                            if directories[letter] != nil {
                                                HStack {
                                                    TextField(
                                                        letter + ": directory",
                                                        text: Binding(
                                                            get: { directories[letter] ?? "" },
                                                            set: { directories[letter] = $0 })
                                                    )
                                                    .font(.system(.body, design: .monospaced))
                                                    .accessibilityIdentifier(
                                                        "directoryOverride" + letter)
                                                    Button("Choose…") {
                                                        folderChoice = FolderChoice(
                                                            letter: letter,
                                                            path: directories[letter] ?? letter
                                                                + ":\\")
                                                    }
                                                }
                                            }
                                            Text(
                                                "Ball default: "
                                                    + (snapshot.workingDirectories[letter] ?? letter
                                                        + ":\\")
                                            ).font(.caption).foregroundStyle(.secondary)
                                        }
                                    }
                                }
                            }.formStyle(.grouped)
                        case .environment:
                            EnvironmentOverridesView(
                                defaults: snapshot.environmentDefaults, values: $environment)
                        }
                    }.frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                .sheet(isPresented: $executablePicker) {
                    GuestPickerSheet(
                        model: model, snapshot: snapshot,
                        path: program.isEmpty
                            ? (action == .install
                                ? mediaLetter + ":\\"
                                : (snapshot.workingDirectories[snapshot.currentDrive] ?? snapshot
                                    .currentDrive + ":\\")) : program, directoriesOnly: false
                    ) {
                        program = $0
                        refreshIcon()
                    }
                }
                .sheet(item: $folderChoice) { choice in
                    GuestPickerSheet(
                        model: model, snapshot: snapshot, path: choice.path, directoriesOnly: true,
                        requiredDrive: choice.letter
                    ) { directories[choice.letter] = $0 }
                }
            }
            if let error {
                Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier(
                    "programError")
            }
            HStack {
                if !embedded {
                    Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                        .disabled(model.busy || model.mediaBusy).accessibilityIdentifier(
                            "cancelProgram")
                }
                Spacer()
                Button(model.editingApplication == nil ? action.rawValue : "Save Application") {
                    Task {
                        do {
                            try await model.program(
                                action, name: name, program: program, arguments: arguments,
                                directories: directories, environment: environment,
                                iconSource: iconSource,
                                mediaDrive: action == .install
                                    && model.snapshot?.manifest.drives[mediaLetter]?.image != nil
                                    ? mediaLetter : nil)
                        } catch { self.error = error.localizedDescription }
                    }
                }.keyboardShortcut(.defaultAction).disabled(
                    program.isEmpty || model.busy || model.mediaBusy
                        || model.componentOperationRunning
                        || (action == .install && model.processRunning) || discovering
                ).accessibilityIdentifier("confirmProgram")
            }
        }.padding(24)
            .frame(
                minWidth: embedded ? nil : 740, maxWidth: embedded ? .infinity : 740,
                minHeight: embedded ? nil : 600, maxHeight: embedded ? .infinity : 600
            )
            .onAppear {
                if let name = model.editingApplication,
                    let app = model.snapshot?.manifest.applications[name]
                {
                    self.name = name
                    program = app.program
                    arguments = (try? Arguments.format(app.arguments)) ?? ""
                    directories = app.workingDirectories ?? [:]
                    environment = app.environment ?? [:]
                    if let old = app.cwd, let drive = old.first { directories[String(drive)] = old }
                    icon = model.applicationIcons[name]
                    if app.icon != nil { iconDescription = "Custom icon saved in the ball." }
                }
                if action == .install {
                    if let pending = model.pendingLaunch, pending.arguments.first == "install" {
                        program = pending.title
                        arguments =
                            (try? Arguments.format(Array(pending.arguments.dropFirst(2)))) ?? ""
                        directories = pending.directories ?? [:]
                        environment = pending.environment ?? [:]
                        if let letter = pending.mediaDrive {
                            mediaLetter = letter
                            mediaKind = model.snapshot?.manifest.drives[letter]?.device ?? .cdrom
                        }
                    }
                    selectMediaLetter()
                    if model.snapshot?.manifest.drives[mediaLetter]?.image != nil {
                        discoverInstallers(reset: program.isEmpty)
                    }
                }
                refreshIcon()
            }
            .onChange(of: model.snapshot?.manifest.drives[mediaLetter]?.image) { _, _ in
                if action == .install, !attachingMedia { discoverInstallers(reset: true) }
            }
            .onChange(of: program) { _, _ in refreshIcon() }
            .onDisappear {
                discoveryID = UUID()
                discoveryTask?.cancel()
            }
    }
    private var programRow: some View {
        LabeledContent(action == .install ? "Installer" : "Program") {
            HStack {
                if action == .install {
                    InstallerProgramPicker(program: $program, suggestions: candidates).frame(
                        minWidth: 240)
                } else {
                    TextField(
                        "OS/2 program or command file", text: $program,
                        prompt: Text("C:\\OS2\\APP.EXE")
                    )
                    .labelsHidden().accessibilityIdentifier("programPath").frame(minWidth: 240)
                }
                Button("Browse…") { executablePicker = true }.accessibilityIdentifier(
                    "browseExecutable")
            }
        }
    }
    private var argumentRow: some View {
        TextField(
            "Arguments", text: $arguments, prompt: Text("\"a value with spaces\" C:\\DATA\\FILE")
        ).accessibilityIdentifier("programArguments")
    }
    private func selectMediaLetter() {
        if !mediaLetters.contains(mediaLetter) { mediaLetter = mediaLetters.first ?? "D" }
    }
    private func chooseInstallerMedia() {
        guard
            let url = AppModel.pick(
                directory: false, title: "Choose Installation \(mediaKind.title) Image")
        else { return }
        discoveryID = UUID()
        discoveryTask?.cancel()
        program = ""
        previousSuggestion = nil
        attachingMedia = true
        discovering = true
        error = nil
        Task {
            defer {
                attachingMedia = false
                discovering = false
            }
            do {
                setSuggestions(
                    try await model.attachInstallerMedia(url, kind: mediaKind, letter: mediaLetter),
                    reset: false)
            } catch { self.error = error.localizedDescription }
        }
    }
    private func ejectInstallerMedia() {
        discoveryID = UUID()
        discoveryTask?.cancel()
        attachingMedia = true
        Task {
            defer {
                attachingMedia = false
                discovering = false
            }
            do {
                try await model.changeMedia(["eject", mediaLetter])
                setSuggestions(
                    [], reset: program.uppercased().hasPrefix(mediaLetter.uppercased() + ":"))
                mediaNotice = "Image ejected. Attach an image or browse another drive."
            } catch { self.error = error.localizedDescription }
        }
    }
    private func discoverInstallers(reset: Bool) {
        guard action == .install, !attachingMedia else { return }
        discoveryTask?.cancel()
        let token = UUID()
        discoveryID = token
        if reset {
            program = ""
            previousSuggestion = nil
        }
        guard model.snapshot?.manifest.drives[mediaLetter]?.image != nil else {
            discovering = false
            setSuggestions(
                [], reset: reset && program.uppercased().hasPrefix(mediaLetter.uppercased() + ":"))
            return
        }
        discovering = true
        let letter = mediaLetter
        let ball = model.selected
        discoveryTask = Task {
            defer { if discoveryID == token { discovering = false } }
            do {
                let paths = try await model.installerCandidates(letter: letter)
                guard !Task.isCancelled, discoveryID == token, model.selected == ball else {
                    return
                }
                setSuggestions(paths, reset: false)
            } catch {
                if !Task.isCancelled, discoveryID == token {
                    self.error = error.localizedDescription
                }
            }
        }
    }
    private func setSuggestions(_ paths: [String], reset: Bool) {
        candidates = InstallerSuggestions.sorted(paths)
        program = InstallerSuggestions.selectedProgram(
            current: program, previousSuggestion: previousSuggestion, suggestions: candidates,
            reset: reset)
        previousSuggestion = candidates.first
        mediaNotice =
            candidates.isEmpty
            ? "Browse to choose an installer or command file."
            : "The root installer is preferred. Choose another suggestion, enter a path or browse."
    }
    private func refreshIcon() {
        guard let ball = model.selected, !program.isEmpty else { return }
        let expected = program
        let token = UUID()
        iconRequest = token
        Task {
            let saved = iconSource == nil ? model.editingApplication : nil
            if let data = try? await model.executableIcon(
                program: expected, name: saved, ball: ball), expected == program,
                token == iconRequest
            {
                if action == .save, model.editingApplication == nil,
                    name.isEmpty || name == suggestedName
                {
                    suggestedName = data.displayName ?? ""
                    name = suggestedName
                }
                if iconSource == nil || iconSource == "" {
                    icon = AppModel.image(data)
                    iconDescription =
                        data.source == "custom"
                        ? "Custom icon saved in the ball."
                        : data.source == "embedded"
                            ? "OS/2 icon from the executable."
                            : "No readable embedded icon; using the application symbol."
                }
            }
        }
    }
    private func chooseIcon() {
        let panel = NSOpenPanel()
        panel.title = "Choose Application Icon"
        panel.allowedContentTypes = [.image, .icns]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            let token = UUID()
            iconRequest = token
            // The C core imports the image on Save; this is only a native preview.
            Task {
                do {
                    let data = try await model.previewImage(url.path)
                    guard token == iconRequest else { return }
                    guard let image = AppModel.image(data) else {
                        throw BallError("This file is not a supported icon image.")
                    }
                    icon = image
                    iconSource = url.path
                    iconDescription = "Custom icon: " + url.lastPathComponent
                } catch { self.error = error.localizedDescription }
            }
        }
    }
}

struct EnvironmentOverridesView: View {
    let defaults: [String: String]
    @Binding var values: [String: String]
    @State private var newName = ""
    @State private var newValue = ""
    var body: some View {
        Form {
            Section("Environment Overrides") {
                Text(
                    "Values inherit from CONFIG.SYS and the ball. %NAME% reads an inherited variable; PATH=%PATH%;C:\\DOOM appends a guest directory. The host PATH is unchanged."
                )
                .font(.callout).foregroundStyle(.secondary)
                ForEach(Array(Set(defaults.keys).union(values.keys)).sorted(), id: \.self) { key in
                    VStack(alignment: .leading, spacing: 6) {
                        Toggle(
                            "Override " + key,
                            isOn: Binding(
                                get: { values[key] != nil },
                                set: { values[key] = $0 ? (defaults[key] ?? "") : nil })
                        )
                        .toggleStyle(.checkbox).accessibilityIdentifier("overrideEnvironment" + key)
                        if values[key] != nil {
                            TextField(
                                key,
                                text: Binding(get: { values[key] ?? "" }, set: { values[key] = $0 })
                            )
                            .font(.system(.body, design: .monospaced)).accessibilityIdentifier(
                                "environmentOverride" + key)
                        }
                        Text("Inherited: " + (defaults[key] ?? "Not defined")).font(.caption)
                            .foregroundStyle(.secondary).textSelection(.enabled)
                    }
                }
            }
            Section("Add Variable") {
                TextField("Name", text: $newName)
                TextField("Value", text: $newValue)
                Button("Add Override") {
                    values[newName.uppercased()] = newValue
                    newName = ""
                    newValue = ""
                }.disabled(newName.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }.formStyle(.grouped)
    }
}

struct MapDriveSheet: View {
    @Bindable var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var letter = "D"
    @State private var image = false
    @State private var directory = ""
    @State private var error: String?
    let initialLetter: String?
    init(model: AppModel, initialLetter: String? = nil, initialSource: String = "") {
        self.model = model
        self.initialLetter = initialLetter
        _letter = State(initialValue: initialLetter ?? "D")
        _directory = State(initialValue: initialSource)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text(initialLetter == nil ? "Map Drive" : "Change Mountpoint").font(.title2.bold())
            Picker("Drive letter", selection: $letter) {
                ForEach(Array("ABDEFGHIJKLMNOPQRSTUVWXYZ").map(String.init), id: \.self) {
                    Text($0 + ":").tag($0)
                }
            }.disabled(initialLetter != nil)
            Picker("Source", selection: $image) {
                Text("Folder").tag(false)
                Text("Disk Image").tag(true)
            }.pickerStyle(.segmented).accessibilityIdentifier("driveSourceType")
            HStack {
                TextField(image ? "Image file" : "Host folder", text: $directory)
                    .accessibilityIdentifier(image ? "mappedImage" : "mappedHostFolder")
                Button("Choose…") {
                    if let url = AppModel.pick(
                        directory: !image,
                        title: image ? "Choose ISO or Floppy Image" : "Choose Drive Folder")
                    {
                        directory = url.path
                    }
                }
            }
            if image {
                Text(
                    "ISO 9660/Joliet CDs are read-only. FAT, FAT32, exFAT and HPFS images are writable by default, with Read Only available in device settings. Images stay inside 2ine."
                )
                .font(.callout).foregroundStyle(.secondary)
            }
            Text(
                initialLetter == nil
                    ? "An existing mapping for this letter will be replaced. Source files are kept."
                    : "The drive letter and guest paths stay the same. Files in the previous folder are kept."
            ).foregroundStyle(.secondary)
            if let error {
                Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier(
                    "mapDriveError")
            }
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    .disabled(model.busy || model.mediaBusy)
                Spacer()
                if model.busy {
                    ProgressView().controlSize(.small).accessibilityLabel("Mapping drive")
                }
                Button(
                    initialLetter == nil ? "Map Drive" : image ? "Attach Image" : "Connect Folder"
                ) {
                    Task {
                        do {
                            try await model.mapDrive(letter, directory: directory, image: image)
                            dismiss()
                        } catch { self.error = error.localizedDescription }
                    }
                }
                .keyboardShortcut(.defaultAction).disabled(
                    directory.isEmpty || model.busy || model.mediaBusy)
            }
        }.padding(24).frame(width: 520)
    }
}

struct BallSettingsView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @Bindable var draft: SettingsDraft
    var body: some View {
        Form {
            DeviceSettingsView(model: model, snapshot: snapshot)
            Group {
                Section("Execution Runtime") {
                    LabeledContent(
                        "Build",
                        value: model.build.isEmpty
                            ? "Not selected" : DisplayPath.homeAbbreviated(model.build))
                    HStack {
                        Button("Choose Runtime Build…") { model.chooseRuntime() }
                        Button("Use Bundled Runtime") { model.useBundledRuntime() }
                            .disabled(snapshot.manifest.runtime["build"] == nil)
                    }
                    LabeledContent(
                        "Backend",
                        value: snapshot.manifest.runtime["backend"]
                            ?? "Inherited (portable on macOS)")
                    HStack {
                        Button("Use Unicorn") {
                            model.command(["config", "runtime.backend", "unicorn"])
                        }
                        Button("Use Inherited Backend") {
                            model.command(["unset", "runtime.backend"])
                        }
                    }
                    Text(
                        "Native x86 execution requires Linux. Use Unicorn on macOS. External runtime provider assets remain in the selected build."
                    )
                    .font(.caption).foregroundStyle(.secondary)
                }
                Section("Network Security") {
                    Toggle(
                        "Upgrade Legacy TLS",
                        isOn: Binding(
                            get: {
                                ["1", "yes", "y", "true", "t", "on"].contains(
                                    snapshot.manifest.system["tls_upgrade"]?.text.lowercased()
                                        ?? "true")
                            },
                            set: {
                                model.command([
                                    "config", "system.tls_upgrade", $0 ? "true" : "false",
                                ])
                            })
                    )
                    .accessibilityIdentifier("tlsUpgrade")
                    Text(
                        "Allows supported applications to use TLS 1.2 or 1.3 through host OpenSSL. Off preserves the application's protocol selection. Certificate checks stay enabled. Applies next launch."
                    )
                    .font(.callout).foregroundStyle(.secondary)
                }
                Section("Diagnostics") {
                    ForEach(["trace_native", "trace_events"], id: \.self) { name in
                        Toggle(
                            name == "trace_native" ? "Trace Native Calls" : "Trace Events",
                            isOn: Binding(
                                get: {
                                    ["1", "yes", "y", "true", "t", "on"].contains(
                                        snapshot.manifest.system[name]?.text.lowercased() ?? "")
                                },
                                set: {
                                    model.command([
                                        "config", "system." + name, $0 ? "true" : "false",
                                    ])
                                }))
                    }
                }
                Section("Persistent Application State") {
                    Text(
                        "Config Files and Registry keep their data in this ball. Graphics, sound, printing and environment preferences also travel with it."
                    )
                    Button("Open State Folder") {
                        if let root = snapshot.roots["C"] { model.openFile(root + "/OS2") }
                    }
                }
            }.disabled(model.busy || model.mediaBusy)
        }.formStyle(.grouped)
    }
}

struct ConfigSysView: View {
    @Bindable var model: AppModel
    @State private var discard = false
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Guest CONFIG.SYS").font(.headline)
            Text(
                "SET and LIBPATH lines are read at each launch. Kernel drivers and boot commands are not executed. Saving checks for changes made by installers or other tools."
            )
            .foregroundStyle(.secondary)
            if model.configConflict {
                Label(
                    "CONFIG.SYS changed on disk. Your draft is preserved. Reload to use the installer's version before making further edits.",
                    systemImage: "exclamationmark.triangle"
                )
                .foregroundStyle(.orange)
            }
            if let notice = model.configurationNotice { Text(notice).foregroundStyle(.red) }
            TextEditor(text: $model.configText).font(.system(.body, design: .monospaced)).border(
                .separator
            )
            .accessibilityLabel("CONFIG.SYS contents").accessibilityIdentifier("configSysEditor")
            .disabled(
                (model.busy && !model.processRunning) || model.mediaBusy || model.configSaving)
            HStack {
                Button("Reload from Disk") {
                    if model.configDirty { discard = true } else { model.discardConfigDraft() }
                }.disabled(
                    (model.busy && !model.processRunning) || model.mediaBusy || model.configSaving)
                Spacer()
                Button("Save CONFIG.SYS") {
                    model.configSys(model.configText, expected: model.configBase)
                }
                .disabled(
                    (model.busy && !model.processRunning) || !model.configDirty || model.mediaBusy
                        || model.configSaving || model.configConflict)
            }
        }.padding(20)
            .confirmationDialog("Discard unsaved CONFIG.SYS edits?", isPresented: $discard) {
                Button("Discard Edits", role: .destructive) { model.discardConfigDraft() }
            }
    }
}
