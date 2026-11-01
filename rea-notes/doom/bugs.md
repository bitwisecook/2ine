# Doom application defects

These are third-party application observations, separate from original OS/2
core reconstruction and its bug ledger.

## Minute-relative game clock

Archived `os2doom110.zip` executable SHA
360993334c9f033339f674e2a248fd8b712aea6013cfcb430158768126ed9e2d.
Published source `i_system.c:I_GetTime` subtracts seconds-of-minute and ignores
minutes. A runtime crossing the minute boundary reached the melt-loop panic
WinMessageBox path. State: source-observed and reproduced under 2ine.

Correct OS/2 date/time semantics are preserved. The archived executable stays
unmodified. The explicitly source-built application variant uses serialized
QSV_MS_COUNT differences with wrap handling; the actual patch-body contract
and 70-second native/portable game checks pass. See source-build.md.

## Published network startup and packet serialization

The pinned source release defers initial game setup through G_DoNewGame,
clearing netgame/playeringame after real UDP discovery. Its ticcmd_t uses
32-bit ints but i_net.c swaps two fields as16-bit and leaves checksum-covered
padding uninitialized. The separately built source variant initializes the
network level directly, copies the complete native record and swaps the
actual32-bit fields. Real two-peer command streams now match on both workers.
See network-game.md and finish.md. The archived executable is unchanged;
these source conclusions do not assert source/binary byte identity.

## Published audio shutdown

The source's audio_close frees prebuffer before synchronous stop and frees it
again later. I_Quit and GameLoopThread can both request shutdown. The source
variant serializes close ownership, stops callbacks before releasing buffers
and frees prebuffer once. Compiled actual-body tests cover active completion,
failure retry, repeated close and concurrent callers. This independent defect
was not the cause of the larger-save corruption below.

## Published screen-backed save archive overflow

G_DoSaveGame writes its archive at screens[1]+0x4000. V_Init allocates each
320x200 screen independently, leaving47,616 bytes there. Real E1M3 combat
saves exceed81KB, corrupting guest heap metadata: native free traversed a bad
0x600000 link, and Unicorn reached the same invalid read or failed write.
The source variant allocates the declared SAVEGAMESIZE independently, writes,
frees and clears the archive pointers. Actual patched-function tests cover
large payloads with screen/allocation canaries. Final native/Unicorn gameplay
saves81,406 bytes, reloads/resaves81,086 bytes and exits0. Exact source-build
identity and retained failed/successful runs are recorded in finish.md.
