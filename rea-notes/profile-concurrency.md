# Persistent PM profile transactions

The multi-application runtime previously cached each INI at first open, applied writes to
that process's snapshot and atomically replaced the file. Atomic replacement prevented a
partial file, but another application's committed keys could be lost on the next write.

The SDK printing profile sample `sdk/os2tk45-master/samples/pm/print/prtprof.c` uses
`PrfQueryProfileData`/`PrfWriteProfileData` for application settings and explicitly documents
that application plus NULL key/data deletes every key in that application (lines 119–145).
The existing implementation's case-sensitive names, opaque binary values, NULL deletion
forms, merged user/system precedence, virtual defaults and native INI reader remain intact.
This is a portable-runtime persistence correction, not reconstructed PM byte identity.

## Transaction and cache contract

Every `PrfWriteProfileData` (and `PrfWriteProfileString`, which delegates to it) holds the
existing process monitor, then an advisory host-file lock on `<canonical-profile>.prf-lock`.
It reloads the current on-disk profile under that lock, applies only the requested change,
flushes and atomically replaces the INI, updates its local entries, then closes the lock.
A dead process's OS-owned lock is released without timeout or stale-lock-file polling.
The sidecar inode remains in place. Its suffix deliberately differs from WPS's `.lock`:
closing a second POSIX descriptor for the same inode could release WPS's enclosing lock.
Interrupted temporary files with the same save name are removed by the next transaction.

Queries acquire no file locks and create no files. An unchanged host file takes a stat-only
cache path using device/inode/size and nanosecond mtime/ctime. The HAL's new separate
`LxHostFileIdentity` API does not change the layout of the existing `LxHostFileInfo`.
The selected macOS/Linux `host_identity.c` adapter owns its native stat layout; the shared
POSIX layer validates the path and translates errors without platform macros.
A loaded snapshot is associated with an identity only when that identity is stable across
its read. Replacements invalidate the cache even when size and modification time match.
Paths without a host identity use byte comparisons; unchanged bytes avoid repeated decode.
Merged queries refresh both profiles, and virtual preferences use refreshed entries.
Invalid replacement files fail reads/writes rather than silently returning or publishing a
stale cached version. Their bytes are preserved.

## Verification

`tests/os2libs/profile_concurrency.py` controls two long-lived native clients through pipes.
Both clients open the same profile before writes begin. It verifies private, user and
system independent keys, equal-sized binary changes, last-writer behavior, key/application/
whole-profile deletion without resurrection, merged fallback after deleting a user override,
160 simultaneously submitted independent writes, and continued updates after a sidecar lock
owner is killed. Read-only opens/queries assert no files were created. A malformed external
replacement remains byte-for-byte unchanged after failed reads/writes.

- `cmake --build build-image-app-runtime --target twoine_profile_worker twoine_settings_tests -j 6`
  passed.
- `ctest --test-dir build-image-app-runtime -R '^(profile_concurrency|application_settings)$' --output-on-failure`
  passed 2/2 in 0.26 s. Existing settings coverage includes generated native INIs, corrupt
  inputs, binary values, profile aliases, registry persistence, WPS objects and virtual defaults.
- Clang-format 21 dry run, Ruff check/format, global ty and `git diff --check` passed.
  A final harness-only typed-pipe cleanup was followed by a passing concurrency recheck (0.18 s).
- The unified source-ownership gate initially caught a macOS stat-field conditional in
  shared POSIX code. Native stat identity was moved into genuine macOS/Linux adapters,
  selected by CMake; the rebuilt source_boundaries/settings/concurrency checks then passed
  3/3 in 1.86 s (`identity-build.log`, `identity-tests.log`).
- An isolated alternate `libpmshapi.so`, compiled from the previous HEAD `profiles.c` and
  current unchanged module objects, failed the new test at its first cross-process read:
  `missing` instead of `hex:11223344`. No tracked source was temporarily reverted.

Artifacts: `build-profile-concurrency-current/{build.log,tests.log,baseline-negative.log}`;
previous implementation source/object/library are under its `baseline/` directory.

## Limits

Cross-process transactions cover normal host-backed profiles, including default private-C
OS2.INI/OS2SYS.INI and runtime databases. Custom profile files stored inside filesystem image
members are not claimed cross-process safe: the current image HAL opens private snapshots,
so ordinary inode advisory locks do not coordinate those members. Image byte-refresh behavior
is retained. No image transaction adapter was added in this bounded change.

Locks are cooperative among updated runtime processes; external writers that ignore the
sidecar protocol can still race a transaction. Multiple Prf calls are separate transactions;
this does not make a multi-call application protocol atomic. Existing persisted profile
encoding and guest process/session semantics are unchanged. Tests ran on macOS ARM64;
Linux uses the same POSIX locking and identity implementation but was not executed locally.
