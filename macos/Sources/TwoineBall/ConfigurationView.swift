import BallCore
import SwiftUI

struct ConfigurationView: View {
    @Bindable var model: AppModel
    var body: some View {
        TabView {
            Tab("General", systemImage: "gearshape") {
                if let snapshot = model.snapshot { BallSettingsView(model: model, snapshot: snapshot, draft: model.settingsDraft) }
            }
            Tab("Graphics & Sound", systemImage: "display") { DisplayConfigurationView(model: model) }
            Tab("Environment", systemImage: "terminal") { EnvironmentSettingsView(model: model) }
            Tab("Printing", systemImage: "printer") { PrintingSettingsView(model: model) }
            Tab("Config Files", systemImage: "doc.text") { OS2FilesSettingsView(model: model) }
            Tab("Registry", systemImage: "list.bullet.indent") { RegistrySettingsView(model: model) }
        }
        .padding(12)
    }
}

private struct DisplayConfigurationView: View {
    @Bindable var model: AppModel
    @State private var features: RuntimeFeatures?
    @State private var error: String?
    @State private var beep = 0.05
    @State private var editingBeep = false
    private var dpi: Int { Int(Double(model.snapshot?.manifest.system["display_dpi"]?.text ?? "96") ?? 96) }
    var body: some View {
        Form {
            Section("OS/2 Display") {
                Picker("Title Bars", selection: Binding(get: { model.snapshot?.manifest.system["window_chrome"]?.text ?? "both" }, set: {
                    model.command(["config", "system.window_chrome", $0])
                })) {
                    Text("Native").tag("native")
                    Text("OS/2").tag("os2")
                    Text("Both").tag("both")
                }.accessibilityIdentifier("windowChrome")
                Text("Controls the title bars of top-level application windows. Embedded OS/2 document windows keep their own controls.")
                    .foregroundStyle(.secondary)
                Picker("Resolution", selection: Binding(get: { dpi }, set: { model.command(["config", "system.display_dpi", String($0)]) })) {
                    ForEach([96, 120, 144, 192], id: \.self) { value in Text("\(value) DPI (\(value * 100 / 96)%)").tag(value) }
                    if ![96, 120, 144, 192].contains(dpi) { Text("\(dpi) DPI (Custom)").tag(dpi) }
                }.accessibilityIdentifier("displayDPI")
                Text("Applies when an application next starts. OS/2 fonts, dialog units and control metrics scale together. Printer resolution remains 300 DPI.")
                    .foregroundStyle(.secondary)
            }
            if let features {
                Section("Graphics") {
                    Picker("OpenGL", selection: Binding(get: { features.opengl.requested ?? "automatic" }, set: {
                        model.command($0 == "automatic" ? ["unset", "system.opengl"] : ["config", "system.opengl", $0])
                    })) {
                        Text("Runtime Default (\(providerName(features.opengl.default)))").tag("automatic")
                        Text("Mesa").tag("mesa")
                        Text("Original OS/2 Software Renderer").tag("original")
                        Text("Off").tag("off")
                    }.accessibilityIdentifier("openGLProvider")
                    if !features.opengl.reason.isEmpty {
                        Text(features.opengl.reason).foregroundStyle(.secondary)
                    }
                    featureToggle("DIVE", key: "dive", feature: features.dive,
                                  detail: "Direct video buffer access used by games such as OS/2 Doom.")
                }
                Section("Multimedia") {
                    featureToggle("Video Playback", key: "video", feature: features.video)
                    featureToggle("PCM Sound", key: "pcm", feature: features.pcm)
                    featureToggle("MIDI Music", key: "midi", feature: features.midi)
                }
                Section {
                    Text("Changes apply when an application next starts. Settings travel with the ball. An unavailable feature can be selected for use with another runtime or after extracting OS/2 files; applications require that feature to be available before starting.")
                        .foregroundStyle(.secondary)
                }
            } else if let error {
                Text(error).foregroundStyle(.red).textSelection(.enabled)
            } else { ProgressView("Checking runtime capabilities…") }
            Section("Sound") {
                Slider(value: $beep, in: 0...1, onEditingChanged: { editing in
                    editingBeep = editing
                    if !editing { model.command(["config", "system.beep_volume", String(beep)]) }
                }) { Text("PC Speaker Volume") }
                LabeledContent("Volume", value: "\(Int(beep * 100))%")
                Button("Use Default PC Speaker Volume") { model.command(["unset", "system.beep_volume"]) }
                    .disabled(model.snapshot?.manifest.system["beep_volume"] == nil)
                Button("Sound Banks & Channel Mapping…") { model.audioEditor = true }.accessibilityIdentifier("soundBanks")
                Text("Import SF2, SF3, SFZ and DLS instruments and configure each MIDI channel.").foregroundStyle(.secondary)
            }
        }.formStyle(.grouped).disabled(model.busy || model.mediaBusy)
        .task(id: "\(model.selected ?? "")|\(model.build)|\(model.revision)") { await load() }
    }
    @ViewBuilder private func featureToggle(_ title: String, key: String, feature: RuntimeFeature, detail: String? = nil) -> some View {
        Toggle(title, isOn: Binding(get: { feature.requested ?? feature.default }, set: {
            model.command(["config", "system." + key, $0 ? "true" : "false"])
        })).accessibilityIdentifier("runtimeFeature." + key)
        if let detail { Text(detail).font(.callout).foregroundStyle(.secondary) }
        if !feature.reason.isEmpty { Text(feature.reason).font(.callout).foregroundStyle(.secondary) }
        if feature.requested != nil {
            Button("Use Runtime Default (\(feature.default ? "On" : "Off"))") { model.command(["unset", "system." + key]) }
                .font(.callout)
        }
    }
    private func providerName(_ value: String) -> String {
        switch value { case "mesa": "Mesa"; case "original": "Original OS/2"; default: "Off" }
    }
    private func load() async {
        guard let ball = model.selected else { features = nil; return }
        if !editingBeep { beep = Double(model.snapshot?.manifest.system["beep_volume"]?.text ?? "0.05") ?? 0.05 }
        let build = model.build
        do {
            let result = try await model.runtimeFeatures(ball: ball)
            guard !Task.isCancelled, ball == model.selected, build == model.build else { return }
            features = result; error = nil
        } catch {
            guard !Task.isCancelled, ball == model.selected else { return }
            features = nil; self.error = error.localizedDescription
        }
    }
}

