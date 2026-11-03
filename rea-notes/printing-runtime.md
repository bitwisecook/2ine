# Native PDF/PCL printing runtime

User requested full printer support, PDF and PCL file destinations, and normal
macOS/GNOME/Windows filename/location dialogs. Printing agent work is uncommitted
in the shared tree. Parent requested handoff to free its slot for a new 1-2-3 agent.

## Implemented

- `hal/include/hal/print.h`: portable save-result interface (error/cancel/OK).
- `hal/macos/print_dialog.m`: NSSavePanel, UTType PDF/PCL filter, main-thread
  execution, filename sanitization, returned owned UTF-8 host path.
- `hal/linux/print_dialog.c`: GNOME Zenity save chooser via pipe/fork/exec with
  explicit argv (no shell), cancel/error distinction, filename sanitization,
  bounded returned UTF-8 path. Missing Zenity prints an actionable diagnostic.
- `hal/win/print_dialog.c`: GetSaveFileNameW common-dialog adapter, Unicode
  conversions, extension/filter and replacement confirmation. Windows runtime
  itself remains unavailable, so this source is uncompiled/unvalidated.
- `os2libs/pmwin/print_job.{c,h}`: actual losslessly compressed color PDF image
  pages and monochrome ordered-dither PCL5 raster pages; job staging, 1,000-page
  and 1 GiB spool bounds, atomic final commit through HAL filesystem services.
  Cancel/abort never truncate an existing destination. Headless host-only
  `TWOINE_PRINT_OUTPUT` override; empty string simulates Cancel.
- Native A4 portrait 2480x3508 @300dpi typed printer DCs, retained bitmap page
  target, existing GPI drawing into pages, close guard while PS associated,
  cleanup on thread/anchor teardown, STARTDOC/NEWFRAME/ENDDOC/ABORTDOC/flush.
  STARTDOC bounded title bytes are copied and NUL-terminated; callers need not
  include a NUL in their explicit input byte count.
- `DevEscape` ordinal605 and `DevPostDeviceModes`609 with custom checked guest
  bridges, plus proper hardcopy-form output borrowing (68-byte HCINFO).
- PMSPL fixed queue catalogue `2INEPDF`, `2INEPCL`, levels0–6, local query and
  enum, ERROR_MORE_DATA size discovery, proper 32-bit embedded guest addresses,
  output counts on size queries, existing unknown-queue NERR_QNotFound behavior.
- `docs/printing.md` describes current behavior/limits.

Shared edits: `hal/CMakeLists.txt` adds platform save-dialog source to media
runtime; `os2libs/CMakeLists.txt` adds print_job.c and direct ZLIB linkage;
`tests/CMakeLists.txt` adds native test executable and `printer_output` CTest.
`internal.h` only PmDeviceContext printer fields; `graphics.h` private printer
helper prototypes; `device_context.c` implementation and automatic page bitmap
association; `graphics.c` erase supports bitmap PS; `bitmap.c` refuses selecting
arbitrary bitmaps into printer DCs; `coordinates.c` unit_scale uses DC dpi.
Visual agent has been told these exact overlaps. Help agent is adding unrelated
GPI APIs and metadata; do not overwrite its changes when regenerating bindings.
`guest/bindings/api32.json`, pmgpi.h prototypes/bridges and generated pmgpi-lx.h
contain DevEscape/PostDeviceModes additions. `pmspl.h` bridge owns all queue
marshalling. `tests/integration/desktop.py` expects two queues on empty-buffer
size discovery rather than an empty catalogue.

## Evidence

Original SDK: `/Users/jimd/src/os2/sdk/os2tk45-master/h/pmdev.h`, pmspl.h.
`HCINFO` 68 bytes; PRQINFO1/PRQINFO3 44 bytes, PRQINFO6 52 bytes; levels2/4
include zero queued jobs. DEVESC constants: query0, scaling1, STARTDOC8150,
ENDDOC8151, ABORTDOC8153, NEWFRAME16300, FLUSHOUTPUT16302. Tool import catalogue
binds PMGPI Dev32Escape605 and Dev32PostDeviceModes609.

