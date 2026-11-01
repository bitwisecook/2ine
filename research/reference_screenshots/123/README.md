# Lotus 1-2-3 stock reference

Original Warp 4.52 and SmartSuite 1.7 with Fixpack 3, captured in an isolated QEMU overlay. See [manifest.json](manifest.json) for media hashes, exact input sequence, workbook observations and screenshot hashes. The original application, workbook and fonts are private fixtures and are not bundled.

These are stock masters. A passing 2ine comparison requires the same create, formula, font, save, reopen and recalculation cycle. Font rasterization and confirmed display DPI differences may be reviewed leniently; missing controls, cells or formatting are defects.


The `lotus-123` compatibility branch can open a blank workbook. Cell entry, workbook persistence and the screenshot comparison are not yet verified. Current contracts include native resource dialogs/icons, pattern and polygon drawing, text alignment, scrollbar controls, worker-to-window posting and PM object windows. `gpi-contracts.json` records focused stock API measurements in addition to the locale and PM contracts.

The original Lotus OLE runtime also requires private Warp SOM runtime DLLs, `SOM.IR`, the installation's `LOTUSW4/ETC/*.IR` interface repositories and the `OS2/ETC/DSOM/SOMD*.DAT`/`SOMD*.TOC` implementation repository. Point `SOMIR` to the semicolon-separated guest paths and `SOMDDIR` to a writable private copy of the DSOM directory; the reference installation sets `SOMDTHREADPEER=1`. These original files are not bundled. Each test session must use separate writable profiles and DSOM registration files.

The current PM provider has no distributed SOM daemon: its readiness query returns false and starting it returns `ERROR_NOT_SUPPORTED`. PM error-state exchange stores and returns the previous error ID; optional extended error records are not retained.

Named OS/2 queue contracts are recorded in `queue-contracts.json`. Named queues use bottle-local host mailboxes between 2ine processes. Same-process writes preserve opaque guest addresses; cross-process records carry copied payloads. Session termination uses a private queue reference so closing the session does not invalidate the application's queue handle.

Mux-wait contracts are recorded in `mux-contracts.json`. Event posts and mutex releases notify blocked ANY/ALL waits. PM wait exports currently delegate to the DOS semaphore waits; synchronous cross-thread PM message pumping is not implemented.

POSIX thread cancellation runs cleanup at host cancellation points; Unicorn also checks between guest execution slices and before API dispatch. Native guest loops that never enter a host cancellation point cannot yet be interrupted. The Windows host adapter reports cancellation as unsupported.
