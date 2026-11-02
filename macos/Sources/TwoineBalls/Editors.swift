#if canImport(BallCore)
import BallCore
#endif
import SwiftUI

struct ProgramSheet: View {
    @Bindable var model: AppModel
    let action: ProgramAction
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var program = ""
    @State private var arguments = ""
    @State private var cwd = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text(action.rawValue).font(.title2.bold())
            Form {
                if action == .save { TextField("Application name", text: $name).accessibilityIdentifier("applicationName") }
                HStack {
                    TextField("Executable", text: $program, prompt: Text("C:\\OS2\\APP.EXE"))
                        .accessibilityIdentifier("programPath")
                    Button("Choose…") { if let url = AppModel.pick(directory: false, title: "Choose Executable Inside a Mapped Drive") { program = url.path } }
                }
                TextField("Arguments", text: $arguments, prompt: Text("\"a value with spaces\" C:\\DATA\\FILE"))
                    .accessibilityIdentifier("programArguments")
                HStack {
                    TextField("Working directory", text: $cwd, prompt: Text("Executable folder"))
                    Button("Choose…") { if let url = AppModel.pick(directory: true, title: "Choose Mapped Working Directory") { cwd = url.path } }
                }
            }
            Text("Choose files inside a mapped drive, or enter an OS/2 path. Quotes group arguments; backslashes stay literal. Use \"\" for an empty argument.")
                .font(.callout).foregroundStyle(.secondary)
            if action == .install {
                Text("Use your original installation media. The installer’s process exit and runtime identity will be recorded in logs/last-install.json.").font(.callout)
            }
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Button(action.rawValue) { model.program(action, name: name, program: program, arguments: arguments, cwd: cwd) }
                    .keyboardShortcut(.defaultAction).disabled(program.isEmpty || model.busy)
                    .accessibilityIdentifier("confirmProgram")
            }
        }.padding(24).frame(width: 610)
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
                Text("Explicit overrides take precedence over CONFIG.SYS and defaults. Reset restores inheritance. Use OS2PATH for guest programs; the host PATH is reserved.")
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
