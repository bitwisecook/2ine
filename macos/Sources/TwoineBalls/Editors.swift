#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit
import UniformTypeIdentifiers

private struct FolderChoice: Identifiable {
    let letter: String
    let path: String
    var id: String { letter }
}
struct ProgramSheet: View {
    @Bindable var model: AppModel
    let action: ProgramAction
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var program = ""
    @State private var arguments = ""
    @State private var directories: [String: String] = [:]
    @State private var environment: [String: String] = [:]
    @State private var iconSource: String?
    @State private var icon: NSImage?
    @State private var iconRequest = UUID()
    @State private var iconDescription = "Uses the icon embedded in the executable."
    @State private var executablePicker = false
    @State private var folderChoice: FolderChoice?
    @State private var error: String?
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(model.editingApplication == nil ? action.rawValue : "Edit Application").font(.title2.bold())
            if let snapshot = model.snapshot {
                TabView {
                    Form {
                        if action == .save { TextField("Application name", text: $name).accessibilityIdentifier("applicationName") }
                        LabeledContent("Executable") {
                            HStack {
                                TextField("OS/2 executable", text: $program, prompt: Text("C:\\OS2\\APP.EXE"))
                                    .labelsHidden().accessibilityIdentifier("programPath").frame(minWidth: 240)
                                Button("Browse Drives…") { executablePicker = true }.accessibilityIdentifier("browseExecutable")
                            }
                        }
                        TextField("Arguments", text: $arguments, prompt: Text("\"a value with spaces\" C:\\DATA\\FILE")).accessibilityIdentifier("programArguments")
                        Text("Quotes group arguments; backslashes remain literal. Working directories and environment inherit from the ball unless overridden below.")
                            .font(.callout).foregroundStyle(.secondary)
                        if action == .save {
                            HStack(spacing: 16) {
                                if let icon { Image(nsImage: icon).resizable().interpolation(.none).scaledToFit().frame(width: 48, height: 48).accessibilityLabel("Application icon") }
                                else { Image(systemName: "app.dashed").font(.largeTitle).frame(width: 48, height: 48).accessibilityLabel("No embedded icon found") }
                                VStack(alignment: .leading) {
                                    Text(iconDescription).font(.callout)
                                    HStack {
                                        Button("Choose Icon…") { chooseIcon() }
                                        Button("Use Executable Icon") { iconSource = ""; icon = nil; iconDescription = "Uses the icon embedded in the executable."; refreshIcon() }
                                    }
                                }
                            }
                        }
                        if action == .install { Text("Use your original media. The installer’s exit code and runtime identity are recorded in its receipt.").font(.callout) }
                    }.formStyle(.grouped).tabItem { Text("General") }
                    Form {
                        Section("Working Directory Overrides") {
                            Text("Each drive has its own directory. Leave an override off to use the ball’s default for that drive.").foregroundStyle(.secondary)
                            ForEach(snapshot.roots.keys.sorted(), id: \.self) { letter in
                                VStack(alignment: .leading, spacing: 8) {
                                    Toggle("Override \(letter):", isOn: Binding(get: { directories[letter] != nil }, set: { enabled in
                                        directories[letter] = enabled ? snapshot.workingDirectories[letter] : nil
                                    })).toggleStyle(.checkbox).accessibilityIdentifier("overrideDirectory" + letter)
                                    if directories[letter] != nil {
                                        HStack {
                                            TextField(letter + ": directory", text: Binding(get: { directories[letter] ?? "" }, set: { directories[letter] = $0 }))
                                                .font(.system(.body, design: .monospaced)).accessibilityIdentifier("directoryOverride" + letter)
                                            Button("Choose…") { folderChoice = FolderChoice(letter: letter, path: directories[letter] ?? letter + ":\\") }
                                        }
                                    }
                                    Text("Ball default: " + (snapshot.workingDirectories[letter] ?? letter + ":\\")).font(.caption).foregroundStyle(.secondary)
                                }
                            }
                        }
                    }.formStyle(.grouped).tabItem { Text("Working Directories") }
                    EnvironmentOverridesView(defaults: snapshot.environmentDefaults, values: $environment).tabItem { Text("Environment") }
                }
                .sheet(isPresented: $executablePicker) {
                    GuestPickerSheet(model: model, snapshot: snapshot, path: program.isEmpty ? (snapshot.workingDirectories[snapshot.currentDrive] ?? snapshot.currentDrive + ":\\") : program, directoriesOnly: false) {
                        program = $0; refreshIcon()
                    }
                }
                .sheet(item: $folderChoice) { choice in
                    GuestPickerSheet(model: model, snapshot: snapshot, path: choice.path, directoriesOnly: true, requiredDrive: choice.letter) { directories[choice.letter] = $0 }
                }
            }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("programError") }
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction).disabled(model.busy).accessibilityIdentifier("cancelProgram")
                Spacer()
                Button(model.editingApplication == nil ? action.rawValue : "Save Application") {
                    Task {
                        do { try await model.program(action, name: name, program: program, arguments: arguments, directories: directories, environment: environment, iconSource: iconSource) }
                        catch { self.error = error.localizedDescription }
                    }
                }.keyboardShortcut(.defaultAction).disabled(program.isEmpty || model.busy).accessibilityIdentifier("confirmProgram")
            }
        }.padding(24).frame(width: 740, height: 600)
        .onAppear {
            if let name = model.editingApplication, let app = model.snapshot?.manifest.applications[name] {
                self.name = name; program = app.program; arguments = (try? Arguments.format(app.arguments)) ?? ""
                directories = app.workingDirectories ?? [:]; environment = app.environment ?? [:]
                if let old = app.cwd, let drive = old.first { directories[String(drive)] = old }
                icon = model.applicationIcons[name]
                if app.icon != nil { iconDescription = "Custom icon saved in the ball." }
            }
            refreshIcon()
        }
        .onChange(of: program) { _, _ in if iconSource == nil || iconSource == "" { refreshIcon() } }
    }
    private func refreshIcon() {
        guard let ball = model.selected, !program.isEmpty else { return }
        let expected = program
        let token = UUID(); iconRequest = token
        Task {
            let saved = iconSource == nil ? model.editingApplication : nil
            if let data = try? await model.executableIcon(program: expected, name: saved, ball: ball), expected == program, token == iconRequest, iconSource == nil || iconSource == "" {
                icon = AppModel.image(data)
                iconDescription = data.source == "custom" ? "Custom icon saved in the ball." : data.source == "embedded" ? "OS/2 icon from the executable." : "No readable embedded icon; using the application symbol."
            }
        }
    }
    private func chooseIcon() {
        let panel = NSOpenPanel(); panel.title = "Choose Application Icon"
        panel.allowedContentTypes = [.image, .icns]; panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            let token = UUID(); iconRequest = token
            // The C core imports the image on Save; this is only a native preview.
            Task {
                do {
                    let data = try await model.previewImage(url.path)
                    guard token == iconRequest else { return }
                    guard let image = AppModel.image(data) else { throw BallError("This file is not a supported icon image.") }
                    icon = image; iconSource = url.path; iconDescription = "Custom icon: " + url.lastPathComponent
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
                Text("Values inherit from CONFIG.SYS and the ball. %NAME% reads an inherited variable; PATH=%PATH%;C:\\DOOM appends a guest directory. The host PATH is unchanged.")
                    .font(.callout).foregroundStyle(.secondary)
                ForEach(Array(Set(defaults.keys).union(values.keys)).sorted(), id: \.self) { key in
                    VStack(alignment: .leading, spacing: 6) {
                        Toggle("Override " + key, isOn: Binding(get: { values[key] != nil }, set: { values[key] = $0 ? (defaults[key] ?? "") : nil }))
                            .toggleStyle(.checkbox).accessibilityIdentifier("overrideEnvironment" + key)
                        if values[key] != nil {
                            TextField(key, text: Binding(get: { values[key] ?? "" }, set: { values[key] = $0 }))
                                .font(.system(.body, design: .monospaced)).accessibilityIdentifier("environmentOverride" + key)
                        }
                        Text("Inherited: " + (defaults[key] ?? "Not defined")).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                    }
                }
            }
            Section("Add Variable") {
                TextField("Name", text: $newName)
                TextField("Value", text: $newValue)
                Button("Add Override") { values[newName.uppercased()] = newValue; newName = ""; newValue = "" }.disabled(newName.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }.formStyle(.grouped)
    }
}

struct MapDriveSheet: View {
    @Bindable var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var letter = "D"
    @State private var directory = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text("Map Drive").font(.title2.bold())
            Picker("Drive letter", selection: $letter) {
                ForEach(Array("ABDEFGHIJKLMNOPQRSTUVWXYZ").map(String.init), id: \.self) { Text($0 + ":").tag($0) }
            }
            HStack {
                TextField("Host folder", text: $directory)
                Button("Choose…") { if let url = AppModel.pick(directory: true, title: "Choose Drive Folder") { directory = url.path } }
            }
            Text("An existing mapping for this letter will be replaced. Files in both folders are kept.").foregroundStyle(.secondary)
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Button("Map Drive") { model.command(["map", letter, directory]); dismiss() }
                    .keyboardShortcut(.defaultAction).disabled(directory.isEmpty || model.busy)
            }
        }.padding(24).frame(width: 520)
    }
}

