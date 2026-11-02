#if canImport(BallCore)
import BallCore
#endif
import SwiftUI

struct GuestBrowserView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    var initialPath: String = "C:"
    var directoriesOnly = false
    var requiredDrive: String?
    var allowsImport = false
    var choose: ((String) -> Void)?
    @State private var drive = "C"
    @State private var path = "C:"
    @State private var address = "C:"
    @State private var locations: [String: String] = [:]
    @State private var listing: GuestDirectory?
    @State private var selection: String?
    @State private var loading = false
    @State private var error: String?
    @State private var targeted = false
    private var selectedEntry: GuestEntry? { listing?.entries.first { $0.path == selection } }
    private var chosenPath: String? { directoriesOnly ? (selectedEntry?.directory == true ? selection : listing?.path) : (selectedEntry?.directory == false ? selection : nil) }
    private var entries: [GuestEntry] {
        (listing?.entries ?? []).filter { !directoriesOnly || $0.directory }.sorted {
            if $0.directory != $1.directory { return $0.directory }
            return $0.name.localizedStandardCompare($1.name) == .orderedAscending
        }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Picker("Drive", selection: $drive) {
                    ForEach(snapshot.roots.keys.sorted().filter { requiredDrive == nil || $0 == requiredDrive }, id: \.self) { Text($0 + ":").tag($0) }
                }.frame(width: 145).accessibilityIdentifier("guestDrivePicker")
                Button("Up", systemImage: "arrow.up") { goUp() }
                    .disabled(listing?.parent == nil || loading)
                    .keyboardShortcut(.upArrow, modifiers: .command)
                TextField("OS/2 path", text: $address).onSubmit { navigate(address) }
                    .font(.system(.body, design: .monospaced)).accessibilityIdentifier("guestBrowserPath")
                Button("Go") { navigate(address) }.disabled(loading)
            }
            Table(of: GuestEntry.self, selection: $selection) {
                TableColumn("Name") { entry in
                    Label(entry.name, systemImage: entry.directory ? "folder" : "doc")
                        .contextMenu {
                            if entry.directory { Button("Open Folder") { navigate(entry.path) } }
                            Button("Show in Finder") { model.reveal(entry.hostPath) }
                        }
                }.width(min: 160, ideal: 320)
                TableColumn("Kind") { Text($0.directory ? "Folder" : "File") }.width(80)
            } rows: {
                ForEach(entries) { entry in
                    TableRow(entry).draggable(URL(filePath: entry.hostPath))
                        .dropDestination(for: URL.self) { urls in
                            if allowsImport, entry.directory { model.importFiles(urls, into: entry.path) }
                        }
                }
            }
            .accessibilityIdentifier("guestFilesTable").accessibilityLabel("Files on drive " + drive)
            .contextMenu(forSelectionType: String.self) { paths in
                if let entry = entries.first(where: { paths.contains($0.path) }), entry.directory {
                    Button("Open Folder") { navigate(entry.path) }
                }
            } primaryAction: { paths in
                if let entry = entries.first(where: { paths.contains($0.path) }) {
                    if entry.directory { navigate(entry.path) } else if !directoriesOnly { choose?(entry.path) }
                }
            }
            .overlay {
                if loading { ProgressView("Reading drive…") }
                else if entries.isEmpty {
                    ContentUnavailableView("Empty Folder", systemImage: "folder", description: Text(allowsImport ? "Drop files or folders here to copy them into this drive." : "This folder has no matching items."))
                        .allowsHitTesting(false)
                }
            }
            .overlay { if targeted && allowsImport { RoundedRectangle(cornerRadius: 6).stroke(.tint, lineWidth: 3).allowsHitTesting(false) } }
            .dropDestination(for: URL.self) { urls, _ in
                guard allowsImport, let target = listing?.path else { return false }
                return model.importFiles(urls, into: target)
            } isTargeted: { targeted = $0 }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("guestBrowserError") }
            HStack {
                Text(listing?.path ?? path).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                Spacer()
                if let choose {
                    Button(directoriesOnly ? "Choose Folder" : "Choose Executable") { if let chosenPath { choose(chosenPath) } }
                        .disabled(chosenPath == nil || loading || model.busy)
                        .keyboardShortcut(.defaultAction).accessibilityIdentifier("chooseGuestItem")
                }
            }
        }
        .onAppear { navigate(initialPath) }
        .onChange(of: drive) { _, letter in
            guard !path.hasPrefix(letter + ":") else { return }
            navigate(locations[letter] ?? snapshot.workingDirectories[letter] ?? letter + ":\\")
        }
        .task(id: "\(model.selected ?? "")|\(path)|\(model.revision)") {
            guard let ball = model.selected else { return }
            loading = true; error = nil
            do {
                let result = try await model.browse(path, ball: ball)
                try Task.checkCancellation()
                listing = result; address = result.path; locations[drive] = result.path; selection = nil
            } catch is CancellationError { return }
            catch { listing = nil; self.error = error.localizedDescription }
            loading = false
        }
    }
    private func navigate(_ value: String) {
        if value.count >= 2, value[value.index(after: value.startIndex)] == ":" {
            let letter = String(value.prefix(1)).uppercased()
            if let requiredDrive, letter != requiredDrive { error = "Choose a folder on " + requiredDrive + ":"; return }
            if snapshot.roots[letter] != nil { drive = letter }
        }
        path = value; address = value; selection = nil
    }
    private func goUp() {
        if let parent = listing?.parent { navigate(parent) }
    }
}

