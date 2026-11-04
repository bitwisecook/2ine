import BallCore
import Foundation
import Testing
@testable import TwoineBall

private func directoryFixture(_ path: String, entries: [[String: Any]]) throws -> GuestDirectory {
    let object: [String: Any] = ["path": path, "hostPath": "/fixture", "entries": entries]
    return try JSONDecoder().decode(GuestDirectory.self, from: JSONSerialization.data(withJSONObject: object))
}

@MainActor private final class DirectoryGate {
    var requested = false
    var requestWaiter: CheckedContinuation<Void, Never>?
    var result: CheckedContinuation<GuestDirectory, Never>?
    func read() async -> GuestDirectory {
        requested = true
        requestWaiter?.resume(); requestWaiter = nil
        return await withCheckedContinuation { result = $0 }
    }
    func waitForRequest() async {
        if !requested { await withCheckedContinuation { requestWaiter = $0 } }
    }
}

@MainActor @Test func guestDirectoryDisclosureLoadsOnlyRequestedBranchesAndDiscardsStaleResults() async throws {
    let tree = GuestBrowserTree()
    let gate = DirectoryGate()
    let result = try directoryFixture("C:\\Folder", entries: [
        ["name": "Setup.exe", "path": "C:\\Folder\\Setup.exe", "hostPath": "/fixture/Setup.exe", "directory": false]
    ])
    var requests: [String] = []
    let task = tree.expand("C:\\Folder") { path in
        requests.append(path)
        return await gate.read()
    }
    await gate.waitForRequest()
    #expect(requests == ["C:\\Folder"])
    #expect(tree.loading == ["C:\\Folder"])
    #expect(tree.expand("C:\\Folder") { _ in result } == nil)
    tree.reset() // Navigation or a different ball invalidates outstanding results.
    gate.result?.resume(returning: result)
    await task?.value
    #expect(tree.directories.isEmpty && tree.loading.isEmpty && tree.errors.isEmpty)
    let next = tree.expand("C:\\Folder") { path in requests.append(path); return result }
    await next?.value
    #expect(requests.count == 2)
    #expect(tree.entry("C:\\Folder\\Setup.exe", roots: [])?.hostPath == "/fixture/Setup.exe")
    #expect(tree.expand("C:\\Folder") { _ in result } == nil)
}

@MainActor @Test func guestDirectoryDisclosureCanRetryFailuresAndMarksKnownEmptyFolders() async throws {
    let tree = GuestBrowserTree()
    let empty = try directoryFixture("C:\\Folder", entries: [])
    let root = try directoryFixture("C:\\", entries: [
        ["name": "Folder", "path": "C:\\Folder", "hostPath": "/fixture/Folder", "directory": true]
    ])
    #expect(tree.nodes(root.entries).first?.canExpand == true)
    await tree.expand("C:\\Folder") { _ in throw NSError(domain: "Fixture", code: 1) }?.value
    #expect(tree.errors["C:\\Folder"] != nil && tree.loading.isEmpty)
    #expect(tree.nodes(root.entries).first?.canExpand == true)
    await tree.expand("C:\\Folder") { _ in empty }?.value
    #expect(tree.errors.isEmpty)
    #expect(tree.nodes(root.entries).first?.canExpand == false)
}