private struct ProfileConfigurationView: View {
    @Bindable var model: AppModel
    let area: String
    var filePath: String? = nil
    @State private var value: ConfigurationSnapshot?
    @State private var selection: String?
    @State private var search = ""
    @State private var error: String?
    @State private var loading = false
    @State private var editor: ProfileEditRequest?
    private var entries: [ConfigurationEntry] {
        (value?.entries ?? []).filter {
            (area != "registry" || $0.key.hasPrefix("v:")) &&
            (search.isEmpty || $0.application.localizedCaseInsensitiveContains(search) || $0.key.localizedCaseInsensitiveContains(search))
        }.sorted { ($0.application, $0.key) < ($1.application, $1.key) }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                TextField(area == "registry" ? "Search registry keys and values" : "Search applications and keys", text: $search)
                    .textFieldStyle(.roundedBorder)
                Button("Reload") { Task { await load() } }.disabled(loading)
                Button("Add Value…") { editor = ProfileEditRequest(entry: nil) }.disabled(value == nil || model.busy || model.mediaBusy)
            }
            if let value { Text(DisplayPath.homeAbbreviated(value.path)).font(.caption).foregroundStyle(.secondary).textSelection(.enabled) }
            if model.processRunning { Label("Stop the guest before editing profiles or the registry.", systemImage: "info.circle").foregroundStyle(.secondary) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            Table(entries, selection: $selection) {
                TableColumn(area == "registry" ? "Registry Key" : "Application", value: \.application)
                TableColumn("Value") { entry in Text(area == "registry" ? String(entry.key.dropFirst(2)) : entry.key) }
                TableColumn("Data") { entry in Text(summary(entry)).lineLimit(1).font(.system(.body, design: .monospaced)) }
            }
            HStack {
                Text(entries.count == 1 ? "1 value" : "\(entries.count) values").foregroundStyle(.secondary)
                Spacer()
                Button("Edit Value…") {
                    if let entry = entries.first(where: { $0.id == selection }) { editor = ProfileEditRequest(entry: entry) }
                }.disabled(selection == nil || model.busy || model.mediaBusy)
            }
        }.padding(16)
        .task(id: "\(model.selected ?? "")|\(filePath ?? area)") { await load() }
        .sheet(item: $editor) { request in
            if let value, let ball = model.selected {
                ProfileValueSheet(model: model, ball: ball, snapshot: value, entry: request.entry, filePath: filePath) { saved in self.value = saved }
            }
        }
    }
    private func summary(_ entry: ConfigurationEntry) -> String {
        let bytes = area == "registry" ? Array(entry.bytes.dropFirst(4)) : entry.bytes
        if area == "registry", entry.registryType == 4, bytes.count == 4 {
            return "DWORD: " + String(bytes.enumerated().reduce(UInt32(0)) { $0 | UInt32($1.element) << ($1.offset * 8) })
        }
        if bytes.last == 0, !bytes.dropLast().contains(0) {
            return ConfigurationValue.latin1(Array(bytes.dropLast())).replacingOccurrences(of: "\n", with: " ↵ ")
        }
        return "\(bytes.count) bytes: " + ConfigurationValue.hex(Array(bytes.prefix(32)))
    }
    private func load() async {
        guard let ball = model.selected else { return }
        loading = true; defer { loading = false }
        do {
            let result = if let filePath { try await model.configurationFile(filePath, ball: ball).profile }
                         else { try await model.configuration(area, ball: ball) }
            if ball == model.selected, !Task.isCancelled { value = result; error = nil }
        }
        catch { self.error = error.localizedDescription }
    }
}

