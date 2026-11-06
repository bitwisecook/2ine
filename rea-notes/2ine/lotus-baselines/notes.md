# Freelance and Approach: release 0.6 baseline

2026-11-05. Read-only source: `/Users/jimd/Documents/Lotus.2ineball`.
The registered WPS programs are:

| Application | Executable | Working directory | SHA256 |
| --- | --- | --- | --- |
| Lotus Freelance Graphics for OS/2 Warp 4 | `C:\LOTUSW4\FLG\FLG.EXE` | `C:\LOTUSW4\FLG` | `4d5a3b5ac9df801c4ec584801b0f1b313b55e27d69dd5e7c65e1c1d0cfd30cb1` |
| Lotus Approach for OS/2 Warp 4 | `C:\LOTUSW4\APPROACH\approach.EXE` | `C:\LOTUSW4\WORK\APPROACH` | `142d797c3b8bc5de5f653bdfdf488bad7ccfee1ce51cd7100bf64a846592b06d` |

`build/lotus/baseline` contains an untouched APFS clone `Source.2ineball`,
separate mutable `Freelance.2ineball`/`Approach.2ineball`, and a frozen,
relocated/signed 34-image `Runtime`. All three balls were copied with
`/bin/cp -cRp`; only each disposable probe ball's copied IPC directory was
cleared. The external D: ISO is read-only; there are no external writable drive
mappings. Manifest, generated configuration, audio configuration, OS/2 root
profile files and application hashes in the original ball were unchanged.

The source checkout was `99f59582b7a83e9ea9792010f5654c4dad2a9c43` (release
0.6). The build's configure-time `2ine-build.json` still records `a4b4df3-dirty`;
that metadata is stale, so the raw image hashes in
`runtime-source-inventory.json` identify the compiled baseline. No runtime was
rebuilt during capture. Working source had other agents' later edits, which
were not incorporated into the frozen image copy.

## Reproduction

The preparation request is stored separately for each application, as
`Freelance-request.json` and `Approach-request.json`. It invokes the private
`Runtime/2ine-ball` with `action: prepare-launch`, the cloned manifest and
ball, private runtime build, `backend: unicorn`, `resolve: false`, and
`arguments: ["launch", <registered application name>]`. No support-file
recovery is performed. The exact prepared commands, environments and owned
SOMDD service commands are in `Freelance-plan.json` / `Approach-plan.json`.
The cached dependency audit reports complete static closures (23 / 41 records),
which does not cover every runtime API.

Run `python3 build/lotus/baseline/probe.py` **outside the execution sandbox**.
The driver uses the exact saved plans, dummy SDL video/audio and software
rendering, `TRACE_NATIVE=1`, `DYLD_PRINT_LIBRARIES=1` and
`TWOINE_DUMP_MAPS=1`. Each probe is bounded to 25 seconds / 12 MiB and stops
only its owned processes/groups. It starts the configured SOMDD, runs the
application, then terminates the owned service. Do not substitute a live user
ball. The helper intentionally remains pinned to this baseline; later API
integration needs a new prepared candidate runtime, not an overlay onto this
closure.

Inside the sandbox, Unicorn 2.1.4 traps at host instruction `0xd53b0028` in
`init_cache_info+60`, before guest code. Re-signing only the private loader
with `allow-jit` did not fix this. Outside the sandbox, both the normal
no-entitlements profile and the entitlement control reach the same guest
failure. No production signing change follows from this host restriction.

## Observed first blockers

- Freelance: main exits 1 with `Missing import PMWINX:ordinal #262`.
- Approach: main exits 1 with `Missing import PMWINX:ordinal #636`.

Neither reaches a CPU-stop report, MEM_BAD_POINTER or video-initialization
failure before the missing import under this headless profile. This is not
proof that the later application paths work. No guest register/stack report
is emitted on these missing-import exits.

Each main loads 22 non-system Mach-O images and each SOMDD loads 14, all
inside the private baseline and with no duplicate library basename. In
particular PMWIN/PMGPI/PMWINX/VIO are single instances. Final raw logs and
maps are `*-main.stderr` and `*-service-0.stderr`;
`loaded-images-and-blockers.json` contains the image inventory and first
PM calls. Both main processes exit naturally; the driver stops its SOMDD
services, and a read-only process inventory confirmed no probes remain.

Freelance's worker first traces `LoadCursor(0,0x7f02)`, then class/string and
FLG.RI operations. Its immediate PM acquisition sequence is **LoadBitmap →
FindResource → WinInitialize → WinCreateMsgQueue → WinBeginPaint →
GpiLoadBitmap** on that worker. `Freelance-first-worker-context.txt` preserves
the surrounding calls. This sequence does not prove LoadCursor initializes
video; untraced SetCursor or resource-context acquisition must be checked
separately. Approach's first traced WinInitialize occurs on its main thread.

The small durable receipt is `2026-11-05-release-0.6.json`. Scratch clones,
private runtime and raw logs remain temporarily under `build/lotus/baseline`
as active inputs for the parent and API workers' before/after integration
checks; remove them once those consumers release them. No production code,
user data, installed application or desktop state was changed.
