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

MIDI uses pinned [TinySoundFont](https://github.com/schellingb/TinySoundFont)
(MIT) and [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS)
(its own permissive license). Revisions and four SHA-256 checks are declared in
`cmake/Midi.cmake`. Source/header, bank and license files are fetched separately;
none is stored as a repository binary. CMake stages the bank and both notices
in `build/soundfonts/`. See [audio](audio.md) for offline and runtime options.

The optional [Munt backend](midi-mt32.md) pins libmt32emu 2.8.3 in
`cmake/Munt.cmake`, SHA-256
`81f8c462f46bc8901618762ae34cf9de93894ff81f41db73c79472fa3baef875`.
It retains Munt's LGPL-2.1-or-later notices and requires separately supplied
ROM images. Default builds do not fetch or link this optional component.
