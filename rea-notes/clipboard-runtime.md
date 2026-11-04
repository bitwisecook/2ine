# OS/2 and host clipboard integration

## Original contracts and source authority

Local OS/2 Toolkit 4.5 inputs:

| Input | SHA-256 |
| --- | --- |
| `sdk/os2tk45-master/h/pmwin.h` | `c8baeb8f9001ded986275a26874b2d1727d3224dd3a92047dd26e59a7b534107` |
| `sdk/os2tk45-master/h/os2win.h` | `c1165cfbe627cd056e651ea165e5064098c89b230200ffc248f991967bb19c95` |
| `samples/pm/clipbrd/clipbrd.c` | `812c666d8db049353b792f5a0c5b71ea301019bc67dcf3b6e65b3ecb6f2e3457` |
| `samples/bidi/arabic/style/sty_edit.c` | `df9a60f8e1888c86284fff6712c8c6f0dfbebd3eed1ecc61ea413eb7036302f8` |

`pmwin.h` defines CF_TEXT=1, CF_BITMAP=2, CFI_OWNERFREE=1,
CFI_OWNERDISPLAY=2, CFI_POINTER=0x400, CFI_HANDLE=0x200 and the
WM_RENDERFMT/WM_RENDERALLFMTS/WM_DESTROYCLIPBOARD messages. Its registered
RTF name is exactly `Rich Text Format`. The original clipboard sample gives
the clipboard a fresh bitmap with flags zero. The bidi editor gives shared
text memory with CFI_POINTER alone: the former implementation incorrectly
required OWNERFREE as well. `tools/data/os2imports.json` supplies the original
Open32 ordinals for Get/SetClipboardData, EnumClipboardFormats,
GetClipboardFormatName and GetClipboardOwner; Toolkit os2win.h supplies their
Windows-style contracts.

Primary host references:

