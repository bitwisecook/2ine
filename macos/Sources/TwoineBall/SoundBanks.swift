// Public domain. No rights reserved. Wholly AI-authored.
#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit
import UniformTypeIdentifiers

struct AudioSheet: View {
    @Bindable var model: AppModel
    let ball: String
    @Environment(\.dismiss) private var dismiss
    @State private var snapshot: AudioSnapshot?
    @State private var settings: AudioSettings?
    @State private var loading = false
    @State private var error: String?
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Text("Sound Banks & MIDI").font(.headline)
                Spacer()
                Button("Save") { save() }.keyboardShortcut(.defaultAction).disabled(settings == nil)
            }.padding()
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).padding(.horizontal) }
            if let snapshot, settings != nil {
                editor(snapshot)
            } else { ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity) }
        }
        .frame(minWidth: 660, idealWidth: 760, minHeight: 540, idealHeight: 720)
        .disabled(loading)
        .interactiveDismissDisabled(loading)
        .task { await load() }
    }
    @ViewBuilder private func editor(_ snapshot: AudioSnapshot) -> some View {
        if let draft = Binding($settings) {
            Form {
                Section("Sound Banks") {
                    Picker("Default bank", selection: draft.bank) {
                        ForEach(snapshot.banks) { bank in Text(bank.name).tag(bank.id) }
                    }
                    Button("Import Sound Bank…", action: choose)
                        .accessibilityIdentifier("importSoundBank")
                    Text("Import SF2, SF3, DLS, SFZ or a ZIP containing one bank. SFZ copies the complete containing folder, including its samples and license files. Original files are kept.")
                        .font(.caption).foregroundStyle(.secondary)
                    ForEach(snapshot.banks) { bank in
                        LabeledContent(bank.name, value: bank.format.uppercased())
                        if let license = bank.license { Text(license).font(.caption).foregroundStyle(.secondary) }
                    }
                }
                Section("MIDI Mapping") {
                    Picker("Mapping preset", selection: draft.profile) {
                        ForEach(snapshot.profiles) { profile in Text(profile.name).tag(profile.id) }
                    }
                    Button("Apply Mapping Defaults") {
                        if let preset = snapshot.profiles.first(where: { $0.id == settings?.profile }) { settings?.applyDefaults(preset) }
                    }
                    DisclosureGroup("Mapping Guide") {
                        Text("Program 0 follows the MIDI file; 1–128 pins an instrument. Bank −1 and ‘Follow Profile’ inherit the mapping. GS/XG use available bank variations with GM fallback. MT-32 maps channels only; authentic MT-32 synthesis requires Munt and your ROMs.")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    ForEach(0..<16, id: \.self) { index in
                        DisclosureGroup("Channel \(index + 1)") {
                            channelEditor(draft.channels[index], banks: snapshot.banks)
                        }
                    }
                }
                Text("Changes apply to the next launch.").foregroundStyle(.secondary)
            }.formStyle(.grouped)
        }
    }
    private func channelEditor(_ channel: Binding<MIDIChannel>, banks: [SoundBank]) -> some View {
        let id = channel.wrappedValue.bank.isEmpty ? (settings?.bank ?? "default") : channel.wrappedValue.bank
        let sfz = banks.first(where: { $0.id == id })?.format == "sfz"
        return Group {
            Picker("Output channel", selection: channel.target) {
                ForEach(1...16, id: \.self) { Text(String($0)).tag($0) }
            }
            Picker("Sound bank", selection: channel.bank) {
                Text("Default Bank").tag("")
                ForEach(banks) { bank in Text(bank.name).tag(bank.id) }
            }
            Stepper("Program: \(channel.wrappedValue.program + 1) (0 follows MIDI)", value: Binding(get: { channel.wrappedValue.program + 1 }, set: { channel.wrappedValue.program = $0 - 1 }), in: 0...128).disabled(sfz)
            TextField("Bank number (−1 follows MIDI)", value: channel.bankNumber, format: .number).disabled(sfz)
            Picker("Channel type", selection: channel.percussion) {
                Text("Follow Profile").tag(-1); Text("Melodic").tag(0); Text("Percussion").tag(1)
            }.disabled(sfz)
            if sfz { Text("SFZ defines its own instruments. Bank/program/type controls apply to SF2, SF3 and DLS.").font(.caption).foregroundStyle(.secondary) }
            Toggle("Mute", isOn: channel.muted)
        }
    }
    private func load() async {
        loading = true; defer { loading = false }
        do {
            let value = try await model.audioSnapshot(ball: ball)
            snapshot = value; settings = value.settings
        } catch { self.error = error.localizedDescription }
    }
    private func choose() {
        let panel = NSOpenPanel()
        panel.title = "Import Sound Bank"
        panel.message = "SFZ imports its complete containing folder. Choose an instrument in its own folder, with its samples and license files."
        panel.allowedContentTypes = ["sf2", "sf3", "sfz", "dls", "zip"].compactMap { UTType(filenameExtension: $0) }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task {
            loading = true; defer { loading = false }
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            do { snapshot = try await model.importSoundBank(url.path, ball: ball) }
            catch { self.error = error.localizedDescription }
        }
    }
    private func save() {
        guard let settings else { return }
        Task {
            loading = true; defer { loading = false }
            do { try await model.saveAudio(settings, ball: ball); dismiss() }
            catch { self.error = error.localizedDescription }
        }
    }
}

