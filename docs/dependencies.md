<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Dependencies

Original and upstream-derived files retain 2ine's zlib-style license. Wholly
AI-authored files carry a public-domain notice. [Licensing](licensing.md).

**Unicorn is GPLv2.** The adapter's public-domain notice does not relicense the
engine or a linked distribution. Preserve its notices, including QEMU-derived
components, and meet its source/distribution requirements.
[Upstream source and license](https://github.com/unicorn-engine/unicorn/tree/2.1.4).

The portable adapter uses Unicorn 2.1.4+. Source builds in CI pin revision
`8028ec436f2d9376525352dd38ed9ed6b9f6be10`.

SDL **3.4.18** is the pinned stable release. Default builds download its source
archive and verify SHA-256
`9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3`.
Native workers build an independent i386 SDL; portable applications use the host
ABI. Runtime-only builds do not configure or download SDL. Offline and installed
providers are described in [build instructions](build.md).

[Official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18)
· [SDL3 migration guide](https://wiki.libsdl.org/SDL3/README-migration).
SDL keeps its zlib license and ncurses keeps its own notices. No dependency
shared library is checked in. Existing guest-program fixtures are inventoried in
`tests/programs/fixtures/manifest.json`; historical compiler and SDK installations
are supplied separately.

Application profiles also build **FreeType 2.14.3** from the source release,
SHA-256 `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f`.
The native worker builds its own i386 library. Optional compression, bitmap-image
and HarfBuzz dependencies are disabled; this provides outline metrics and
rasterization, without complex-script shaping. Runtime-only profiles omit it.
[FreeType release](https://freetype.org/) · [API reference](https://freetype.org/freetype2/docs/reference/).

The public font catalogue uses **Liberation 2.1.5**, downloaded separately from
its official release with SHA-256
`7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0`.
Unchanged Sans, Serif and Mono faces supply explicit Helv, Tms Rmn and Courier
substitutions, including bold/italic variants. They are not original IBM fonts.
CMake copies the twelve faces and their SIL Open Font License to `build/fonts/`.
Keep that directory beside a relocated worker. `TWOINE_FONT_DIRECTORY` selects
an alternate directory with the same filenames.
[Official font release](https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5).

FreeType has two alternative licenses. Native-only distributions use the
[FreeType License](licenses/freetype-FTL.txt): portions of this software are
copyright © 2026 The FreeType Project (www.freetype.org). All rights reserved.
Portable distributions linked with GPLv2 Unicorn use FreeType's
[GPLv2 option](licenses/freetype-GPLv2.txt), preserving the compatible license
choice for that linked distribution. Keep the applicable notices and source
requirements. Liberation retains its [SIL OFL 1.1](licenses/liberation-OFL.txt)
notice; no font data is relicensed by this project.

MIDI defaults to hash-pinned **FluidR3 GM 3.1** by Frank Wen (MIT),
**FluidSynth 2.4.8** (LGPL-2.1-or-later), **libInstPatch 1.1.7**
(LGPL-2.1-only) and **liquidsfz 0.4.2** (MPL-2.0, including retained MIT pugixml).
The releases and build options are in `cmake/Midi.cmake` and `cmake/MidiBanks.cmake`.
GLib and libsndfile supply matching host/worker ABIs. Optional device drivers,
JACK/LV2 plugins and GUI dependencies of these synthesis libraries are disabled.
TinySoundFont remains the explicit minimal SF2-only fallback. GeneralUser GS
is user-downloadable, rather than bundled.

[Project source and credit catalogue](project-sources.md) records every direct
component, bundled-library family, declared version, source/download location,
license and named origin authors. Packaged builds bind host dependency versions
to the actual inputs in their runtime `project-sources.json` inventory. Both
native About views expose that catalogue. [Bank installation and mapping](sound-banks.md)
describes the shared native GUI and runtime interface.

The optional [Munt backend](midi-mt32.md) pins libmt32emu 2.8.3 in
`cmake/Munt.cmake`, SHA-256
`81f8c462f46bc8901618762ae34cf9de93894ff81f41db73c79472fa3baef875`.
It retains Munt's LGPL-2.1-or-later notices and requires separately supplied
ROM images. Default builds do not fetch or link this optional component.

[Digital-video playback](video.md) pins **FFmpeg 9.0.2**, SHA-256
`8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.
The source profile disables automatic external dependencies and network support
and selects its demuxers/decoders in `cmake/Video.cmake`. It reports LGPL 2.1
or later. The optional system provider retains the linked library's actual
version, license and configuration in `ffmpeg-build.json`; GPL-enabled system
libraries can have a different license. Runtime-only profiles omit FFmpeg.
The [catalogue](project-sources.md) and bundled FFmpeg license texts retain its
upstream credits and notices.

OS/2 media import uses vendored **HWZIP 2.4** by Hans Wennborg for legacy ZIP
methods 1–6 (Shrink, Reduce and Implode), alongside libarchive's other formats.
The unchanged codec sources retain their public-domain dedication and complete
[Unlicense text](../storage/vendor/hwzip/LICENSE). The original
[source archive](https://www.hanshq.net/files/hwzip/hwzip-2.4.zip) SHA-256 is
`5a3fecfaf362972673d40ca0611622cfa94170c70029fbd8c9aa64a9e7d387ac`.
Only individual selected members are decoded; legacy compressed members have a
16 MiB input/output bound, and size, header bounds and CRC-32 are checked.
