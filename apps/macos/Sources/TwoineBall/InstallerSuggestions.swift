import Foundation

enum InstallerSuggestions {
    static func sorted(_ paths: [String]) -> [String] {
        func rank(_ path: String) -> [Int] {
            let parts = path.replacingOccurrences(of: "/", with: "\\").split(separator: "\\")
            let name = String(parts.last ?? "").lowercased()
            let file = URL(filePath: name)
            return [
                max(0, parts.count - 2),
                ["install", "setup", "inst", "os2inst", "autorun"].firstIndex(
                    of: file.deletingPathExtension().lastPathComponent) ?? 99,
                ["exe", "cmd", "bat", "com"].firstIndex(of: file.pathExtension) ?? 99,
                path.uppercased().contains("\\ENGLISH\\") ? 0 : 1,
            ]
        }
        var seen: Set<String> = []
        return paths.filter { seen.insert($0.uppercased()).inserted }.sorted {
            let a = rank($0)
            let b = rank($1)
            return a == b
                ? $0.localizedStandardCompare($1) == .orderedAscending
                : a.lexicographicallyPrecedes(b)
        }
    }
    static func selectedProgram(
        current: String, previousSuggestion: String?, suggestions: [String], reset: Bool
    ) -> String {
        reset || current.isEmpty || current == previousSuggestion
            ? suggestions.first ?? "" : current
    }
}