PCL commands consulted in HP's primary reference:
https://www.hp.com/ctg/Manual/bpl13210.pdf (PCL5 Technical Reference).
Output starts universal exit/PJL PCL mode, printer reset, A4 portrait, zero top
margin, raster positioning/resolution/width/height, uncompressed raster rows,
end raster/form feed and reset/UEL. A physical PCL printer has not been tested.

Build succeeded (`/tmp/2ine-print-build2.log`).

```
python3 tests/integration/printing.py --build build-image-app-runtime --backend unicorn --work build-print-review
ctest --test-dir build-image-app-runtime -R 'printer_output|desktop_services|open32' --output-on-failure
```

passed outside the sandbox. Native harness checks queue0 catalogue, DC/form
capabilities and output canary, GPI rectangle/text drawing, page reset,
close-while-PS-associated rejection, two-page PDF/PCL, Cancel and Abort. The
Python verifier independently parses PDF stream lengths/xref, decompresses
RGB pages and checks actual colored sample pixels, parses PCL row lengths and
monochrome coverage. LX guest additionally tests queue levels0/1/3/5/6 embedded
pointers, unknown query compatibility, size-only counts, hardcopy canary,
escape-support query and actual guest PDF start/end. CTest printer_output and
open32_apis: 2/2 passed in 3.59 seconds. Files retained:
`build-print-review/printing.pdf` (2 pages), printing.pcl (2 pages), guest.pdf.

Pinned clang-format21.1.8 applied to new C files and printer-owned existing C
files. Ruff format/check for printing.py and desktop.py pass. Root should add
`tests/integration/printing.py` to Ruff/ty include arrays if those are explicit.

## Handoff / remaining validation

Latest very small title-copy/diagnostic changes were not rebuilt: the shared
build was interrupted by root's in-progress stream conversion. Current
`/tmp/2ine-print-final-build.log` fails at `_tb_stream_*` unresolved from
`disk_image.c`, requiring read_stream.c in HAL/platform and image_fs test source
lists. Parent has been notified; printing did not cause this link failure.
After root's link integration, rerun build and focused command above, then
full CTest. No further unchanged tests are needed before that fix.

Independent graphical PDF rendering inspection remains to be done. `pdftoppm`
is absent. Use macOS PDFKit/CoreGraphics in a temporary helper to render the
retained printing.pdf pages, then inspect PNGs; direct stream pixel tests pass
but do not prove a separate viewer renders the PDF. Native macOS save-panel
interaction has not been exercised; root can launch a small print probe without
TWOINE_PRINT_OUTPUT to verify the panel and Cancel, avoiding changes to user
files. GNOME and Windows dialogs are not locally verified.

Original Word Pro printing is not yet validated. Potential next gap: it may
query OS2SYS `PM_SPOOLER*` profile defaults rather than only SplEnumQueue. Native
queues exist but original app could require corresponding default-profile
entries or driver-mode queries. Do not claim full Word Pro printing until an
original app job is observed. Print forms are currently A4 portrait only;
landscape/job-property and physical printer transport are not asserted.

## Final resumed integration and acceptance (supersedes the earlier handoff)

The parent resumed this task after requesting real PCL host-printer submission.
The streaming link issue was fixed by the root; printer targets now build.

PCL now offers a native printer choice or Save PCL File. macOS uses Cocoa
controls (system-printer popup with Print / Save PCL File / Cancel). GNOME uses
Zenity's native list and save dialogs, with an explicit actionable missing-
Zenity error. POSIX submission reads the staged PCL stream through the fixed
`/usr/bin/lp -d <printer> -n <copies> -t <title> -o raw -- -` command, with literal
argv and child stdin. Printer enumeration uses `/usr/bin/lpstat -a`. Native
Windows source provides PrintDlgW and RAW OpenPrinter/StartDocPrinter/WritePrinter
submission; it remains uncompiled and unqualified because the Windows runtime
is unavailable. `TWOINE_PRINT_COMMAND` was removed: a guest/environment setting
cannot choose an executable. Isolated backend tests inject their trusted fake
spooler through a native HAL test primitive instead. No test submitted real
paper to a configured system printer.

