# Native ball UI review

Scope: all pending native UI changes for image drives, device settings, drive
browsing, ZIP import/export, unavailable mounts and operation feedback. This is
a ball library/utility with one operation owner, not an OS/2 document editor.
The merged GNOME frontend retains its GTK/libadwaita conventions; Apple HIG is
applied to the macOS views and panels.

Local skills used:

- /Users/jimd/.codex/skills/apple-design-hig/SKILL.md
- /Users/jimd/.codex/skills/mac-arsed-mac-app/SKILL.md
- Axiom macOS, SwiftUI, concurrency and accessibility references.

Apple references: [layout](https://developer.apple.com/design/human-interface-guidelines/layout),
[toggles](https://developer.apple.com/design/human-interface-guidelines/toggles),
and [NSSavePanel accessory views](https://developer.apple.com/documentation/appkit/nssavepanel/accessoryview).
The concrete spacing values below are local layout decisions, not quoted HIG
requirements.

## Findings resolved

| Area | Finding | Resolution and evidence |
| --- | --- | --- |
| Export save panel | Checkbox and explanation touched the accessory edges. | Native stack now has 20-point horizontal insets, 12 above and 16 below; helper text uses the standard small font and secondary label color. Visually checked in compact and expanded native save panels in Dark appearance. |
| Reconnecting folders | Change Mountpoint was available only inside Drives. | The selected drive publishes a menu action, including when ejected. Ball menu action was visibly enabled for E: and opened the preselected reconnect sheet. |
| Reconnect sheet | Generic Map Drive action did not describe reconnecting a folder. | Primary action is Connect Folder or Attach Image. The sheet explains that drive letters, guest paths and previous files are preserved. Native default/cancel keyboard actions remain. |
| Command availability | Some controls appeared enabled during media mutations although the model rejected them. | Commands, sidebars and affected editors now include mediaBusy in their availability checks. Intentional live image changes while a guest runs retain their separate canChangeMedia policy. |
| Media operation feedback | Media work could lack a visible progress indicator. | Toolbar covers mediaBusy; image-creation sheets show Creating image and prevent dismissal while the owned operation runs. |
| Disconnected media settings | Settings could show a stale image as attached; an ejected locked device could not be unlocked. | Unavailable paths are labeled Ejected consistently. Lock can be cleared even with no image; locking a disconnected device is disabled. |
| Path and toolbar affordances | Long disconnected paths were not selectable; archive toolbar icons needed help. | Paths support native text selection. Import/export toolbar controls have descriptive help and retain accessible labels. |

## Reviewed behavior retained

- ContentView uses a native split view, standard toolbar controls, persistent
  navigation and native file drops; archive actions also exist in File.
- AppModel uses native open/save panels, typed ZIP/package content restrictions,
  new-target imports, saved recents and a worker actor for filesystem/archive
  work. CONFIG.SYS saves retain their existing conflict check.
- DriveBrowser shows status in text and with the eject symbol, preserves the
  previous path, and offers a direct reconnect action. Drive letters and guest
  working directories remain stable after reconnecting. Unavailable drives
  cannot import files or choose a default folder until reconnected.
- Editors and MediaDevices use labeled native text controls, pickers and
  checkboxes; sheets have padded content, explicit primary/cancel actions, and
  visible error text. Source files are retained when mappings change.
- TwoineBall commands expose archive, media, runtime and drive operations;
  stop/force-stop retain their explicit confirmation and actual-exit handling.
  The native control and menu architecture preserves standard editing/undo.
- Standard system colors and semantic text styles remain in place. No custom
  checkbox, replacement file picker, or color-only ejected state was introduced.
- The newer GNOME worker, responsive layout, drafts and packaging changes are
  preserved in the merge. Archive operations use that worker and busy state;
  reconnect controls include unavailable manifest drives. Unsaved GNOME config
  drafts remain editable and must be saved before export.

## Verification and limits

Native inspection used an isolated bundle and preferences under
/tmp/2ine-hig-ui. Verified export unchecked/checked states, compact and expanded
save panels, accessible checkbox identity, toolbar help, ejected drive status,
menu availability and the reconnect sheet's margins and primary/cancel actions.
The source review also covers the other changed editors, media controls and
creation sheets. This is not a claim of full HIG or accessibility certification.

Checks: 87 Python tool tests; 20 strict Swift package/model/core tests;
Ruff check/format; ty; pinned C/header formatting; GTK builder XML parsing; and
the image_drives, filesystem_images, floppy_drives and image_install CTest
integration tests. The installer test passed outside the command sandbox; its
Unicorn cache initialization trapped when sandboxed on this Mac, so the
sandboxed invocation is not qualified as passing.

Full spoken VoiceOver traversal, a complete keyboard-only pass, Light and
Increase Contrast visual passes, minimum-size/multiple-display checks, and
Linux GNOME/Windows runtime UI execution remain unverified. Native controls
expose labels/roles in the inspected accessibility tree, but that alone does
not establish those manual checks.
