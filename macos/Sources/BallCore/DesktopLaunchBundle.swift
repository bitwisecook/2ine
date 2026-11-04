import Foundation

struct DesktopIdentity: Decodable, Sendable {
    let name: String
    let nameSource: String
    let icon: ExecutableIcon?
}

/// A private immutable bundle supplies public LaunchServices identity without renaming a live app.
struct DesktopLaunchBundle: Sendable {
    let executable: URL
    let name: String
    let icon: URL?

    static func create(ball: String, sessionID: String, loader: URL,
                       identity: DesktopIdentity?) throws -> DesktopLaunchBundle {
        guard validLaunchSessionID(sessionID) else { throw BallError("Invalid desktop session ID.") }
        let files = FileManager.default, root = URL(filePath: canonicalLaunchBall(ball))
        let components = ["cache", "desktop-sessions", sessionID]
        var directory = root
        var createdSession = false
        do {
            for (index, component) in components.enumerated() {
                directory.append(path: component)
                let attributes = try? files.attributesOfItem(atPath: directory.path)
                if let type = attributes?[.type] as? FileAttributeType, type != .typeDirectory {
                    throw BallError("Desktop session cache must use private directories, without symbolic links.")
                }
                if index == components.count - 1 {
                    guard attributes == nil else { throw BallError("This desktop session ID has already been used.") }
                    try files.createDirectory(at: directory, withIntermediateDirectories: false)
                    createdSession = true
                } else if attributes == nil {
                    try files.createDirectory(at: directory, withIntermediateDirectories: false)
                }
            }
            let proposed = identity?.name.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let name = proposed.isEmpty ? "OS2 Application" : String(proposed.prefix(180))
            let stem = String(name.map { $0 == "/" || $0 == ":" || $0.isNewline ? "-" : $0 }.prefix(80))
            let bundle = directory.appending(path: (stem.isEmpty || stem == "." || stem == ".." ? "OS2 Application" : stem) + ".app")
            let macOS = bundle.appending(path: "Contents/MacOS")
            let resources = bundle.appending(path: "Contents/Resources")
            try files.createDirectory(at: macOS, withIntermediateDirectories: true)
            try files.createDirectory(at: resources, withIntermediateDirectories: true)
            let executable = macOS.appending(path: "lx_loader")
            try files.copyItem(at: loader, to: executable)
            let closure = URL(filePath: canonicalLaunchBall(loader.deletingLastPathComponent().path))
            let rootParts = root.pathComponents, closureParts = closure.pathComponents
            if closureParts.starts(with: rootParts) {
                let resourceParts = resources.pathComponents
                var shared = 0
                while shared < min(resourceParts.count, closureParts.count), resourceParts[shared] == closureParts[shared] { shared += 1 }
                let relative = Array(repeating: "..", count: resourceParts.count - shared) + closureParts.dropFirst(shared)
                try files.createSymbolicLink(atPath: resources.appending(path: "Runtime").path,
                                             withDestinationPath: relative.joined(separator: "/"))
            }
            var iconURL: URL?
            if let icon = identity?.icon {
                if let bytes = icon.rgba, let width = icon.width, let height = icon.height,
                   width > 0, height > 0, width <= 1024, height <= 1024, bytes.count == width * height * 4 {
                    var data = Data("2INERGBA".utf8)
                    for value in [UInt32(width), UInt32(height)] {
                        var little = value.littleEndian
                        withUnsafeBytes(of: &little) { data.append(contentsOf: $0) }
                    }
                    data.append(bytes)
                    let output = resources.appending(path: "Application.rgba")
                    try data.write(to: output, options: .atomic); iconURL = output
                } else if let bytes = icon.image, bytes.count <= 16 * 1024 * 1024 {
                    let output = resources.appending(path: "Application.image")
                    try bytes.write(to: output, options: .atomic); iconURL = output
                }
            }
            let token = sessionID.utf8.map { String(format: "%02x", $0) }.joined()
            let info: [String: Any] = ["CFBundleExecutable": "lx_loader", "CFBundleName": name,
                                      "CFBundleDisplayName": name, "CFBundleIdentifier": "io.github.bitwisecook.2ine.session." + token,
                                      "CFBundlePackageType": "APPL", "CFBundleVersion": "1", "LSUIElement": true,
                                      "NSHighResolutionCapable": true]
            try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
                .write(to: bundle.appending(path: "Contents/Info.plist"), options: .atomic)
            return DesktopLaunchBundle(executable: executable, name: name, icon: iconURL)
        } catch {
            if createdSession { try? files.removeItem(at: directory) }
            throw error
        }
    }
}
