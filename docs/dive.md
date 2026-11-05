<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# DIVE

The native DIVE module implements the Toolkit's public ordinals 1–19, including
checked allocated/associated image buffers, palettes, blitter setup, partial-line
and transparent blits, and a linear software framebuffer. Fullscreen init/term
are notifications; external VMAN hardware mode switching is not provided.

Capabilities enumerate the formats actually accepted. Input formats are LUT8,
LT12, MONO, VGA, R565, R555, R664, RGB3, BGR3, RGB4, BGR4, Y888, Y2X2, Y4X4,
YUV9, Y644, Y422, Y42B, Y42D and Y411. Output formats are the indexed, monochrome,
VGA and packed RGB formats in that list. YUV output, GREY and GY16 are not
advertised. The screen uses BGR4. Conversion and nearest-neighbor scaling run on
the CPU; direct-color to LUT8 conversion accepts 2x2 ordered dithering.

Non-screen instances convert between allocated or associated buffers without a
PM window. Screen instances submit copied frames to their captured PM owner;
only that owner draws retained host surfaces. Transparency and unchanged source
lines preserve the destination. Pending partial updates coalesce without erasing
earlier changes. Setup accepts the Toolkit's 8/28/32/52/60/68-byte guest records;
short updates retain previously established attributes. The original IBMVID
7/8/4 palette supplies default indexed colors. Physical-palette requests query
the software PM display palette.

Screen instances expose checked guest mappings for a top-down BGR4 framebuffer.
Acquisition snapshots the requested PM window rectangle; deacquisition submits
its changed pixels. Acquisition is exclusive across instances. Calculated
addresses, scanline counts and buffer-access pointers retain guest width.
This is a software display aperture, not access to host VRAM. Bank switching
returns `DIVE_ERR_NOT_BANK_SWITCHED`, matching `fBankSwitched = FALSE`.
An alternate/overlay plane is unavailable. Framebuffer acquisition requires a
live PM destination owned by the calling thread; arbitrary host desktop capture
and complete desktop occlusion reconstruction are not implemented.

Dimensions are bounded to 16,384 pixels per axis, image storage to 256 MiB, and
one converted blit to 16,777,216 pixels. Associated planar buffers include all
chroma planes in their checked guest-memory lease. Instances own all allocations
and release their mappings and associations on close or module shutdown.

`dive_formats` checks packed RGB/indexed round trips, YUV decoding and storage
extents. `dive_video` checks PM pixels, worker submission, transparent/partial
updates, framebuffer acquisition and offscreen conversion. `dive_guest_apis`
executes generated LX callers with real guest pointers and ordinals, short setup
records, offscreen conversion, transparency and invalid-pointer rejection.
