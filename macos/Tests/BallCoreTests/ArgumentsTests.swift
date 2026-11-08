import Testing

#if canImport(BallCore)
import BallCore
#else
@testable import TwoineBall
#endif

@Test func argumentsKeepOS2PathsAndEmptyValues() throws {
    let parsed = try Arguments.parse(#"/a:I '/l1:C:\suite.log' "space value" "" C:\DATA\FILE.EXE"#)
    #expect(parsed == ["/a:I", #"/l1:C:\suite.log"#, "space value", "", #"C:\DATA\FILE.EXE"#])
    #expect(try Arguments.parse("   ").isEmpty)
    #expect(throws: BallError.self) { try Arguments.parse("'unfinished") }
}
