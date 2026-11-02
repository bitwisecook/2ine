# Original IBM REXX system service under 2ine

## Scope and original inputs

The SmartSuite 1.7 root `INSTALL.CMD` is REXX. Its first external command is
`@ECHO OFF`; it then registers and calls `REXXUTIL`'s `SysLoadFuncs`, sets a user
profile entry through `SysIni`, and invokes `ENGLISH/INSTALL.EXE`.
The original interpreter and utility DLLs are taken from the user's OS/2
installation media. No replacement interpreter, patched REXX image or
proprietary payload is committed.

| Original media file | SHA-256 |
|---|---|
| CMD.EXE | `98c390ba8ff4ecc9b783da92b1a5746496ca0e5e650ef78c54f24fefb5a1c308` |
| REXX.DLL | `e6bb344632ab3281f337652a9666f76ae5b69d75634b17f18eb7ba08b9c45a0b` |
| REXXAPI.DLL | `1ce3584fefb2f4c2663946777e66507a8955fc084e8bc8cf7d4603b8ae1b9cfb` |
| REXXINIT.DLL | `a84fb160a1cf95456ad1cfa4e0dde536b48c480e3fe6ff42af494538d216147f` |
| REXXCRT.DLL | `50461785a3929e636b937c2242b443f48fe2c3920e1646de898d1285320eaa4c` |
| REXXUTIL.DLL | `4c64e5535def7da94cd36f86d577652e92b1de493ddf8fcb412e995aa6f18a89` |

## Observed system prerequisite

After correcting selector contracts, original REXXAPI attempted to open
`\SEM32\REXXAPI.SEM` and returned initialization error 1002 because the system
service was absent. Independent decoding of original REXXINIT identifies its
`RxApiInit` export at object-1 offset `01B8`: it starts a guest thread at offset
`0024`. Executing that original initializer creates `REXXAPI.SEM`, the
`REXXMEM.SEM` event and `\QUEUES\RXQUEUE`, then reads requests from the queue.

