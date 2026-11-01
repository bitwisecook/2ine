# macOS bottle manager implementation prompt

Build the native macOS application for https://github.com/bitwisecook/2ine.
Fetch the latest main first and preserve its macOS, Word Pro, Lotus and bottle
fixes. Read repository instructions and the existing implementation before
editing. Implement and verify the application; continue through routine build
and test failures instead of stopping at a design or scaffold.

Target macOS 27+, use SwiftUI and Swift 6.4, enable Swift 6 language mode and
complete strict concurrency checking. Use MainActor for UI state and explicit
actor/Sendable boundaries for filesystem operations, subprocesses and process
events. Keep directory scans, runtime hashing/copying and process waits off the
main actor. Do not suppress concurrency diagnostics with unchecked conformances
without a documented, tested isolation argument.

Use the applicable development skills from:

- https://github.com/CharlesWiltgen/Axiom
- https://github.com/giginet/apple-icon-composer-skill

Read their current installation and skill instructions, apply their SwiftUI,
macOS and concurrency guidance, and use Icon Composer tooling to author and
validate the icon. These are development tools, not application runtime libraries.

Use `tools/bottles.py`, `docs/bottles.md` and `tools/gnome_bottles.py` as the
existing behavior and interoperability contract. Preserve version-1 bottle
manifests, private C: drives, additional mapped drives, CONFIG.SYS, 2ine settings,
environment overrides, profiles, registry, shell objects, IPC directories,
immutable runtime snapshots, installation receipts and saved launchers.
Opening and configuring the same bottle from the CLI and native apps must
preserve all unrelated state. Reuse the backend or extract a shared interface
where useful; avoid incompatible duplicate persistence and launch logic.

Implement a complete native workflow:

- Create, discover and open bottles, with clear selection and empty states.
- Map and unmap host directories as guest drives, retaining the private C: drive.
- Select a 2ine runtime/build and execution backend; persist these choices.
- Configure supported 2ine settings, guest environment overrides and CONFIG.SYS.
  Preserve installer changes and the existing setting-precedence rules.
- Run original installers and executables, including arguments and working
  directories, through the selected bottle. Use structured argv, never a shell.
- Add, remove and launch saved applications without deleting installed files.
- Show asynchronous process activity, output logs, errors and actual exit codes.
  Preserve the existing installer receipt format and runtime identity.
- Retain profiles, registry and application state across launches. Describe any
  editor limitations accurately rather than presenting unsupported editing UI.

Use native macOS navigation, sheets, file/folder pickers, menus, keyboard
shortcuts, accessibility and window behavior. Handle process ownership and app
shutdown deliberately. Provide a reproducible Xcode/Swift build and a runnable
`.app`, including a dependable way to locate or package the bottle backend and
its dependencies. Do not assume macOS supplies Python. Keep installation media
and copyrighted OS/2/application binaries outside the repository and app bundle.

The icon must be an OS/2 riff on WINE's wine-glass motif, with recognizable OS/2
Warp cues and a silhouette readable at small sizes. Produce the editable `.icon`
package, required exported assets and an actual native Icon Composer render
check on macOS. Integrate the result into the built app.

Validate with real Swift 6.4 strict-concurrency builds, meaningful persistence
and argument-handling tests, UI smoke tests, and a real bottle launch on macOS.
Exercise the original Lotus SmartSuite installation workflow from
`docs/bottles.md` using separately supplied media when available. Its existing
Linux installer gate covers all seven components: Shared Components, Freelance,
Word Pro, 1-2-3, Approach, Organizer and DocOnline. The original installer exited
0 and all five main executable hashes matched original base-1.7 media. This is
not proof of complete application functionality or macOS compatibility; verify
and report macOS behavior separately. Word Pro's strict font/save/reopen workflow
was still unverified when the bottle foundations were completed.

Document exact build/run/test commands, toolchain versions, results and any
missing fixtures or platform validation. If Swift 6.4, macOS 27 or licensed media
are unavailable, complete independent work and state precisely which checks
remain unverified. Do not silently lower the deployment target or toolchain.
