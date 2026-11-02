import XCTest

@MainActor
final class TwoineBallsUITests: XCTestCase {
    func testBallNavigationConfigurationAndLaunchFailure() throws {
        continueAfterFailure = false
        var repo = URL(filePath: #filePath)
        for _ in 0..<5 { repo.deleteLastPathComponent() }
        let resources = repo.appending(path: "build-macos/2ine Balls.app/Contents/Resources")
        let work = FileManager.default.temporaryDirectory.appending(path: "2ine-ui-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: work) }
        let ball = work.appending(path: "UI Smoke.2ineball")
        let process = Process()
        process.executableURL = resources.appending(path: "Runtime/2ine-ball")
        process.arguments = []
        let input = Pipe()
        process.standardInput = input
        process.standardOutput = FileHandle.nullDevice
        try process.run()
        try input.fileHandleForWriting.write(contentsOf: JSONSerialization.data(withJSONObject: ["action": "command", "ball": ball.path, "arguments": ["init"]]))
        try input.fileHandleForWriting.close()
        process.waitUntilExit()
        XCTAssertEqual(process.terminationStatus, 0)
        let app = XCUIApplication()
        app.launchArguments = ["-lastPage", "Applications", "-ballFolder", work.path]
        app.launchEnvironment = ["TWOINE_MACOS_RESOURCES": resources.path,
                                 "TWOINE_MACOS_BALL": ball.path,
                                 "TWOINE_MACOS_PREFERENCES": work.appending(path: "preferences.json").path]
        app.launch()
        app.activate()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["installSoftware"].waitForExistence(timeout: 15))
        expectation(for: NSPredicate(format: "enabled == true"), evaluatedWith: app.buttons["installSoftware"])
        waitForExpectations(timeout: 10)
        XCTAssertEqual(app.buttons["installSoftware"].label, "Install Software…")
        XCTAssertEqual(app.outlines["applicationsTable"].label, "Saved applications")
        app.activate()
        app.typeKey("2", modifierFlags: .command)
        XCTAssertTrue(app.staticTexts["C:"].waitForExistence(timeout: 5))
        app.typeKey("3", modifierFlags: .command)
        XCTAssertTrue(app.staticTexts["Execution Runtime"].waitForExistence(timeout: 5))
        app.typeKey("4", modifierFlags: .command)
        let editor = app.textViews.firstMatch
        XCTAssertTrue(editor.waitForExistence(timeout: 5))
        editor.click()
        editor.typeKey("a", modifierFlags: .command)
        editor.typeText("SET LANG=en_US\nSET UI_SMOKE=preserved\n")
        app.buttons["Save CONFIG.SYS"].click()
        let saved = ball.appending(path: "drive_c/CONFIG.SYS")
        let savedPredicate = NSPredicate { _, _ in
            (try? String(contentsOf: saved, encoding: .isoLatin1).contains("UI_SMOKE=preserved")) == true
        }
        expectation(for: savedPredicate, evaluatedWith: nil)
        waitForExpectations(timeout: 10)
        app.typeKey("r", modifierFlags: [.command, .option])
        let program = app.textFields["programPath"]
        XCTAssertTrue(program.waitForExistence(timeout: 5))
        program.click(); program.typeText("C:\\missing.exe")
        app.buttons["confirmProgram"].click()
        XCTAssertTrue(app.staticTexts["Could Not Start"].waitForExistence(timeout: 15))
        XCTAssertTrue(app.staticTexts.containing(NSPredicate(format: "value CONTAINS[c] 'program does not exist'")).firstMatch.exists)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Ball launch failure and recovery"
        screenshot.lifetime = .keepAlways
        add(screenshot)
        app.typeKey("1", modifierFlags: .command)
        XCTAssertTrue(app.buttons["installSoftware"].exists)
        app.typeKey("n", modifierFlags: .command)
        XCTAssertTrue(app.textFields["saveAsNameTextField"].waitForExistence(timeout: 5))
        let filename = app.textFields["saveAsNameTextField"]
        filename.click(); filename.typeKey("a", modifierFlags: .command)
        filename.typeText("Created in UI.2ineball")
        app.buttons["Create"].click()
        let created = work.appending(path: "Created in UI.2ineball/ball.json")
        let createdPredicate = NSPredicate { _, _ in FileManager.default.fileExists(atPath: created.path) }
        expectation(for: createdPredicate, evaluatedWith: nil)
        waitForExpectations(timeout: 10)
        XCTAssertTrue(app.buttons["installSoftware"].waitForExistence(timeout: 10))
        let mainScreenshot = XCTAttachment(screenshot: app.screenshot())
        mainScreenshot.name = "Native ball creation"
        mainScreenshot.lifetime = .keepAlways
        add(mainScreenshot)
    }
}