struct GuestPickerSheet: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    let path: String
    let directoriesOnly: Bool
    var requiredDrive: String?
    let choose: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(directoriesOnly ? "Choose OS/2 Folder" : "Choose OS/2 Executable").font(.title2.bold())
            GuestBrowserView(model: model, snapshot: snapshot, initialPath: path, directoriesOnly: directoriesOnly, requiredDrive: requiredDrive) {
                choose($0); dismiss()
            }
            Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction).accessibilityIdentifier("cancelGuestPicker")
        }.padding(20).frame(width: 740, height: 520)
    }
}

private struct DriveRow: Identifiable {
    let letter: String
    let root: String
    let working: String
    var id: String { letter }
}
struct DrivesView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    @Environment(\.undoManager) private var undo
    @State private var selected: String? = "C"
    @State private var defaultPicker = false
    private var letter: String { selected ?? snapshot.currentDrive }
    private var rows: [DriveRow] { snapshot.roots.keys.sorted().map { DriveRow(letter: $0, root: snapshot.roots[$0] ?? "", working: snapshot.workingDirectories[$0] ?? $0 + ":\\") } }
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Button("Map Drive…", systemImage: "plus") { model.mapping = true }
                Button("Unmap", systemImage: "minus") { model.command(["unmap", letter], undo: undo) }.disabled(letter == "C" || model.busy)
                Spacer()
                Button("Set Default Folder…") { defaultPicker = true }.disabled(model.busy)
            }.disabled(model.busy)
            Table(of: DriveRow.self, selection: $selected) {
                TableColumn("Drive") { row in
                    Text(row.letter + ":").font(.system(.body, design: .monospaced).bold())
                        .frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
                }.width(min: 70, ideal: 85, max: 110)
                TableColumn("Default Working Directory", value: \.working).width(min: 150, ideal: 210)
                TableColumn("Mapped Host Folder", value: \.root).width(min: 180, ideal: 360)
            } rows: {
                ForEach(rows) { row in
                    TableRow(row).dropDestination(for: URL.self) { urls in model.importFiles(urls, into: row.letter + ":\\") }
                }
            }.frame(height: 150).accessibilityIdentifier("drivesTable").accessibilityLabel("Mapped OS/2 drives")
            Text("Drop onto a drive letter to copy to its root, or into the folder below. Existing files are never replaced.")
                .font(.caption).foregroundStyle(.secondary)
            GuestBrowserView(model: model, snapshot: snapshot, initialPath: snapshot.workingDirectories[letter] ?? letter + ":\\", allowsImport: true)
                .id("\(model.selected ?? "")|\(letter)")
            if !model.driveNotice.isEmpty { Text(model.driveNotice).font(.callout) }
        }.padding(20)
        .sheet(isPresented: $defaultPicker) {
            GuestPickerSheet(model: model, snapshot: snapshot, path: snapshot.workingDirectories[letter] ?? letter + ":\\", directoriesOnly: true, requiredDrive: letter) { model.setWorkingDirectory($0) }
        }
    }
}
