# Per-ball graphics and media controls

Persisted `system.opengl` is `mesa|original|off`; `system.video`, `pcm`, `midi`,
`dive` are booleans. Missing OpenGL preference always selects Mesa; missing
boolean keys inherit compiled capabilities. The generated
runtime-capabilities.json adds `features:{mesa,video,pcm,midi,dive}` booleans while
retaining schema1 and worker backend metadata; `defaultOpenGL` is `mesa`. Older workers are inspected for
native modules and FFmpeg metadata; backend portability retains its previous
strict declaration rules.

`runtime-features` takes ball/build and returns:

- Each boolean: requested null/bool, default/effective/available bool, reason.
- OpenGL: requested null/string, default/effective string, available, reason,
  providers map with mesa/original/off availability/reason. Original additionally
  has qualified:false and its reachable guest path when present.

Resolution is read-only. Unavailable explicit choices can be saved for later media
extraction or another host, but prepare-launch fails clearly. Runtime config state
lives in a scoped features service, avoiding another LxLoaderState ABI change.
Config parsing finishes before guest threads start. No success-only disabled stubs.

The loader refuses Off OpenGL/DIVE imports. Mesa skips guest OPENGL lookup;
Original skips native Mesa fallback. Explicit original OPENGL.DLL path loads are
also excluded by Off/Mesa. MCI refuses disabled type12 video, type9 PCM/DART or MIDI
MMIO opens with DEVICE_NOT_READY. Native DIVE caps/open return NO_DIRECT_ACCESS.
PCM-off movies skip audio codec/resampler/ring setup, advertise no audio, and use
the monotonic scheduling path. MIDI synthesis and speaker volume remain independent.

Dependency auditing and extraction respect explicit provider choices, with Mesa
as the unconditional default for old and new balls, even without capability
metadata or when Mesa is compiled out. Original is never selected or restored
automatically. Disabled imports
are listed separately and make the report incomplete; media recovery cannot
silently restore them. Original selection allows OS/2 OPENGL.DLL recovery. Presence
is not a compatibility claim: original rendering remains unqualified.

Validation on macOS ARM64/Unicorn:

- 43 Python tests: configuration, receiving-runtime capability changes, staged
  Original followed by extraction, preserved manifest bytes, provider recovery
  policy, cache environment inheritance and portable runtime/Undo behavior.
- 4/4 CTest checks: digital_video, digital_video_audio, dive_video,
  macos_native_cache. Movie tests include PCM-off full MCI lifecycle, no audio
  reported, independent MIDI, disabled device opens and original sound capture.
- Generated LX runtime_features acceptance: all three media opens disabled,
  Original guest DLL ordinal invoked, Off import refused, Mesa cannot fall back
  to the present original DLL, DIVE Off import refused.
- Ruff/ty check the new Python tests.

Evidence: /tmp/2ine-runtime-feature-policy-tools.log,
/tmp/2ine-runtime-features-ctest.log, /tmp/2ine-runtime-features-final-guest.log.
Linux, Windows, original commercial OpenGL renderers and a compiled Mesa provider
were not exercised by these focused checks. The Mesa bridge has its existing
separate OpenGL acceptance suite.

## Desktop preference controls

The native macOS review confirmed the grouped Graphics & Media layout, automatic
OpenGL choice plus Mesa/Original/Off, and all four feature toggles. PCM Off
persisted; Use Runtime Default removed its override. Selecting unavailable
Original showed the correct reason and could be reset. Root's integrated ball
regression run passes72 checks. These UI observations do not qualify the actual
original renderer.

GNOME now has the same choices in native Adw ComboRow/SwitchRow controls alongside
DPI. Capabilities load asynchronously from runtime-features with the receiving
build; loading does not save or materialize any preference. Reset uses unset,
unavailable selections remain editable for another host, and reasons plus
Original's unqualified status stay visible. Saved choices apply next launch.
Both runtime-support requests and installer audit receipts retain the receiving
build captured for their operation, independent of later runtime selection.

Ruff check/format and Python syntax checks pass for tools/gnome_configuration.py
and the renamed tools/gnome_ball.py. No live GTK acceptance was performed in this
macOS task; Linux runtime, keyboard and visual checks remain outstanding. The
concurrent external singular-name migration was preserved, not initiated by
this UI subtask.

## Mesa default steering