struct ProjectAboutView: View {
    @Bindable var model: AppModel
    @State private var catalogue: ProjectCatalogue?
    @State private var error: String?
    private var versionLabel: String {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.1"
        guard let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String else { return "Version " + version }
        return "Version " + version + "(" + build + ")"
    }
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                HStack {
                    Image(nsImage: NSApplication.shared.applicationIconImage).resizable().frame(width: 72, height: 72)
                    VStack(alignment: .leading) {
                        Text("2ine").font(.largeTitle)
                        Text(versionLabel).foregroundStyle(.secondary)
                    }
                }
                Text("Created by Ryan C. Gordon (icculus)").font(.headline)
                Text("This fork builds on Ryan’s original OS/2 compatibility layer and retains his copyright and zlib-style license. Bundled projects retain their own licenses.")
                HStack {
                    Link("Ryan C. Gordon", destination: URL(string: "https://icculus.org/")!)
                    Link("Original 2ine", destination: URL(string: "https://github.com/icculus/2ine")!)
                    Link("2ine Project", destination: URL(string: "https://github.com/bitwisecook/2ine")!)
                }
                Link("All Project Sources, Versions & Downloads", destination: URL(string: "https://github.com/bitwisecook/2ine/blob/main/docs/project-sources.md")!)
                if let error { Text(error).foregroundStyle(.red) }
                if let catalogue {
                    ForEach(catalogue.projects) { project in
                        Divider()
                        VStack(alignment: .leading, spacing: 6) {
                            Text(project.name).font(.headline)
                            Text(project.version).foregroundStyle(.secondary)
                            if let role = project.role { Text(role).font(.caption).foregroundStyle(.secondary) }
                            Text(project.authors.joined(separator: "; "))
                            Text(project.license).font(.caption)
                            HStack {
                                if let url = URL(string: project.homepage) { Link("Project", destination: url) }
                                if let url = URL(string: project.source) { Link("Source", destination: url) }
                                if let url = URL(string: project.download) { Link("Download", destination: url) }
                            }
                        }
                    }
                } else if error == nil { ProgressView() }
            }.padding(24).textSelection(.enabled)
        }
        .frame(minWidth: 540, idealWidth: 640, minHeight: 420, idealHeight: 660)
        .task(id: model.build) {
            do { catalogue = try await model.projectCatalogue() }
            catch { self.error = error.localizedDescription }
        }
    }
}
