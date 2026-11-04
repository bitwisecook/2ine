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
}
