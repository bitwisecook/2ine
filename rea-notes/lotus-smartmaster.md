# Lotus 1-2-3 SmartMaster compatibility check

The 2026-11-05 user crash report is an emulator abort while dispatching a queued
Open32 callback. Its matching session log identifies `PMWINX:ordinal #630`,
`SetMetaFileBitsEx`; the missing import explains that particular SIGABRT.
The cached launch check covers stored static imports, not every optional DLL
or computed runtime API lookup.

The maintained runtime now ends missing-import and guest CPU-fault executions
with a logged worker exit status 1, without SIGABRT or reentering guest exit
callbacks. The controlling launcher is a separate process. The regression
executes missing ordinal/name imports and guest null reads both from the entry
point and a nested timer callback, then starts successful OS/2 sessions.
Internal emulator invariants still retain diagnostic aborts.

The Amortize a Loan choice loads `C:\LOTUSW4\SMASTERS\123\loan.12m`.
Its subsequent runtime gaps were text fitting (#1021), viewport origins/extents
(#655/#654), object type (#327), metafile enumeration (#181), nearest colour
(#320), tabbed text extent (#362), cursor position (#262), and window hit
testing (#716). These now have checked guest entry points. Metafile storage
also supports retrieval/deletion; bitmap playback and record playback use the
existing DIB decoder plus mapped, clipped raster transfers. The template's own
five-argument callback processes its mapping and bitmap records.

The actual copied application now opens and renders the amortization sheet,
including inputs, payment figures, payment table and embedded pictures. Editing
the principal updates the displayed calculations. Switching first/second/first/
second tabs leaves both Welcome labels correctly separated. The main SDL window
reports resizable and updates from 640x480 to 900x650. The final host-Quit probe
returns naturally with status 1, without missing imports or a CPU-fault log.

See [verification receipt](lotus-smartmaster-verification.json),
[workbook screenshot](../research/reference_screenshots/123/loan-amortization.png)
and [second-tab screenshot](../research/reference_screenshots/123/welcome-second-tab.png).
Seven focused CTests pass, including complete Open32, PM, execution, handles,
forwarded exports and the new error-exit regression. Generated glue, source
listing, Ruff and diff-whitespace checks pass. These were verification-only
builds with dummy SDL video/audio; physical native resizing and the green
maximise control were not exercised in this pass. No installed app or user ball
was changed.

Earlier shutdown probes reached a guest null-pointer read during teardown.
That underlying compatibility problem remains; such guest faults now retain
their guest-register diagnostic and end with status 1 instead of SIGABRT.
Metafile playback is intentionally limited to the bitmap-record path, with
unsupported vector/enhanced records returning failure. Mapping is implemented
for MM_TEXT/MM_ANISOTROPIC point conversion and DIB transfers; other drawing
primitives reject nonidentity mappings. See [GUI coverage](../docs/gui.md).

API contracts were checked against Microsoft's
[metafile storage](https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-setmetafilebitsex),
[enumeration](https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-enummetafile),
[text fitting](https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-gettextextentexpointa),
and [bitmap transfer](https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-stretchdibits)
references, together with the original OS/2 ordinal catalogue. These references
do not establish complete Open32 equivalence.

Disposable runtime, copied test ball, injection helpers and detailed traces
were removed after verification. Only maintained sources, this small receipt
and two small screenshots are retained. Other active jobs' build trees were
left intact.


## Later native-export batch handoff — 6 November 2026

The 393-export implementation and portable-policy fix are unfinished and have
not been built or tested. Results above describe the earlier SmartMaster pass.
Continue from [the current handoff](2ine/lotus-ordinals/export-batch-handoff.md);
it records remaining ordinals, unintegrated source groups and preserved RE data.
