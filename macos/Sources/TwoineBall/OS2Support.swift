#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit

struct OS2SupportSheet: View {
    @Bindable var model: AppModel
    let request: OS2ImportRequest
    @Environment(\.dismiss) private var dismiss
    private enum Step { case source, options, finished }
    @State private var step: Step = .source
    @State private var sources: [String] = []
    @State private var listing: MediaDirectory?
    @State private var relevantOnly = true
    @State private var query = ""
    @State private var searchFocused = false
    @State private var sortOrder = [KeyPathComparator(\MediaDirectory.Entry.name, comparator: .localizedStandard)]
    @State private var selected: String?
    @State private var plan: OS2ExtractionPlan?
    @State private var categories = OS2ExtractionCategory.defaults
    @State private var loading = false
    @State private var notice = ""
    @State private var error: String?
    @State private var result: OS2SupportStatus?
    private var blocked: Bool { loading || model.busy || model.mediaBusy }
    private var files: [OS2ExtractionPlan.File] { plan?.selectedFiles(categories) ?? [] }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(step == .source ? "Browse OS/2 Media" : step == .options ? "Extract OS/2 Files" : "OS/2 Extraction Complete")
                .font(.title2.bold())
            switch step {
            case .source: sourceBrowser
            case .options: extractionOptions
            case .finished: completion
            }
            if !notice.isEmpty { Text(notice).font(.callout).foregroundStyle(.secondary).textSelection(.enabled) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("supportMediaError") }
            HStack {
                Button(step == .finished ? "Done" : "Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction).disabled(blocked)
                if loading { ProgressView().controlSize(.small) }
                Spacer()
                if step == .options {
                    Button("Back") { step = .source; error = nil; notice = "" }.disabled(blocked)
                    Button("Extract " + fileCount(files.count)) { extract() }
                        .keyboardShortcut(.defaultAction).disabled(blocked || files.isEmpty)
                        .accessibilityIdentifier("extractOS2Support")
                } else if step == .source {
                    Button("Continue") { prepare() }
                        .keyboardShortcut(.defaultAction).disabled(blocked || listing == nil)
                        .accessibilityIdentifier("reviewOS2Extraction")
                }
            }
        }.padding(24).frame(width: 780, height: 680)
            .onChange(of: query) { _, _ in selected = nil }
            .onChange(of: relevantOnly) { _, _ in selected = nil }
            .interactiveDismissDisabled(blocked)
            .task { open(request.sources) }
    }
    private var sourceBrowser: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Browse the installation media, or continue to search the selected media. Folders and archives can be opened inside other archives.")
            HStack {
                Button("Choose ISO or Archive…") { if let paths = model.pickOS2Media() { open(paths) } }
                    .accessibilityIdentifier("chooseSupportMedia")
                Button("Choose Folder…") { if let paths = model.pickOS2Media(directory: true) { open(paths) } }
                if sources.count > 1 {
                    Menu("\(sources.count) Sources") {
                        ForEach(Array(sources.enumerated()), id: \.offset) { index, source in
                            Button("Source \(index + 1)") { navigate(source) }
                        }
                    }
                }
            }.disabled(blocked)
            if let listing {
                HStack {
                    Button("Up", systemImage: "arrow.up") { if let parent = listing.parent { navigate(parent) } }
                        .disabled(listing.parent == nil || blocked)
                    Text(DisplayPath.homeAbbreviated(listing.label)).font(.caption).lineLimit(3).textSelection(.enabled)
                }
                BrowserSearchField(text: $query, focused: $searchFocused, identifier: "supportMediaSearch")
                    .frame(height: 32).accessibilityIdentifier("supportMediaSearch")
                Toggle("Relevant Files Only", isOn: $relevantOnly)
                    .toggleStyle(.checkbox).accessibilityIdentifier("relevantSupportMediaFiles")
                    .help("Show OS/2 media, archives and installable files. Turn off to show all files.")
                Table(mediaEntries, selection: $selected, sortOrder: $sortOrder) {
                    TableColumn("Name", value: \.name) { entry in
                        Label(entry.name, systemImage: entry.directory ? "folder" : entry.container ? "archivebox" : "doc")
                    }.width(min: 180, ideal: 310)
                    TableColumn("Size", value: \.size) { Text(browserSize($0.size, directory: $0.directory)).monospacedDigit() }.width(min: 75, ideal: 90)
                    TableColumn("Date Modified", value: \.sortModified) { Text(browserDate($0.modified)) }.width(min: 135, ideal: 165)
                }.contextMenu(forSelectionType: String.self) { paths in
                    if let entry = mediaEntries.first(where: { paths.contains($0.path) }), entry.directory || entry.container {
                        Button("Open") { navigate(entry.path) }
                    }
                } primaryAction: { paths in
                    if let entry = mediaEntries.first(where: { paths.contains($0.path) }), entry.directory || entry.container { navigate(entry.path) }
                }
                .alternatingRowBackgrounds()
                .modifier(BrowserTypeToSearch(query: $query, focused: $searchFocused))
                .accessibilityIdentifier("supportMediaEntries").disabled(blocked)
                Toggle("Search only the current folder or selected item", isOn: $useCurrentFolder)
                    .disabled(blocked)
            } else {
                ContentUnavailableView("Opening OS/2 Media", systemImage: "opticaldisc")
            }
        }.frame(maxHeight: .infinity)
    }
    private var mediaEntries: [MediaDirectory.Entry] {
        (listing?.entries ?? []).filter {
            (!relevantOnly || $0.directory || FileSearch.isOS2MediaFile($0.name, container: $0.container)) && FileSearch.matches($0.name, query: query)
        }
            .sorted(using: sortOrder).sorted { $0.directory && !$1.directory }
    }
    private func fileCount(_ count: Int) -> String { "\(count) " + (count == 1 ? "File" : "Files") }
    @State private var useCurrentFolder = false
    private var extractionOptions: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Choose what to copy into this ball’s C: drive. Existing files are kept.")
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    ForEach(OS2ExtractionCategory.allCases) { category in
                        Toggle(isOn: Binding(get: { categories.contains(category) || categories.contains(.full) }, set: { enabled in
                            if enabled { categories.insert(category) } else { categories.remove(category) }
                        })) {
                            VStack(alignment: .leading, spacing: 3) {
                                HStack {
                                    Text(category.title).font(.headline)
                                    Spacer()
                                    Text(fileCount(count(category))).foregroundStyle(.secondary).monospacedDigit()
                                }
                                Text(category.detail).font(.callout).foregroundStyle(.secondary)
                            }
                        }.toggleStyle(.checkbox)
                            .disabled(blocked || (categories.contains(.full) && category != .full))
                            .accessibilityIdentifier("extractCategory_" + category.rawValue)
                    }
                    Divider()
                    DisclosureGroup("Files to Extract (\(files.count))") {
                        LazyVStack(alignment: .leading, spacing: 4) {
                            ForEach(files) { file in
                                Text(file.path).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading)
                    }
                    if let plan {
                        Text("\(plan.files.filter(\.present).count) existing files and \(plan.files.filter(\.provided).count) components supplied by 2ine will be skipped.")
                            .font(.caption).foregroundStyle(.secondary)
                        if !plan.unmapped.isEmpty {
                            DisclosureGroup("\(plan.unmapped.count) files without an installation destination") {
                                ForEach(Array(plan.unmapped.enumerated()), id: \.offset) { _, name in
                                    Text(name).font(.caption).textSelection(.enabled)
                                }
                            }
                        }
                    }
                }.padding(.trailing, 8)
            }
        }.frame(maxHeight: .infinity)
    }
    private var completion: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Extracted " + fileCount(result?.imported?.count ?? 0), systemImage: "checkmark.circle.fill").font(.headline)
            if let result, categories.contains(.cmd) || categories.contains(.full) {
                Text(result.complete && result.rexxComplete == true ? "CMD and REXX support are ready." : "Some CMD or REXX files are still missing. Choose additional installation media to add them.")
            }
            Text("The extracted files are in their C: folders. You can return to your installer or application.")
            Spacer()
        }.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
    private func count(_ category: OS2ExtractionCategory) -> Int {
        plan?.selectedFiles([category]).count ?? 0
    }
    private func open(_ paths: [String]) {
        loading = true; error = nil; notice = "Looking for ISO images…"; selected = nil
        Task {
            defer { loading = false }
            do {
                let source = try await model.openOS2Source(paths, ball: request.ball)
                sources = source.sources; listing = source.directory
                useCurrentFolder = false
                notice = source.installationDirectory == true ? "Found the OS/2 installation file set. Browse it or continue to choose components." : source.detectedISO ? "Detected \(sources.count) ISO image\(sources.count == 1 ? "" : "s") in the selected media." : ""
                notice = [notice, source.warningSummary].filter { !$0.isEmpty }.joined(separator: "\n")
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
    private func navigate(_ path: String) {
        loading = true; error = nil; selected = nil; query = ""
        Task {
            defer { loading = false }
            do { listing = try await model.browseMedia(path) }
            catch { self.error = error.localizedDescription }
        }
    }
    @State private var extractionSources: [String] = []
    private func prepare() {
        guard let listing else { return }
        extractionSources = useCurrentFolder ? [selected ?? listing.path] : sources
        loading = true; error = nil; notice = "Examining OS/2 files…"
        Task {
            defer { loading = false }
            do {
                plan = try await model.planOS2Extraction(extractionSources, ball: request.ball)
                step = .options; notice = ""
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
    private func extract() {
        loading = true; error = nil; notice = "Extracting selected files…"
        Task {
            defer { loading = false }
            do {
                result = try await model.installOS2Support(extractionSources, categories: categories, ball: request.ball)
                step = .finished; notice = ""
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
}
