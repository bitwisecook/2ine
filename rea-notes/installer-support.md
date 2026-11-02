# OS/2 installer support and nested installation media

Bounded unit: the ball engine's installer prerequisites/media reader, its desktop
entry points, DOSCALLS ordinal 47, and redirected VIO output. Acceptance is a
single ISO selection installing **only** CMD.EXE, SYSLEVEL.OS2 and OSO001.MSG
into the ball's C: drive, plus mixed nested-media navigation and a real 16-bit
module-handle implementation. This does not change a reconstruction milestone
or establish a complete SmartSuite installation gate.

## Interface and source authority

The 2004 DDK's `combase/DDK/base/h/bsedos16.h` declares
`DosGetModHandle(PSZ, PHMODULE)` in its USE16 module manager; `bseord.h` in
the Toolkit 4.5 and DDK assigns `ORD_DOSGETMODHANDLE` 47. The bridge is far
Pascal with two four-byte far pointers, eight argument bytes, a two-byte
result, and a two-byte output module ID. The implementation shares existing
module-name lookup and exposes the registry's nonzero slot index as the
legacy ID. It never copies a four-byte HMODULE into a two-byte guest buffer.

Media traversal uses libarchive for standard archives and the existing ISO/FAT
reader on seekable streams. An iterative boundary resolver and linearly encoded
paths avoid a fixed nesting limit or exponential path growth. Resource limits
are documented in `ball/README.md`; unsupported formats are not universal
archive support. The importer searches for the three support files, validates
their file signatures, stages candidate data, rejects conflicting selected
versions, preserves existing destinations and leaves mapped media untouched. An installed C:\OS2\CMD.EXE supplies the
default guest COMSPEC while explicit configuration layers retain precedence.

IBM PACK/PACK2 decoders are retained from the local Deark checkout at commit
`c19b47c80a5ff8a7b41eb4f578e1afdda99202a2` (no local changes in those source
files). `ftcomp.inc` changes only its include paths. Its bounded adapter owns
all allocations per invocation and unwinds malformed streams without process
termination. Deark's MIT-style notice and dskdcmps's public-domain origin notice
are retained, and the packaged app carries `docs/licenses/Deark-MIT.txt`.

Retained source SHA-256:

- ftcomp.inc: `4b057ef3f57a92db93da5226b8aa4cd5f3ffd647daa908fdb2f77b4277ee78c6`
- ftcompdata.h: `b71c7e60880e793f3cf1435df1eba07ebbd19975794a11df3e99a592df3cddf7`
- dskdcmps.h: `d92de691cd8e5dc08389e30f1d843f659d2fae36cb418c06ceb99f84374f267f`

WarpIN parsing uses the revision-3/4 structure contracts in the original
WinWPI 0.21 source shipped in Hobbes's `pub/os2/util/archiver/WinWPI_0-21.zip`:
`wiarchive.h`, `wiarchive.cpp` and `widecomp.cpp`. No GPL source is copied into
the parser. WIArcHeader is 532 bytes, WIPackHeader 48, and the packed
WIFileHeader 285 (its filename field is **256** bytes). Revisions 3/4, stored
and bzip2 payloads, embedded scripts, and revision-4 executable stubs are
recognized. Scripts are never executed by the media reader.

## Hobbes evidence

The local January 2024 `file-catalog.json` contains 11,310 ZIP and 257 WPI
files; the September 1999 CD contains 824 ZIP and seven RAR files. Those
inventories motivated explicit WarpIN support in addition to ZIP/RAR and
libarchive's LHA/LZH, TAR, CAB, 7z and compression filters. ARC/ARJ/ZOO are
not advertised as supported formats by this implementation.

The original `pub/multimedia/fonts/truetype/caladea-fonts1002.wpi` package
(SHA-256 `b62d2d0f36bf22d38cc09157f74e36fbbcbb77a234dc6a906413189dcbead8f1`)
was listed, and its 9,126-byte script and 58,892-byte `caladea-bold.ttf` were
extracted to temporary files. The latter is recognized as a TrueType font.
No Hobbes or OS/2 binary fixtures are committed.

## Original OS/2 extraction

The original `4.5.2/OS2 Warp 452 - Client - Disk 1 - Install Disk - ISO.iso`
is the media recorded in `research/reference_screenshots/wordpro/manifest.json`
(recorded ISO SHA-256 `6fc32f65dce15fc39a4d4c1dddf5289f9482422ced9168bcc7c39f8abcae69d9`).
Both ISO import and extraction from its previously extracted disk folders
produced the support files. Extracted payload identities:

| Destination | Bytes | SHA-256 |
|---|---:|---|
| C:\OS2\CMD.EXE | 100108 | 98c390ba8ff4ecc9b783da92b1a5746496ca0e5e650ef78c54f24fefb5a1c308 |
| C:\OS2\SYSLEVEL.OS2 | 165 | dd748216f05c1c4ca680dbaae03c88e0d629ba581472c8aafa15ded93024122b |
| C:\OS2\SYSTEM\OSO001.MSG | 175354 | 0c72c08a31f0e3fbf670e10f85fc98bcb61842c979853ed3a517d0bb6eaaad99 |

The extracted-directory test recovered OSO001.MSG from disk_4/BUNDLE,
exercising compressed FTCOMP rather than just synthetic stored members.

## Validation

- Ball engine built with CMake; all 100 Python tool tests pass using the new
  engine, an explicit valid runtime build and canonical TMPDIR. The broad
  suite's initial default environment hit an existing dangling build DLL
  fixture and a `/var` versus `/private/var` soundfont-path mismatch; changing
  only the test environment resolves both.
- All 21 Swift tests pass, including model refresh after support import.
  The application and shared C engine build under Swift's strict concurrency.
- Mixed ZIP/PACK2/ISO navigation passes all six format orders; 40 nested ZIP
  boundaries pass with linear path growth. TAR/gzip, WPI stored/bzip2, nested
  FAT images, missing/corrupt inputs, conflicts and unsafe member paths are
  covered by `tests/tools/test_os2_support.py`.
- `ctest --test-dir build-image-app-runtime -R '^(file_compat|host_terminal)$'
  passes, as do image_drives, filesystem_images and floppy_drives. The module test checks name variants, error returns and adjacent
  two-byte canaries. The terminal test checks readable pipe output without
  escape sequences and retains its real PTY rendering/mode-restoration check.
- `tools/lxapigen.py` generates the ordinal-47 bridge from api16.json.
- Full original-CMD/SmartSuite execution remains **unverified here**. Both
  the integration guest and `lx_loader --help` fail before guest execution
  with host SIGILL inside Unicorn 2.1.4 `init_cache_info` at a cache-register
  instruction. A crash report predating this change shows the same failure.
  Trying the existing app's bundled Unicorn gives the same result. The native
  API/terminal tests run successfully without starting Unicorn execution.

Commands and retained logs are in build-macos/support-swift-test-final.log,
/tmp/2ine-support-tools-final.log, /tmp/2ine-support-runtime-build.log and
/tmp/2ine-support-image-install.log. Desktop review uses an isolated
build-macos/support-review app and ball; it does not modify user balls.

Native UI review completed the direct ISO chooser and verified the missing-file
card disappeared after installation. Install Software then reported all files
installed. The advanced browser reached CMD.EXE through ZIP → PACK2 → ISO →
INSTALL using its actual controls; both setup sheets were visually inspected.
