import BallCore
import SwiftUI

@MainActor
struct RegistrySettingsView: View {
    @Bindable var model: AppModel
    @State private var snapshot: ConfigurationSnapshot?
    @State private var tree: RegistryTree?
    @State private var selectedKey: String? = "hkcu"
    @State private var selectedValue: String?
    @State private var filter = ""
    @State private var error: String?
    @State private var loading = false
    @State private var loadID = UUID()
    @State private var editor: RegistryEditRequest?
    @State private var sortOrder = [KeyPathComparator(\RegistryValueRecord.displayName)]
    private var values: [RegistryValueRecord] {
        guard let selectedKey else { return [] }
        return (tree?.values(at: selectedKey) ?? []).filter {
            filter.isEmpty || $0.displayName.localizedCaseInsensitiveContains(filter) ||
            $0.typeName.localizedCaseInsensitiveContains(filter) || $0.summary.localizedCaseInsensitiveContains(filter)
        }.sorted(using: sortOrder)
    }
    private var chosen: RegistryValueRecord? { values.first { $0.id == selectedValue } }
    private var canEdit: Bool { !model.busy && !model.mediaBusy && !model.processRunning && !loading }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("Open32 Registry").font(.headline)
                Spacer()
                Button("Reload", systemImage: "arrow.clockwise") { Task { await load() } }.disabled(loading)
                Button("Add Value…", systemImage: "plus") { edit(nil) }.disabled(snapshot == nil || !canEdit)
            }
            if let snapshot {
                Text(DisplayPath.homeAbbreviated(snapshot.path)).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            }
            if model.processRunning {
                Label("Stop the guest before editing the registry. The running guest caches its registry data.", systemImage: "info.circle")
                    .foregroundStyle(.secondary)
            }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            HSplitView {
                List(selection: $selectedKey) {
                    if let tree {
                        OutlineGroup(tree.roots, children: \.children) { key in
                            Label(key.title.isEmpty ? "(Empty component)" : key.title, systemImage: "folder")
                                .tag(key.path)
                                .help(key.path)
                        }
                    }
                }.listStyle(.sidebar).frame(minWidth: 210, idealWidth: 270)
                    .accessibilityLabel("Registry hives and keys")
                VStack(alignment: .leading, spacing: 10) {
                    Text(selectedKey ?? "Select a registry key").font(.system(.body, design: .monospaced)).textSelection(.enabled)
                    TextField("Filter values in this key", text: $filter).textFieldStyle(.roundedBorder)
                        .accessibilityIdentifier("registryValueFilter")
                    Table(values, selection: $selectedValue, sortOrder: $sortOrder) {
                        TableColumn("Name", value: \.displayName)
                        TableColumn("Type", value: \.typeName)
                        TableColumn("Data") { value in
                            HStack {
                                if value.warning != nil { Image(systemName: "exclamationmark.triangle").foregroundStyle(.orange).help(value.warning ?? "") }
                                Text(value.summary).lineLimit(1).font(.system(.body, design: .monospaced))
                            }
                        }
                    }
                    .contextMenu(forSelectionType: String.self) { selected in
                        Button("Edit Value…") { if let id = selected.first, let value = values.first(where: { $0.id == id }) { edit(value) } }
                            .disabled(selected.count != 1 || !canEdit)
                    } primaryAction: { selected in
                        if canEdit, let id = selected.first, let value = values.first(where: { $0.id == id }) { edit(value) }
                    }
                    HStack {
                        Text(values.count == 1 ? "1 value" : "\(values.count) values").foregroundStyle(.secondary)
                        Spacer()
                        Button("Edit Value…") { edit(chosen) }.disabled(chosen == nil || !canEdit)
                    }
                }.frame(minWidth: 360).padding(.leading, 12)
            }.frame(minHeight: 260)
            Text("Open32 stores typed values in this ball, not a Windows registry hive file. Names fold ASCII case. Text is displayed as Latin-1; hexadecimal preserves bytes from other code pages. Expandable strings are stored literally.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding(16)
        .task(id: model.selected) { await load() }
        .sheet(item: $editor) { request in
            RegistryValueSheet(model: model, request: request) { saved in
                guard model.selected == request.ball else { return }
                snapshot = saved; tree = RegistryTree(saved)
            }
        }
    }
    private func edit(_ record: RegistryValueRecord?) {
        guard let snapshot, let ball = model.selected else { return }
        editor = RegistryEditRequest(ball: ball, snapshot: snapshot, record: record, path: selectedKey ?? "hkcu")
    }
    private func load() async {
        guard let ball = model.selected else { snapshot = nil; tree = nil; return }
        let token = UUID(); loadID = token; loading = true
        defer { if loadID == token { loading = false } }
        do {
            let result = try await model.configuration("registry", ball: ball)
            guard !Task.isCancelled, ball == model.selected, token == loadID else { return }
            snapshot = result; tree = RegistryTree(result); error = nil
            selectedValue = nil
        } catch {
            guard !Task.isCancelled, ball == model.selected, token == loadID else { return }
            self.error = error.localizedDescription
        }
    }
}

private struct RegistryEditRequest: Identifiable {
    let id = UUID()
    let ball: String
    let snapshot: ConfigurationSnapshot
    let record: RegistryValueRecord?
    let path: String
}

