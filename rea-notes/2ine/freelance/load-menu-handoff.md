# LoadMenu continuation handoff — 2026-11-06

Work stopped at the user's requested handoff. No additional implementations,
probes, commits or app installation were performed after that request.

## Source boundary

The current bounded implementation is in `os2libs/pmwinx/menus.c`:

- LoadMenu.462 uses checked native RT_MENU resources, numeric/#decimal/named
  identities (RT_RESNAMES=255), original HWND_OBJECT parent, and module 0/-1
  normalization.
- Complete submenu metadata is published atomically after conversion succeeds.
  Labels retain the declared resource codepage, mnemonics and shortcuts; native
  item state is reflected by Open32 queries. Destroy/Delete prune dead children;
  Remove preserves detached submenu ownership.
- `SetMenu` accepts a loaded HWND_OBJECT-parented root.
- `os2libs/pmwin/menus.h` appends `PmMenu.codepage` and the native parser in
  `os2libs/pmwin/menus.c` records it; native `lx_pm_menu_attach` accepts OBJECT.

Existing external ModifyMenu, TrackPopupMenu and menu context-help additions
were preserved. An external concurrent writer previously replaced LoadMenu with
numeric-only/desktop-parent behavior; the verified implementation has been
restored. Recheck source drift before continuing. Do not overwrite the broad
unrelated dirty checkout or regenerate shared interfaces without coordinating.

Maintained tests are `tests/os2libs/test_open32_load_menu.c` and
`tests/integration/open32_load_menu.py`. The C source/test compile with
-Wall -Wextra -Werror; Python Ruff/ty pass, and the generated LX image builds
(9,981 bytes). Runtime execution has **not** passed or been attempted after a
successful coherent LoadMenu integration.

## Exact pending integration

As checked at handoff, central registration is still absent:

1. Add public `ULONG/HMENU OS2API LoadMenu(ULONG/HMODULE module, PSZ resource)`
   to pmwinx.h, ordinal 462 / name LOADMENU. Proven argument bytes=8,
   callee-pop=0. The guarded guest bridge must preserve numeric resource IDs
   <=65535 and copy actual string pointers safely, including #ordinal names.
   A plain declaration currently exists only in batch_services.h.
2. Add api32 metadata/guest binding and regenerate the normal exports. Parent
   owns these shared files. Do not invent a Win32 PE menu-resource parser.
3. Register native target `tests/os2libs/test_open32_load_menu.c`, linking
   pmwinx, pmwin, pmgpi, 2ine and SDL3::SDL3 with twoine_application_target;
   CTest name proposed `open32_load_menu_ownership`.
4. Register generated LX CTest `open32_load_menu` with arguments
   `--build BUILD --backend unicorn --work-dir BUILD/open32-load-menu-tests`.
   Native/LX tests use only SDL dummy/software, never actual desktop input.

The attempted existing-target command was:

    cmake --build build/runtime --target pmwinx --parallel 6

It completed with exit 2 at the PMWIN link. Full log:
`build/lotus/freelance-menu/native-build.log`. Unrelated external additions leave
undefined WinBroadcastMsg, cleanupErrorInfo, destroyStandardFont and
`tb_icon_executable` (the last originates in the new pointer-file fallback;
ball/icon_executable.c is not in that PMWIN closure). Parent owns integration.
Do not insert stubs or bypass link/test assertions. The partially rebuilt shared
runtime must be rebuilt coherently before copying a closure or launching probes;
it is not a newly accepted runtime.

After resolving integration, run the two focused tests and existing Open32 menu
contracts. Fix actual regressions if observed; then regenerate a private launch
plan with the fresh native-cache closure and run one bounded original Freelance
startup. Record the next true blocker. Actual GUI rendering/interaction remains
a distinct acceptance gate and is not proven by dummy startup.

## Evidence and scratch

`load-menu.md` and `load-menu-evidence.json` beside this handoff contain provider
SHA, full permanent-analysis ID, native call/import provenance, original wrapper
ABI, actual FLWRSEN resource hash/layout and current source/check receipts.
The permanent original PMWINX cached analysis is already available; do not repeat
Ghidra analysis just to recover these bodies.

Retain these active test inputs until the pending integration/startup gate:

- `build/lotus/freelance-menu`: decoded original helper proof, actual resource
  reports, scratch extracted resource bytes, failed build log and test objects.
- `build/lotus/freelance-startup`: disposable COW Freelance ball, private launch
  plan/probe script and prior bounded logs. The latest run passed306 and stopped
  at missing462; original user ball was never launched or changed.
- `build/runtime`: parent's shared active runtime/dependency build. Do not delete.

The Freelance probe script is self-contained and no longer consumes
`build/lotus/baseline`. This agent did not consume `build/lotus/ordinal-audit`.
Those two trees may still be parent/other-job evidence inputs; do not delete
without checking their owners. Completed pointer and metric scratch was already
removed after permanent-cache/report intake. There is no additional completed
scratch owned by this agent to remove now.

Parent cleanup confirmation: both `build/lotus/baseline` and
`build/lotus/ordinal-audit` were subsequently removed after all task consumers
released them and full proof data was archived. The main
`rea-notes/2ine/handoff-2026-11-06.md` records the permanent archive paths.

All subprocesses launched by this agent have finished: the native build returned
exit2; the most recent private Freelance probe joined its own main/service process
groups after TERM/KILL cleanup. No new process was launched for handoff. A global
process inventory was attempted but the sandbox denies /bin/ps, so this is an
ownership-based confirmation, not a claim that unrelated jobs are stopped.

## Suggested continuation prompt

Continue the existing Lotus Freelance LoadMenu.462 work from
`rea-notes/2ine/freelance/load-menu-handoff.md`. Preserve the dirty checkout and
external menu additions. First recheck source drift and central export/CMake
registration, then repair the unrelated PMWIN linker closure without stubs.
Build a coherent current runtime; run the maintained native ownership and LX
LoadMenu tests plus existing menu contracts. If they pass, refresh only the
private COW Freelance launch closure under build/lotus/freelance-startup and
perform one bounded SDL-dummy startup to identify the next real blocker. Do not
launch or modify the user's actual ball, move the desktop cursor, install an app,
or claim a working UI from headless evidence. Preserve durable evidence before
cleaning completed scratch under build/.
