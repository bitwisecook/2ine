import Foundation

public enum DisplayPath {
    /// Abbreviate only a complete home-directory component; never change stored paths.
    public static func homeAbbreviated(_ path: String, home: String = NSHomeDirectory()) -> String {
        if path == home { return "~" }
        guard path.hasPrefix(home + "/") else { return path }
        return "~" + path.dropFirst(home.count)
    }
}