- [SDL_SetClipboardData](https://wiki.libsdl.org/SDL3/SDL_SetClipboardData):
  MIME offers, delayed callbacks, copied MIME names, text byte lengths and the
  main-thread contract.
- [SDL_GetClipboardData](https://wiki.libsdl.org/SDL3/SDL_GetClipboardData):
  returned buffer ownership and explicit lengths.
- [SDL_RunOnMainThread](https://wiki.libsdl.org/SDL3/SDL_RunOnMainThread):
  asynchronous dispatch and the documented deadlock risk of indefinite waits.
- [SDL_SavePNG_IO](https://wiki.libsdl.org/SDL3/SDL_SavePNG_IO): the PNG encoder
  present since SDL 3.4.0. The project pins SDL 3.4.18 and uses its existing
  PNG/BMP codecs without adding a dependency.
- [SDL clipboard implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/SDL_clipboard.c):
  ownership is adopted before calling the backend, including backend failure.
- [Cocoa clipboard implementation](https://github.com/libsdl-org/SDL/blob/main/src/video/cocoa/SDL_cocoaclipboard.m):
  MIME-to-UTI mapping and callbacks using preferred MIME names. Its sole
  `text/plain;charset=utf-8` specialization assumes userdata is a char string;
  a generic provider must use the general path. Offers use `text/plain` and
  callback aliases; RTF prefers `application/rtf` on Cocoa. Linux offers both
  RTF MIME names.
- [Windows clipboard implementation](https://github.com/libsdl-org/SDL/blob/main/src/video/windows/SDL_windowsclipboard.c):
  SDL only natively publishes text/images and treats text MIME types as plain
  Unicode text. The narrow Windows adapter separately handles native
  `Rich Text Format` and `HTML Format`, without publishing HTML as plain text.
- [Microsoft CF_HTML contract](https://learn.microsoft.com/en-us/windows/win32/dataxchg/html-clipboard-format):
  UTF-8 byte offsets and optional missing HTML context.
- [Microsoft SetClipboardData contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata):
  movable memory ownership and delayed rendering.

## Implemented paths

`hal/clipboard.h` contains no platform or guest types. The SDL implementation
owns copied payloads, caches each delayed MIME result, and bounds individual
and aggregate payloads. Worker requests queue through SDL_RunOnMainThread
with a two-second wait. A pending request cancelled by timeout cannot publish
later; reference leases keep timed-out requests alive until their queued
callbacks retire. Every native SDL clipboard call executes on the main thread.
Clipboard events and cleanup callbacks track ownership changes without logging
data. Native clipboard reads occur in response to guest clipboard queries.

The codec converts CP437/850/1004 explicitly, validates UTF-8, normalizes
OS/2 CRLF to host LF and restores CRLF on paste. Unrepresentable characters
become `?`; unsupported codepages fail instead of guessing. PNG/BMP dimensions
and payload extents are checked before decoding. Pixels are top-down RGBA
inside the HAL; the PM adapter reverses rows to/from GPI's bottom-up RGB.
PNG byte formats preserve original alpha and binary bytes. The OS/2 bitmap
representation has no alpha channel.

PM retains format IDs, flags, owner and exact guest data values. Pointer data
is copied through checked execution mappings; imported payloads get guest
mappings rather than truncated host pointers. RTF length parsing respects
embedded NUL bytes in `\\binN` data. CF_HTML parsing validates byte offsets;
plain MIME HTML and registered `HTML Format` are adapted without interpreting
the document. Registered private data keeps its guest representation. Explicit
lengths supplied by Open32 preserve embedded binary NULs in private formats.

Delayed data=0 is accepted for an actual owner. WM_RENDERFMT runs outside the
clipboard mutex and permits the owner's SetClipbrdData while rendering.
WM_DESTROYCLIPBOARD follows replacement. Before an owner window is destroyed,
WM_RENDERALLFMTS and host materialization detach host callbacks from guest
window/module lifetime. Renderer failure returns no data rather than an empty
success payload.

The appended `GLoaderState.freeSharedMemory` hook follows existing DOSCALLS
resource hooks. DOSCALLS installs it after initialization and clears it before
teardown if still owned. PM drains transferred pointer frees outside its mutex
and after guest borrows release. This avoids a PMWIN→DOSCALLS dependency that
would incorrectly initialize DOSCALLS during native PM-only fixtures. Imported
PM mappings use their own mapping lifetime. OWNERFREE keeps allocation cleanup
with the application. DOSCALLS retains retired allocations according to its
existing runtime memory policy.

Open32 registered formats share PM's system atom IDs. Get/SetClipboardData
adapt HGLOBAL ownership and preserve per-format handles while other formats
are added. Format enumeration, format-name lookup and owner queries use the
same PM state. WM_RENDERFORMAT, WM_RENDERALLFORMATS and clipboard destruction
notifications reach the Open32 window callback. Common text, bitmap and
registered formats are supported; unsupported standard formats return errors.

## Verification and boundary

Fresh coherent build: `build-clipboard-current`. It uses the pinned versions
through explicit system SDL/FFmpeg providers and existing offline FreeType,
font, MIDI and Unicorn sources. No proprietary application or OS/2 binaries
are added. No actual ball, live guest or desktop clipboard was changed.

```sh
cmake --build build-clipboard-current -j8
ctest --test-dir build-clipboard-current \
  -R '^(clipboard_hal|clipboard_pm|clipboard_guest|presentation_manager|source_boundaries|open32_apis)$' \
  --output-on-failure
```

The coherent full build succeeds. All six focused tests pass in 7.28 seconds.
Fixtures enforce SDL dummy video before using any clipboard API. They cover
CP437/850/1004, UTF-8 validation, CRLF, embedded-NUL RTF, CF_HTML offsets,
oversized/truncated image rejection, distinct 2×2 corners, PNG/BMP alpha,
bottom-up GPI conversion, unchanged raw PNG, one packet containing text/RTF/
HTML/bitmap, deferred rendering, OWNERFREE, copied-memory cleanup outside
locks/borrows and cancellation of a blocked worker publication.

The generated LX/Open32 test verifies shared registered format IDs, format
names/enumeration, binary RTF, stable HGLOBALs across adding another format,
and transferred handle cleanup on EmptyClipboard. The native memory-hook test
loads/unloads the real DOSCALLS adapter using controlled info-segment storage,
checks clearing of its own hook, and confirms it does not clear a replacement
hook owned by another component.

Unicorn-backed fixtures require the already-authorized unsandboxed test run
on this host; the sandbox initially stops initialization with SIGILL. The
tests still force dummy video/audio and do not access the actual clipboard.
Generator checks, architecture checks, Ruff, ty and formatting checks pass.

Desktop Copy/Paste with a live Mac or Linux application has not been exercised
because the native UI pipe is unavailable. The current proof is controlled
HAL/PM/real-LX contracts plus native adapter source contracts, not a claim of
interactive desktop acceptance. The Windows-specific adapter is prepared in
the HAL design but uncompiled/unqualified here; the overall Windows host is
still unsupported by the build. OS/2 vector metafiles, multimedia clipboard
formats, arbitrary private inter-process formats, other codepages and general
OLE data objects are not converted. Open32 CF_UNICODETEXT/CF_DIB adaptations
are not implemented. Deferred owners on other PM threads remain limited by
the existing PM window-dispatch architecture; bounded HAL dispatch does not
claim to repair that wider thread model.