Read-only merged/system INI fallback defaults now provide PM_SPOOLER QUEUE and
PRINTER, PM_SPOOLER_QUEUE_DD, QUEUE_DESCR and PRINTER entries. Actual stored
entries win; defaults are not persisted by reads, and private/user profiles
are unchanged. Actual SDK print samples use these keys. Native regression
verifies merged/system enumeration and absence in HINI_USERPROFILE.

Printer font metrics, default font size and default character-box size now use
300dpi instead of display96dpi. Public/native drawing and independent PDFKit
rendering confirm a readable 12-point default printed font. Open32 capabilities
identify raster printers, correct physical dimensions and resolution; PCL
reports its monochrome device depth.

### Original Word Pro printer module and added prerequisites

Read-only binary inspection of the user-supplied installed Word Pro copy found
LTCPRR10.DLL statically imports PMSPL SplQueryDevice381 and PMWINX PrintDlg530,
which were missing. Added those imports plus native Open32 printer-context and
StartDoc/StartPage/EndPage/EndDoc/AbortDoc/CreateDC adapters. The shared job uses
the same PM printer target and serialization. Saved DC drawing state preserves
active document/page state. Original SDK `os2win.h` explicitly packs PRINTDLG
as66 bytes; its DEVMODE is128 bytes and its DOCINFO is12 bytes. DOCINFO16/20
prefix-compatible variants are accepted with exact checked extents. Unsupported
custom print hooks/templates and non-default explicit job settings fail honestly.

Original module:
`build-lotus-visuals/WordPro.2ineball/drive_c/LOTUSW4/COMPNENT/LTCPRR10.DLL`
SHA256 `5ebc12edc8738b347b39eb569a582720a21aaeecc496f772ed4b8e23d95b40de`.
A generated LX guest loaded that unchanged module successfully (DosLoadModule
result0) in a separately cloned `build-print-review/WordPro.2ineball`.
No missing-import trap occurred. Retained evidence:
`build-print-review/{wordpro-imports.txt,ltcprr10-imports.txt,spooler-clients.json,
original-printer-plan.json,original-printer.stdout,original-printer.stderr}`.
The user's Documents/Lotus.2ineball was not modified. This proves loadability,
not an actual Word Pro print UI or end-to-end original application print job.

### Completed checks

```
cmake -S . -B build-image-app-runtime -DTWOINE_FFMPEG_PROVIDER=system -DTWOINE_MIDI_EXTENDED=OFF
cmake --build build-image-app-runtime --target pmwinx pmspl twoine_printing_tests -j8
python3 tests/integration/printing.py --build build-image-app-runtime --backend unicorn --work build-print-review
ctest --test-dir build-image-app-runtime -R 'printer_output|desktop_apis|rexx_apis|open32_apis' --output-on-failure
```

Targeted4/4 CTest checks passed in4.56 seconds at
`/tmp/2ine-print-handoff-tests.log`. Subsequent negative ABI tests passed in
`/tmp/2ine-print-negative.log`: bad PRINTDLG size, default-query caller-owned
handle preservation, default printer DEVMODE acceptance and unsupported landscape
rejection. The harness additionally verifies native and flat-guest packed queue
records, printer device pointers/canaries, native two-page PDF/PCL drawing,
Open32 default PRINTDLG and DOCINFO12 drawing-to-PDF, SaveDC/RestoreDC across an
active job, cancellation/abort, captured literal host spooler argv/data and
nonzero spooler status propagation. The printed `system spooler did not accept`
message is the intentional negative test, not a local printer failure.
`desktop.py`'s old missing-library expectation11 was updated to the corrected
SDK126 contract at root direction; desktop_apis now passes.

