# Lotus 1-2-3 original SOM startup

## Scope and selected inputs

User-reported 123W.EXE startup failed through LTOLEO20 -> LTSIO40 -> MAIN123W after an absent SOMD runtime, followed by an unmapped guest instruction fetch. The real Documents/Lotus.2ineball was inspected read-only; all execution and supplied-DLL staging used isolated ball copies.

123W.EXE SHA-256: `abfc454dce6fe9acd7a375d0a9f03698fa1469e3c2b5127bbc101f3fc78e5e99`. It is an LX32 launcher. REA/Ghidra 12.2 entry investigation at original object address 0x10938 is retained in `build-lotus-123-review/123w-entry-rea.json`, evidence `ev_017a3f10d698adb6700da956c569fc302e50e47d7ee06f3d751a4d7890bf038d`. The analysis binds the selected original hash and per-object decode context; no external OS/2 symbol set was imported.

The initially reported LTSSO40.DLL, LTSCMO10.DLL and LGLO12.DLL exist under LOTUSW4/COMPNENT. The printed paths under LOTUSW4/123 are failed explicit search attempts, not proof of absent application payloads. SOMD was absent from the ball. Its genuine OS/2 dependency closure imports the following original DLLs (all supplied by the user’s OS/2 media):

| DLL | SHA-256 | Extracted package member |
| --- | --- | --- |
| SOM.DLL | `1145626f7782d55c0559f9ef96a727752d6c263e6f1058dd35f2f40453dece06` | `6ece66246120b49415e84ca4f75cbe814bff7c4f33ee7649c87d53d5da17b7a9/output.009.OS2_DLL_SOM.DLL` |
| SOMD.DLL | `a9fa7abdc82c0f302c0cb811d5b723e147c9b97200ded637322d14722b98e0ac` | `61e9e5894b1aa2f50c822d0ba9bb052a67734fd41f953c383549fc11764a916f/output.001.OS2_DLL_SOMD.DLL` |
| SOMEM.DLL | `3a31d553dd404b6d57d8034929837cb5237c38ab79531506e43faf20aefaf61c` | `57fd7204de4c2ca19c3aa72d4a2c58d375801ed6865c5da1dbbca924b84296f5/output.017.OS2_DLL_SOMEM.DLL` |
| SOMIR.DLL | `be59e0e63697d014af5b2cb33aa8de81f701c087ac40d79cd1425f0171514edc` | `6ece66246120b49415e84ca4f75cbe814bff7c4f33ee7649c87d53d5da17b7a9/output.013.OS2_DLL_SOMIR.DLL` |
| SOMS.DLL | `3530ff7dec8e933b40a1f3a0c436791aba2d0cafb80897d9af09ac6fd44e417c` | `bd2b204ad0b5ac7835f5526230cf0bcee4c8988f5c0229e05f5b3ccea77ef729/output.045.OS2_DLL_SOMS.DLL` |
| SOMSEC.DLL | `e89f7fbb8d2e8d9f920bab6f36812dd8ed3442d7046c0e0c48d8a0d9aa49c944` | `d55671cfcd1deec318f9a0b754c483572f99134addbf17d901ed397641373428/output.007.OS2_DLL_SOMSEC.DLL` |
| SOMTC.DLL | `17cd11f8fe0a61b3f0bc06a0730f37be3c10f990bfc1f5bdf12f84158129589c` | `57fd7204de4c2ca19c3aa72d4a2c58d375801ed6865c5da1dbbca924b84296f5/output.046.OS2_DLL_SOMTC.DLL` |
| SOMU.DLL | `15787bb4eb81f54ffce3606beb43340df8ec2946a46062d8cd28ae403017ba14` | `6ece66246120b49415e84ca4f75cbe814bff7c4f33ee7649c87d53d5da17b7a9/output.017.OS2_DLL_SOMU.DLL` |

SOMD imports SOMSEC, SOMEM, SOMS, SOMU, SOMTC, SOMIR, SOM, DOSCALLS, NLS, SESMGR, QUECALLS, PMWP and MSG. The original SOMSEC/SOMD relationship is circular. Original SOM is needed: the existing native SOM module has no exports and cannot replace this object runtime. Full OS/2 extraction must therefore include SOM.DLL despite the native module filename. Full extraction may also supply DSOM service programs, configuration, messages and repositories; those broader service behaviors are not established by this startup check.

## Runtime and loader changes