@MainActor
private struct RegistryValueSheet: View {
    let model: AppModel
    let request: RegistryEditRequest
    let saved: (ConfigurationSnapshot) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var draft: RegistryValueDraft
    @State private var error: String?
    @State private var saving = false
    @State private var confirmDelete = false
    @State private var customType = false
    init(model: AppModel, request: RegistryEditRequest, saved: @escaping (ConfigurationSnapshot) -> Void) {
        self.model = model; self.request = request; self.saved = saved
        _draft = State(initialValue: RegistryValueDraft(record: request.record, path: request.path))
    }
    private var permitted: Bool {
        !model.processRunning && !model.busy && !model.mediaBusy && model.selected == request.ball && (request.record?.editable ?? true)
    }
    private var validation: String? {
        do {
            _ = try RegistryCodec.path(draft.path)
            _ = try RegistryCodec.valueName(draft.name)
            _ = try draft.bytes()
            return nil
        } catch { return error.localizedDescription }
    }
    private var changed: Bool {
        guard let record = request.record else { return true }
        return (try? draft.bytes(validate: false)) != record.entry.bytes
    }
    private var representations: [RegistryRepresentation] {
        [draft.typedRepresentation, .hex, .rawRecord].compactMap { $0 }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(request.record == nil ? "Add Registry Value" : "Edit Registry Value").font(.title2.bold())
            LabeledContent("Key") {
                TextField("hkcu\\Software\\Lotus", text: $draft.path).disabled(request.record != nil)
                    .accessibilityIdentifier("registryKeyPath")
            }
            LabeledContent("Name") {
                TextField("Empty for the default value", text: $draft.name).disabled(request.record != nil)
                    .accessibilityIdentifier("registryValueName")
            }
            if request.record == nil {
                Picker("Type", selection: Binding(get: { customType ? UInt32.max : draft.type ?? 1 }, set: { value in
                    customType = value == UInt32.max
                    if customType { draft.newType(3) } else { draft.newType(value) }
                })) {
                    ForEach(RegistryType.allCases) { type in Text(type.name).tag(type.rawValue) }
                    Text("Other type number…").tag(UInt32.max)
                }.accessibilityIdentifier("registryValueType").disabled(draft.representation == .rawRecord)
                if customType { LabeledContent("Type number") { TextField("Unsigned 32-bit number", text: $draft.typeText).disabled(draft.representation == .rawRecord) } }
            } else {
                LabeledContent("Type", value: RegistryCodec.typeName(draft.type))
            }
            Picker("Edit as", selection: Binding(get: { draft.representation }, set: { mode in
                do { try draft.selectRepresentation(mode); error = nil }
                catch { self.error = error.localizedDescription }
            })) {
                ForEach(representations) { mode in Text(mode.title).tag(mode) }
            }.accessibilityIdentifier("registryValueRepresentation")
            TextEditor(text: $draft.text).font(.system(.body, design: .monospaced))
                .border(.separator).frame(minHeight: 160)
                .accessibilityIdentifier("registryValueData")
            if draft.representation == .multiString { Text("One nonempty string per line. Empty data represents an empty list; a double NUL terminator is included.").font(.caption).foregroundStyle(.secondary) }
            if draft.representation == .rawRecord { Text("Includes the four-byte little-endian type header followed by the exact value bytes. Changing the header explicitly changes the value type.").font(.caption).foregroundStyle(.secondary) }
            if let record = request.record, let warning = record.warning { Label(warning, systemImage: "exclamationmark.triangle").font(.callout).foregroundStyle(.orange) }
            if request.record?.editable == false { Text("This stored name is not normalized for Open32. It can be inspected, but this editor will not silently rename or duplicate it.").foregroundStyle(.secondary) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            if let validation { Text(validation).font(.callout).foregroundStyle(.secondary) }
            Text("Text uses Latin-1 bytes with a terminating NUL. Integers accept decimal or 0x hexadecimal. Values are limited to 16 MiB; the profile file is limited to 32 MiB. Saving checks the file revision and preserves other records. New key paths are created when their first value is saved.")
                .font(.caption).foregroundStyle(.secondary)
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                if request.record != nil { Button("Delete Value…", role: .destructive) { confirmDelete = true }.disabled(!permitted || saving) }
                Spacer()
                if saving { ProgressView().controlSize(.small) }
                Button("Save") { save() }.keyboardShortcut(.defaultAction)
                    .disabled(!permitted || saving || validation != nil || !changed)
            }
        }.padding(24).frame(width: 680)
        .confirmationDialog("Delete \(request.record?.displayName ?? "this value")?", isPresented: $confirmDelete) {
            Button("Delete Value", role: .destructive) { save(remove: true) }
        }
        .interactiveDismissDisabled(saving)
    }
    private func save(remove: Bool = false) {
        guard permitted, !saving else { return }
        do {
            let path = try RegistryCodec.path(draft.path)
            let name = try RegistryCodec.valueName(draft.name)
            let data = remove ? "" : ConfigurationValue.hex(try draft.bytes())
            saving = true; error = nil
            Task {
                defer { saving = false }
                do {
                    let result = try await model.saveConfiguration("registry", ball: request.ball, revision: request.snapshot.revision,
                                                                  application: path, key: "v:" + name, data: data, remove: remove)
                    saved(result); dismiss()
                } catch { self.error = error.localizedDescription }
            }
        } catch { self.error = error.localizedDescription }
    }
}