Independent PDFKit opened and rendered both printing.pdf pages. Inspected
`build-print-review/page-1.png` and page-2.png: red/blue lower-left rectangles,
readable12-point text, correct white page geometry. Helpers and logs:
`build-print-review/render-pdf.swift`, `/tmp/2ine-print-pdf-render2.log`.
This is independent viewer rendering in addition to PDF stream/xref checks.

Real native macOS UI smoke tests displayed and automatically Cancelled PDF
NSSavePanel, PCL printer chooser and PDF/PCL device chooser. All returned
Cancel0, visibility1, no path and no printer. No file was saved or printed.
Logs `build-print-review/dialog-{pdf,pcl,device}.stderr`, source dialog-probe.m.
GNOME save/spool adapter C files also compile with -Wall -Wextra -Werror on the
POSIX macOS host (not a GNOME UI runtime qualification). Windows is uncompiled.
Ruff/check/format, ty for printing.py, generated binding check and pinned C
formatting pass for printer-owned files; root should perform final global checks.

Remaining acceptance: original Word Pro print UI/job and an actual physical PCL
printer are unverified. No original licence or installation choices were made.
Custom print-dialog callbacks, landscape/other media and complete Win32 printer
API coverage are not claimed. All print work remains uncommitted for root
integration; new files include print_spool.c, pmwinx/printing.c and the native
printer output regression.

## Word Pro plain-document crash follow-up (2026-11-03)

User incident E1836F93-C037-4A36-B9B0-D261972E708F, 14:36:26 UTC, follows
Create a Plain Document. Guest failure is a read at address8, EIP004dfc34,
EDX0. The original WORDPRO.EXE is the same SHA-bound fixture documented in
lotus-visuals.md. Local patched REA/Ghidra12.2 function analysis retained in
`build-lotus-visuals/wordpro-plain-crash-rea.json` identifies a noncontiguous
function (Ghidra-inferred entry000a3e3c, second body range004dfbaa..004dfd52).
It calls PMSPL ordinal399 with queue level3, allocates the reported buffer, then
unconditionally reads the device name at the queue's pDriverData+8. The actual
faulting instruction and register state agree. Inferred function names do not
establish original source names or compiler calling conventions.

SDK authority: os2tk45-master/h/pmspl.h PRQINFO3/PRQINFO6 puts pDriverData at
byte40; h/os2def.h DRIVDATA has cb/version followed by szDeviceName at byte8.
Our catalogue supplied null driver data. It now returns an aligned, initialized
40-byte public DRIVDATA prefix inside the caller's buffer for levels3/4/6, names2INEPDF/2INEPCL,
virtual driver schema version1 and no private job properties. The same helper produces
DevPostDeviceModes defaults, preventing divergent queue/default data. Required-size
queries include the structure and alignment. Level0/1/2/5 record behavior is
unchanged. The new data describes our virtual printer; it is not claimed as a
reconstruction of an IBM printer driver's private format.

Guest printing integration now dereferences this same field for both queues,
checks size/version/device text, level4, query and guard bytes. Build and focused
printer_output test pass (1/1,1.27s), retained in
`/tmp/2ine-wordpro-driverdata-{build,tests}.log`. Original Word Pro document
creation remains pending a coherent updated probe; this regression pass alone
is not an application-level acceptance claim.

The first visible probe also loaded two different PMWIN builds because its
handmade bundle retained source-build rpaths alongside a copied native cache.
That probe packaging is being replaced with a frozen, relocated native closure
and a DYLD loaded-path audit. Original full trace was 2GB due the titlebar paint
loop; it is retained losslessly as visible-wordpro.stderr.gz, with a filtered
480KB trace at the original path. No original binaries or profile data were
patched to bypass the fault.

## Application default queue/profile scope