struct BallSettingsView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @State private var key = "system.beep_volume"
    @State private var value = ""
    @State private var envName = ""
    @State private var envValue = ""
    var body: some View {
        Form {
            Section("Execution Runtime") {
                LabeledContent("Build", value: model.build.isEmpty ? "Not selected" : model.build)
                Button("Choose Runtime Build…") { model.chooseRuntime() }
                LabeledContent("Backend", value: snapshot.manifest.runtime["backend"] ?? "Inherited (portable on macOS)")
                HStack {
                    Button("Use Unicorn") { model.command(["config", "runtime.backend", "unicorn"]) }
                    Button("Reset to Inherited") { model.command(["unset", "runtime.backend"]) }
                }
                Text("Native x86 execution requires Linux. Use Unicorn on macOS. External runtime provider assets remain in the selected build.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("2ine Settings") {
                ForEach(snapshot.manifest.system.keys.sorted(), id: \.self) { name in
                    LabeledContent(name, value: snapshot.manifest.system[name]?.text ?? "")
                }
                Picker("Setting", selection: $key) {
                    Text("Beep volume (0–1)").tag("system.beep_volume")
                    Text("Trace native calls (true/false)").tag("system.trace_native")
                    Text("Trace events (true/false)").tag("system.trace_events")
                }
                TextField("Override value", text: $value)
                HStack {
                    Button("Set Override") { model.command(["config", key, value]) }.disabled(value.isEmpty)
                    Button("Reset Selected Setting") { model.command(["unset", key]) }
                }
            }
            Section("Default Guest Environment") {
                ForEach(snapshot.environmentDefaults.keys.sorted(), id: \.self) { name in
                    LabeledContent(name) { Text(snapshot.environmentDefaults[name] ?? "").textSelection(.enabled) }
                }
            }
            Section("Guest Environment Overrides") {
                ForEach(snapshot.manifest.environment.keys.sorted(), id: \.self) { name in
                    LabeledContent(name) {
                        Text(snapshot.manifest.environment[name] ?? "").textSelection(.enabled)
                        Spacer()
                        Button("Reset") { model.command(["unset", "env." + name]) }
                    }
                }
                TextField("Variable name", text: $envName, prompt: Text("LANG, LIBPATH or OS2PATH"))
                TextField("Value", text: $envValue)
                Button("Set Environment Override") { model.command(["config", "env." + envName, envValue]) }.disabled(envName.isEmpty)
                Text("Explicit overrides take precedence over CONFIG.SYS and defaults. Reset restores inheritance. PATH and OS2PATH both configure the guest search path. %NAME% expands inherited defaults; the host PATH is unchanged.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("Persistent Application State") {
                Text("Profiles, registry and shell metadata remain in the ball’s C:\\OS2 folder. This app does not edit binary profiles, registry entries or Workplace Shell objects.")
                Button("Open State Folder") { if let root = snapshot.roots["C"] { model.reveal(root + "/OS2") } }
            }
        }.formStyle(.grouped).disabled(model.busy)
    }
}

struct ConfigSysView: View {
    @Bindable var model: AppModel
    @State private var discard = false
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Guest CONFIG.SYS").font(.headline)
            Text("SET and LIBPATH lines are read at each launch. Kernel drivers and boot commands are not executed. Saving checks for changes made by installers or other tools.")
                .foregroundStyle(.secondary)
            TextEditor(text: $model.configText).font(.system(.body, design: .monospaced)).border(.separator)
                .accessibilityLabel("CONFIG.SYS contents").accessibilityIdentifier("configSysEditor")
                .disabled(model.busy)
            HStack {
                Button("Reload from Disk") { if model.configDirty { discard = true } else { model.discardConfigDraft() } }.disabled(model.busy)
                Spacer()
                Button("Save CONFIG.SYS") { model.configSys(model.configText, expected: model.configBase) }
                    .keyboardShortcut("s").disabled(model.busy || !model.configDirty)
            }
        }.padding(20)
        .confirmationDialog("Discard unsaved CONFIG.SYS edits?", isPresented: $discard) {
            Button("Discard Edits", role: .destructive) { model.discardConfigDraft() }
        }
    }
}