- Added checked flat DosSetFileLocks ordinal 428 (20 argument bytes), the only previously absent native import found across the eight-DLL SOM closure. The 8-byte FILELOCK contains signed 32-bit offset/range fields. Normal exclusive lock/unlock, zero-range/no-op rules, signed bounds, reserved flags and bounded monotonic waits use the existing host range-lock adapters. Guest file reads/writes now enforce conflicting byte ranges. Shared mode reports ERROR_NOT_SUPPORTED; atomic mode reports ERROR_ATOMIC_LOCK_NOT_SUPPORTED before changing a held range. Unequal atomic ranges return ERROR_LOCK_VIOLATION. These unsupported modes are explicit limits, not success stubs.
- Loader errors have scoped per-call, per-thread state. A nested optional DosLoadModule failure cannot poison its caller’s initialization context. A failed dependency reports its actual module name and OS/2 error instead of generic ERROR_BAD_FORMAT. Invalid original bytes report bad format; explicit absent paths report file-not-found; absent module names report module-not-found. A found but invalid guest DLL does not silently fall through to a native counterpart.
- DosLoadModule clears its module handle on failure through the checked guest bridge. Error strings respect the caller capacity and NUL termination. A following valid load remains usable. An optional tail callback in LxLoaderState transports loader errors without changing earlier fields.
- Explicit path candidate read failures are shown only when native tracing is requested. Genuine failed dependency diagnostics remain. The original source already avoids dlopen of nonexistent native library candidates; the much older installed app log preceded that guard.

ABI authority: OS/2 Toolkit 4.5 `h/bsedos.h` FILELOCK and DosSetFileLocks, `h/bseord.h` ordinal 428, `h/bseerr.h` error values. Semantics checked against IBM Control Program Programming Reference and IFS file-lock descriptions. [IBM CP reference](https://bitsavers.trailing-edge.com/pdf/ibm/pc/os2/OS2_2.x/IBM_OS2_2.0_Technical_Library_1992/S10G-6263-00_OS2_2.0_Control_Program_Programming_Reference_199203.pdf). Shared and atomic locking remain unimplemented by the current host adapter.

## Verification

The generated `tests/integration/file_compat.py` now runs two independent LX test images so each code object remains within one page. It verifies exclusive conflict across separate opens, a finite wait, denied reads with untouched buffers, supported unlock/relock/close, rejected reserved/shared/atomic modes without discarding an existing range, invalid guest pointers, truncated module-error names with canaries and NUL boundary, zero failed handles, a missing dependency, invalid DLL bytes, and successful load/free recovery.

Commands and results:

```sh
cmake --build build-image-app-runtime --target lx_loader doscalls twoine_file_compat_tests -j8
python3 tests/integration/file_compat.py --build build-image-app-runtime --backend unicorn
ctest --test-dir build-image-app-runtime -R '^(guest_file_compat|file_compat|legacy_file_apis|runtime_execution|loader_startup)$' --output-on-failure
```

Build and generated guest checks pass. All five focused CTest checks pass; logs `/tmp/2ine-123-focused.log`, `/tmp/2ine-123-final-build.log`. Ruff check/format, ty for the two modified/new Python harnesses and lxapigen --check pass. Maintained C/H files were formatted with pinned clang-format 21.1.8.

Optional original acceptance (not silently part of a fixture-free CTest pass):

```sh
TWOINE_BALL_LIBRARY="$PWD/build-ball-engine/libtwoine_ball.dylib" python3 tests/integration/lotus_123_original.py \
  --ball /Users/jimd/Documents/Lotus.2ineball \
  --os2-dlls /Users/jimd/src/os2/extracted/module-packages \
  --build build-image-app-runtime --work build-lotus-123-original2 --backend unicorn
```

Pass: creates its named 1-2-3 frame and enters WinGetMsg without missing imports, failed dependencies or guest CPU faults. `build-lotus-123-original2` retains hashes, prepared launch plan, stdout/stderr and result.json. The harness detaches all non-C drive mappings and uses an isolated copied C drive. It makes no UI/license choices and deliberately terminates after that startup checkpoint. Its return 84 is the post-stop process status, not an application success code. A first harness version incorrectly waited for TEMP_SOMD_WIN, which occurs during cleanup; that false checkpoint was removed. Original cleanup after our stop logs DosExit(1,30036); a clean interactive application exit remains unverified.

Full spreadsheet operations, OLE/DSOM servers, printer use, native i386 and interactive clean exit are not claimed. The real user ball and installed app bundle were not modified. No original DLL or Lotus binary is included in source changes.