The original Word Pro trace then queried HINI_USERPROFILE (0xffffffff),
PM_SPOOLER/QUEUE, and warned that no default printer driver was installed. The
virtual queue defaults were available only through merged/system queries, so
that user-profile query missed them. The selected SDK's
samples/pm/print/prtprint.c around796 uses PM_SPOOLER/QUEUE through HINI_PROFILE
on OS/2 2.0; newer versions find PRQINFO3.fsType &PRQ3_TYPE_APPDEFAULT. The SDK's
pmspl.h defines that flag as0x0004 and places fsType at byte10 in PRQINFO3/6.
Our catalogue left both queues' flags zero.

Standard merged/user/system profile handles now expose virtual read-only
spooler defaults. An empty profile's queue is2INEPDF;, matching the user's
explicit PDF default. Actual stored user preferences take precedence over
stored system preferences; stored empty/unknown choices are retained and do
not silently mark a virtual queue as default. Reserved-handle fallback does not
apply to PrfOpenProfile handles, even aliases of the user/system backing files.
These service values are never inserted into Profile.entries or written merely
because an application reads or enumerates them. Other stored values continue
to win. Virtual QUEUE/PRINTER fallback follows the same stored preferred queue.

SplEnumQueue/SplQueryQueue levels3/4/6 now read that merged preference once per
call and mark only its matching virtual queue with APPDEFAULT. The PDF/PCL
queue catalogue, checked DRIVDATA and driver mappings remain the existing
virtual service. PMSPL links PMSHAPI for its public profile query; no reverse
dependency cycle is introduced.

Native checks cover initial PDF, PCL choice, explicit empty/unknown choice,
user-over-system precedence, both standard-file private aliases, private stored
values, no materialization of defaults and all relevant enum/query levels.
Guest printing checks query the original HINI_USERPROFILE path and change a
stored default to verify its flags. application_settings passes as well.
The sandboxed guest initially stopped with SIGILL after all native checks; the
permitted unrestricted Unicorn run passes. Final build/tests are retained in
`/tmp/2ine-default-printer-last-{build,tests}.log`, initial profile regression in
`/tmp/2ine-default-printer-tests.log`. These API contracts do not yet establish
that the original warning disappeared; root's next coherent Word Pro probe
owns that application-level acceptance.

## Open32 default queue parity (2026-11-03)

The remaining `PrintDlg(PD_RETURNDEFAULT)` hardcoded PDF choice is removed.
`printer_profile.h` shares the exact queue-name/default resolver with PMSPL,
using merged `HINI_PROFILE` and preserving explicit empty/unknown preferences.
USER PCL and SYSTEM PCL defaults now return PCL DEVMODE/DEVNAMES and a PCL DC;
USER PDF wins over SYSTEM PCL. `DN_DEFAULTPRN` describes the actual preference,
rather than assuming only PDF can be default. Invalid defaults return
PDERR_NODEFAULTPRN(0x1008) before allocations or caller-record mutation. The
interactive host chooser's failure maps to PDERR_NODEVICES(0x1007). Constants
verified against Microsoft's primary CommDlgExtendedError reference:
https://learn.microsoft.com/en-us/windows/win32/api/commdlg/nf-commdlg-commdlgextendederror

`open32PrinterDefaults` also bounds the 32-byte device name, rejects names for
another virtual printer, and retains explicit unsupported-layout errors.
The real-LX test now checks output names, default flags, struct canaries, device
mismatch and missing termination, plus actual PDF and monochrome PCL artifacts
for the three merged preference cases. Empty/unknown cases preserve every input
record byte and produce no file. Its PCL ink assertion uses Open32's top-origin
coordinates (the native PM test uses bottom-origin coordinates).

Complete main build and focused `printer_output|open32_apis` passed 2/2 in3.90s;
logs `/tmp/2ine-printer-default-build.log` and
`/tmp/2ine-printer-default-tests.log`. This verifies native/Open32 queue parity;
it does not claim a physical printer or unobserved original Word Pro print UI.