A prototype loading REXXINIT in the application process proved that these are
necessary resources, but also exposed why the service requires a distinct
process. REXXAPI opens and closes its writer access repeatedly. IBM's
[Control Program Programming Reference](https://ftpmirror.your.org/pub/misc/bitsavers/pdf/ibm/pc/os2/OS2_2.x/IBM_OS2_2.0_Technical_Library_1992/S10G-6263-00_OS2_2.0_Control_Program_Programming_Reference_199203.pdf)
(pp. 2-28 and 2-170) distinguishes owner deletion from a writer closing access.
A same-process service would cause ordinary client cleanup to delete its own
service queue. General queue semantics were preserved.

## Service worker and checked handoff

The loader lazily ensures the service only when loading an original REXXAPI
image. A private `--rexx-service` mode creates a small internal LX stack/TIB
host, loads original REXXINIT and parks its main host thread while the original
worker handles requests. This internal executable supplies invocation storage;
it does not implement REXX. Native REXX overrides do not trigger this path.

Clients retain a same-ball shared-object lease for their process lifetime. The
service observes lease liveness without retaining it, stays available while any
client is alive, and exits after a one-second idle grace. Kernel descriptor
cleanup releases leases after a client crash. Concurrent clients reuse a live
queue owner; if startup races create two service workers, a client stops only
its retained losing child. Startup is bounded to five seconds and reports a
missing/failed original initialization service explicitly.

The parent task added shared named mutexes and same-ball process peer leases,
so an independent client can open the original service's mutex and give it
shared memory. Queue receipt checks known shared backing and granted access,
and maps an authorized raw shared pointer before the original service consumes
it. This is lazy recipient attachment at the checked IPC handoff; it does not
claim general eager mapping into arbitrary running guest CPUs.

Existing queue files are not proof of a live service. The host mailbox open
path now verifies the creator's existing lifetime lock, rejecting stale records
left after owner death. A regression kills a queue owner, checks that opening
its stale name fails, then recreates the name successfully.

Original REXX also temporarily detaches from its shared suballocation pool.
Tracing showed service `DosSubSetMem(...,5,65536)` and client attach with flags
4 both succeeding, followed by client `DosSubUnsetMem`. Erasing the shared
allocator magic during that detach made subsequent API calls fail with
`ERROR_DOSSUB_CORRUPTED` (532). The parent task corrected pool attachment to be
process-local while retaining shared allocator metadata.

## Verification and retained artifacts

Passed original execution gate:

```
TWOINE_IPC_DIR=/tmp/2ine-rexx-service-debug5 \
  python3 build-cmd-runtime-review/run.py /C CALL 'C:\SAY.CMD'
```

This executed original CMD and REXX, printed `ORIGINAL-REXX-WORKS`, and exited
zero. Logs are `/tmp/2ine-rexx-say5.stdout` and `.stderr`; the private fixture
script sets the configured C: mount, original DLL LIBPATH, Unicorn backend and
native library search path. Execution was unsandboxed because the sandbox
blocks Unicorn's required cache-configuration query.

The standalone original-fixture regression also passed:

```
python3 tests/integration/rexx_original.py \
  --build build-image-app-runtime --backend unicorn \
  --cmd build-cmd-runtime-review/OS2/CMD.EXE \
  --dlls build-cmd-runtime-review/OS2/DLL \
  --work build-original-rexx-regression
```

It runs one original REXX client blocked on built-in `CHARIN`, executes a second
original SAY client, verifies the same separate service owner is reused and
remains alive after the second exits, resumes the first client, and verifies
service retirement after the final client exits. The work directory retains
stdout/stderr, configuration and hashes of every supplied original input.
This requires explicit original files; it is not registered as a silently
skipped default CTest.

The synthetic exception suite additionally validates dynamic DLL termination:
DLL callbacks retain the main stack and can call their native API dependencies.
`lxTerminate` unloads the complete graph together, and graph termination orders
dependents before their dependencies while mappings remain present.

```
python3 tests/integration/exceptions.py --build build-image-app-runtime --backend unicorn
python3 tests/integration/queues.py --build build-image-app-runtime --backend unicorn
```

Both passed, including raised/nested exceptions, selector imports/fixups,
dynamic termination, opaque queue pointers, notification, cancellation, owner
close behavior and stale-owner recovery. Native i386 runtime verification is
still unavailable locally because the configured Colima socket is absent.

## Extended original API and installer gate

After honoring original `OBJ_TILE` requests, the original interpreter's
32-to-16-bit CMD callback also passed. Original REXXAPI's module-loading path
then exposed an error-code distinction: object-1 offset `32CF` calls DOSCALLS
319 and offset `32DA` compares its result with `007E` (126), loading the named
DLL only for that missing-module result. 2ine had returned 123 for an ordinary
unloaded module, so REXXUTIL was never loaded. Valid missing modules now return
`ERROR_MOD_NOT_FOUND`; invalid name characters retain `ERROR_INVALID_NAME`.
This compatibility decision is supported by the selected caller bytes and the
successful original DLL load, rather than a claim that a stock OS/2 execution
of the query was observed.

The extended original fixture passed in `build-original-rexx-regression2`:

```
python3 tests/integration/rexx_original.py \
  --build build-image-app-runtime --backend unicorn \
  --cmd build-cmd-runtime-review/OS2/CMD.EXE \
  --dlls build-cmd-runtime-review/OS2/DLL \
  --work build-original-rexx-regression2
```

Its second client now executes `ADDRESS CMD`, registers and loads original
REXXUTIL through `RxFuncAdd`/`SysLoadFuncs`, and verifies binary-NUL profile
round trips for both USER and a named private INI file. The latter exercises
`PrfQueryProfile`'s actual nested guest buffers. It printed all three markers:
`ORIGINAL-REXX-WORKS`, `ORIGINAL-CMD-WORKS`, and
`ORIGINAL-REXX-APIS-WORK`. Concurrent service reuse, blocking input continuation
and final-client retirement passed again.

Original REXXUTIL requires dynamic PMSHAPI ordinal 107 even when operating on
USER profiles. The implemented `PrfQueryProfile` returns the actual configured
user/system profile paths in guest notation and uses the SDK's four-word,
16-byte guest PRFPROFILE structure; its host structure can have wider pointers.
The SDK authority is `h/pmshl.h` and the real caller example is
`samples/rexx/api/rexxutil/rexxutil.c`. Native settings regression covers
required lengths including NUL, insufficient capacities, unchanged buffers on
capacity failure, pointer preservation and guard bytes. CTest
`application_settings` passed with this addition.

The final actual launch command was:

```
TWOINE_IPC_DIR=/tmp/2ine-rexx-smartsuite11 \
  python3 build-cmd-runtime-review/run.py /C CALL 'D:\INSTALL.CMD'
```

The original root script loaded REXXUTIL, completed its SysIni step and spawned
`D:\ENGLISH\INSTALL.EXE`. `/tmp/2ine-smartsuite11.stderr` records window creation,
dialog/message dispatch and painting inside the original installer. The
20-second fixture timeout ended this observation while the installer was in its
GUI message loop. It is a launcher/startup gate, not a completed installation,
help-viewer check or installed-application regression.

## Final original launcher gate

After process-local SubUnsetMem attachment state, OBJ_TILE mapping, correct
ERROR_MOD_NOT_FOUND for a valid missing module name, and PMSHAPI107 profile
filename discovery, the actual SmartSuite root INSTALL.CMD loads REXXUTIL,
completes SysLoadFuncs/SysIni and starts ENGLISH/INSTALL.EXE. Its PM dialog/message
loop is observed in /tmp/2ine-smartsuite11.stderr. This proves the original
launcher path reaches the installer, not completion of its interactive choices.
The explicit original regression was rerun in build-original-rexx-final and
passes CMD callbacks, utilities/profiles, concurrent service reuse and cleanup.