The explicit user preference is now Mesa by default across the native feature
service, runtime-features response, generated capability declaration and
installed-file/media provider policy. A compiled-out Mesa provider remains
available:false/effective:off with a clear reason; requested:null/default:mesa
remain intact. Merely inheriting an unavailable default does not block a program
that never requests OpenGL. An explicit unavailable Mesa choice retains the
existing prepare-launch validation. An installed original OPENGL.DLL does not
change the default and is not a fallback. Original and Off remain explicit.

Bounded acceptance: runtime feature/config/import policy Python tests, real LX
Original/Mesa/Off imports including the absent-preference case, and formatting,
Ruff/ty/boundary checks. Evidence logs use /tmp/2ine-mesa-default-*.

## Portable title-bar preference

`system.window_chrome` accepts `native`, `os2` or `both`; absence follows the
preserved Both default. Runtime configuration is read through
`lxRuntimeConfigureFeature`/`lxRuntimeWindowChrome`, without a GLoaderState ABI
addition. GNOME has a native Adw Title Bars ComboRow beside DPI. Root owns the
matching manifest validation and Swift selector. Embedded document frames retain
guest chrome; top-level WC_MENU popups remain borderless in all modes.

GNOME Ruff check/format and Python compilation pass. No GTK visual or keyboard
acceptance was performed on this macOS host. Runtime/PM acceptance is recorded
in `lotus-visuals.md`; these preferences do not claim original-renderer support.

## Consolidated GNOME Settings

User steering consolidated the former Settings/Configuration pages into one
Settings destination with General, Graphics & Sound, Environment, Printing,
Config Files and Registry tabs. General holds runtime/backend and traces. Graphics & Sound holds
DPI, title bars, OpenGL, video/DIVE, PCM/MIDI, beep volume and the existing sound
bank/channel dialog. Config Files discovers CONFIG.SYS, USER_INI, SYSTEM_INI and
additional INI files through configuration-files. Generic text/profile adapters
use checked configuration-file-read/write; unsupported binary files are readonly.
CONFIG.SYS retains its existing live editor/conflict behavior. Other text drafts
are retained per-ball/file across navigation, checked by revision on save, and
protected against accidental close/export. The selected file survives re-render.
The former show_page("config") internal navigation routes to Config Files.

Environment presents one row per normalized guest variable, joining parsed
CONFIG.SYS source, literal ball override and backend-resolved
snapshot.environmentDefaults. PATH/OS2PATH normalize to the guest OS2PATH; no host
PATH is presented as guest state. Application-specific launch overrides are
explicitly distinguished. The page changes existing ball overrides through the
same native backend and does not edit CONFIG.SYS implicitly.
Live CONFIG.SYS polling also refreshes the backend guest-environment values;
the UI does not re-expand ball overrides or treat its source parser as authority.

Printing reads User/System Profile records without materializing defaults.
PM_SPOOLER/QUEUE is the actual PMSPL authoritative default; USER wins over SYSTEM,
then virtual2INEPDF. Choices PDF/PCL write a semicolon+NUL queue string using the
configuration-write revision guard. Original case of an existing profile record
is retained, unknown/empty stored queues are shown and preserved on read, reset
removes only the user preference, and stale writes display an error instead of
overwriting. The raw user editor is invalidated after a successful change.

Ruff check/format and Python compilation pass for both GNOME modules and updated
GTK integration fixtures. The fixture now covers consolidated navigation,
printer read purity, PCL persistence, unknown preservation and inherited reset.
PyGObject is unavailable on this macOS host, so these new GTK interactions are
not executed or visually accepted here. Existing portable backend tests own the
binary profile and revision-write contracts; Linux GTK acceptance remains open.

The separate Registry tab uses native Gtk.TreeListModel/TreeExpander controls for
HKCR/HKCU/HKLM/HKU paths and filters the values pane to the selected key. Selecting
or adding a value keeps that key selected across reload. Known types0–11 have
named choices; arbitrary UInt32 types retain their numeric code and hexadecimal
payload. Known strings/MULTI_SZ, DWORD endian forms and QWORD have checked payload
requirements. Incomplete headers require explicit replacement before save.
Paths/value names use the runtime's ASCII-only case folding and byte limits;
the registry payload limit is16MiB. No key-delete API or unsupported mutation is
invented. Twenty direct type/data/folding contract cases passed without GTK;
these do not qualify tree widgets, focus, keyboard behavior or Linux rendering.