private struct ProfileEditRequest: Identifiable {
    let id = UUID()
    let entry: ConfigurationEntry?
}
private struct ProfileValueSheet: View {
    let model: AppModel
    let ball: String
    let snapshot: ConfigurationSnapshot
    let entry: ConfigurationEntry?
    var filePath: String? = nil
    let saved: (ConfigurationSnapshot) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var application = ""
    @State private var key = ""
    @State private var mode = "hex"
    @State private var text = ""
    @State private var terminated = true
    @State private var registryType = "3"
    @State private var error: String?
    @State private var saving = false
    @State private var confirmDelete = false
    private var registry: Bool { snapshot.area == "registry" }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(entry == nil ? "Add Value" : "Edit Value").font(.title2.bold())
            LabeledContent(registry ? "Registry key" : "Application") {
                TextField("e.g. hkcu\\Software\\Lotus", text: $application).disabled(entry != nil)
            }
            LabeledContent(registry ? "Value name" : "Key") {
                TextField(registry ? "Empty for default" : "Key name", text: $key).disabled(entry != nil)
            }
            if registry {
                LabeledContent("Registry type") { TextField("Type number", text: $registryType) }
                Text("1: String   2: Expandable string   3: Binary   4: DWORD   7: Multi-string").font(.caption).foregroundStyle(.secondary)
            }
            Picker("Edit as", selection: Binding(get: { mode }, set: { convert(to: $0) })) {
                Text("Hexadecimal").tag("hex"); Text("Latin-1 Text").tag("text"); Text("DWORD (Little Endian)").tag("integer")
            }
            TextEditor(text: $text).font(.system(.body, design: .monospaced)).border(.separator).frame(minHeight: 160)
            if mode == "text" { Toggle("Include terminating NUL byte", isOn: $terminated) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            Text("Saving checks the file revision. Unedited values retain their exact bytes. Changes take effect when the guest next starts.")
                .font(.callout).foregroundStyle(.secondary)
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                if entry != nil { Button("Delete Value…", role: .destructive) { confirmDelete = true } }
                Spacer()
                if saving { ProgressView().controlSize(.small) }
                Button("Save") { save() }.keyboardShortcut(.defaultAction)
            }.disabled(saving)
        }.padding(24).frame(width: 650)
        .task {
            application = entry?.application ?? (registry ? "hkcu\\Software\\" : "")
            key = entry.map { registry ? String($0.key.dropFirst(2)) : $0.key } ?? ""
            registryType = String(entry?.registryType ?? 3)
            let bytes = entry.map { registry ? Array($0.bytes.dropFirst(4)) : $0.bytes } ?? []
            text = ConfigurationValue.hex(bytes)
        }
        .confirmationDialog("Delete this value?", isPresented: $confirmDelete) { Button("Delete Value", role: .destructive) { save(remove: true) } }
        .interactiveDismissDisabled(saving)
    }
    private func bytes(_ mode: String) throws -> [UInt8] {
        switch mode {
        case "text": return try ConfigurationValue.encodeText(text, terminated: terminated)
        case "integer":
            guard let value = UInt32(text.trimmingCharacters(in: .whitespacesAndNewlines)) else { throw BallError("Enter an unsigned 32-bit decimal integer.") }
            return (0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) }
        default: return try ConfigurationValue.decodeHex(text)
        }
    }
    private func convert(to new: String) {
        guard new != mode else { return }
        do {
            let data = try bytes(mode)
            if new == "hex" { text = ConfigurationValue.hex(data) }
            else if new == "text" {
                terminated = data.last == 0
                let content = terminated ? Array(data.dropLast()) : data
                guard !content.contains(0) else { throw BallError("This value contains embedded NUL bytes. Edit it as hexadecimal.") }
                text = ConfigurationValue.latin1(content)
            } else {
                guard data.count == 4 else { throw BallError("A 32-bit integer must contain exactly four bytes.") }
                text = String(data.enumerated().reduce(UInt32(0)) { $0 | UInt32($1.element) << ($1.offset * 8) })
            }
            mode = new; error = nil
        } catch { self.error = error.localizedDescription }
    }
    private func save(remove: Bool = false) {
        do {
            var data = remove ? [] : try bytes(mode)
            if registry {
                guard let type = UInt32(registryType) else { throw BallError("Enter a registry type number.") }
                if type == 4 && data.count != 4 && !remove { throw BallError("REG_DWORD requires exactly four bytes.") }
                data = (0..<4).map { UInt8(truncatingIfNeeded: type >> ($0 * 8)) } + data
            }
            let wire = ConfigurationValue.hex(data)
            saving = true
            Task {
                defer { saving = false }
                do {
                    let result = if let filePath {
                        try await model.saveConfigurationFile(filePath, ball: ball, revision: snapshot.revision, text: wire, application: application, key: key, remove: remove).profile
                    } else {
                        try await model.saveConfiguration(snapshot.area, ball: ball, revision: snapshot.revision, application: registry ? application.lowercased() : application, key: registry ? "v:" + key.lowercased() : key, data: wire, remove: remove)
                    }
                    saved(result); dismiss()
                } catch { self.error = error.localizedDescription }
            }
        } catch { self.error = error.localizedDescription }
    }
}

