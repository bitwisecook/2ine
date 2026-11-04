# Lazy guest folder disclosure

The common guest file browser showed folder icons but supplied a flat list to
NSOutlineView. With no children or explicit expandability, AppKit correctly omitted
its disclosure triangles. This affected Browse for Installers and the other guest
program/folder/drive views using the same browser.

`OutlineBrowserNode.canExpand` now describes an unloaded branch. The native outline
asks its adapter to load that branch on a user expansion; restoring expanded state
and search display never initiates directory reads. Existing component, registry,
INI and other static branches retain their previous behavior. An optional branch
activation callback preserves the guest browser's double-click folder navigation
rather than replacing it with expand/collapse.

`GuestBrowserTree` stores only requested immediate directory listings. It deduplicates
pending reads, displays row activity, permits error retry, and drops the triangle
when a loaded folder has no visible children. Each expanded level applies the existing
relevant-file filter, preserving directories and excluding nonprogram files by default
in the program picker. Switching that filter can reveal already-loaded nonprogram
files without another directory read. The existing fuzzy query operates on the loaded
hierarchy and retains ancestors of matches; it does not recursively search unread
folders or extract an ISO.

Changing ball, current path or revision cancels requests and invalidates their generation.
An old completion cannot repopulate a new browser. Disappearing views cancel child reads.
Nested selection, choose, context actions, drag URLs and drop targets use the selected
entry's full identity from the loaded tree, including the real host path. Existing
write/import permissions and conflict handling remain in their adapters.

## Verification

- Real NSOutlineView regression starts with a folder that has zero loaded children,
  verifies native expandability, expands it and observes one immediate-directory read.
  It verifies nested directory arrows, default executable filtering, retained expansion,
  nested selection/size/sorting/search, no unsolicited nested loads, folder double-click
  navigation and file activation. No NSWindow or desktop input is involved.
- Deterministic async model tests suspend a child read, reset navigation, resume it and
  verify that its result is discarded. They also check duplicate suppression, nested
  entry identity, failure/retry and known-empty state, without sleeps or polling.
- Full `swift test --package-path macos --scratch-path build-os2-component-tree-swift`
  passed **82 tests**: 44 app/model and 38 BallCore. New focused tests passed 3/3.
- Artifacts: `build-guest-folder-disclosure-current/{focused-tests,full-swift-tests}.log`.

The UI uses the existing native outline renderer and its system light/dark behavior.
No new extraction engine, recursive prefetch, guest launch, actual user-ball write,
installed application change or proprietary fixture execution was used for this change.
