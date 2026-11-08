import BallCore
import Foundation
import Testing

private func componentFixture() throws -> OS2ExtractionPlan {
    func file(
        _ source: String, _ category: String, _ title: String, present: Bool = false,
        provided: Bool = false
    ) -> [String: Any] {
        [
            "id": source, "source": source, "title": title, "kind": "file",
            "category": category, "path": "C:\\OS2\\" + title, "size": 128,
            "present": present, "provided": provided,
        ]
    }
    func group(
        _ id: String, _ title: String, _ children: [[String: Any]],
        category: String? = nil
    ) -> [String: Any] {
        [
            "id": id, "title": title, "kind": "group", "category": category ?? id,
            "children": children,
        ]
    }
    let files = [
        file("cmd", "cmd", "CMD.EXE", present: true),
        file("som1", "som", "SOM.DLL"), file("som2", "som", "SOMDD.EXE"),
        file("runtime", "runtime", "UCONV.DLL"),
        file("ttf", "fonts", "HELV.TTF"), file("bitmap", "fonts", "SYSTEM.FON"),
        file("engine", "help", "HELPMGR.DLL"), file("book", "help", "CMDREF.INF"),
        file("gl", "opengl", "OPENGL.DLL"),
        file("native", "runtime", "DOSCALLS.DLL", provided: true),
    ]
    let provider: [String: Any] = [
        "id": "opengl.provider", "kind": "setting", "title": "Active OpenGL Provider",
        "selection": "mesa", "setting": "graphics.opengl", "group": "opengl",
    ]
    let tree = group(
        "all", "Full OS/2 File Set",
        [
            group("cmd", "CMD and REXX", [files[0]]),
            group("som", "SOM/DSOM", [files[1], files[2]]),
            group("runtime", "Language Runtime", [files[3], files[9]]),
            group(
                "fonts", "Fonts",
                [
                    group("fonts.truetype", "TrueType", [files[4]], category: "fonts"),
                    group("fonts.bitmap", "Bitmap", [files[5]], category: "fonts"),
                ]),
            group(
                "help", "Help",
                [
                    group("help.engine", "Help Engine", [files[6]], category: "help"),
                    group("help.books", "Help Books", [files[7]], category: "help"),
                ]),
            group("opengl", "Original OpenGL", [provider, files[8]]),
        ], category: "full")
    let object: [String: Any] = [
        "files": files, "unmapped": [], "tree": tree,
        "groups": [
            [
                "id": "som", "title": "SOM", "detail": "Runtime",
                "defaultSelected": false, "requires": ["runtime"],
            ]
        ],
    ]
    return try JSONDecoder().decode(
        OS2ExtractionPlan.self, from: JSONSerialization.data(withJSONObject: object))
}

@Test func os2ComponentTreeDefaultsIncludeInstalledAndDependencies() throws {
    let plan = try componentFixture()
    #expect(plan.sourceSelection(OS2ExtractionCategory.defaults) == ["cmd"])
    #expect(plan.sourceSelection([.som]) == ["som1", "som2", "runtime"])
    #expect(!plan.sourceSelection([.full]).contains("native"))
    #expect(OS2ExtractionCategory.allCases.count == 15)
}

@Test func os2ComponentTreePartialSelectionsStayExplicit() throws {
    let plan = try componentFixture()
    let root = try #require(plan.tree)
    let som = try #require(root.find("som"))
    let service = try #require(root.find("som2"))
    var selected = plan.selecting(som, in: [], enabled: true)
    #expect(selected == ["som1", "som2", "runtime"])
    #expect(plan.configuredComponents(for: selected) == ["som"])
    selected = plan.selecting(service, in: selected, enabled: false)
    #expect(selected == ["som1", "runtime"])
    #expect(som.selectionState(selected) == .mixed)
    #expect(plan.configuredComponents(for: selected).isEmpty)
    let font = try #require(root.find("ttf"))
    selected = plan.selecting(font, in: selected, enabled: true)
    #expect(!selected.contains("som2"))
    #expect(font.selectionState(selected) == .on)
    #expect(root.selectionState([]) == .off)
    #expect(root.selectionState(root.eligibleSources) == .on)
}

@Test func os2ComponentTreeSeparatesHelpFontsAndProviderSettings() throws {
    let plan = try componentFixture()
    let root = try #require(plan.tree)
    let trueType = try #require(root.find("fonts.truetype"))
    #expect(plan.selecting(trueType, in: [], enabled: true) == ["ttf"])
    let books = try #require(root.find("help.books"))
    #expect(plan.selecting(books, in: [], enabled: true) == ["book"])
    let provider = try #require(root.find("opengl.provider"))
    #expect(provider.setting == "graphics.opengl")
    #expect(provider.selection == "mesa")
    #expect(provider.eligibleSources.isEmpty)
    #expect(plan.sourceSelection([.opengl]) == ["gl"])
    let filtered = try #require(root.filtered(query: "hlvtf"))
    #expect(filtered.files.map(\.id) == ["ttf"])
    #expect(filtered.find("fonts.truetype") != nil)
    #expect(root.filtered(query: "no matching component") == nil)
}

@Test func os2ComponentTreeOldPlansRemainDecodable() throws {
    let old = try JSONDecoder().decode(
        OS2ExtractionPlan.self, from: Data(#"{"files":[],"unmapped":[]}"#.utf8))
    #expect(old.tree == nil)
    #expect(old.groups == nil)
    #expect(old.sourceSelection([.full]).isEmpty)
}

@Test func filesystemPresenceRefreshPreservesMediaIdentity() throws {
    let plan = try componentFixture()
    let updated = plan.updatingPresence([#"C:\OS2\HELPMGR.DLL"#, #"c:\os2\helv.ttf"#])
    #expect(updated.files.first { $0.id == "engine" }?.present == true)
    #expect(updated.tree?.find("ttf")?.present == true)
    #expect(updated.files.first { $0.id == "cmd" }?.present == false)
    #expect(updated.files.map(\.source) == plan.files.map(\.source))
    #expect(updated.tree?.find("engine")?.path == plan.tree?.find("engine")?.path)
    #expect(updated.tree?.find("engine")?.id == "engine")
    #expect(updated.tree?.eligibleSources == plan.tree?.eligibleSources)
}

@Test func sourcePresenceDistinguishesVariantsAtTheSameDestination() throws {
    let plan = try componentFixture()
    let updated = plan.updatingSourcePresence(["engine", "ttf"])
    #expect(updated.files.first { $0.source == "engine" }?.present == true)
    #expect(updated.tree?.find("ttf")?.present == true)
    #expect(updated.tree?.files.first { $0.source == "cmd" }?.present == false)
    #expect(updated.files.map(\.source) == plan.files.map(\.source))
    #expect(updated.files.map(\.path) == plan.files.map(\.path))
    let state = try JSONDecoder().decode(
        OS2ComponentState.self,
        from: Data(
            #"{"files":[{"path":"C:\\OS2\\DLL\\2INE\\OTHER\\HELPMGR.DLL","selectedSource":"engine","source":"engine","present":true,"managed":true,"status":"managed"},{"path":"C:\\OS2\\HELPMGR.DLL","selectedSource":"another-engine","present":false,"managed":false,"status":"missing"}],"installedPaths":[],"managedPaths":[],"protectedPaths":[],"modifiedPaths":[],"unownedPaths":[]}"#
                .utf8))
    #expect(Set(state.files.filter(\.present).compactMap(\.selectedSource)) == ["engine"])
}
