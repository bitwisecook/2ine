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
    @State private var step: Step = .options
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
    @State private var files: [OS2ExtractionPlan.File] = []
    @State private var availableCounts: [OS2ExtractionCategory: Int] = [:]
    @State private var installedCounts: [OS2ExtractionCategory: Int] = [:]

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(step == .source ? "Browse Installation Media" : step == .options ? "Install Components" : "Components Installed")
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
                    Button("Install") { extract() }
                        .keyboardShortcut(.defaultAction).disabled(blocked || files.isEmpty)
                        .accessibilityIdentifier("extractOS2Support")
                } else if step == .source {
                    Button(useCurrentFolder ? "Use This Folder" : "Done Browsing") { if useCurrentFolder { prepare() } else { step = .options } }
                        .keyboardShortcut(.defaultAction).disabled(blocked || listing == nil)
                        .accessibilityIdentifier("reviewOS2Extraction")
                }
            }
        }.padding(24).frame(width: 780, height: 680)
            .onChange(of: query) { _, _ in selected = nil }
            .onChange(of: relevantOnly) { _, _ in selected = nil }
            .interactiveDismissDisabled(blocked)
            .task { await loadInitial() }
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
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 4) {
                    Label("OS/2 Installation Media", systemImage: "opticaldisc").font(.headline)
                    Text(listing.map { DisplayPath.homeAbbreviated($0.label) } ?? "Choose your OS/2 ISO once to install components from it.")
                        .font(.callout).foregroundStyle(.secondary).lineLimit(3).textSelection(.enabled)
                }
                Spacer()
                Button(sources.isEmpty ? "Choose ISO…" : "Change…") { if let paths = model.pickOS2Media() { open(paths) } }
                    .accessibilityIdentifier("chooseSupportMedia").disabled(blocked)
                if listing != nil {
                    Button("Browse…") { step = .source }.disabled(blocked)
                }
            }
            Text("Select the components to add. Installed components are already ticked; existing files are kept.")
                .foregroundStyle(.secondary)
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    ForEach(OS2ExtractionCategory.allCases) { category in
                        Toggle(isOn: Binding(get: { categories.contains(category) || categories.contains(.full) }, set: { enabled in
                            if enabled { categories.insert(category) } else { categories.remove(category) }
                            refreshSelection()
                        })) {
                            VStack(alignment: .leading, spacing: 3) {
                                HStack {
                                    Text(category.title).font(.headline)
                                    Spacer()
                                    Text(componentStatus(category)).foregroundStyle(.secondary).monospacedDigit()
                                }
                                Text(category.detail).font(.callout).foregroundStyle(.secondary)
                            }
                        }.toggleStyle(.checkbox)
                            .disabled(blocked || (categories.contains(.full) && category != .full) || ((installedCounts[category] ?? 0) > 0 && count(category) == 0))
                            .accessibilityIdentifier("extractCategory_" + category.rawValue)
                    }
                    Divider()
                    if plan != nil && files.isEmpty {
                        Text("The selected components are already installed.").foregroundStyle(.secondary)
                    }
                    DisclosureGroup("Files to Install (\(files.count))") {
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
            Label("Installed " + fileCount(result?.imported?.count ?? 0), systemImage: "checkmark.circle.fill").font(.headline)
            if let result, categories.contains(.cmd) || categories.contains(.full) {
                Text(result.complete && result.rexxComplete == true ? "CMD and REXX support are ready." : "Some CMD or REXX files are still missing. Choose additional installation media to add them.")
            }
            if let skipped = result?.skippedFiles, !skipped.isEmpty {
                Label("\(skipped.count) damaged fonts were skipped", systemImage: "exclamationmark.triangle")
                    .foregroundStyle(.orange)
                ScrollView {
                    VStack(alignment: .leading, spacing: 8) {
                        ForEach(skipped) { file in
                            VStack(alignment: .leading) {
                                Text(file.path).font(.headline)
                                Text(file.reason)
                                Text(DisplayPath.homeAbbreviated(file.source)).font(.caption).foregroundStyle(.secondary)
                            }.textSelection(.enabled)
                        }
                    }.frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            Text("You can now return to your installer or application. Newly installed Help is available when it next starts.")
            Spacer()
        }.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
    private func count(_ category: OS2ExtractionCategory) -> Int {
        availableCounts[category] ?? 0
    }
    private func componentStatus(_ category: OS2ExtractionCategory) -> String {
        let present = installedCounts[category] ?? 0
        if count(category) > 0 {
            return present > 0 ? "\(present) installed · \(count(category)) to add" : "\(count(category)) files to add"
        }
        if present > 0 { return "Installed" }
        return plan == nil ? "" : "Not on this media"
    }
    private func refreshSelection() {
        files = plan?.selectedFiles(categories) ?? []
    }
    private func applyPlan(_ value: OS2ExtractionPlan) {
        plan = value
        for category in OS2ExtractionCategory.allCases {
            availableCounts[category] = value.selectedFiles([category]).count
            installedCounts[category] = Set(value.files.filter {
                !$0.provided && $0.present && (category == .full || $0.category == category)
            }.map { $0.path.uppercased() }).count
        }
        categories.formUnion(value.installedCategories)
        refreshSelection()
    }
    private func loadInitial() async {
        categories = request.categories
        loading = true
        do {
            let status = try await model.os2Support()
            if status.complete && status.rexxComplete == true {
                categories.insert(.cmd); installedCounts[.cmd] = 11
            }
            let saved = request.sources.isEmpty ? status.mediaSources ?? [] : request.sources
            loading = false
            if !saved.isEmpty { await loadSources(saved) }
        } catch { loading = false; self.error = error.localizedDescription }
    }
    private func open(_ paths: [String]) {
        Task { await loadSources(paths) }
    }
    private func loadSources(_ paths: [String]) async {
        loading = true; error = nil; notice = "Reading installation media…"; selected = nil
        defer { loading = false }
        do {
            let source = try await model.openOS2Source(paths, ball: request.ball)
            sources = source.sources; listing = source.directory
            useCurrentFolder = false
            try await model.rememberOS2Media(source.sources, ball: request.ball)
            extractionSources = source.sources
            notice = "Checking installed components…"
            applyPlan(try await model.planOS2Extraction(extractionSources, ball: request.ball))
            step = .options; notice = source.warningSummary
        } catch { self.error = error.localizedDescription; notice = "" }
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
                applyPlan(try await model.planOS2Extraction(extractionSources, ball: request.ball))
                step = .options; notice = ""
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
    private func extract() {
        loading = true; error = nil; notice = "Installing selected components…"
        Task {
            defer { loading = false }
            do {
                result = try await model.installOS2Support(extractionSources, categories: categories, ball: request.ball)
                step = .finished; notice = ""
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
}
