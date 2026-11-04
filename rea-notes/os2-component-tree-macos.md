# macOS component picker (2026-11-04)

`OS2SupportSheet` uses the shared extraction plan's optional `tree` with a native
`NSOutlineView`. The source browser remains separate. The outline has component/
file, status, and size columns, native mixed-state checkboxes, alternating system
row backgrounds, and explicit provider Settings buttons. Expanding a group exposes
font types and individual font files, help engine/viewers/books/references, and
the other core-defined subgroups. Fully 2ine-provided originals are omitted by the
shared tree producer and are ineligible in the selection model.

Selection is a set of exact opaque source IDs. Requested/default CMD groups,
dependency recommendations, and fully installed groups supply the initial
selection. A parent checkbox selects its whole eligible subtree and, for a
component group, its dependency groups. Individual unchecked leaves stay off;
subsequent unrelated selection does not silently restore them. Installed leaves
remain selectable for path/configuration repair. The payload uses `fileSelection`
and emits `configureComponents: ["som"]` only when every eligible SOM leaf is
selected; partial SOM copies emit an empty explicit configuration list.

Fuzzy search uses the existing case/accent-insensitive subsequence matcher.
Typing while the outline has focus moves typing to search; Command-F focuses
search, Space toggles the selected outline row, and standard outline arrow-key
navigation remains native. Search retains ancestor rows. A filtered parent still
owns and reports its complete original subtree, preventing an apparently checked
parent from omitting hidden leaves. Expansion state survives selection changes
and clearing search. File-set indexing is linear rather than repeatedly searching
the entire tree for every source.

Provider rows display the active choice and open the existing Graphics & Sound
or Printing controls in a nested sheet. Returning refreshes provider metadata
while retaining exact source selection. Copying original OpenGL files does not
change the chosen provider. Older fixture plans without `tree` retain the category
picker and existing dependency helper behavior.

Verification used the Apple HIG, AppKit interoperability, SwiftUI, Swift
concurrency, and Swift Testing guidance. The UI and coordinator are main-actor
owned under the package's strict concurrency/warnings-as-errors settings.

```
swift test --package-path macos --scratch-path build-os2-component-tree-swift
```

All **57 package tests pass**: 27 application-model tests and 30 BallCore tests,
recorded in `/tmp/2ine-os2-component-tree-final-tests.log`. Eight new tests cover:

* installed/default sources and dependency expansion;
* partial SOM, explicit leaf unchecks, and mixed/on/off parent states;
* independent font types, help books versus engine, provider settings, fuzzy
  ancestor retention, and compatibility with pre-tree plan fixtures;
* actual no-window AppKit mixed checkbox and filtered-parent ownership;
* an actual native Mesa Settings button with no selectable source;
* an 11,000-leaf outline that initially exposes only its root and component row
  (0.152 seconds for that no-window test on this host);
* actual post-layout hosted checkbox state, status text and byte size, preventing
  a legacy cell-value callback from replacing the view-based controls' values.

The core agent separately verified a real-media tree with 10,811 eligible unique
leaves and 15 root groups in `build-tree-current/real-tree-summary.json`. The
macOS tests use generated metadata rather than redistributing original files.
No active user ball, live UI, clipboard, installed application bundle, or desktop
focus was changed. Full receiving-application interactive acceptance remains
separate from the offscreen checks below.

## Offscreen appearance check

A temporary diagnostic test hosted the production outline in `NSHostingView`
without creating any `NSWindow`, calling `NSApplication.shared`, or sending input.
`cacheDisplay` captured expanded font/help/provider rows into PNGs in both Aqua
and Dark Aqua. The temporary test is archived as
`build-tree-current/outline-qa/render-tests.swift` and removed from package tests.

This exposed a concrete view-based outline defect: the legacy
`objectValueFor` data-source callback returned the row title for every column.
AppKit consequently replaced status/size labels with that title and reset native
checkbox object values. Removing that callback preserves the values supplied by
the view delegate. The permanent post-layout regression observes the actual
hosted controls, including the mixed checkbox, rather than inspecting an isolated
delegate return value.

Before/after images are `before-light.png`, `before-dark.png`,
`outline-light.png`, and `outline-dark.png` under that directory. Both corrected
images were inspected: mixed dashes and ticks, disclosure controls, alternating
rows, provider buttons, status labels and right-aligned sizes are visible without
clipping. The corrected 18-row diagnostic fixture passed in 0.204 seconds.
This compile also caught the newly added optional `Node.detail` missing from its
filtered reconstruction; it is now carried through unchanged.
