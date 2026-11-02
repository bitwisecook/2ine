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
    var driveActions = BallViewActions()
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
    private var removable: Bool { snapshot.manifest.drives[drive]?.isRemovable == true }
    private var readOnly: Bool { snapshot.manifest.drives[drive]?.isReadOnly == true }
    private var ejected: Bool { removable && (imageSource == nil || snapshot.unavailableDrives?[drive] != nil) }
    private var imageSource: String? { model.imageSource(drive) }
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
                            Button(!removable ? "Show in Finder" : "Show Image in Finder") { model.reveal(imageSource ?? entry.hostPath) }
                        }
                }.width(min: 160, ideal: 320)
                TableColumn("Kind") { Text($0.directory ? "Folder" : "File") }.width(80)
            } rows: {
                ForEach(entries) { entry in
                    if removable {
                        TableRow(entry)
                    } else {
                        TableRow(entry).draggable(URL(filePath: entry.hostPath))
                            .dropDestination(for: URL.self) { urls in
                                if allowsImport, entry.directory { model.importFiles(urls, into: entry.path) }
                            }
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
                else if ejected {
                    ContentUnavailableView {
                        Label("\(drive): — Ejected", systemImage: "eject")
                    } description: { Text("Attach an image using the drive menus in the title bar.") }
                      actions: { Button("Attach Image…") { model.chooseMedia(drive) }.disabled(!model.canChangeMedia) }
                }
                else if entries.isEmpty {
                    ContentUnavailableView("Empty Folder", systemImage: "folder", description: Text(allowsImport && !readOnly && !ejected ? "Drop files or folders here to copy them into this drive." : "This folder has no matching items."))
                        .allowsHitTesting(false)
                }
            }
            .overlay { if targeted && allowsImport && !readOnly && !ejected { RoundedRectangle(cornerRadius: 6).stroke(.tint, lineWidth: 3).allowsHitTesting(false) } }
            .dropDestination(for: URL.self) { urls, _ in
                guard allowsImport, !readOnly, !ejected, let target = listing?.path else { return false }
                return model.importFiles(urls, into: target)
            } isTargeted: { targeted = $0 }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("guestBrowserError") }
            HStack {
                Text(listing?.path ?? path).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                if allowsImport && !readOnly && !ejected {
                    Button("Import Files…") { model.chooseImport(into: listing?.path ?? path) }
                        .disabled(loading || listing == nil || model.busy || model.mediaBusy).accessibilityIdentifier("importFiles")
                }
                if removable { Label(ejected ? "Ejected" : readOnly ? "Read-only image" : "Writable image", systemImage: ejected ? "eject" : "opticaldisc").font(.caption).foregroundStyle(.secondary) }
                Spacer()
                if let choose {
                    Button(directoriesOnly ? "Choose Folder" : "Choose File") { if let chosenPath { choose(chosenPath) } }
                        .disabled(chosenPath == nil || loading || model.busy || model.mediaBusy)
                        .keyboardShortcut(.defaultAction).accessibilityIdentifier("chooseGuestItem")
                }
            }
        }
        .focusedSceneValue(\.ballViewActions, allowsImport ? browserActions : nil)
        .onAppear { navigate(initialPath) }
        .onChange(of: drive) { _, letter in
            guard !path.hasPrefix(letter + ":") else { return }
            navigate(locations[letter] ?? snapshot.workingDirectories[letter] ?? letter + ":\\")
        }
        .task(id: "\(model.selected ?? "")|\(path)|\(model.revision)") {
            guard let ball = model.selected else { return }
            loading = true; error = nil
            if ejected { listing = nil; loading = false; return }
            do {
                let result = try await model.browse(path, ball: ball)
                try Task.checkCancellation()
                listing = result; address = result.path; locations[drive] = result.path; selection = nil
            } catch is CancellationError { return }
            catch { listing = nil; self.error = error.localizedDescription }
            loading = false
        }
    }
    private var browserActions: BallViewActions {
        var actions = driveActions
        if let path = listing?.path, !loading, !readOnly, !ejected { actions.importFiles = { model.chooseImport(into: path) } }
        return actions
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
            Text(directoriesOnly ? "Choose OS/2 Folder" : "Choose OS/2 Program or Command File").font(.title2.bold())
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
    @State private var selected: String? = "C"
    @State private var defaultPicker = false
    private var letter: String { selected ?? snapshot.currentDrive }
    private var driveActions: BallViewActions {
        let mapping = snapshot.manifest.drives[letter]
        let available = snapshot.roots[letter] != nil && snapshot.unavailableDrives?[letter] == nil
        return BallViewActions(unmap: letter != "C" && mapping?.isLocked != true ? { model.command(["unmap", letter]) } : nil,
                               setDefaultFolder: available ? { defaultPicker = true } : nil,
                               changeMountpoint: letter != "C" && mapping?.isRemovable == false ? { model.chooseMountpoint(letter) } : nil)
    }
    private var rows: [DriveRow] {
        snapshot.manifest.drives.keys.sorted().map { letter in
            let mapping = snapshot.manifest.drives[letter]
            let source = mapping?.isRemovable == true ? (mapping?.source ?? "Ejected") : (snapshot.roots[letter] ?? mapping?.source ?? "")
            return DriveRow(letter: letter, root: (snapshot.unavailableDrives?[letter] == nil ? "" : "Ejected — ") + source,
                            working: snapshot.workingDirectories[letter] ?? letter + ":\\")
        }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Button("Map Drive…", systemImage: "plus") { model.mapping = true }
                Button("Unmap", systemImage: "minus") { model.command(["unmap", letter]) }.disabled(letter == "C" || snapshot.manifest.drives[letter]?.isLocked == true || model.busy || model.mediaBusy)
                Button("Change Mountpoint…") { model.chooseMountpoint(letter) }
                    .disabled(letter == "C" || snapshot.manifest.drives[letter]?.isRemovable != false || model.busy || model.mediaBusy)
                    .accessibilityIdentifier("changeMountpoint")
                Spacer()
                Button("Set Default Folder…") { defaultPicker = true }.disabled(model.busy || snapshot.roots[letter] == nil || snapshot.unavailableDrives?[letter] != nil || model.mediaBusy)
            }.disabled(model.busy || model.mediaBusy)
            Table(of: DriveRow.self, selection: $selected) {
                TableColumn("Drive") { row in
                    Text(row.letter + ":").font(.system(.body, design: .monospaced).bold())
                        .frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
                }.width(min: 70, ideal: 85, max: 110)
                TableColumn("Default Working Directory", value: \.working).width(min: 150, ideal: 210)
                TableColumn("Folder or Image", value: \.root).width(min: 180, ideal: 360)
            } rows: {
                ForEach(rows) { row in
                    TableRow(row).dropDestination(for: URL.self) { urls in
                        if snapshot.unavailableDrives?[row.letter] == nil && snapshot.roots[row.letter] != nil &&
                            (snapshot.manifest.drives[row.letter]?.isReadOnly != true && snapshot.manifest.drives[row.letter]?.image != nil || snapshot.manifest.drives[row.letter]?.isRemovable != true) {
                            model.importFiles(urls, into: row.letter + ":\\")
                        }
                    }
                }
            }.frame(height: 150).accessibilityIdentifier("drivesTable").accessibilityLabel("Mapped OS/2 drives")
            Text("Drop files onto writable drives to copy them. CDs and protected floppies are read-only. Existing files are never replaced.")
                .font(.caption).foregroundStyle(.secondary)
            if snapshot.roots[letter] == nil {
                ContentUnavailableView {
                    Label("\(letter): — Ejected", systemImage: "eject")
                } description: {
                    Text("The previous folder is unavailable: " + (snapshot.manifest.drives[letter]?.source ?? "")).textSelection(.enabled)
                } actions: {
                    Button("Choose Folder…") { model.chooseMountpoint(letter) }.disabled(model.busy || model.mediaBusy)
                }
            } else {
                GuestBrowserView(model: model, snapshot: snapshot, initialPath: snapshot.workingDirectories[letter] ?? letter + ":\\", allowsImport: true,
                             driveActions: driveActions)
                .id("\(model.selected ?? "")|\(letter)")
            }
            if !model.driveNotice.isEmpty { Text(model.driveNotice).font(.callout) }
        }.padding(20)
        .onAppear { if let drive = model.browseDrive { selected = drive; model.browseDrive = nil } }
        .onChange(of: model.browseDrive) { _, drive in if let drive { selected = drive; model.browseDrive = nil } }
        .onChange(of: snapshot.manifest.drives.keys.sorted()) { _, keys in
            if !keys.contains(letter) { selected = snapshot.currentDrive }
        }
        .sheet(isPresented: $defaultPicker) {
            GuestPickerSheet(model: model, snapshot: snapshot, path: snapshot.workingDirectories[letter] ?? letter + ":\\", directoriesOnly: true, requiredDrive: letter) { model.setWorkingDirectory($0) }
        }
        .focusedSceneValue(\.ballViewActions, driveActions)
    }
}
