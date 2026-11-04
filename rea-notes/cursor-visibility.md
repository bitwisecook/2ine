# Pointer visibility and default processing, 2026-11-04

The user reports a disappearing pointer during the original Lotus SmartSuite
installer. The active `Documents/Lotus.2ineball`, its process and live app
bundle were not changed or driven by this investigation. All diagnostics are
new under `build-cursor-current`, using source HEAD `096bf4e` plus the repair.

## Established contracts and defects

Microsoft's original OS/2 Programmer's Reference Volume2 (1989), printed
pages357–358,372 and442–444, describes WinSetPointer(NULL) as removal of the
pointer, WinShowPointer as a nested display-level counter (also exposed through
SV_POINTERLEVEL), and default mouse processing as responsible for selecting the
normal pointer. WM_CONTROLPOINTER carries a proposed pointer and returns the
pointer chosen by the control owner. The local IBM Toolkit
`h/pmwin.h` defines SV_POINTERLEVEL=44; SDK PM samples use WinSetPointer to
switch between wait and arrow pointers and WinShowPointer to manage visibility.

The previous default WM_CONTROLPOINTER handler returned0, losing the supplied
pointer handle. An original control can pass that result to WinSetPointer and
hide the pointer. Default WM_MOUSEMOVE did nothing, so it could not recover
the normal arrow after an explicit null pointer. WinShowPointer ignored nesting
and SV_POINTERLEVEL always returned0. Setting any nonnull pointer also forced
SDL visibility on, overriding an explicit hide operation.

## Repair

* Default WM_CONTROLPOINTER preserves the supplied pointer handle.
* Default WM_MOUSEMOVE selects the normal arrow; entry/multiline-entry controls
  select the text pointer. Standard controls ask their owner to choose the final
  pointer. A deliberate owner result0 remains a valid request to hide it.
* Failure to obtain a host system pointer preserves the preceding selection.
* WinShowPointer maintains a desktop display level: hide increments, show
  decrements down to0, and visibility requires both level0 and a selected pointer.
  SV_POINTERLEVEL reports that level. Changing shape does not reset it.
* The HAL's nonnull pointer selection changes shape without forcing visibility;
  PM applies its display-level policy. Explicit null selection still removes
  the pointer. All callers of lx_host_pointer_set were checked: only PM's
  WinSetPointer uses it. Capture visibility overrides are unchanged; DIVE has
  no direct caller of this API.

There are no shared Window/AnchorBlock layout changes, global forced-show
workarounds or live-process modifications. Trace-native output now includes
WinSetPointer and WinShowPointer calls for subsequent original-installer traces.

## Native failing/passing evidence

`build-cursor-current/run-probe.py` copies and relocates a private native
dependency closure, creates only a hidden PM window, and uses Cocoa/Metal with
SDL_MAC_BACKGROUND_APP=1 and no window activation when shown/raised. It sends
PM messages directly and inspects actual SDL cursor visibility and PM handles.
It does not move the desktop mouse or send desktop input.

The baseline uses the installed0.3/build14 Runtime in private copies:
`build-cursor-current/baseline.log`. Seven of nine original assertions fail:
default control-pointer preservation, default-motion restoration, nesting,
shape-change visibility, partial show, null selection and restoration after null.
The private closure's actual relocated library hashes are in
`baseline/metadata.json`.

The frozen repaired result is `build-cursor-current/fixed-final.log` and
`fixed-final/metadata.json`. All twelve native assertions pass, including the
original nine and entry-field text selection, deliberate owner hiding, and
owner replacement with the arrow. Thus the repair preserves intentional hidden
pointers while restoring those handled through default processing.

The new C regression in `test_pm.c` checks default WM_CONTROLPOINTER's return
handle and nested pointer levels. The fresh PM, capture, menu and dialog tests
pass4/4 in0.72seconds (`final-tests.log`); PM thread-posting also passes
(`thread-tests.log`). The build, clang-format and git diff checks pass.

The diagnostic profile uses current local sources/dependencies, Mesa disabled,
system SDL/FFmpeg and extended MIDI disabled because the optional sndfile
dependency is absent in this fresh configuration. That audio-only difference
does not alter the cursor implementation. The root integration build remains
responsible for its complete receiving-host feature profile and full suite.

These results reproduce and repair the SDK pointer sequences. They do not
identify the exact original installer call site or establish acceptance inside
the user's already running process, which still uses its existing runtime.
