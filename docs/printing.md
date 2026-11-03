# Printing to PDF and PCL

2ine provides two OS/2 print queues:

- **2INEPDF — Save as PDF** creates a color PDF document.
- **2INEPCL — PCL 5 Printer** submits monochrome PCL 5 data to a compatible
  system printer, or saves the same data as a `.pcl` file.

These devices are part of the runtime and do not need an original OS/2 printer
DLL. The media importer can separately copy original printer packages; copying
one does not make an unsupported guest device driver executable.

PDF uses the host's ordinary save-location and filename dialog. PCL offers a
system-printer destination or Save PCL File. Choose a printer that understands
PCL 5: raw PCL is sent without conversion by another host printer driver.
macOS uses Cocoa dialogs. Linux uses GNOME Zenity dialogs and the CUPS `lp` and
`lpstat` commands; packages install `zenity` and the CUPS client tools; run in a desktop
session. Windows adapters use the common Save/Print dialogs and RAW spool API;
the full Windows runtime is not yet built or qualified.

The filename and location are chosen when a job starts. Cancel leaves existing
files unchanged. File jobs are staged and atomically replace the selected
file after every page is serialized. System-printer jobs are submitted only
when the document ends, and a spooler rejection is reported as a failed print
job. Aborting or closing an unfinished printer DC discards its staged output.

The current devices expose A4 portrait pages at 300 dpi. PM drawing goes through
the same GPI raster implementation as off-screen bitmaps. PDF retains 24-bit
RGB pages with lossless compression; PCL converts those pages to monochrome
with ordered dithering. PDF text is rasterized, not selectable text. The job
limit is 1,000 pages and 1 GiB of serialized spool output. Custom paper sizes,
landscape, duplex and guest-defined print-dialog hooks/templates are currently
unsupported; unsupported explicit DEVMODE settings fail rather than silently
changing the requested layout.

The OS/2 queue interfaces expose packed level 0–6 records and printer-device
queries. Read-only `PM_SPOOLER` profile defaults expose the two devices to
applications using OS/2's INI printer catalogue; stored values take precedence.
`DevOpenDC`, device/form/capability queries and `DevEscape` document/page calls
operate on the native printer targets. Open32's default/common printer records,
printer DCs, and StartDoc/StartPage/EndPage/EndDoc/AbortDoc use the same pipeline.
Restoring saved drawing state does not reset the active print job.
Open32 `PD_RETURNDEFAULT` follows the same per-ball merged USER/SYSTEM queue as
PMSPL, including a selected PCL default. An explicitly empty or unknown stored
queue reports no default printer; it does not silently select PDF. Returned
DEVMODE/DEVNAMES records identify the selected device, and printer contexts
reject a DEVMODE naming another device or an unterminated device-name field.

For explicitly unattended host jobs, `TWOINE_PRINT_OUTPUT` selects a host output
file and `TWOINE_PRINT_PRINTER` selects a system printer for PCL. Empty values
simulate Cancel. `TWOINE_PRINT_DEVICE=pdf` or `pcl` selects the Open32 dialog's
device; an empty value simulates Cancel. Interactive applications do not need
these overrides. Printer names, titles and filenames are passed as literal
arguments or native API data; no shell interprets them. The production POSIX
spooler executable is fixed at `/usr/bin/lp`.

The Snap stages the same dialog and client helpers, declares the restricted
`cups` printing interface and selects its bundled clients with
`TWOINE_CUPS_BINDIR=$SNAP/usr/bin` and the interface socket at
`CUPS_SERVER=/var/cups/cups.sock`. Debian/RPM use `/usr/bin` clients.
The CUPS interface permits job submission and discovery; it does not request
printer administration. Physical printing still requires a configured printer
and CUPS service.