private struct OS2FilesSettingsView: View {
    @Bindable var model: AppModel
    @State private var files: ConfigurationFiles?
    @State private var selection: String?
    @State private var error: String?
    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading) {
                List(files?.files ?? [], selection: $selection) { file in
                    VStack(alignment: .leading) {
                        Text(file.label)
                        Text(file.path).font(.caption).foregroundStyle(.secondary).lineLimit(2)
                    }.tag(file.path)
                }.accessibilityLabel("OS/2 Configuration Files")
                Button("Reload File List") { Task { await load() } }.padding(8)
                if files?.truncated == true { Text("The file scan reached its limit. Some INI files are not listed.").font(.caption).padding(8) }
            }.frame(minWidth: 160, idealWidth: 210, maxWidth: 260)
            Divider()
            Group {
                if let file = files?.files.first(where: { $0.path == selection }) {
                    if file.role == "config" { ConfigSysView(model: model).padding(16) }
                    else if ["user", "system", "registry"].contains(file.role) { ProfileConfigurationView(model: model, area: file.role).id(file.path) }
                    else if file.kind == "profile" { ProfileConfigurationView(model: model, area: "file", filePath: file.path).id(file.path) }
                    else { ConfigurationFileView(model: model, path: file.path).id(file.path) }
                } else if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).padding() }
                else { ContentUnavailableView("Config Files", systemImage: "doc.text", description: Text("Choose CONFIG.SYS or an INI file to inspect its contents.")) }
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }.task(id: model.selected) { await load() }
    }
    private func load() async {
        guard let ball = model.selected else { return }
        do {
            let result = try await model.configurationFiles(ball: ball)
            guard ball == model.selected, !Task.isCancelled else { return }
            files = result; error = nil
            if !result.files.contains(where: { $0.path == selection }) { selection = result.files.first?.path }
        } catch { self.error = error.localizedDescription }
    }
}

