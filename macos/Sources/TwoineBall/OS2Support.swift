#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit

struct OS2SupportSheet: View {
    @Bindable var model: AppModel
    let request: OS2ImportRequest
    var embedded = false
    @Environment(\.dismiss) private var dismiss
    private enum Step { case source, options, finished }
    @State private var step: Step = .options
    @State private var sources: [String] = []
    @State private var listing: MediaDirectory?
    @State private var relevantOnly = true
    @State private var query = ""
    @State private var searchFocused = false
    @State private var mediaSort: OutlineBrowserSort? = .init(column: "name")
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
    @State private var fileSelection: Set<String> = []
    @State private var componentQuery = ""
    @State private var componentSearchFocused = false
    @State private var requiredBy: [OS2ExtractionCategory: [String]] = [:]

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(embedded ? "OS/2 System Components" : step == .source ? "Browse OS/2 Media" : step == .options ? "Install Components" : "Components Installed")
                .font(.title2.bold())
            switch step {
            case .source: sourceBrowser
            case .options: extractionOptions
            case .finished: completion
            }
            if !notice.isEmpty { Text(notice).font(.callout).foregroundStyle(.secondary).textSelection(.enabled) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("supportMediaError") }
            HStack {
                if !embedded {
                    Button(step == .finished ? "Done" : "Cancel", role: .cancel) { dismiss() }
                        .keyboardShortcut(.cancelAction).disabled(blocked)
                } else if step == .finished {
                    Button("Review Components") { step = .options }.disabled(blocked)
                }
                if loading { ProgressView().controlSize(.small) }
                Spacer()
                if step == .options {
                    Button("Install") { extract() }
                        .keyboardShortcut(.defaultAction).disabled(blocked || !hasSelectedComponents)
                        .accessibilityIdentifier("extractOS2Support")
                } else if step == .source {
                    Button(useCurrentFolder ? "Use This Folder" : "Done Browsing") { if useCurrentFolder { prepare() } else { step = .options } }
                        .keyboardShortcut(.defaultAction).disabled(blocked || listing == nil)
                        .accessibilityIdentifier("reviewOS2Extraction")
                }
            }
        }.padding(24)
            .frame(minWidth: embedded ? nil : 780, maxWidth: embedded ? .infinity : 780,
                   minHeight: embedded ? nil : 680, maxHeight: embedded ? .infinity : 680)
            .onChange(of: query) { _, _ in selected = nil }
            .onChange(of: relevantOnly) { _, _ in selected = nil }
            .interactiveDismissDisabled(blocked)
            .task { await loadInitial() }
            .onDisappear {
                if !extractionSources.isEmpty, FileManager.default.fileExists(atPath: request.ball) {
                    model.componentDrafts[request.ball] = ComponentDraft(sources: extractionSources, selection: fileSelection)
                }
            }
            .onChange(of: model.componentNavigation) { _, _ in applyNavigation() }
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
                NativeOutlineBrowser(nodes: BrowserRows.media(mediaEntries), columns: BrowserRows.fileColumns,
                                     selection: $selected, query: $query, searchFocused: $searchFocused,
                                     sort: $mediaSort, disabled: blocked,
                                     identifier: "supportMediaEntries", accessibilityLabel: "Installation media files",
                                     onAction: { id, _ in if let id { navigate(id) } },
                                     onActivate: { id in
                                         if let entry = mediaEntries.first(where: { $0.path == id }), entry.directory || entry.container { navigate(entry.path) }
                                     }, contextActions: { id in
                                         guard let entry = mediaEntries.first(where: { $0.path == id }), entry.directory || entry.container else { return [] }
                                         return [OutlineMenuAction(id: "open", title: "Open")]
                                     })
                Toggle("Search only the current folder or selected item", isOn: $useCurrentFolder)
                    .disabled(blocked)
            } else {
                ContentUnavailableView("Opening OS/2 Media", systemImage: "opticaldisc")
            }
        }.frame(maxHeight: .infinity)
    }
    private var mediaEntries: [MediaDirectory.Entry] {
        BrowserRows.filteredMediaEntries(listing?.entries ?? [], relevantOnly: relevantOnly, query: query)
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
            Text("Select a component, expand it to choose individual files, or search. Installed components are already ticked; existing files are kept.")
                .foregroundStyle(.secondary)
            if let tree = plan?.tree {
                BrowserSearchField(text: $componentQuery, focused: $componentSearchFocused,
                                   placeholder: "Search Components and Files", identifier: "componentSearch")
                    .frame(height: 32)
                OS2ComponentTree(root: tree, selection: $fileSelection, query: $componentQuery,
                                 searchFocused: $componentSearchFocused, disabled: blocked,
                                 toggle: { node, enabled in
                                     guard let plan else { return }
                                     fileSelection = plan.selecting(node, in: fileSelection, enabled: enabled)
                                     refreshTreeSelection()
                                 }, openSetting: { token in
                                     model.openComponentSetting(token)
                                 }, focusRequest: model.componentNavigation.flatMap { $0.ball == request.ball ? OutlineFocusRequest(id: $0.id, row: $0.category.rawValue) : nil })
                HStack {
                    Text("\(fileSelection.count) selected · \(files.count) files to add")
                    Spacer()
                    Text("Installed files are kept.")
                }.font(.caption).foregroundStyle(.secondary)
                if !requiredBy.isEmpty {
                    DisclosureGroup("Recommended for Installed Applications") {
                        ForEach(OS2ExtractionCategory.allCases.filter { requiredBy[$0]?.isEmpty == false }) { category in
                            Text(category.title + ": " + (requiredBy[category] ?? []).map { $0.components(separatedBy: "\\").last ?? $0 }.joined(separator: ", "))
                                .font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                        }
                    }
                }
            } else {
                legacyExtractionOptions
            }
        }.frame(maxHeight: .infinity)
    }

    private var legacyExtractionOptions: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                ForEach(OS2ExtractionCategory.allCases) { category in
                    let group = plan?.groups?.first { $0.id == category }
                    Toggle(isOn: Binding(get: { categories.contains(category) || categories.contains(.full) }, set: { enabled in
                        if enabled { categories.insert(category) } else { categories.remove(category) }
                        refreshSelection()
                    })) {
                        VStack(alignment: .leading, spacing: 3) {
                            HStack {
                                Text(group?.title ?? category.title).font(.headline)
                                Spacer()
                                Text(componentStatus(category)).foregroundStyle(.secondary).monospacedDigit()
                            }
                            Text(group?.detail ?? category.detail).font(.callout).foregroundStyle(.secondary)
                            if let paths = requiredBy[category], !paths.isEmpty {
                                Text("Required by " + paths.map { $0.components(separatedBy: "\\").last ?? $0 }.joined(separator: ", "))
                                    .font(.caption).foregroundStyle(.secondary).lineLimit(3)
                            }
                            if requiredSelection.contains(category) {
                                Text("Required by another selected component.").font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }.toggleStyle(.checkbox)
                        .disabled(blocked || requiredSelection.contains(category) || (categories.contains(.full) && category != .full) || ((installedCounts[category] ?? 0) > 0 && count(category) == 0))
                        .accessibilityIdentifier("extractCategory_" + category.rawValue)
                }
                Divider()
                if plan != nil && files.isEmpty {
                    Text("The selected files are already installed. Install will repair their search paths and component configuration.").foregroundStyle(.secondary)
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
    }
    private var hasCMDSupportSelection: Bool {
        if let plan, plan.tree != nil {
            return plan.files.contains { $0.category == .cmd && fileSelection.contains($0.source) }
        }
        return categories.contains(.cmd) || categories.contains(.full)
    }
    private var completion: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Installed " + fileCount(result?.imported?.count ?? 0), systemImage: "checkmark.circle.fill").font(.headline)
            if let result, hasCMDSupportSelection {
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
        categories = plan?.expanded(categories) ?? OS2ExtractionCategory.expanded(categories)
        files = plan?.selectedFiles(categories) ?? []
    }
    private var requiredSelection: Set<OS2ExtractionCategory> {
        Set(categories.flatMap { category in
            plan?.groups?.first { $0.id == category }?.requires ?? Array(category.requires)
        })
    }
    private var hasSelectedComponents: Bool {
        if plan?.tree != nil { return !fileSelection.isEmpty }
        return plan?.files.contains { !$0.provided && (categories.contains(.full) || categories.contains($0.category)) } == true
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
        if value.tree != nil {
            let installed = Set(value.files.filter { $0.present && !$0.provided }.map(\.source))
            let draft = model.componentDrafts[request.ball]
            let initial = draft?.sources == extractionSources ? draft?.selection ?? [] : value.sourceSelection(categories).union(installed)
            fileSelection = initial
                .intersection(value.tree?.eligibleSources ?? [])
            refreshTreeSelection()
            applyNavigation()
        }
    }
    private func refreshTreeSelection() {
        guard let plan else { return }
        files = plan.files.filter { fileSelection.contains($0.source) && !$0.provided && !$0.present }
        categories = Set(plan.files.filter { fileSelection.contains($0.source) }.map(\.category))
    }
    private func applyNavigation() {
        guard let navigation = model.componentNavigation, navigation.ball == request.ball,
              let plan, plan.tree?.find(navigation.category.rawValue) != nil else { return }
        componentQuery = ""
        fileSelection = model.componentSelection(in: plan, preserving: fileSelection, ball: request.ball)
        refreshTreeSelection()
        step = .options
    }
    private func loadInitial() async {
        categories = request.categories
        loading = true
        do {
            while model.busy || model.mediaBusy {
                try Task.checkCancellation()
                try await Task.sleep(for: .milliseconds(20))
            }
            let status = try await model.os2Support()
            if status.complete && status.rexxComplete == true {
                categories.insert(.cmd); installedCounts[.cmd] = 11
            }
            let audit = try await model.dependencyRequirements()
            for group in audit.componentGroups ?? [] {
                if let category = OS2ExtractionCategory(rawValue: group.id) {
                    categories.insert(category)
                    requiredBy[category] = group.requiredBy ?? []
                }
            }
            let saved = request.sources.isEmpty ? model.suggestedOS2MediaSources(status.mediaSources) : request.sources
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
                result = try await model.installOS2Support(extractionSources, categories: categories, ball: request.ball,
                                                          fileSelection: plan?.tree == nil ? nil : fileSelection.sorted(),
                                                          configureComponents: plan?.tree == nil ? nil : plan?.configuredComponents(for: fileSelection))
                step = .finished; notice = ""
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
}
