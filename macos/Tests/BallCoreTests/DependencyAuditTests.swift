import BallCore
import Foundation
import Testing

@Test func dependencyRecoveryOffersMediaOnlyWhenRequired() throws {
    for (missing, statuses, expected) in [
        ([], [], false), (["SOMD"], [], true),
        (["SOMD"], ["unavailable"], true), (["SOMD"], ["unverified"], true),
        (["SOMD"], ["verified-os2-media"], false),
        (["SOMD"], ["unavailable", "verified-os2-set"], false)
    ] as [([String], [String], Bool)] {
        let object: [String: Any] = [
            "summary": "Audit", "logPath": "/private/log", "jsonPath": "/private/log.json",
            "missing": missing, "imported": [],
            "componentGroups": missing.isEmpty ? [] : [["id": "som", "title": "SOM/DSOM", "modules": ["SOMD"]]],
            "media": statuses.map { ["path": "/media/OS2.iso", "status": $0] }
        ]
        let result = try JSONDecoder().decode(DependencyAudit.self, from: JSONSerialization.data(withJSONObject: object))
        #expect(result.needsMediaSelection == expected)
    }
}

@Test func skippedIsoWarningsRemainVisibleAlongsideUsableSource() throws {
    let bytes = Data(#"{"sources":["valid.iso"],"directory":{"path":"valid.iso","label":"Valid ISO","parent":null,"entries":[]},"detectedISO":true,"skippedSources":[{"path":"broken.iso","reason":"Unreadable ISO"}]}"#.utf8)
    let source = try JSONDecoder().decode(OS2MediaSource.self, from: bytes)
    #expect(source.sources == ["valid.iso"])
    #expect(source.warningSummary == "Skipped broken.iso: Unreadable ISO")
}

@Test func missingServiceCompanionOffersMediaWithoutMissingDlls() throws {
    let data = Data(#"""
    {"summary":"DSOM files missing","logPath":"/logs/audit.txt","jsonPath":"/logs/audit.json","missing":[],"imported":[],
     "componentFiles":[{"path":"C:\\OS2\\SOMDD.EXE","status":"missing"}],
     "componentGroups":[{"id":"som","title":"SOM/DSOM","modules":[],"requiredBy":["C:\\APPS\\EDITOR.EXE"]}],"media":[]}
    """#.utf8)
    let audit = try JSONDecoder().decode(DependencyAudit.self, from: data)
    #expect(audit.needsMediaSelection)
    #expect(audit.componentGroups?.first?.id == "som")
    #expect(audit.hasComponentAdvice)
}

@Test func componentAdviceKeepsThirdPartyLibrariesSeparate() throws {
    let data = Data(#"""
    {"summary":"Missing","logPath":"/logs/audit.txt","jsonPath":"/logs/audit.json",
     "missing":["NETAPI","NETAPI32","INOTES","DB2APP"],"imported":[],
     "componentGroups":[{"id":"network","title":"Networking Files","modules":["NETAPI","netapi32"]}]}
    """#.utf8)
    let audit = try JSONDecoder().decode(DependencyAudit.self, from: data)
    #expect(audit.hasComponentAdvice)
    #expect(audit.ungroupedMissingLibraries == ["INOTES", "DB2APP"])
    for (fields, expected) in [
        (#""missing":[],"stale":false"#, false),
        (#""missing":[],"stale":true"#, true),
        (#""missing":["INOTES"]"#, true)
    ] {
        let json = "{\"summary\":\"Audit\",\"logPath\":\"/log\",\"jsonPath\":\"/log.json\",\"imported\":[]," + fields + "}"
        let result = try JSONDecoder().decode(DependencyAudit.self, from: Data(json.utf8))
        #expect(result.hasComponentAdvice == expected)
    }
}

@Test func thirdPartyMissesDoNotRequireOS2Media() throws {
    let bytes = Data(#"""
    {"summary":"Optional connectors","logPath":"/log","jsonPath":"/log.json",
     "scope":"ball","missing":["INOTES","DB2APP","ORA_D73O","BTRCALLS"],"imported":[],
     "componentGroups":[],"media":[{"path":"/OS2.iso","status":"not-inspected"}]}
    """#.utf8)
    let audit = try JSONDecoder().decode(DependencyAudit.self, from: bytes)
    #expect(audit.hasComponentAdvice)
    #expect(!audit.needsMediaSelection)
    #expect(audit.scope == "ball")
    #expect(audit.ungroupedMissingLibraries.count == 4)
}