private struct ConfigurationFileView: View {
    @Bindable var model: AppModel
    let path: String
    @State private var value: ConfigurationFileSnapshot?
    @State private var error: String?
    @State private var editing = false
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(path).font(.headline).textSelection(.enabled)
            if let value {
                Text(value.format).foregroundStyle(.secondary)
                if !value.reason.isEmpty { Text(value.reason).foregroundStyle(.secondary) }
                ScrollView { Text(value.text ?? value.data ?? "").font(.system(.body, design: .monospaced)).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading) }
                Button("Edit File…") { editing = true }.disabled(!value.editable || model.processRunning || model.busy || model.mediaBusy)
            }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
        }.padding(16)
        .task(id: "\(model.selected ?? "")|\(path)") {
            guard let ball = model.selected else { return }
            do { let result = try await model.configurationFile(path, ball: ball); if ball == model.selected, !Task.isCancelled { value = result } }
            catch { self.error = error.localizedDescription }
        }
        .sheet(isPresented: $editing) {
            if let value, let ball = model.selected { ConfigurationTextSheet(model: model, ball: ball, snapshot: value) { self.value = $0 } }
        }
    }
}

private struct ConfigurationTextSheet: View {
    let model: AppModel
    let ball: String
    let snapshot: ConfigurationFileSnapshot
    let saved: (ConfigurationFileSnapshot) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""
    @State private var saving = false
    @State private var error: String?
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(snapshot.path).font(.headline)
            TextEditor(text: $text).font(.system(.body, design: .monospaced)).border(.separator)
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            Text("Latin-1 text. Saving checks for changes made by installers or other tools.").foregroundStyle(.secondary)
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Button("Save") {
                    saving = true
                    Task {
                        defer { saving = false }
                        do { let result = try await model.saveConfigurationFile(snapshot.path, ball: ball, revision: snapshot.revision, text: text); saved(result); dismiss() }
                        catch { self.error = error.localizedDescription }
                    }
                }.keyboardShortcut(.defaultAction)
            }.disabled(saving)
        }.padding(24).frame(width: 680, height: 520)
        .task { text = snapshot.text ?? "" }
        .interactiveDismissDisabled(saving || text != snapshot.text)
    }
}
