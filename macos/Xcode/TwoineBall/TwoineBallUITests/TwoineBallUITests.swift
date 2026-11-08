import XCTest

@MainActor
final class TwoineBallUITests: XCTestCase {
    func testBallNavigationConfigurationAndLaunchFailure() throws {
        continueAfterFailure = false
        var repo = URL(filePath: #filePath)
        for _ in 0..<5 { repo.deleteLastPathComponent() }
        let resources = repo.appending(path: "build-macos/2ine.app/Contents/Resources")
        let work = FileManager.default.temporaryDirectory.appending(
            path: "2ine-ui-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: work) }
        let ball = work.appending(path: "UI Smoke.2ineball")
        func command(_ arguments: [String]) throws {
            let process = Process()
            process.executableURL = resources.appending(path: "Runtime/2ine-ball")
            let input = Pipe()
            process.standardInput = input
            process.standardOutput = FileHandle.nullDevice
            try process.run()
            try input.fileHandleForWriting.write(
                contentsOf: JSONSerialization.data(withJSONObject: [
                    "action": "command", "ball": ball.path, "arguments": arguments,
                ]))
            try input.fileHandleForWriting.close()
            process.waitUntilExit()
            XCTAssertEqual(process.terminationStatus, 0)
        }
        try command(["init"])
        let media = work.appending(path: "Media")
        try FileManager.default.createDirectory(at: media, withIntermediateDirectories: true)
        try Data("dragged from drive D".utf8).write(to: media.appending(path: "drop.txt"))
        try command(["map", "D", media.path])
        let app = XCUIApplication()
        app.launchArguments = ["-lastPage", "Drives", "-ballFolder", work.path]
        app.launchEnvironment = [
            "TWOINE_MACOS_RESOURCES": resources.path,
            "TWOINE_MACOS_BALL": ball.path,
            "TWOINE_MACOS_PREFERENCES": work.appending(path: "preferences.json").path,
        ]
        app.launch()
        app.activate()
        defer { app.terminate() }
        XCTAssertTrue(app.outlines["drivesTable"].waitForExistence(timeout: 15))
        XCTAssertTrue(app.staticTexts["C:"].exists)
        app.outlines["drivesTable"].staticTexts["D:"].click()
        let droppedFile = app.staticTexts["drop.txt"]
        XCTAssertTrue(droppedFile.waitForExistence(timeout: 10))
        droppedFile.click(
            forDuration: 0.5, thenDragTo: app.outlines["drivesTable"].staticTexts["C:"])
        let copied = ball.appending(path: "drive_c/drop.txt")
        expectation(
            for: NSPredicate { _, _ in FileManager.default.fileExists(atPath: copied.path) },
            evaluatedWith: nil)
        waitForExpectations(timeout: 15)
        XCTAssertEqual(try Data(contentsOf: copied), Data("dragged from drive D".utf8))
        app.typeKey("2", modifierFlags: .command)
        XCTAssertTrue(app.buttons["installSoftware"].waitForExistence(timeout: 15))
        expectation(
            for: NSPredicate(format: "enabled == true"),
            evaluatedWith: app.buttons["installSoftware"])
        waitForExpectations(timeout: 10)
        XCTAssertEqual(app.buttons["installSoftware"].label, "Install Software…")
        XCTAssertEqual(app.outlines["applicationsTable"].label, "Saved applications")
        app.activate()
        app.typeKey("1", modifierFlags: .command)
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
            (try? String(contentsOf: saved, encoding: .isoLatin1).contains("UI_SMOKE=preserved"))
                == true
        }
        expectation(for: savedPredicate, evaluatedWith: nil)
        waitForExpectations(timeout: 10)
        app.typeKey("r", modifierFlags: [.command, .option])
        let program = app.textFields["programPath"]
        XCTAssertTrue(program.waitForExistence(timeout: 5))
        app.buttons["browseExecutable"].click()
        XCTAssertTrue(app.popUpButtons["guestDrivePicker"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.outlines["guestFilesTable"].exists)
        let pickerScreenshot = XCTAttachment(screenshot: app.screenshot())
        pickerScreenshot.name = "Drive letter executable browser"
        pickerScreenshot.lifetime = .keepAlways
        add(pickerScreenshot)
        app.buttons["cancelGuestPicker"].click()
        XCTAssertTrue(app.popUpButtons["guestDrivePicker"].waitForNonExistence(timeout: 5))
        app.activate()
        expectation(for: NSPredicate(format: "enabled == true"), evaluatedWith: program)
        waitForExpectations(timeout: 5)
        program.click()
        program.typeText("C:\\missing.exe")
        app.buttons["confirmProgram"].click()
        XCTAssertTrue(app.staticTexts["Could Not Start"].waitForExistence(timeout: 15))
        XCTAssertTrue(
            app.staticTexts.containing(
                NSPredicate(format: "value CONTAINS[c] 'program does not exist'")
            ).firstMatch.exists)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Ball launch failure and recovery"
        screenshot.lifetime = .keepAlways
        add(screenshot)
        app.typeKey("2", modifierFlags: .command)
        XCTAssertTrue(app.buttons["installSoftware"].exists)
        app.typeKey("a", modifierFlags: [.command, .shift])
        XCTAssertTrue(app.textFields["applicationName"].waitForExistence(timeout: 5))
        app.textFields["applicationName"].click()
        app.textFields["applicationName"].typeText("Imported Item")
        app.textFields["programPath"].click()
        app.textFields["programPath"].typeText("C:\\drop.txt")
        app.tabs["Working Directories"].click()
        app.checkBoxes["overrideDirectoryC"].click()
        let directoryOverride = app.textFields["directoryOverrideC"]
        directoryOverride.click()
        directoryOverride.typeKey("a", modifierFlags: .command)
        directoryOverride.typeText("C:\\TEMP")
        app.tabs["Environment"].click()
        app.checkBoxes["overrideEnvironmentPATH"].click()
        let environmentOverride = app.textFields["environmentOverridePATH"]
        environmentOverride.click()
        environmentOverride.typeKey("a", modifierFlags: .command)
        environmentOverride.typeText("%PATH%;C:\\DOOM")
        app.buttons["confirmProgram"].click()
        XCTAssertTrue(app.staticTexts["Imported Item"].waitForExistence(timeout: 10))
        let manifest =
            try JSONSerialization.jsonObject(
                with: Data(contentsOf: ball.appending(path: "ball.json"))) as! [String: Any]
        let applications = manifest["applications"] as! [String: [String: Any]]
        XCTAssertEqual(
            applications["Imported Item"]?["environment"] as? [String: String],
            ["PATH": "%PATH%;C:\\DOOM"])
        XCTAssertEqual(
            applications["Imported Item"]?["workingDirectories"] as? [String: String],
            ["C": "C:\\TEMP"])
        app.typeKey("n", modifierFlags: .command)
        XCTAssertTrue(app.textFields["saveAsNameTextField"].waitForExistence(timeout: 5))
        let filename = app.textFields["saveAsNameTextField"]
        filename.click()
        filename.typeKey("a", modifierFlags: .command)
        filename.typeText("Created in UI.2ineball")
        app.buttons["Create"].click()
        let created = work.appending(path: "Created in UI.2ineball/ball.json")
        let createdPredicate = NSPredicate { _, _ in
            FileManager.default.fileExists(atPath: created.path)
        }
        expectation(for: createdPredicate, evaluatedWith: nil)
        waitForExpectations(timeout: 10)
        XCTAssertTrue(app.buttons["installSoftware"].waitForExistence(timeout: 10))
        let mainScreenshot = XCTAttachment(screenshot: app.screenshot())
        mainScreenshot.name = "Native ball creation"
        mainScreenshot.lifetime = .keepAlways
        add(mainScreenshot)
    }
}
