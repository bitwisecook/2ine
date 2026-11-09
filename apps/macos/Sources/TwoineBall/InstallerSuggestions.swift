import Foundation

enum InstallerSuggestions {
    static let stems = ["install", "setup", "inst", "os2inst", "autorun"]
    static let extensions = ["exe", "cmd", "bat", "com"]
    /// Ranks after every named stem or extension.
    static let unnamedRank = 99
    /// Discovery browses at most this many folders, this deep, and offers this
    /// many candidates: enough for real install media, quick on a large disk.
    static let searchFolderLimit = 96
    static let searchDepthLimit = 2
    static let candidateLimit = 32
    static func sorted(_ paths: [String]) -> [String] {
        func rank(_ path: String) -> [Int] {
            let parts = path.replacingOccurrences(of: "/", with: "\\").split(separator: "\\")
            let name = String(parts.last ?? "").lowercased()
            let file = URL(filePath: name)
            return [
                max(0, parts.count - 2),
                stems.firstIndex(of: file.deletingPathExtension().lastPathComponent)
                    ?? unnamedRank,
                extensions.firstIndex(of: file.pathExtension) ?? unnamedRank,
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
