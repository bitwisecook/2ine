#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit

struct OS2SupportSheet: View {
    @Bindable var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var status: OS2SupportStatus?
    @State private var sources: [String] = []
    @State private var listing: MediaDirectory?
    @State private var selected: String?
    @State private var loading = false
    @State private var notice = ""
    @State private var error: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Install OS/2 Support Files").font(.title2.bold())
            Text("Choose your original OS/2 installation CD, floppy images or archives. Select several images for a floppy set. Open folders and archives to look inside them, or search all selected media.")
            HStack {
                Button("Choose Images or Archives…") { chooseMedia() }.accessibilityIdentifier("chooseSupportMedia")
                Button("Choose Folder…") { chooseFolder() }
                if sources.count > 1 {
                    Menu("\(sources.count) Sources") {
                        ForEach(sources, id: \.self) { source in
                            Button(URL(filePath: source).lastPathComponent) { navigate(source) }
                        }
                    }
                }
                Spacer()
                if loading || model.busy { ProgressView().controlSize(.small) }
            }.disabled(loading || model.busy || model.mediaBusy)
            if let listing {
                HStack {
                    Button("Up", systemImage: "arrow.up") { if let parent = listing.parent { navigate(parent) } }
                        .disabled(listing.parent == nil || loading || model.busy)
                    Text(listing.label).font(.caption).lineLimit(2).textSelection(.enabled)
                }
                List(selection: $selected) {
                    ForEach(listing.entries.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }) { entry in
                        HStack {
                            Label(entry.name, systemImage: entry.directory ? "folder" : entry.container ? "archivebox" : "doc")
                            Spacer()
                            if entry.directory || entry.container {
                                Button("Open") { navigate(entry.path) }.buttonStyle(.borderless)
                            }
                        }.tag(entry.path).contentShape(Rectangle())
                            .onTapGesture(count: 2) { navigate(entry.path) }
                    }
                }.accessibilityIdentifier("supportMediaEntries").disabled(loading || model.busy)
            } else {
                ContentUnavailableView("Choose OS/2 Media", systemImage: "opticaldisc", description: Text("ZIP, IBM PACK/PACK2, ISO and floppy images can be opened inside other archives."))
            }
            if let status {
                ForEach(status.files) { file in
                    Label(file.path + (file.present ? " — Installed" : " — Missing"), systemImage: file.present ? "checkmark.circle" : "circle")
                        .font(.callout)
                }
            }
            if !notice.isEmpty { Text(notice).font(.callout).textSelection(.enabled) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("supportMediaError") }
            HStack {
                Button("Done") { dismiss() }.keyboardShortcut(.cancelAction).disabled(loading || model.busy)
                Spacer()
                if let listing {
                    Button("Install from Here") { install([selected ?? listing.path]) }
                        .disabled(loading || model.busy || model.mediaBusy)
                }
                Button("Search Selected Media and Install") { install(sources) }
                    .keyboardShortcut(.defaultAction).disabled(sources.isEmpty || loading || model.busy || model.mediaBusy)
                    .accessibilityIdentifier("extractOS2Support")
            }
        }.padding(24).frame(width: 760, height: 640)
            .interactiveDismissDisabled(loading || model.busy)
            .task { status = try? await model.os2Support() }
    }
    private func chooseMedia() {
        let panel = NSOpenPanel()
        panel.title = "Choose OS/2 CD, Floppy Images or Archives"
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            sources = panel.urls.map(\.path)
            if let first = sources.first { navigate(first) }
        }
    }
    private func chooseFolder() {
        if let url = AppModel.pick(directory: true, title: "Choose OS/2 Installation Media Folder") {
            sources = [url.path]
            notice = "Search this folder to install the support files."
            listing = nil
        }
    }
    private func navigate(_ path: String) {
        loading = true; error = nil; selected = nil
        Task {
            defer { loading = false }
            do { listing = try await model.browseMedia(path) }
            catch { self.error = error.localizedDescription }
        }
    }
    private func install(_ paths: [String]) {
        error = nil; notice = "Searching installation media…"
        Task {
            do {
                let result = try await model.installOS2Support(paths)
                status = result
                let count = result.imported?.count ?? 0
                notice = result.complete ? "OS/2 support files are ready. Installed \(count) files." : "Installed \(count) files. Still missing: \(result.missing). Choose more OS/2 media to continue."
            } catch { notice = ""; self.error = error.localizedDescription }
        }
    }
}
