import BallCore
import SwiftUI

private struct EnvironmentRow: Identifiable {
    let id: String
    let value: String
    let source: String
    let configured: String?
    let override: String?
    let overrideKey: String?
}

struct EnvironmentSettingsView: View {
    @Bindable var model: AppModel
    @State private var current: BallSnapshot?
    @State private var error: String?
    private func normalized(_ key: String) -> String { key.uppercased() == "OS2PATH" ? "PATH" : key.uppercased() }
    private var rows: [EnvironmentRow] {
        guard let current else { return [] }
        var configured: [String: String] = [:]
        for line in current.configSys.components(separatedBy: .newlines) {
            let line = line.trimmingCharacters(in: .whitespaces)
            let entry = line.uppercased().hasPrefix("SET ") ? String(line.dropFirst(4)) : line.uppercased().hasPrefix("LIBPATH=") ? line : ""
            let parts = entry.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            if parts.count == 2 { configured[normalized(parts[0].trimmingCharacters(in: .whitespaces))] = parts[1].trimmingCharacters(in: .whitespaces) }
        }
        var overrides: [String: String] = [:]
        var overrideKeys: [String: String] = [:]
        for (key, value) in current.manifest.environment { overrides[normalized(key)] = value; overrideKeys[normalized(key)] = key }
        return current.environmentDefaults.keys.sorted().map { key in
            let name = normalized(key)
            return EnvironmentRow(id: name, value: current.environmentDefaults[key] ?? "", source: overrides[name] != nil ? "Ball Override" : configured[name] != nil ? "CONFIG.SYS" : "2ine Default", configured: configured[name], override: overrides[name], overrideKey: overrideKeys[name])
        }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("CONFIG.SYS supplies the starting values. Ball overrides apply afterward; application overrides apply when that application launches. PATH is the guest search path.").foregroundStyle(.secondary)
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            Table(rows) {
                TableColumn("Variable", value: \.id).width(min: 100, ideal: 140)
                TableColumn("Effective Value") { row in Text(row.value).textSelection(.enabled).help(row.value) }
                TableColumn("Source") { row in
                    VStack(alignment: .leading) {
                        Text(row.source)
                        if let configured = row.configured, row.override != nil { Text("CONFIG.SYS: " + configured).font(.caption).foregroundStyle(.secondary) }
                        if let value = row.override { Text("Override: " + value).font(.caption).foregroundStyle(.secondary) }
                    }
                }
                TableColumn("") { row in
                    HStack {
                        Button("Override…") { model.settingsDraft.envName = row.overrideKey ?? row.id; model.settingsDraft.envValue = row.override ?? row.value }
                        if let key = row.overrideKey { Button("Reset") { model.command(["unset", "env." + key]) } }
                    }.disabled(model.busy || model.mediaBusy)
                }.width(min: 150, ideal: 170)
            }
            HStack {
                TextField("Variable", text: Binding(get: { model.settingsDraft.envName }, set: { model.settingsDraft.envName = $0 })).frame(width: 160)
                TextField("Override value", text: Binding(get: { model.settingsDraft.envValue }, set: { model.settingsDraft.envValue = $0 }))
                Button("Set Override") { model.command(["config", "env." + model.settingsDraft.envName, model.settingsDraft.envValue]) }
                    .disabled(model.settingsDraft.envName.trimmingCharacters(in: .whitespaces).isEmpty || model.busy || model.mediaBusy)
            }
            Text("Use %NAME% to refer to an inherited value. Reset removes the override and restores inheritance. Edit the original SET lines under Config Files → CONFIG.SYS.").font(.callout).foregroundStyle(.secondary)
        }.padding(16)
        .task(id: "\(model.selected ?? "")|\(model.revision)|\(model.selected.flatMap { model.liveConfigTexts[$0] } ?? "")") { await load() }
    }
    private func load() async {
        guard let ball = model.selected else { current = nil; return }
        do {
            let value = try await model.settingsSnapshot(ball: ball)
            guard !Task.isCancelled, ball == model.selected else { return }
            current = value; error = nil
        } catch { if !Task.isCancelled { self.error = error.localizedDescription } }
    }
}

