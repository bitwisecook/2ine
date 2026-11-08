import SwiftUI

#if canImport(BallCore)
import BallCore
#endif

struct GuestBrowserView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    var initialPath: String = "C:"
    var directoriesOnly = false
    var programsOnly = false
    var requiredDrive: String?
    var allowsImport = false
    var driveActions = BallViewActions()
    var choose: ((String) -> Void)?
    @State private var drive = "C"
    @State private var path = "C:"
    @State private var address = "C:"
    @State private var locations: [String: String] = [:]
    @State private var listing: GuestDirectory?
    @State private var tree = GuestBrowserTree()
    @State private var selection: String?
    @State private var loading = false
    @State private var error: String?
    @State private var targeted = false
    @State private var relevantOnly = true
    @State private var query = ""
    @State private var searchFocused = false
    @State private var browserSort: OutlineBrowserSort? = .init(column: "name")
    private var removable: Bool { snapshot.manifest.drives[drive]?.isRemovable == true }
    private var readOnly: Bool { snapshot.manifest.drives[drive]?.isReadOnly == true }
    private var ejected: Bool {
        removable && (imageSource == nil || snapshot.unavailableDrives?[drive] != nil)
    }
    private var imageSource: String? { model.imageSource(drive) }
    private var selectedEntry: GuestEntry? { tree.entry(selection, roots: listing?.entries ?? []) }
    private var chosenPath: String? {
        directoriesOnly
            ? (selectedEntry?.directory == true ? selection : listing?.path)
            : (selectedEntry?.directory == false ? selection : nil)
    }
    private var browserNodes: [OutlineBrowserNode] {
        tree.nodes(
            listing?.entries ?? [], directoriesOnly: directoriesOnly,
            programsOnly: programsOnly, relevantOnly: relevantOnly)
    }
    private func entry(_ path: String?) -> GuestEntry? {
        tree.entry(path, roots: listing?.entries ?? [])
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Picker("Drive", selection: $drive) {
                    ForEach(
                        snapshot.roots.keys.sorted().filter {
                            requiredDrive == nil || $0 == requiredDrive
                        }, id: \.self
                    ) { Text($0 + ":").tag($0) }
                }.frame(width: 145).accessibilityIdentifier("guestDrivePicker")
                Button("Up", systemImage: "arrow.up") { goUp() }
                    .disabled(listing?.parent == nil || loading)
                    .keyboardShortcut(.upArrow, modifiers: .command)
                TextField("OS/2 path", text: $address).onSubmit { navigate(address) }
                    .font(.system(.body, design: .monospaced)).accessibilityIdentifier(
                        "guestBrowserPath")
                Button("Go") { navigate(address) }.disabled(loading)
            }
            BrowserSearchField(
                text: $query, focused: $searchFocused, identifier: "guestFilesSearch"
            )
            .frame(height: 32).accessibilityIdentifier("guestFilesSearch")
            if programsOnly || directoriesOnly {
                Toggle("Relevant Files Only", isOn: $relevantOnly)
                    .toggleStyle(.checkbox).accessibilityIdentifier("relevantGuestFiles")
                    .help(
                        directoriesOnly
                            ? "Show folders only. Turn off to show all files."
                            : "Show executable and command files. Turn off to show all files."
                    )
                    .onChange(of: relevantOnly) { _, _ in selection = nil }
            }
            NativeOutlineBrowser(
                nodes: browserNodes, columns: BrowserRows.fileColumns,
                selection: $selection, query: $query, searchFocused: $searchFocused,
                sort: $browserSort, disabled: loading,
                identifier: "guestFilesTable", accessibilityLabel: "Files on drive " + drive,
                onAction: { id, action in
                    guard let entry = entry(id) else { return }
                    if action == "open" { navigate(entry.path) }
                    if action == "reveal" { model.reveal(imageSource ?? entry.hostPath) }
                },
                onActivate: { id in
                    guard let entry = entry(id) else { return }
                    if entry.directory {
                        navigate(entry.path)
                    } else if !directoriesOnly {
                        choose?(entry.path)
                    }
                },
                onActivateBranch: { id in
                    if let entry = entry(id), entry.directory { navigate(entry.path) }
                },
                onExpand: { id in
                    guard let ball = model.selected, entry(id)?.directory == true else { return }
                    tree.expand(id) { path in try await model.browse(path, ball: ball) }
                },
                contextActions: { id in
                    guard let entry = entry(id) else { return [] }
                    return
                        (entry.directory
                        ? [OutlineMenuAction(id: "open", title: "Open Folder")] : []) + [
                            OutlineMenuAction(
                                id: "reveal",
                                title: removable ? "Show Image in Finder" : "Show in Finder")
                        ]
                },
                dragURL: { id in
                    guard !removable, let entry = entry(id) else { return nil }
                    return URL(filePath: entry.hostPath)
                },
                canDrop: { id in
                    guard allowsImport, !readOnly, !ejected, !loading, !model.busy, !model.mediaBusy
                    else { return false }
                    return id == nil ? listing != nil : entry(id)?.directory == true
                },
                onDrop: { urls, id in
                    guard allowsImport, !readOnly, !ejected, let target = id ?? listing?.path else {
                        return false
                    }
                    return model.importFiles(urls, into: target)
                }, dropTargetChanged: { targeted = $0 }
            )
            .overlay {
                if loading {
                    ProgressView("Reading drive…")
                } else if ejected {
                    ContentUnavailableView {
                        Label("\(drive): — Ejected", systemImage: "eject")
                    } description: {
                        Text("Attach an image using the drive menus in the title bar.")
                    } actions: {
                        Button("Attach Image…") { model.chooseMedia(drive) }.disabled(
                            !model.canChangeMedia)
                    }
                } else if browserNodes.compactMap({ $0.filtered(query) }).isEmpty {
                    ContentUnavailableView(
                        "Empty Folder", systemImage: "folder",
                        description: Text(
                            allowsImport && !readOnly && !ejected
                                ? "Drop files or folders here to copy them into this drive."
                                : "This folder has no matching items.")
                    )
                    .allowsHitTesting(false)
                }
            }
            .overlay {
                if targeted && allowsImport && !readOnly && !ejected {
                    RoundedRectangle(cornerRadius: 6).stroke(.tint, lineWidth: 3).allowsHitTesting(
                        false)
                }
            }
            if let error = error ?? tree.errors.keys.sorted().first.flatMap({ tree.errors[$0] }) {
                Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier(
                    "guestBrowserError")
            }
            HStack {
                Text(listing?.path ?? path).font(.system(.caption, design: .monospaced))
                    .textSelection(.enabled)
                if allowsImport && !readOnly && !ejected {
                    Button("Import Files…") { model.chooseImport(into: listing?.path ?? path) }
                        .disabled(loading || listing == nil || model.busy || model.mediaBusy)
                        .accessibilityIdentifier("importFiles")
                }
                if removable {
                    Label(
                        ejected ? "Ejected" : readOnly ? "Read-only image" : "Writable image",
                        systemImage: ejected ? "eject" : "opticaldisc"
                    ).font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                if let choose {
                    Button(directoriesOnly ? "Choose Folder" : "Choose File") {
                        if let chosenPath { choose(chosenPath) }
                    }
                    .disabled(chosenPath == nil || loading || model.busy || model.mediaBusy)
                    .keyboardShortcut(.defaultAction).accessibilityIdentifier("chooseGuestItem")
                }
            }
        }
        .focusedSceneValue(\.ballViewActions, allowsImport ? browserActions : nil)
        .onChange(of: query) { _, _ in selection = nil }
        .onAppear { navigate(initialPath) }
        .onDisappear { tree.reset() }
        .onChange(of: drive) { _, letter in
            guard !path.hasPrefix(letter + ":") else { return }
            navigate(locations[letter] ?? snapshot.workingDirectories[letter] ?? letter + ":\\")
        }
        .task(id: "\(model.selected ?? "")|\(path)|\(model.revision)") {
            guard let ball = model.selected else { return }
            loading = true
            error = nil
            tree.reset()
            if ejected {
                listing = nil
                loading = false
                return
            }
            do {
                let result = try await model.browse(path, ball: ball)
                try Task.checkCancellation()
                listing = result
                address = result.path
                locations[drive] = result.path
                selection = nil
            } catch is CancellationError { return } catch {
                listing = nil
                self.error = error.localizedDescription
            }
            loading = false
        }
    }
    private var browserActions: BallViewActions {
        var actions = driveActions
        if let path = listing?.path, !loading, !readOnly, !ejected {
            actions.importFiles = { model.chooseImport(into: path) }
        }
        return actions
    }
    private func navigate(_ value: String) {
        if value.count >= 2, value[value.index(after: value.startIndex)] == ":" {
            let letter = String(value.prefix(1)).uppercased()
            if let requiredDrive, letter != requiredDrive {
                error = "Choose a folder on " + requiredDrive + ":"
                return
            }
            if snapshot.roots[letter] != nil { drive = letter }
        }
        path = value
        address = value
        selection = nil
        query = ""
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
            Text(directoriesOnly ? "Choose OS/2 Folder" : "Choose OS/2 Program or Command File")
                .font(.title2.bold())
            GuestBrowserView(
                model: model, snapshot: snapshot, initialPath: path,
                directoriesOnly: directoriesOnly, programsOnly: !directoriesOnly,
                requiredDrive: requiredDrive
            ) {
                choose($0)
                dismiss()
            }
            Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                .accessibilityIdentifier("cancelGuestPicker")
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
    @State private var driveQuery = ""
    @State private var driveSearchFocused = false
    @State private var driveSort: OutlineBrowserSort? = .init(column: "drive")
    private var letter: String { selected ?? snapshot.currentDrive }
    private var driveActions: BallViewActions {
        let mapping = snapshot.manifest.drives[letter]
        let available = snapshot.roots[letter] != nil && snapshot.unavailableDrives?[letter] == nil
        return BallViewActions(
            unmap: letter != "C" && mapping?.isLocked != true
                ? { model.command(["unmap", letter]) } : nil,
            setDefaultFolder: available ? { defaultPicker = true } : nil,
            changeMountpoint: letter != "C" && mapping?.isRemovable == false
                ? { model.chooseMountpoint(letter) } : nil)
    }
    private var rows: [DriveRow] {
        snapshot.manifest.drives.keys.sorted().map { letter in
            let mapping = snapshot.manifest.drives[letter]
            let source =
                mapping?.isRemovable == true
                ? (mapping?.source ?? "Ejected") : (snapshot.roots[letter] ?? mapping?.source ?? "")
            return DriveRow(
                letter: letter,
                root: (snapshot.unavailableDrives?[letter] == nil ? "" : "Ejected — ")
                    + DisplayPath.homeAbbreviated(source),
                working: snapshot.workingDirectories[letter] ?? letter + ":\\")
        }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Button("Map Drive…", systemImage: "plus") { model.mapping = true }
                Button("Unmap", systemImage: "minus") { model.command(["unmap", letter]) }.disabled(
                    letter == "C" || snapshot.manifest.drives[letter]?.isLocked == true
                        || model.busy || model.mediaBusy)
                Button("Change Mountpoint…") { model.chooseMountpoint(letter) }
                    .disabled(
                        letter == "C" || snapshot.manifest.drives[letter]?.isRemovable != false
                            || model.busy || model.mediaBusy
                    )
                    .accessibilityIdentifier("changeMountpoint")
                Spacer()
                Button("Set Default Folder…") { defaultPicker = true }.disabled(
                    model.busy || snapshot.roots[letter] == nil
                        || snapshot.unavailableDrives?[letter] != nil || model.mediaBusy)
            }.disabled(model.busy || model.mediaBusy)
            BrowserSearchField(
                text: $driveQuery, focused: $driveSearchFocused, placeholder: "Search Drives",
                identifier: "driveSearch"
            )
            .frame(height: 28)
            NativeOutlineBrowser(
                nodes: rows.map { row in
                    OutlineBrowserNode(
                        id: row.letter, title: row.letter + ":",
                        cells: [
                            "drive": OutlineCell(
                                text: row.letter + ":", monospaced: true,
                                icon: snapshot.manifest.drives[row.letter]?.isRemovable == true
                                    ? "opticaldisc" : "externaldrive"),
                            "working": OutlineCell(text: row.working),
                            "root": OutlineCell(text: row.root),
                        ], tooltip: row.root,
                        searchText: row.letter + " " + row.working + " " + row.root)
                },
                columns: [
                    OutlineBrowserColumn(
                        id: "drive", title: "Drive", width: 85, minimum: 70, maximum: 110,
                        sortable: true),
                    OutlineBrowserColumn(
                        id: "working", title: "Default Working Directory", width: 210, minimum: 150,
                        sortable: true),
                    OutlineBrowserColumn(
                        id: "root", title: "Folder or Image", width: 360, minimum: 180,
                        sortable: true),
                ], selection: $selected, query: $driveQuery, searchFocused: $driveSearchFocused,
                sort: $driveSort,
                disabled: model.busy || model.mediaBusy, identifier: "drivesTable",
                accessibilityLabel: "Mapped OS/2 drives",
                canDrop: { id in
                    guard let id, let mapping = snapshot.manifest.drives[id] else { return false }
                    return snapshot.unavailableDrives?[id] == nil && snapshot.roots[id] != nil
                        && !mapping.isReadOnly && (mapping.image != nil || !mapping.isRemovable)
                },
                onDrop: { urls, id in
                    guard let id else { return false }
                    return model.importFiles(urls, into: id + ":\\")
                }
            ).frame(height: 150)
            Text(
                "Drop files onto writable drives to copy them. CDs and protected floppies are read-only. Existing files are never replaced."
            )
            .font(.caption).foregroundStyle(.secondary)
            if snapshot.roots[letter] == nil {
                ContentUnavailableView {
                    Label("\(letter): — Ejected", systemImage: "eject")
                } description: {
                    Text(
                        "The previous folder is unavailable: "
                            + DisplayPath.homeAbbreviated(
                                snapshot.manifest.drives[letter]?.source ?? "")
                    ).textSelection(.enabled)
                } actions: {
                    Button("Choose Folder…") { model.chooseMountpoint(letter) }.disabled(
                        model.busy || model.mediaBusy)
                }
            } else {
                GuestBrowserView(
                    model: model, snapshot: snapshot,
                    initialPath: snapshot.workingDirectories[letter] ?? letter + ":\\",
                    allowsImport: true,
                    driveActions: driveActions
                )
                .id("\(model.selected ?? "")|\(letter)")
            }
            if !model.driveNotice.isEmpty { Text(model.driveNotice).font(.callout) }
        }.padding(20)
            .onAppear {
                if let drive = model.browseDrive {
                    selected = drive
                    model.browseDrive = nil
                }
            }
            .onChange(of: model.browseDrive) { _, drive in
                if let drive {
                    selected = drive
                    model.browseDrive = nil
                }
            }
            .onChange(of: snapshot.manifest.drives.keys.sorted()) { _, keys in
                if !keys.contains(letter) { selected = snapshot.currentDrive }
            }
            .sheet(isPresented: $defaultPicker) {
                GuestPickerSheet(
                    model: model, snapshot: snapshot,
                    path: snapshot.workingDirectories[letter] ?? letter + ":\\",
                    directoriesOnly: true, requiredDrive: letter
                ) { model.setWorkingDirectory($0) }
            }
            .focusedSceneValue(\.ballViewActions, driveActions)
    }
}
