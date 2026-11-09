import AppKit
import SwiftUI

#if canImport(BallCore)
import BallCore
#endif

/// Distinct floppy silhouette for the media control; SF Symbols supplies the CD.
private struct FloppyIcon: View {
    var body: some View {
        Canvas { context, size in
            var outline = Path()
            outline.move(to: CGPoint(x: 1, y: 1))
            outline.addLine(to: CGPoint(x: 12, y: 1))
            outline.addLine(to: CGPoint(x: 15, y: 4))
            outline.addLine(to: CGPoint(x: 15, y: 15))
            outline.addLine(to: CGPoint(x: 1, y: 15))
            outline.closeSubpath()
            outline.addRect(CGRect(x: 4, y: 1, width: 7, height: 5))
            outline.addRect(CGRect(x: 4, y: 9, width: 8, height: 6))
            context.stroke(
                outline, with: .foreground, style: StrokeStyle(lineWidth: 1.3, lineJoin: .round))
        }.frame(width: 16, height: 16).accessibilityHidden(true)
    }
}

struct MediaDeviceMenu: View {
    @Bindable var model: AppModel
    let kind: MediaDeviceKind
    private var letters: [String] {
        (model.snapshot?.manifest.drives ?? [:]).keys.sorted().filter {
            model.snapshot?.manifest.drives[$0]?.device == kind
        }
    }
    var body: some View {
        Menu {
            ForEach(letters, id: \.self) { letter in
                Section(letter + ": — " + kind.title + " Drive") {
                    let device = model.snapshot?.manifest.drives[letter]
                    let unavailable = model.snapshot?.unavailableDrives?[letter]
                    let image = unavailable == nil ? device?.image : nil
                    Text(
                        unavailable.map { "Ejected — " + DisplayPath.homeAbbreviated($0) }
                            ?? image.map { URL(filePath: $0).lastPathComponent } ?? "Ejected")
                    if device?.isLocked == true { Label("Locked in Settings", systemImage: "lock") }
                    Button(
                        image == nil
                            ? "Attach Image to \(letter):…" : "Replace Image in \(letter):…"
                    ) { model.chooseMedia(letter) }.disabled(device?.isLocked == true)
                    Button("Eject \(letter):", systemImage: "eject") {
                        model.mediaCommand(["eject", letter])
                    }.disabled(image == nil || device?.isLocked == true)
                    if kind != .cdrom {
                        Toggle(
                            "Read Only",
                            isOn: Binding(
                                get: { device?.isReadOnly == true },
                                set: {
                                    model.mediaCommand([
                                        "media-readonly", letter, $0 ? "on" : "off",
                                    ])
                                })
                        ).disabled(device?.isLocked == true)
                        if kind == .floppy {
                            Button("Create Floppy Image for \(letter):…") {
                                model.creatingFloppy = letter
                            }.disabled(device?.isLocked == true)
                        } else {
                            Button("Create Disk Image for \(letter):…") {
                                model.creatingDisk = letter
                            }.disabled(device?.isLocked == true)
                        }
                    }
                    if !model.busy {
                        Button("Browse \(letter):") {
                            model.page = .drives
                            model.browseDrive = letter
                        }.disabled(image == nil)
                    }
                }
            }
            Button("Add \(kind.title) Drive…") {
                if let letter = model.availableMediaLetter(kind) {
                    model.mediaCommand(["device", letter, kind.rawValue])
                }
            }.disabled(model.availableMediaLetter(kind) == nil || model.busy)
            if let letter = model.availableMediaLetter(kind) {
                if kind == .floppy {
                    Button("Create Floppy Image…") { model.creatingFloppy = letter }.disabled(
                        model.busy || model.mediaBusy)
                }
                if kind == .disk {
                    Button("Create Disk Image…") { model.creatingDisk = letter }.disabled(
                        model.busy || model.mediaBusy)
                }
            }
        } label: {
            if kind == .cdrom {
                Label("CD Drives", systemImage: "opticaldisc")
            } else if kind == .floppy {
                Label {
                    Text("Floppy Drives")
                } icon: {
                    FloppyIcon()
                }
            } else {
                Label("Disk Drives", systemImage: "externaldrive")
            }
        }
        .disabled(!model.canChangeMedia)
        .help(kind.title + " drives: attach, replace, or eject images")
        .accessibilityLabel(kind.title + " drives")
        .accessibilityIdentifier(kind.rawValue + "MediaMenu")
    }
}

struct InstallCompletionSheet: View {
    @Bindable var model: AppModel
    let completion: InstallCompletion
    @Environment(\.dismiss) private var dismiss
    @State private var error: String?
    private var mapping: DriveMapping? { model.snapshot?.manifest.drives[completion.letter] }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Installer Finished").font(.title2.bold())
            Text(
                completion.signalled
                    ? "The installer ended with signal \(completion.code)."
                    : "The installer exited with code \(completion.code).")
            Text(
                "Check the installer’s result before removing its media. You can keep the image attached or eject it now."
            )
            LabeledContent(
                completion.letter + ":",
                value: mapping?.image.map { URL(filePath: $0).lastPathComponent } ?? "Ejected")
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            HStack {
                Button("Keep Attached") { dismiss() }.keyboardShortcut(.defaultAction)
                Spacer()
                Button("Eject \(completion.letter):") {
                    Task {
                        do {
                            try await model.changeMedia(["eject", completion.letter])
                            dismiss()
                        } catch { self.error = error.localizedDescription }
                    }
                }.disabled(
                    mapping?.image == nil || mapping?.isLocked == true || !model.canChangeMedia
                        || model.selected != completion.ball)
            }
        }.padding(24).frame(width: 490)
    }
}

