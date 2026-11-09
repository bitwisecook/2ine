import Foundation
import Testing

// Public domain. No rights reserved. Wholly AI-authored.
#if canImport(BallCore)
import BallCore
#else
@testable import TwoineBall
#endif

@Test func audioSchemaRoundTripAndSharedPresets() throws {
    let root = FileManager.default.temporaryDirectory.appending(
        path: "2ine-audio-" + UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: root) }
    let ball = root.appending(path: "Audio.2ineball").path
    _ = try NativeBallCore.request(
        DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    let snapshot = try NativeBallCore.request(
        DesktopRequest("audio-snapshot", ball: ball), as: AudioSnapshot.self)
    #expect(snapshot.settings.channels.count == 16)
    #expect(snapshot.banks.first?.id == "default")
    let mt32 = try #require(snapshot.profiles.first(where: { $0.id == "mt32" }))
    var settings = snapshot.settings
    settings.applyDefaults(mt32)
    #expect(settings.channels[0].muted)
    #expect(settings.channels[1].target == 1)
    #expect(settings.channels[9].target == 10)
    settings.channels[2].program = 64
    settings.channels[2].bankNumber = 8
    var request = DesktopRequest("save-audio", ball: ball)
    request.settings = settings
    let saved = try NativeBallCore.request(request, as: AudioSnapshot.self)
    #expect(saved.settings == settings)
}

@Test func aboutCatalogueCreditsOriginalAuthorAndProvidesAllLinks() throws {
    let catalogue = try NativeBallCore.request(
        DesktopRequest("project-sources"), as: ProjectCatalogue.self)
    #expect(catalogue.originCredit.contains("Ryan C. Gordon"))
    #expect(catalogue.projects.count > 50)
    for project in catalogue.projects {
        #expect(!project.version.isEmpty)
        #expect(!project.authors.isEmpty)
        #expect(
            URL(string: project.source)?.scheme == "https"
                || URL(string: project.source)?.scheme == "http")
        #expect(
            URL(string: project.download)?.scheme == "https"
                || URL(string: project.download)?.scheme == "http")
    }
}
