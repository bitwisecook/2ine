# UI and desktop session release, 2026-11-04

The release candidate starts from remote main `6712b57f11a9003c4599202ba314f783ff69def6`, with version 0.5. It includes visible ordered navigation, content-based starting pages, a combined editable installer selector, native folder disclosure, Change/Eject image controls, immediate queued component installation/removal, read-only CMD prerequisites with deferred continuation, and native desktop session identity/window cycling. Private source preparation excludes unrelated unfinished graphics/Open32/network diagnostics in the shared checkout.

Component ownership and offline removal use exact source/path pairs. Modified, unowned and protected destinations remain untouched. Cancelling component work pauses automatic continuation of a pending installer. First-setup defaults do not reinstate deliberately removed components. See `immediate-components-macos.md`, `gnome-installation-workflow.md`, and `cmd-bootstrap-prerequisites.md` for contracts and fixtures.

One launch family uses a shared desktop session, immutable original-name/icon wrapper, auxiliary helper policy and lifetime-locked Dock leadership. Command-grave and Shift-Command-grave cycle forward/backward across participating process windows; modal state, popup/dialog exclusion, stale peers, rapid requests and reparenting have explicit coverage. See `macos-desktop-sessions.md`.

The committed native networking implementation also receives a platform-boundary correction: common enumeration/ownership remains POSIX, and macOS/Linux MAC/route/DNS implementations are selected in the HAL platform table. Only the two public neutral network/TLS contracts are added to the architecture checker allowance; private headers and host-specific branches remain prohibited. The Linux relocation has not been built on Linux during this session.

Qualification in the isolated macOS ARM64 Release profile:

- All 81 CTest checks pass, including architecture, desktop group/cycle, PM modal/reparent state, Open32 semaphores, TLS loopback, DIVE, video and audio.
- All 105 Swift tests pass. Both actual generated LX desktop-runtime tests run with explicitly selected built runtime and SDL dummy/software display; parent/child wrapper identity and inter-application shared memory pass.
- The complete tooling suite passes 316 tests. A final rebuilt-core subset passes 63 launch-prerequisite, session, archive, component-operation and dependency-audit tests.
- All changed C/header files pass the pinned Clang 21 formatter. Objective-C compiles in the final runtime; the repository formatter has no Objective-C configuration. Export generation, source boundaries, Ruff, scoped GTK typing, and whitespace checks pass.
- Fourteen offscreen light/dark UI screenshots cover native navigation, installer selectors, component activity and narrow outline columns. No live user ball or running guest was modified for testing.

Native Dock appearance/name/icon and cross-process AppKit focus still require normal-desktop acceptance: a controlled AppKit probe was denied by the execution environment. Headless IPC/policy tests do not establish that acceptance. An initial optional child-fixture checkpoint timeout was retained; individual and paired reruns passed, and its cause was not proven. Expensive runtime fixture setup is serialized while each fixture continues to run its own communicating guests concurrently. Test failures preserve bounded diagnostic logs.

Runtime qualification uses Unicorn, system SDL/FFmpeg, video, PCM/MIDI and DIVE, with OpenGL disabled. This release profile does not establish Mesa availability or change the documented system FFmpeg distribution requirements. Packaging verification and signed installed-artifact receipts are retained separately under `build-evidence/releases`.