struct CreateFloppySheet: View {
    @Bindable var model: AppModel
    let letter: String
    @Environment(\.dismiss) private var dismiss
    @State private var kib = 1440
    @State private var path = ""
    @State private var error: String?
    private let sizes = [
        (360, "360 KB"), (720, "720 KB"), (1200, "1.2 MB"), (1440, "1.44 MB"), (2880, "2.88 MB"),
    ]
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Create Floppy Image for \(letter):").font(.title2.bold())
            Picker("Capacity", selection: $kib) {
                ForEach(sizes, id: \.0) { Text($0.1).tag($0.0) }
            }
            HStack {
                TextField("Image file", text: $path).accessibilityIdentifier("newFloppyPath")
                Button("Choose…") {
                    let panel = NSSavePanel()
                    panel.title = "Create Floppy Image"
                    panel.nameFieldStringValue = "Floppy.img"
                    if panel.runModal() == .OK, let url = panel.url { path = url.path }
                }
            }
            Text(
                "Creates a formatted FAT12 disk and attaches it to \(letter):. Floppies are writable unless Read Only is selected for this device."
            ).foregroundStyle(.secondary)
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    .disabled(model.mediaBusy)
                Spacer()
                if model.mediaBusy { ProgressView("Creating image…").controlSize(.small) }
                Button("Create and Attach") {
                    Task {
                        do {
                            try await model.createFloppy(letter: letter, path: path, kib: kib)
                            dismiss()
                        } catch { self.error = error.localizedDescription }
                    }
                }.keyboardShortcut(.defaultAction).disabled(path.isEmpty || !model.canChangeMedia)
            }
        }.padding(24).frame(width: 540)
    }
}

struct DeviceSettingsView: View {
    @Bindable var model: AppModel
    let snapshot: BallSnapshot
    var body: some View {
        Section("Removable Devices") {
            ForEach(
                snapshot.manifest.drives.keys.sorted().filter {
                    snapshot.manifest.drives[$0]?.isRemovable == true
                }, id: \.self
            ) { letter in
                if let device = snapshot.manifest.drives[letter] {
                    let unavailable = snapshot.unavailableDrives?[letter]
                    VStack(alignment: .leading, spacing: 8) {
                        Text(letter + ": — " + (device.device?.title ?? "") + " Drive").font(
                            .headline)
                        Text(
                            unavailable.map { "Ejected — " + DisplayPath.homeAbbreviated($0) }
                                ?? device.image ?? "Ejected"
                        ).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                        Toggle(
                            "Lock Device to Image",
                            isOn: Binding(
                                get: { device.isLocked },
                                set: {
                                    model.mediaCommand(["device-lock", letter, $0 ? "on" : "off"])
                                })
                        )
                        .toggleStyle(.checkbox).disabled(
                            ((device.image == nil || unavailable != nil) && !device.isLocked)
                                || !model.canChangeMedia)
                        if device.device != .cdrom {
                            Toggle(
                                "Read Only",
                                isOn: Binding(
                                    get: { device.isReadOnly },
                                    set: {
                                        model.mediaCommand([
                                            "media-readonly", letter, $0 ? "on" : "off",
                                        ])
                                    })
                            )
                            .toggleStyle(.checkbox).disabled(
                                device.isLocked || !model.canChangeMedia)
                        }
                    }
                }
            }
            Text(
                "Locked devices keep their image attached. Unlock here before ejecting, replacing, or changing their access mode."
            ).font(.callout).foregroundStyle(.secondary)
        }
    }
}

struct CreateDiskSheet: View {
    @Bindable var model: AppModel
    let letter: String
    @Environment(\.dismiss) private var dismiss
    @State private var filesystem = "fat32"
    @State private var mib = 512
    @State private var path = ""
    @State private var error: String?
    /// Powers of two within the engine's bounds: 8 MiB up to HPFS's 64 GiB,
    /// and at least 64 MiB for FAT32.
    private static let diskSizesMiB = [
        8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536,
    ]
    private static let fat32MinimumMiB = 64
    private var capacities: [Int] {
        Self.diskSizesMiB.filter { filesystem != "fat32" || $0 >= Self.fat32MinimumMiB }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Create Disk Image for \(letter):").font(.title2.bold())
            Picker("File System", selection: $filesystem) {
                Text("FAT32").tag("fat32")
                Text("exFAT").tag("exfat")
                Text("HPFS").tag("hpfs")
            }
            Picker("Capacity", selection: $mib) {
                ForEach(capacities, id: \.self) { value in
                    Text(value >= 1024 ? "\(value / 1024) GB" : "\(value) MB").tag(value)
                }
            }
            HStack {
                TextField("Image file", text: $path).accessibilityIdentifier("newDiskPath")
                Button("Choose…") {
                    let panel = NSSavePanel()
                    panel.title = "Create Disk Image"
                    panel.nameFieldStringValue = "Disk.img"
                    if panel.runModal() == .OK, let url = panel.url { path = url.path }
                }
            }
            Text(
                "Creates a formatted image and attaches it to \(letter):. Use device settings to select Read Only or lock the image in place."
            ).foregroundStyle(.secondary)
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            HStack {
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    .disabled(model.mediaBusy)
                Spacer()
                if model.mediaBusy { ProgressView("Creating image…").controlSize(.small) }
                Button("Create and Attach") {
                    Task {
                        do {
                            try await model.createDisk(
                                letter: letter, path: path, filesystem: filesystem, mib: mib)
                            dismiss()
                        } catch { self.error = error.localizedDescription }
                    }
                }.keyboardShortcut(.defaultAction).disabled(path.isEmpty || !model.canChangeMedia)
            }
        }.padding(24).frame(width: 540)
            .onChange(of: filesystem) { _, _ in
                if !capacities.contains(mib) { mib = capacities.first ?? Self.fat32MinimumMiB }
            }
    }
}
