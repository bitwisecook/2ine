import BallCore
import Foundation
import Testing

private func registryEntry(_ path: String = "hkcu\\software\\lotus", name: String = "v:", bytes: [UInt8]) throws -> ConfigurationEntry {
    try JSONDecoder().decode(ConfigurationEntry.self, from: JSONSerialization.data(withJSONObject: ["application": path, "key": name, "data": ConfigurationValue.hex(bytes)]))
}

@Test func registryTreeRetainsEmptyKeysAncestorsAndUnknownData() throws {
    let entries = [
        try registryEntry("hkcu\\software\\lotus\\wordpro", name: "k:", bytes: [0]),
        try registryEntry("hkcu\\software\\lotus", bytes: [1, 0, 0, 0, 65, 0]),
        try registryEntry("hklm\\empty", name: "c:", bytes: [0]),
        try registryEntry("other\\opaque", name: "v:x", bytes: [1, 2]),
    ]
    let data = try JSONSerialization.data(withJSONObject: ["area": "registry", "path": "/private/registry", "revision": "revision", "format": "2INEPRF1", "entries": entries.map { ["application": $0.application, "key": $0.key, "data": $0.data] }])
    let tree = RegistryTree(try JSONDecoder().decode(ConfigurationSnapshot.self, from: data))
    #expect(tree.roots.map(\.path) == ["hkcr", "hkcu", "hklm", "hku", "other"])
    let hkcu = try #require(tree.roots.first { $0.path == "hkcu" })
    #expect(hkcu.children?.first?.path == "hkcu\\software")
    #expect(hkcu.children?.first?.children?.first?.children?.first?.path == "hkcu\\software\\lotus\\wordpro")
    #expect(tree.values(at: "hkcu\\software\\lotus").first?.displayName == "(Default)")
    #expect(tree.values(at: "other\\opaque").first?.editable == false)
    #expect(tree.values(at: "other\\opaque").first?.entry.bytes == [1, 2])
    #expect(tree.roots.first { $0.path == "hklm" }?.children?.first?.path == "hklm\\empty")
}

@Test func registryIntegerWidthsAndByteOrdersAreExact() throws {
    #expect(try RegistryCodec.encodeInteger("0x12345678", type: .dword) == [0x78, 0x56, 0x34, 0x12])
    #expect(try RegistryCodec.encodeInteger("0x12345678", type: .bigEndianDword) == [0x12, 0x34, 0x56, 0x78])
    #expect(try RegistryCodec.integer([0x12, 0x34, 0x56, 0x78], type: .bigEndianDword) == 0x12345678)
    #expect(try RegistryCodec.encodeInteger("18446744073709551615", type: .qword) == Array(repeating: 255, count: 8))
    #expect(throws: Error.self) { try RegistryCodec.encodeInteger("4294967296", type: .dword) }
    #expect(throws: Error.self) { try RegistryCodec.encodeInteger("-1", type: .qword) }
    #expect(throws: Error.self) { try RegistryCodec.integer([0, 0, 0], type: .dword) }
}

@Test func registryStringsPreserveByteEncodingAndTerminators() throws {
    #expect(try RegistryCodec.string([99, 97, 102, 233, 0]) == "café")
    #expect(try ConfigurationValue.encodeText("café", terminated: true) == [99, 97, 102, 233, 0])
    #expect(throws: Error.self) { try RegistryCodec.string([65]) }
    #expect(throws: Error.self) { try RegistryCodec.string([65, 0, 66, 0]) }
    #expect(throws: Error.self) { try ConfigurationValue.encodeText("😀", terminated: true) }
    #expect(try RegistryCodec.encodeMultiString([]) == [0, 0])
    #expect(try RegistryCodec.multiString([0, 0]) == [])
    #expect(try RegistryCodec.encodeMultiString(["one", "two"]) == Array("one\0two\0\0".utf8))
    #expect(try RegistryCodec.multiString(Array("one\0two\0\0".utf8)) == ["one", "two"])
    #expect(throws: Error.self) { try RegistryCodec.multiString(Array("one\0\0two\0\0".utf8)) }
    #expect(throws: Error.self) { try RegistryCodec.multiString(Array("one\0".utf8)) }
}

@Test func registryUnknownAndMalformedRecordsNeverConvertSilently() throws {
    let bytes: [UInt8] = [0xef, 0xbe, 0xad, 0xde, 0, 255, 17, 0]
    let unknown = RegistryValueRecord(try registryEntry(bytes: bytes))
    var draft = RegistryValueDraft(record: unknown, path: "hkcu")
    #expect(draft.representation == .hex)
    #expect(try draft.bytes() == bytes)
    try draft.selectRepresentation(.rawRecord)
    #expect(draft.text == "efbeadde00ff1100")
    #expect(draft.type == 0xdeadbeef)
    try draft.selectRepresentation(.hex)
    #expect(try draft.bytes() == bytes)
    let brokenHeader = RegistryValueRecord(try registryEntry(bytes: [1, 2]))
    let broken = RegistryValueDraft(record: brokenHeader, path: "hkcu")
    #expect(broken.representation == .rawRecord)
    #expect(try broken.bytes(validate: false) == [1, 2])
    #expect(throws: Error.self) { try broken.bytes() }
    let badDword = RegistryValueDraft(record: RegistryValueRecord(try registryEntry(bytes: [4, 0, 0, 0, 1, 2, 3])), path: "hkcu")
    #expect(try badDword.bytes(validate: false) == [4, 0, 0, 0, 1, 2, 3])
    #expect(throws: Error.self) { try badDword.bytes() }
    let badString = RegistryValueDraft(record: RegistryValueRecord(try registryEntry(bytes: [1, 0, 0, 0, 65])), path: "hkcu")
    #expect(badString.representation == .hex)
    #expect(try badString.bytes() == [1, 0, 0, 0, 65])
}

