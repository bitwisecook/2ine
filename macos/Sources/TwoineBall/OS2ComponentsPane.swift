#if canImport(BallCore)
import BallCore
#endif
import SwiftUI
import AppKit

struct OS2ComponentsPane: View {
    @Bindable var model: AppModel
    let request: OS2ImportRequest
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
    private var blocked: Bool { loading || model.busy || model.mediaBusy || model.hasRunningSession(in: request.ball) }
    private var adviceNavigation: Bool {
        model.componentNavigation?.ball == request.ball && model.componentNavigation?.selectsComponent == false
    }
    @State private var files: [OS2ExtractionPlan.File] = []
    @State private var availableCounts: [OS2ExtractionCategory: Int] = [:]
    @State private var installedCounts: [OS2ExtractionCategory: Int] = [:]
    @State private var initialSelection: Set<String> = []
    @State private var reconciler: ComponentReconciler?
    private var fileSelection: Set<String> { reconciler?.desired ?? initialSelection }
    @State private var componentQuery = ""
    @State private var componentSearchFocused = false
    @State private var requiredBy: [OS2ExtractionCategory: [String]] = [:]

    @State private var browsing = false
    @State private var componentState: OS2ComponentState?
    @State private var planToken = UUID()
    private var job: ComponentJob? { model.componentJobs[request.ball] }
    private var fileStates: [String: OS2ComponentState.File] {
        Dictionary((reconciler?.state?.files ?? componentState?.files ?? []).compactMap { file in file.selectedSource.map { ($0, file) } },
                   uniquingKeysWith: { previous, newest in
                       let severity = ["missing": 0, "managed": 1, "unowned": 2, "modified": 3, "protected": 4]
                       return (severity[newest.status] ?? 5) > (severity[previous.status] ?? 5) ? newest : previous
                   })
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("OS/2 Components").font(.title2.bold())
            if browsing {
                HSplitView {
                    sourceBrowser.frame(minWidth: 260, idealWidth: 320)
                    extractionOptions.frame(minWidth: 300)
                }
            } else { extractionOptions }
            if !notice.isEmpty { Text(notice).font(.callout).foregroundStyle(.secondary).textSelection(.enabled) }
            if let error { Text(error).foregroundStyle(.red).textSelection(.enabled).accessibilityIdentifier("supportMediaError") }
            if let job {
                HStack {
                    if job.running { ProgressView().controlSize(.small) }
                    Text(job.summary).textSelection(.enabled)
                    if job.running {
                        Button(job.cancelling ? "Cancelling…" : "Cancel Job") { model.cancelComponentOperation(job) }.disabled(job.cancelling)
                    }
                    if let path = job.progress?.logPath {
                        Button("Show Log") { model.reveal(path) }
                    }
                }
                if let error = job.error { Text(error).foregroundStyle(.red).textSelection(.enabled) }
                if let error = job.progressError { Text(error).foregroundStyle(.secondary).textSelection(.enabled) }
            }
            if let message = reconciler?.error { Text(message).foregroundStyle(.red).textSelection(.enabled) }
            if let message = reconciler?.notice { Text(message).foregroundStyle(.secondary).textSelection(.enabled) }
            if loading { HStack { ProgressView().controlSize(.small); Text("Reading media…").foregroundStyle(.secondary) } }
            Text("Tick to install; untick to remove. Changes run in the background. Existing, modified and protected files are kept.")
                .font(.caption).foregroundStyle(.secondary)
        }.padding(20)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .onChange(of: query) { _, _ in selected = nil }
            .onChange(of: relevantOnly) { _, _ in selected = nil }
            .task { await loadInitial() }
            .task(id: planToken) { await observeFiles() }
            .onDisappear {
                if !extractionSources.isEmpty, FileManager.default.fileExists(atPath: request.ball) {
                    model.componentDrafts[request.ball] = ComponentDraft(sources: extractionSources, selection: fileSelection)
                }
            }
            .onChange(of: reconciler?.desired) { _, _ in refreshTreeSelection() }
            .onChange(of: model.componentNavigation) { _, _ in applyNavigation() }
    }
    private var sourceBrowser: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Browse the installation media, or continue to search the selected media. Folders and archives can be opened inside other archives.")
            HStack {
                Button("Choose ISO or Archive…") { chooseMedia() }
                    .accessibilityIdentifier("chooseSupportMedia")
                Button("Choose Folder…") { chooseMedia(directory: true) }
                if sources.count > 1 {
                    Menu("\(sources.count) Sources") {
                        ForEach(Array(sources.enumerated()), id: \.offset) { index, source in
                            Button("Source \(index + 1)") { navigate(source) }
                        }
                    }
                }
            }.disabled(blocked || reconciler?.reconciling == true || job?.running == true)
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
                Toggle("Use only the current folder or selected item", isOn: $useCurrentFolder)
                    .disabled(blocked || reconciler?.reconciling == true || job?.running == true)
                Button("Use This Location") { prepare() }.disabled(blocked || reconciler?.reconciling == true || job?.running == true)
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
                Button(sources.isEmpty ? "Choose ISO…" : "Change…") { chooseMedia() }
                    .accessibilityIdentifier("chooseSupportMedia").disabled(blocked || reconciler?.reconciling == true || job?.running == true)
                if listing != nil {
                    Button(browsing ? "Hide Media Browser" : "Browse Media…") { browsing.toggle() }.disabled(blocked)
                }
            }
            Text("Tick components to install them, or untick to remove their managed files. Expand folders to choose individual files. Changes apply immediately.")
                .foregroundStyle(.secondary)
            if let tree = plan?.tree {
                BrowserSearchField(text: $componentQuery, focused: $componentSearchFocused,
                                   placeholder: "Search Components and Files", identifier: "componentSearch")
                    .frame(height: 32)
                OS2ComponentTree(root: tree, selection: .constant(fileSelection), query: $componentQuery,
                                 searchFocused: $componentSearchFocused, disabled: blocked,
                                 toggle: { node, enabled in
                                     guard let plan else { return }
                                     reconciler?.setDesired(plan.selecting(node, in: fileSelection, enabled: enabled))
                                     refreshTreeSelection()
                                 }, openSetting: { token in
                                     model.openComponentSetting(token)
                                 }, focusRequest: model.componentNavigation.flatMap { $0.ball == request.ball ? OutlineFocusRequest(id: $0.id, row: $0.category.rawValue) : nil },
                                 activePaths: Set(job?.activePaths.map { $0.uppercased() } ?? []), fileStates: fileStates,
                                 activeSources: job?.activeSources ?? [], queuedSources: reconciler?.pendingSources ?? [])
                HStack {
                    let pendingCount = reconciler?.pendingSources.count ?? files.count
                    Text("\(fileSelection.count) ticked · \(pendingCount) \(pendingCount == 1 ? "change" : "changes") pending")
                    Spacer()
                    Text("Only managed files can be removed.")
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
                        if let plan {
                            let selected = Set(plan.files.filter { !$0.provided && (categories.contains(.full) || categories.contains($0.category)) }.map(\.source))
                            reconciler?.setDesired(selected)
                        }
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
                    Text("The ticked files are installed.").foregroundStyle(.secondary)
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
        model.componentPlans[request.ball] = value
        model.componentPlanSources[request.ball] = extractionSources
        planToken = UUID()
        for category in OS2ExtractionCategory.allCases {
            availableCounts[category] = value.selectedFiles([category]).count
            installedCounts[category] = Set(value.files.filter {
                !$0.provided && $0.present && (category == .full || $0.category == category)
            }.map { $0.path.uppercased() }).count
        }
        categories.formUnion(value.installedCategories)
        refreshSelection()
        let installed = Set(value.files.filter { $0.present && !$0.provided }.map(\.source))
        let draft = model.componentDrafts[request.ball]
        let defaults = value.tree != nil ? value.sourceSelection(request.categories) : Set(value.files.filter { !$0.provided && (request.categories.contains(.full) || request.categories.contains($0.category)) }.map(\.source))
        initialSelection = draft?.sources == extractionSources ? draft?.selection ?? [] : adviceNavigation ? installed : defaults.union(installed)
        reconciler = model.componentReconciler(ball: request.ball, plan: value, sources: extractionSources, selection: initialSelection, explicitIntent: draft?.sources == extractionSources)
        reconciler?.start()
        refreshTreeSelection()
        applyNavigation()
    }
    private func refreshTreeSelection() {
        guard let plan else { return }
        files = plan.files.filter { fileSelection.contains($0.source) && !$0.provided && !$0.present }
        categories = Set(plan.files.filter { fileSelection.contains($0.source) }.map(\.category))
    }
    private func applyNavigation() {
        guard let navigation = model.componentNavigation, navigation.ball == request.ball,
              let plan, let node = plan.tree?.find(navigation.category.rawValue) else { return }
        componentQuery = ""
        let selection = model.componentSelection(in: plan, preserving: fileSelection, ball: request.ball)
        if navigation.selectsComponent && !node.eligibleSources.isEmpty { reconciler?.setDesired(selection) }
        refreshTreeSelection()
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
            if let cached = model.componentPlans[request.ball], let media = model.componentMedia[request.ball],
               media.sources == saved {
                sources = media.sources; listing = media.directory
                extractionSources = model.componentPlanSources[request.ball] ?? media.sources
                applyPlan(cached)
            } else if !saved.isEmpty { await loadSources(saved) }
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
            model.componentMedia[request.ball] = source
            sources = source.sources; listing = source.directory
            useCurrentFolder = false
            try await model.rememberOS2Media(source.sources, ball: request.ball)
            extractionSources = source.sources
            notice = "Checking installed components…"
            applyPlan(try await model.planOS2Extraction(extractionSources, ball: request.ball))
            notice = source.warningSummary
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
                notice = ""
            } catch { self.error = error.localizedDescription; notice = "" }
        }
    }
    private func chooseMedia(directory: Bool = false) {
        Task { if let paths = await model.pickOS2Media(directory: directory) { await loadSources(paths) } }
    }
    private func observeFiles() async {
        guard let plan else { return }
        let selection = plan.files.filter { !$0.provided }.map { OS2ComponentSelection(source: $0.source, path: $0.path) }
        while !Task.isCancelled {
            do {
                let revision = reconciler?.observationRevision
                let state = try await model.componentState(ball: request.ball, selection: selection)
                guard !Task.isCancelled else { return }
                guard revision == reconciler?.observationRevision else { continue }
                if componentState != state {
                    componentState = state
                    reconciler?.observe(state, expectedRevision: revision)
                    if let current = self.plan {
                        self.plan = current.updatingSourcePresence(Set(state.files.filter(\.present).compactMap(\.selectedSource)))
                        model.componentPlans[request.ball] = self.plan
                        refreshTreeSelection()
                    }
                }
            } catch { if !Task.isCancelled { self.error = "Could not refresh component files: " + error.localizedDescription } }
            try? await Task.sleep(for: .seconds(1))
        }
    }
}
