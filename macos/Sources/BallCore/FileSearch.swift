import Foundation

/// Case and accent insensitive subsequence matching, so "wpro" finds "Word Pro".
public enum FileSearch {
    public static func isProgram(_ name: String) -> Bool {
        ["exe", "com", "cmd", "bat"].contains(URL(filePath: name).pathExtension.lowercased())
    }
    public static func isOS2MediaFile(_ name: String, container: Bool) -> Bool {
        let ext = URL(filePath: name).pathExtension.lowercased()
        return container || ext.isEmpty || ext.hasSuffix("_") ||
            ["zip", "iso", "img", "ima", "dsk", "rar", "7z", "lha", "lzh", "arj", "arc", "zoo", "tar", "gz", "tgz", "bz2", "tbz", "xz", "txz", "z", "cab", "wpi", "pack", "pack2", "exe", "com", "cmd", "bat", "dll", "sys", "os2", "msg", "hlp", "inf", "fon", "fnt", "ttf", "otf", "ttc", "pfb", "pfm", "afm", "ofm", "drv", "pdr", "ppd", "bmp", "ico", "ptr", "met", "jpg", "jpeg", "png", "gif", "tif", "tiff", "wav", "mid", "au", "aiff", "aif", "avi", "mpg", "mpeg", "mov", "mp4", "fli", "flc", "mkv", "webm"].contains(ext)
    }
    public static func matches(_ name: String, query: String) -> Bool {
        let needle = query.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        if needle.isEmpty { return true }
        let haystack = name.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        var index = haystack.startIndex
        for character in needle {
            guard let found = haystack[index...].firstIndex(of: character) else { return false }
            index = haystack.index(after: found)
        }
        return true
    }
}
