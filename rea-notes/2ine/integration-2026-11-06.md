# Lotus integration progress — 2026-11-06

Continues [the handoff](handoff-2026-11-06.md). Another session (`os2-e7`) is
concurrently finishing the frozen 393 export batch in this checkout; see the
ownership split below. Neither application passes acceptance yet.

## Ownership split agreed with the export-batch session

This session: PMWIN link closure, LoadMenu 462, Interlocked 421-423, their test
registration, and PMWINX 6 AdjustWindowRectEx. The batch session owns all other
frozen rows, the fragment merge, and `lxapigen` regeneration on the live tree.
`os2libs/generated/pmwinx-lx.h` on the live tree is stale by agreement.

## Build

Disposable snapshot: `build/lotus/integ/src`, built in `build/lotus/integ/bld`
(Release, BUILD_TESTING=ON, TWOINE_OPENGL=OFF, TWOINE_VIDEO=ON,
TWOINE_MIDI_SYNTH=ON, TWOINE_MIDI_EXTENDED=OFF, pinned dependency sources from
`build/runtime`). It exists so that builds do not compile the batch session's
files mid-edit. It is a point-in-time copy and diverges from the live tree.
Snapshot-only: all generated tables except pmwinx stay stale, and
`lx_pm_window_update_rects` stands in for the batch session's GetUpdateRgn fix.

## Verified in the snapshot

All 14 focused gates passed: open32_atomic_backing, open32_atomic,
open32_adjust_window, open32_load_menu_ownership, open32_load_menu,
open32_unicode_conversion, open32_unicode, open32_menu_metric_dimensions,
open32_menu_metrics, open32_pointer_coordinates, open32_pointers,
open32_priority_mapping, open32_priority, open32_priority_host.

- `open32_load_menu_ownership` fails if SDL_VIDEO_DRIVER is set in the
  environment, because SDL_SetHint cannot override it. That is the invoking
  environment, not a product defect.
- These results include unreviewed batch sources and are not evidence for them.
- This session saw no sandbox SIGILL; the handoff's escalation was not needed.

## PMWINX 6 AdjustWindowRectEx

Original PMWINX SHA `07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`,
body 000a05dc, 426 bytes, analysis `analysis-9lwae4ej`. All 15 calls are fixups
to PMWIN 829 WinQuerySysValue; SV_ and WS_ constants match the SDK. The fourth
argument is never read and the result is always TRUE. The menu adjustment adds
SV_CYMENU + 1 + SV_CYBORDER, preserved as original behaviour. Not on the frozen
list, but frozen #5 tail-calls it. LX bridge path not yet exercised by a test.

## Probes

Private clones, dummy SDL. Headless startup progress only, not acceptance.

- Approach: passes 423 and creates its toolbar, then exits 255 through its normal
  `ApproachEnd` path (string 9301, `appresen.dll` bundle 582) without calling
  ShowWindow or entering a message loop. Original-ball fingerprints unchanged.
- Freelance: passes 462, creates `Lotus Freelance Graphics`, and was still doing
  varied work at the 25 s limit (painting, resources, file reads). It has not been
  run on a real display, so nothing about its UI is established.

### Approach root cause, measured

APPROACH.EXE SHA `142d797c3b8bc5de5f653bdfdf488bad7ccfee1ce51cd7100bf64a846592b06d`.
Main is at 0x85128. A flag at [ebp-24] starts at 1, and if it is 0 at 0x85d18 the
app skips ShowWindow and the Run call. A snapshot-only Unicorn PC probe at all five
flag-clearing sites showed only 0x85bf0 executes: the first init virtual call
returns 0. Later symptoms, including the UI DLL loads, happen after the flag is
already clear. Inferring the failure from trace position was wrong twice; the
probe settled it.

The calls just before it are the superclass idiom: GetClassInfo on a predefined
class (Open32 error 1411), RegisterClass, RegisterClassAlias (error 87), for
example `TabBoxApproach`->`LISTBOX` and `FSFld`->`MLE`. Which class names reach
GetClassInfo is still inferred from those pairs, not traced.

Original PMWINX `RegisterClassAlias` (ordinal 729, 000a2fc8): looks up both names
through 0003e224, sets error 87 if either is missing, stores the alias at class
record +68, returns TRUE. 2ine's logic matches, but its lookup finds only
SCROLLBAR and MDICLIENT. The original table in object 8 at +0x17e0 has BUTTON,
COMBOBOX, COMBOEDIT, COMBOLBOX, DEFCLIENT, DIALOG, EDIT, FRAME, LISTBOX, MDIFRAME,
MDICLIENT, MDICHILD, MENU, MINMAX, MLE, SCROLLBAR, STATIC, SYSMENU, TITLEBAR, USER.
Class records hold name +60, length +64, alias +68, next +72, previous +76.

Not established: how the original consumes the +68 alias at window creation.
CREATEWINDOWEX (000a2dbc) dispatches through C++ vtables. 0003d3ec is a vtable call
site, not the reader. This needs a Ghidra pass, not byte-pattern search. In 2ine,
`alias` is stored but CreateWindowEx never reads it.

### Next work for Approach

1. Find the +68 consumer in the original with Ghidra.
2. Provide the predefined classes through builtin_class, with a chainable
   procedure, so GetClassInfo and RegisterClassAlias succeed.
3. Create aliased classes as native controls subclassed with the app procedure.
   Once COMBOBOX and friends are found by open32ClassAcquire, the 1407 fallback in
   CreateWindowEx stops being reached, so creation must move to the class path.
4. Add a maintained regression test for CreateWindowEx controls and notifications.
   None exists yet; only the Approach probe has exercised them.

Also open: CreateIC('', '', '', 0) fails with 1801 at startup. Approach continues
past it, so it is not the exit cause, but printing will need it.

## Snapshot-only state to discard

`hal/emulator/cpu_unicorn.c` TWOINE_PROBE_PCS diagnostic, the
`lx_pm_window_update_rects` helper, and regions.c changes exist only in the
snapshot. None of them may reach the live tree. The batch session's
`lx_pm_window_damage` replaces the helper.

## Live-tree state of builtin controls

native_controls.c, internal.h and the windows.c 1407 branch are landed but have
not been compiled in the live tree. The dialogs.c ownership line and the messages.c
WM_CONTROL translation are queued with the batch session's review worker. Until
they land, these controls are created but SendMessage to them returns 1400.

## Evidence lost

`reprepare.py` originally re-archived on every run, so
`build/lotus/approach-startup/pre-integ` holds the invalid stale-plan run, not the
5 November Approach logs. Those logs were overwritten; [the Approach
handoff](approach/handoff.md) still records that run's summary.

## Open items for the user

- `git ls-remote fork` fails with `Permission denied (publickey)`, so pushing
  cannot work from this environment and the remote state is unverified.
- The real ball's drive D image, `/Users/jimd/Downloads/Lotus SmartSuite 1.7 for
  OS2.iso`, no longer exists, and prepare-launch fails on it. Approach's private
  clone was reduced to drive C to match Freelance's. The real ball is unchanged.
- Installing or pushing the tree would include the unreviewed, incomplete batch,
  whose 13 process ordinals are not yet implemented.
