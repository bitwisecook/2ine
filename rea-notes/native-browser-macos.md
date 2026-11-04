# Shared native macOS browser (2026-11-04)

The Finder-style browser is one built-in AppKit `NSOutlineView` implementation,
`NativeOutlineBrowser`, embedded through `NSViewRepresentable`. No third-party
component was added. The former component-specific outline is now a domain
adapter supplying rows and handling exact extraction source IDs.

The shared renderer is used by:

* the component tree, including mixed-state checks and provider settings;
* registry hives/keys and the registry value pane;
* INI application → key trees and the Config Files chooser;
* guest file/folder selection and browsing;
* mapped drives and installation-media/archive browsing.

Flat application/ball lists and the environment editing form remain domain
forms. File/media browsers retain current-folder navigation, Up/address controls,
and double-click opening rather than adding implicit recursive media scans.

## Contracts retained

`OutlineBrowserNode` contains stable IDs, child rows, text/icon/action cells,
optional checkbox state, search text, sort values and folder ordering. The
renderer owns adaptive native stripes, disclosure controls, columns, keyboard
handling, row selection, menus and drag adapters. It does not interpret registry
bytes, create configuration records, copy files, or execute guest applications.

Checkbox parents retain their whole original subtree when search filters the
visible children. Component dependency defaults, installed-leaf selection,
individual unchecks, explicit `configureComponents`, and exact opaque source
selection remain in the component adapter/plan. OpenGL's parent shows the
**selected** setting, not an assertion of availability. The child is the sole
Settings action for that group. Returning from Settings retains selection.

Registry paths and names retain their exact stored IDs and bytes. Typed value
editing, malformed/unknown record handling, revision checks, size limits and
ASCII-only normalization are unchanged. INI application and key names likewise
retain their original IDs; selecting an application expands its keys, while
editing activates only a value leaf.

Native header sorting uses appropriate text, numeric byte-size, and timestamp
values, retaining folders first and selection by ID. Search is fuzzy and supports
typing from list focus and Command-F. Space toggles a selected checkable row;
otherwise it remains input for multiword search. Return activates a leaf or
expands a branch, and falls through when no action applies. Action buttons and
menus revalidate current disabled/enabled metadata before invoking domain code.

Guest program/folder and installation-media relevant filters retain their
checked defaults. Folder/file icons, sizes, modification dates, context Open and
Show in Finder actions, local file-URL drag export, import buttons, and protected
row/background drops remain available. Read-only, ejected, busy and wrong-target
checks still gate import; the existing model/backend retains conflict and path
protections. Home abbreviation remains display-only; stored/revealed/exported
paths are unchanged.

The earlier view-based cell fix remains: there is no cell-based `objectValueFor`
callback to overwrite checkbox/status/size values. UI/coordinator/row objects
are main-actor owned under strict Swift concurrency and warnings-as-errors.

## Tests and visual evidence

Five new shared-browser tests exercise actual native controls and adapters:

* native header sorting, numeric/date cells, persistent selected ID, temporary
  filtering, and activation after sorting;
* Space/Cmd-F keyboard handling, applicable checkboxes and disabled toggles;
* native menu row identity/current disabled checks, URL writer identity, valid
  folder/background drops, and rejected read-only/non-file inputs without using
  the system clipboard;
* default relevant filters, archive icons, INI leaf IDs and opaque registry type
  bytes;
* real Registry, Profile, Config Files, Guest and Drives SwiftUI views hosting
  the same `NativeOutlineBrowser.BrowserOutline` class, without a window.

The four previous component-host tests now exercise the common renderer,
including filtered mixed parents, provider buttons, 11,000 collapsed leaves,
and actual hosted post-layout checkbox/status/size values. Existing BallCore
tests continue to cover extraction and typed registry contracts.

```
swift test --package-path macos --scratch-path build-os2-component-tree-swift
```

Final full results are recorded in
`/tmp/2ine-native-browser-final-tests.log`. Earlier full adapter run passed
**62 tests** (32 application-model, 30 BallCore); one initial new test had the
wrong expected existing label (`Type …` rather than `Unknown type …`), corrected
without changing registry behavior.

Controlled no-window light/dark bitmap captures are under
`build-tree-current/common-browser-qa/`:

* `components-{light,dark}.png`
* `files-{light,dark}.png`
* `ini-{light,dark}.png`
* `registry-keys-{light,dark}.png`
* `registry-values-{light,dark}.png`

The temporary renderer test passed in 0.404 seconds. It is archived there as
`render-tests.swift` with `render.log`, then removed from package tests.
No `NSWindow`, `NSApplication.shared`, live desktop input, clipboard access,
user-ball modification or installed-app replacement was used. Flat no-window
tables required explicit row-view materialization before `cacheDisplay`; their
row counts and model state were already correct, so no production layout patch
was made for that diagnostic capture limitation. Images verify adaptive stripes,
disclosure/checkbox controls, icons, readable columns, correct sizes/dates, typed
registry summaries and the single provider action. They do not qualify an active
window's focus/selection accent or a physical drag-and-drop interaction.

## Expansion-driven column growth and final narrow layout gate

Final offscreen native QA exposed a real shared-outline sizing defect: the default
`NSOutlineView.autoresizesOutlineColumn` expands the name column when a deeper child is
revealed, independently of its first-column viewport resizing policy. The macOS SDK
NSOutlineView.h (lines 106–108) documents that default. Five disclosed levels grew the
360-point name column to 440, putting the Size values outside a 760-point pane's 718-point
viewport. The same values were visible in the wider 1100-point ContentView.

The renderer now disables only that expansion-driven growth. Native flexible-column
resizing still handles the viewport: a 640-point pane has viewport/document 598 and columns
262/205/85; a 760-point pane has viewport/document 718 and columns 360/205/85. No custom
redistribution or resetting of user-resized widths was introduced. Legitimate horizontal
scrolling remains when column minima cannot fit.

Two permanent native regressions cover initial 640/760/1100-point NSScrollView layouts,
deep disclosure, visible size text, stable sorting/selection, user column widths retained
through updates and usable horizontal scrolling under oversized minima. The first failed
before the property correction, then both passed. Full Swift passed 105/105 afterward:
63 app/model and 42 BallCore tests.

Fourteen light/dark no-window captures, synthetic fixtures, the archived/removed temporary
harness, geometry and logs are under `build-immediate-components-current/offscreen-qa/`.
They verify six native fit navigation segments (586-point intrinsic width), actual generated
installer discovery, grouped dropdown+Browse, attached Change/Eject and empty Attach states,
and immediate/active component views at 640/760 detail and 1100 full host widths. All images
were inspected. The no-window SwiftUI sidebar List and opened combo popup remain outside
this visual gate; no live app registration, desktop input, user-ball writes or original
application execution occurred.
