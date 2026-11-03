import BallCore
import Testing

@Test func fuzzyFileSearchHandlesIncrementalNamesAndAccents() {
    #expect(FileSearch.matches("Word Pro.exe", query: "wpro"))
    #expect(FileSearch.matches("Résumé.EXE", query: "rsme"))
    #expect(FileSearch.matches("OS2/DLL/REXX.DLL", query: "rexx.dll"))
    #expect(FileSearch.matches("Word Pro.exe", query: ""))
    #expect(!FileSearch.matches("Word Pro.exe", query: "porw"))
    #expect(!FileSearch.matches("OS2", query: "OS22"))
}

@Test func homeAbbreviationRespectsDirectoryBoundaries() {
    #expect(DisplayPath.homeAbbreviated("/Users/test", home: "/Users/test") == "~")
    #expect(DisplayPath.homeAbbreviated("/Users/test/Media/OS2.iso ▸ OS2IMAGE", home: "/Users/test") == "~/Media/OS2.iso ▸ OS2IMAGE")
    #expect(DisplayPath.homeAbbreviated("/Users/test-other/Media", home: "/Users/test") == "/Users/test-other/Media")
    #expect(DisplayPath.homeAbbreviated("C:\\OS2", home: "/Users/test") == "C:\\OS2")
    #expect(FileSearch.isProgram("WORDPRO.EXE"))
    #expect(!FileSearch.isProgram("WORDPRO.DLL"))
    #expect(FileSearch.isOS2MediaFile("OS2_1", container: false))
    #expect(FileSearch.isOS2MediaFile("Warp.zip", container: false))
    #expect(!FileSearch.isOS2MediaFile("README.TXT", container: false))
}
