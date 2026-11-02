# Lotus 1-2-3 stock reference

Original Warp 4.52 and SmartSuite 1.7 with Fixpack 3, captured in an isolated QEMU overlay. See [manifest.json](manifest.json) for media hashes, exact input sequence, workbook observations and screenshot hashes. The original application, workbook and fonts are private fixtures and are not bundled.

These are stock masters. A passing 2ine comparison requires the same create, formula, font, save, reopen and recalculation cycle. Font rasterization and confirmed display DPI differences may be reviewed leniently; missing controls, cells or formatting are defects.


The `lotus-123` branch has editable spreadsheets on native and Unicorn. Both
enter quantity/double in A1/B1, 21 in A2 and +A2*2 in B2, displaying 42; choose
Courier headers; save and reopen; change A2 to24 and observe48; choose Helv
numbers; save and cold-reopen those values and fonts. Qualified captures,
workbook hashes and known differences are in [2ine/manifest.json](2ine/manifest.json).
The complete visual and shutdown gates remain unverified. Native teardown
returns1 without a missing import; its stock return code is unverified.
Unicorn teardown faults in original guest code. Font/DPI lenience does not
excuse missing grid, headers, menu/formula bar or frame controls.

Current contracts include native resource dialogs/icons, pattern and polygon
drawing, text alignment, scrollbar controls, worker-to-window posting, PM object
windows and hooked Open32 single-file dialogs. `gpi-contracts.json` records
focused stock API measurements in addition to locale and PM contracts.

The original Lotus OLE runtime also requires private Warp SOM runtime DLLs, `SOM.IR`, the installation's `LOTUSW4/ETC/*.IR` interface repositories and the `OS2/ETC/DSOM/SOMD*.DAT`/`SOMD*.TOC` implementation repository. Point `SOMIR` to the semicolon-separated guest paths and `SOMDDIR` to a writable private copy of the DSOM directory; the reference installation sets `SOMDTHREADPEER=1`. These original files are not bundled. Each test session must use separate writable profiles and DSOM registration files.

The current PM provider has no distributed SOM daemon: its readiness query returns false and starting it returns `ERROR_NOT_SUPPORTED`. PM error-state exchange stores and returns the previous error ID; optional extended error records are not retained.

Named OS/2 queue contracts are recorded in `queue-contracts.json`. Named queues use ball-local host mailboxes between 2ine processes. Same-process writes preserve opaque guest addresses; cross-process named shared-memory records preserve their original pointer and pool ownership. Private cross-process payloads are copied. Session termination uses a private queue reference so closing the session does not invalidate the application's queue handle.

Mux-wait contracts are recorded in `mux-contracts.json`. Event posts and mutex releases notify blocked ANY/ALL waits. PM wait exports currently delegate to the DOS semaphore waits; synchronous cross-thread PM message pumping is not implemented.

POSIX thread cancellation runs cleanup at host cancellation points; Unicorn also checks between guest execution slices and before API dispatch. Native guest loops that never enter a host cancellation point cannot yet be interrupted. The Windows host adapter reports cancellation as unsupported.

`memory-control-contracts.json` records stock shared-memory, suballocation and list-selection measurements. Suballocations remain within their supplied arena, use eight-byte units and retain pool-relative free-list state across process attachments. The current memory provider commits backing eagerly; sparse commitment and page decommit are not modeled.

Open32 DDE supports thread-owned initialization, ANSI string references and local service-name registration/filter state. Peer registration notifications and DDE conversations are not implemented; registering a service when a local peer requires notification fails with `DMLERR_DLL_USAGE`. This does not provide DDE exchange between applications or host processes.

The original SOMDD executable can be run separately with the same private runtime,
repositories and `TWOINE_IPC_DIR` as Lotus. PMWP does not start it automatically.
Initialization reaches the workbook input and save paths. Native and Unicorn labels, formula recalculation, fonts, save, cold reopen
and further editing are verified; visual and shutdown gates remain unverified.

`string-contracts.json` records original bytewise `lstrcmp` results, including
punctuation and high bytes. `profile-contracts.json` records native private-profile
storage and key normalization; those APIs do not read Windows text INI values.

`directory-contracts.json` records original `GetWindowsDirectory` output and
small-buffer behavior on the isolated Warp reference.

`frame-contracts.json` records the original native frame/DEFCLIENT relationship.
`help-contracts.json` records no-viewer `HELP_QUIT` results. Captioned overlapped
Open32 windows have native frame/client separation; other frame mappings and
complete MDI decorations remain incomplete.