struct PrintingSettingsView: View {
    @Bindable var model: AppModel
    var showMediaLinks = true
    @State private var user: ConfigurationSnapshot?
    @State private var system: ConfigurationSnapshot?
    @State private var loading = false
    @State private var error: String?
    private func queue(_ profile: ConfigurationSnapshot?) -> ConfigurationEntry? {
        profile?.entries.first { $0.application.uppercased() == "PM_SPOOLER" && $0.key.uppercased() == "QUEUE" }
    }
    private var stored: ConfigurationEntry? { queue(user) ?? queue(system) }
    private var selectedQueue: String {
        guard let stored else { return "2INEPDF" }
        return ConfigurationValue.latin1(Array(stored.bytes.prefix { $0 != 0 })).components(separatedBy: ";").first?.trimmingCharacters(in: .whitespaces).uppercased() ?? ""
    }
    var body: some View {
        Form {
            Section("Default Printer") {
                Picker("Printer", selection: Binding(get: { selectedQueue }, set: { save($0) })) {
                    Text("Save as PDF").tag("2INEPDF")
                    Text("PCL 5 Printer").tag("2INEPCL")
                    if !["2INEPDF", "2INEPCL"].contains(selectedQueue) { Text(selectedQueue.isEmpty ? "Stored Queue: None" : "Stored Queue: " + selectedQueue).tag(selectedQueue) }
                }.accessibilityIdentifier("defaultPrinter")
                    .settingHighlight(model.highlightedSetting == "printing")
                LabeledContent("Source", value: queue(user) != nil ? "OS2.INI" : queue(system) != nil ? "OS2SYS.INI" : "2ine Default")
                Button("Use Inherited Default") { save(nil) }.disabled(queue(user) == nil)
                Text("Changes apply when an application next starts. Stop the guest before changing its printer preference.").foregroundStyle(.secondary)
            }.disabled(loading || model.busy || model.mediaBusy || model.processRunning || user == nil)
            Section("Output") {
                Text("PDF opens the system Save dialog so you can choose a file name and location. PCL 5 output supports compatible printers and can also be saved to a file.")
                LabeledContent("Resolution", value: "300 DPI")
                LabeledContent("Page", value: "A4")
                if showMediaLinks {
                    Button("Install OS/2 System Component") { model.openOS2ComponentGroup(.printers) }
                        .disabled(model.busy || model.mediaBusy || model.processRunning)
                    Text("2ine supplies PDF and PCL printing. Original OS/2 printer drivers cannot currently be selected.")
                        .font(.callout).foregroundStyle(.secondary)
                }
            }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            Button("Reload Printer Preferences") { Task { await load() } }.disabled(loading)
        }.formStyle(.grouped)
        .task(id: model.selected) { await load() }
    }
    private func load() async {
        guard let ball = model.selected else { return }
        loading = true; defer { loading = false }
        do {
            let first = try await model.configuration("user", ball: ball)
            let second = try await model.configuration("system", ball: ball)
            guard ball == model.selected, !Task.isCancelled else { return }
            user = first; system = second; error = nil
        } catch { self.error = error.localizedDescription }
    }
    private func save(_ name: String?) {
        guard let ball = model.selected, let user else { return }
        let entry = queue(user)
        loading = true
        Task {
            defer { loading = false }
            do {
                let data = name.map { ConfigurationValue.hex(Array(($0 + ";\0").utf8)) } ?? ""
                let value = try await model.saveConfiguration("user", ball: ball, revision: user.revision, application: entry?.application ?? "PM_SPOOLER", key: entry?.key ?? "QUEUE", data: data, remove: name == nil)
                guard ball == model.selected else { return }
                self.user = value; error = nil
            } catch { self.error = error.localizedDescription }
        }
    }
}