@Test func registryRawHeaderEditsAndPayloadLimitAreExplicit() throws {
    var draft = RegistryValueDraft(record: RegistryValueRecord(try registryEntry(bytes: [1, 0, 0, 0, 65, 0])), path: "hkcu")
    try draft.selectRepresentation(.rawRecord)
    draft.text = "030000004100"
    #expect(draft.type == 3)
    #expect(try draft.bytes() == [3, 0, 0, 0, 65, 0])
    draft.newType(4)
    #expect(draft.type == 4 && draft.representation == .integer)
    let oversized = [UInt8](repeating: 0, count: RegistryCodec.maximumPayload + 5)
    #expect(throws: Error.self) { try RegistryCodec.validateWire(oversized) }
}

@Test func registryDraftRepresentationSwitchesAreLossless() throws {
    let original: [UInt8] = [1, 0, 0, 0, 65, 233, 0]
    var draft = RegistryValueDraft(record: RegistryValueRecord(try registryEntry(bytes: original)), path: "hkcu")
    #expect(draft.representation == .string)
    try draft.selectRepresentation(.hex)
    try draft.selectRepresentation(.rawRecord)
    try draft.selectRepresentation(.string)
    #expect(try draft.bytes() == original)
    let newline = RegistryValueRecord(try registryEntry(bytes: [7, 0, 0, 0] + Array("one\ntwo\0\0".utf8)))
    #expect(RegistryValueDraft(record: newline, path: "hkcu").representation == .hex)
    draft.newType(4)
    draft.text = "0x12345678"
    #expect(try draft.bytes() == [4, 0, 0, 0, 0x78, 0x56, 0x34, 0x12])
}

@Test func registryNameValidationMatchesOpen32Limits() throws {
    #expect(try RegistryCodec.path("HKEY_CURRENT_USER\\Software\\Lotus") == "hkcu\\software\\lotus")
    #expect(RegistryCodec.foldASCII("AÄZ") == "aÄz")
    #expect(try RegistryCodec.valueName("") == "")
    #expect(try RegistryCodec.valueName(String(repeating: "a", count: 32765)).count == 32765)
    #expect(throws: Error.self) { try RegistryCodec.valueName(String(repeating: "a", count: 32766)) }
    #expect(throws: Error.self) { try RegistryCodec.path("hkcc\\software") }
    #expect(throws: Error.self) { try RegistryCodec.path("hkcu\\software\\") }
    #expect(throws: Error.self) { try RegistryCodec.path("hkcu\\\\software") }
    #expect(throws: Error.self) { try RegistryCodec.valueName("bad\0name") }
}

@Test func nativeRegistryEditsPreserveOpaqueRecordsAndRejectStaleRevisions() throws {
    let directory = FileManager.default.temporaryDirectory.appending(path: "2ine-registry-\(UUID().uuidString)")
    defer { try? FileManager.default.removeItem(at: directory) }
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    let ball = directory.appending(path: "Test.2ineball").path
    _ = try NativeBallCore.request(DesktopRequest("command", ball: ball, arguments: ["init"]), as: BallSnapshot.self)
    var request = DesktopRequest("configuration-read", ball: ball); request.name = "registry"
    let initial = try NativeBallCore.request(request, as: ConfigurationSnapshot.self)
    var fixture = Data("2INEPRF1".utf8)
    func append(_ value: UInt32) { fixture.append(contentsOf: (0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) }) }
    append(3)
    let records: [(String, [UInt8])] = [("v:zoom", [4, 0, 0, 0, 150, 0, 0, 0]), ("v:truncated", [1, 2]), ("v:opaque", [0xef, 0xbe, 0xad, 0xde, 255, 0, 17])]
    for (name, bytes) in records {
        let path = Array("hkcu\\software\\fixture\0".utf8), key = Array((name + "\0").utf8)
        append(UInt32(path.count)); append(UInt32(key.count)); append(UInt32(bytes.count))
        fixture.append(contentsOf: path + key + bytes)
    }
    let file = URL(filePath: initial.path)
    try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fixture.write(to: file)
    let snapshot = try NativeBallCore.request(request, as: ConfigurationSnapshot.self)
    let entry = try #require(snapshot.entries.first { $0.key == "v:zoom" })
    var draft = RegistryValueDraft(record: RegistryValueRecord(entry), path: entry.application)
    draft.text = "151"
    var save = DesktopRequest("configuration-write", ball: ball, expected: snapshot.revision, text: ConfigurationValue.hex(try draft.bytes()))
    save.name = "registry"; save.path = entry.application; save.key = entry.key
    let result = try NativeBallCore.request(save, as: ConfigurationSnapshot.self)
    #expect(result.entries.first { $0.key == "v:zoom" }?.bytes == [4, 0, 0, 0, 151, 0, 0, 0])
    #expect(result.entries.first { $0.key == "v:truncated" }?.bytes == [1, 2])
    #expect(result.entries.first { $0.key == "v:opaque" }?.bytes == [0xef, 0xbe, 0xad, 0xde, 255, 0, 17])
    let persisted = try Data(contentsOf: file)
    draft.text = "152"
    save.text = ConfigurationValue.hex(try draft.bytes())
    #expect(throws: BallError.self) { try NativeBallCore.request(save, as: ConfigurationSnapshot.self) }
    #expect(try Data(contentsOf: file) == persisted)
    #expect(draft.text == "152")
}
