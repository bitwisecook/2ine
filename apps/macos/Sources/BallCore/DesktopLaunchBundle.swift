import CoreGraphics
import Foundation
import ImageIO

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

    /// Renders one application icon pixel image as a PNG bundle resource.
    private static func pngImage(rgba: Data, width: Int, height: Int) -> Data? {
        guard let provider = CGDataProvider(data: rgba as CFData),
            let space = CGColorSpace(name: CGColorSpace.sRGB)
        else { return nil }
        guard
            let image = CGImage(
                width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                bytesPerRow: width * 4, space: space,
                bitmapInfo: CGImageAlphaInfo.last.rawValue, provider: provider,
                decode: nil, shouldInterpolate: true, intent: .defaultIntent)
        else {
            return nil
        }
        return pngData(image)
    }

    /// Re-encodes one imported icon image as a PNG bundle resource.
    private static func pngImage(image bytes: Data) -> Data? {
        guard let source = CGImageSourceCreateWithData(bytes as CFData, nil),
            let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
        else { return nil }
        return pngData(image)
    }

    private static func pngData(_ image: CGImage) -> Data? {
        let data = NSMutableData()
        guard
            let destination = CGImageDestinationCreateWithData(
                data, "public.png" as CFString, 1, nil)
        else {
            return nil
        }
        CGImageDestinationAddImage(destination, image, nil)
        return CGImageDestinationFinalize(destination) && data.length > 0 ? data as Data : nil
    }

    /// Writes the icon the Dock and App Switcher present: the application's own icon, or
    /// this app's own bundled icon as the fallback. An AppKit applicationIconImage override
    /// never changes what LaunchServices presents for a running process.
    private static func writeIcon(
        _ icon: ExecutableIcon?, fallback: URL?, resources: URL
    ) throws -> String? {
        var png: Data?
        if let bytes = icon?.rgba, let width = icon?.width, let height = icon?.height,
            width > 0, height > 0, width <= 1024, height <= 1024, bytes.count == width * height * 4
        {
            png = pngImage(rgba: bytes, width: width, height: height)
        } else if let bytes = icon?.image, bytes.count <= 16 * 1024 * 1024 {
            png = pngImage(image: bytes)
        }
        if let png {
            try png.write(to: resources.appending(path: "ApplicationIcon.png"), options: .atomic)
            return "ApplicationIcon.png"
        }
        guard let fallback, let bytes = try? Data(contentsOf: fallback), !bytes.isEmpty else {
            return nil
        }
        try bytes.write(to: resources.appending(path: fallback.lastPathComponent), options: .atomic)
        return fallback.lastPathComponent
    }

    static func create(
        ball: String, sessionID: String, loader: URL,
        identity: DesktopIdentity?, fallbackIcon: URL?
    ) async throws -> DesktopLaunchBundle {
        guard validLaunchSessionID(sessionID) else {
            throw BallError("Invalid desktop session ID.")
        }
        let files = FileManager.default
        let root = URL(filePath: canonicalLaunchBall(ball))
        let components = ["cache", "desktop-sessions", sessionID]
        var directory = root
        var createdSession = false
        do {
            for (index, component) in components.enumerated() {
                directory.append(path: component)
                let attributes = try? files.attributesOfItem(atPath: directory.path)
                if let type = attributes?[.type] as? FileAttributeType, type != .typeDirectory {
                    throw BallError(
                        "Desktop session cache must use private directories, without symbolic links."
                    )
                }
                if index == components.count - 1 {
                    guard attributes == nil else {
                        throw BallError("This desktop session ID has already been used.")
                    }
                    try files.createDirectory(at: directory, withIntermediateDirectories: false)
                    createdSession = true
                } else if attributes == nil {
                    try files.createDirectory(at: directory, withIntermediateDirectories: false)
                }
            }
            let proposed = identity?.name.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let name = proposed.isEmpty ? "OS2 Application" : String(proposed.prefix(180))
            let stem = String(
                name.map { $0 == "/" || $0 == ":" || $0.isNewline ? "-" : $0 }.prefix(80))
            let bundle = directory.appending(
                path: (stem.isEmpty || stem == "." || stem == ".." ? "OS2 Application" : stem)
                    + ".app")
            let macOS = bundle.appending(path: "Contents/MacOS")
            let resources = bundle.appending(path: "Contents/Resources")
            try files.createDirectory(at: macOS, withIntermediateDirectories: true)
            try files.createDirectory(at: resources, withIntermediateDirectories: true)
            let executable = macOS.appending(path: "lx_loader")
            try files.copyItem(at: loader, to: executable)
            // prepare-launch's cache-first host library paths pin the shared runtime.
            // Escaping symlinks inside an application bundle invalidate its resource seal.
            var iconURL: URL?
            if let icon = identity?.icon {
                if let bytes = icon.rgba, let width = icon.width, let height = icon.height,
                    width > 0, height > 0, width <= 1024, height <= 1024,
                    bytes.count == width * height * 4
                {
                    var data = Data("2INERGBA".utf8)
                    for value in [UInt32(width), UInt32(height)] {
                        var little = value.littleEndian
                        withUnsafeBytes(of: &little) { data.append(contentsOf: $0) }
                    }
                    data.append(bytes)
                    let output = resources.appending(path: "Application.rgba")
                    try data.write(to: output, options: .atomic)
                    iconURL = output
                } else if let bytes = icon.image, bytes.count <= 16 * 1024 * 1024 {
                    let output = resources.appending(path: "Application.image")
                    try bytes.write(to: output, options: .atomic)
                    iconURL = output
                }
            }
            let iconName = try writeIcon(
                identity?.icon, fallback: fallbackIcon, resources: resources)
            if let iconName { iconURL = iconURL ?? resources.appending(path: iconName) }
            let token = sessionID.utf8.map { String(format: "%02x", $0) }.joined()
            var info: [String: Any] = [
                "CFBundleExecutable": "lx_loader", "CFBundleName": name,
                "CFBundleDisplayName": name,
                "CFBundleIdentifier": "io.github.bitwisecook.2ine.session." + token,
                "CFBundlePackageType": "APPL", "CFBundleVersion": "1", "LSUIElement": true,
                "NSHighResolutionCapable": true,
            ]
            if let iconName { info["CFBundleIconFile"] = iconName }
            try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
                .write(to: bundle.appending(path: "Contents/Info.plist"), options: .atomic)
            // The copied loader's standalone signature does not seal this new bundle's plist/resources.
            // Bind its actual identity before AppKit/LaunchServices can register the application.
            let input = try FileHandle(forReadingFrom: executable)
            let prefix = try input.read(upToCount: 4) ?? Data()
            try input.close()
            let machO = [
                Data([0xcf, 0xfa, 0xed, 0xfe]), Data([0xce, 0xfa, 0xed, 0xfe]),
                Data([0xfe, 0xed, 0xfa, 0xcf]), Data([0xfe, 0xed, 0xfa, 0xce]),
                Data([0xca, 0xfe, 0xba, 0xbe]), Data([0xca, 0xfe, 0xba, 0xbf]),
                Data([0xbe, 0xba, 0xfe, 0xca]), Data([0xbf, 0xba, 0xfe, 0xca]),
            ].contains(prefix)
            if machO {
                for (arguments, suffix) in [
                    (
                        [
                            "--force", "--sign", "-", "--preserve-metadata=entitlements", "--",
                            bundle.path,
                        ], "sign"
                    ),
                    (["--verify", "--strict", "--", bundle.path], "verify"),
                ] {
                    let result = try await ProcessRunner().run(
                        executable: URL(filePath: "/usr/bin/codesign"), arguments: arguments,
                        log: directory.appending(path: "signature-\(suffix).log"))
                    guard result.code == 0, !result.signalled else {
                        throw BallError(
                            "Cannot prepare the macOS application identity: \(result.output)")
                    }
                }
            }
            return DesktopLaunchBundle(executable: executable, name: name, icon: iconURL)
        } catch {
            if createdSession { try? files.removeItem(at: directory) }
            throw error
        }
    }
}
