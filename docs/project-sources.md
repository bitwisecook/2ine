<!-- Public domain. No rights reserved. Catalogue text only; component licenses remain unchanged. -->

# Project sources, versions and credits

**2ine was created by [Ryan C. Gordon (icculus)](https://icculus.org/).**
This fork builds on Ryan’s original OS/2 compatibility layer. His copyright
and zlib-style license are retained. Fork extensions do not change the
ownership or licenses of upstream code.

This index covers incorporated code, bundled assets, optional backends,
platform dependencies and direct build tools. Small projects’ authors are
named; larger projects retain their contributor lists in the linked source.
Host versions below are the Ubuntu 26.04 reference build, not universal
pins. Each packaged build carries its actual dependency inventory in
`project-sources.json` alongside the runtime and exposes it in About.
OS graphics drivers and desktop services remain distribution components.

**Source availability:** links identify upstream source and downloads.
They do not replace a release’s complete corresponding-source distribution
for GPL/LGPL components, including distribution patches and build scripts.
See [licensing](licensing.md). Original OS/2/Lotus media and MT-32 ROMs
are user-supplied and are not licensed or redistributed by this project.

## 2ine

- Version: 0.1; original upstream lineage 2016–2022
- License: Zlib; public-domain additions
- Used as: origin
- Authors / origin credits: Ryan C. Gordon (original author); 2ine contributors (fork extensions)
- [Project](https://github.com/icculus/2ine) · [Source](https://github.com/icculus/2ine) · [Download](https://github.com/bitwisecook/2ine/archive/refs/heads/main.tar.gz)

## Unicorn Engine

- Version: 2.1.4
- License: GPL-2.0-only and retained QEMU component licenses
- Used as: runtime
- Authors / origin credits: Nguyen Anh Quynh; Dang Hoang Vu; Unicorn contributors
- [Project](https://github.com/unicorn-engine/unicorn) · [Source](https://github.com/unicorn-engine/unicorn/tree/2.1.4) · [Download](https://github.com/unicorn-engine/unicorn/archive/refs/tags/2.1.4.tar.gz)

## QEMU components in Unicorn

- Version: snapshot in Unicorn 2.1.4
- License: GPL-2.0-or-later; LGPL-2.0-or-later; per-file permissive terms
- Used as: runtime
- Authors / origin credits: Fabrice Bellard; QEMU contributors
- [Project](https://www.qemu.org/) · [Source](https://github.com/unicorn-engine/unicorn/tree/2.1.4/qemu) · [Download](https://github.com/unicorn-engine/unicorn/archive/refs/tags/2.1.4.tar.gz)

## SoftFloat components in Unicorn/QEMU

- Version: snapshot in Unicorn 2.1.4
- License: Retained per-file SoftFloat notices
- Used as: runtime
- Authors / origin credits: John R. Hauser; QEMU contributors
- [Project](http://www.jhauser.us/arithmetic/SoftFloat.html) · [Source](https://github.com/unicorn-engine/unicorn/tree/2.1.4/qemu/fpu) · [Download](https://github.com/unicorn-engine/unicorn/archive/refs/tags/2.1.4.tar.gz)

## Simple DirectMedia Layer

- Version: 3.4.18
- License: Zlib
- Used as: runtime
- Authors / origin credits: Sam Lantinga; SDL contributors
- [Project](https://github.com/libsdl-org/SDL) · [Source](https://github.com/libsdl-org/SDL/tree/release-3.4.18) · [Download](https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.tar.gz)

## FreeType

- Version: 2.14.3
- License: GPL-2.0-only option for Unicorn-linked builds; FTL alternative for independent builds
- Used as: runtime
- Authors / origin credits: David Turner; Robert Wilhelm; Werner Lemberg; FreeType contributors
- [Project](https://freetype.org/) · [Source](https://gitlab.freedesktop.org/freetype/freetype/-/tree/VER-2-14-3) · [Download](https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz)

## Liberation Fonts

- Version: 2.1.5
- License: OFL-1.1
- Used as: runtime
- Authors / origin credits: Steve Matteson; Ascender Corporation; Google; Red Hat; Liberation contributors
- [Project](https://github.com/liberationfonts/liberation-fonts) · [Source](https://github.com/liberationfonts/liberation-fonts/tree/2.1.5) · [Download](https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz)

## TinySoundFont (minimal fallback)

- Version: 853a0a171759f1ddba0de1442133a75912bbeffa
- License: MIT
- Used as: optional
- Authors / origin credits: Bernhard Schelling; Steve Folta (SFZero origin)
- [Project](https://github.com/schellingb/TinySoundFont) · [Source](https://github.com/schellingb/TinySoundFont/tree/853a0a171759f1ddba0de1442133a75912bbeffa) · [Download](https://github.com/schellingb/TinySoundFont/archive/853a0a171759f1ddba0de1442133a75912bbeffa.tar.gz)

## FluidR3 GM

- Version: 3.1 / original Release 3
- License: MIT
- Used as: asset
- Authors / origin credits: Frank Wen; Suren M. Seron; Scott Hanan; Steve Aupperle; Chris Gillman; Alex Taubr; Chris Prola; Andrew Klenk; Winfried Hubbe; Dylan; Tim; Gort; Uros Katic; Ethan Winer
- [Project](https://github.com/pianobooster/fluid-soundfont) · [Source](https://github.com/pianobooster/fluid-soundfont/tree/v3.1) · [Download](https://github.com/pianobooster/fluid-soundfont/releases/download/v3.1/FluidR3_GM.sf2)
- Sha256: 74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0
- Provenance: Frank Wen states that Fluid uses his own/collaborators’ recordings and edited public-domain samples. The unchanged release is distributed through Ubuntu and PianoBooster. This records the author’s published provenance, rather than claiming an independent rights audit of each sample.
- [License text](https://github.com/pianobooster/fluid-soundfont/blob/v3.1/COPYING)
- [Full credits](https://github.com/pianobooster/fluid-soundfont/blob/v3.1/README)

## FluidSynth

- Version: 2.4.8
- License: LGPL-2.1-or-later
- Used as: runtime
- Authors / origin credits: Peter Hanappe; Tom Moebert; Josh Green; Pedro Lopez-Cabanillas; David Henningsson; FluidSynth contributors (see AUTHORS)
- [Project](https://github.com/FluidSynth/fluidsynth) · [Source](https://github.com/FluidSynth/fluidsynth/tree/v2.4.8) · [Download](https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.4.8.tar.gz)
- Notes: Uses libInstPatch for DLS/GIG. The native DLS parser introduced in 2.5 is not used. Audio/MIDI device drivers and optional effects are disabled; the 2ine mixer consumes PCM.
- [Full credits](https://github.com/FluidSynth/fluidsynth/blob/v2.4.8/AUTHORS)

## libInstPatch

- Version: 1.1.7
- License: LGPL-2.1-only
- Used as: runtime
- Authors / origin credits: Element Green; BALATON Zoltan; Ebrahim Mayat; Keishi Suenaga
- [Project](https://github.com/swami/libinstpatch) · [Source](https://github.com/swami/libinstpatch/tree/v1.1.7) · [Download](https://github.com/swami/libinstpatch/archive/refs/tags/v1.1.7.tar.gz)
- [Full credits](https://github.com/swami/libinstpatch/blob/v1.1.7/AUTHORS)

## liquidsfz

- Version: 0.4.2
- License: MPL-2.0 with secondary-license compatibility
- Used as: runtime
- Authors / origin credits: Stefan Westerfeld; liquidsfz contributors
- [Project](https://github.com/swesterfeld/liquidsfz) · [Source](https://github.com/swesterfeld/liquidsfz/tree/0.4.2) · [Download](https://github.com/swesterfeld/liquidsfz/releases/download/0.4.2/liquidsfz-0.4.2.tar.bz2)

## pugixml (in liquidsfz)

- Version: 1.10
- License: MIT
- Used as: runtime
- Authors / origin credits: Arseny Kapoulkine; Kristen Wegner (pugxml origin)
- [Project](https://pugixml.org/) · [Source](https://github.com/swesterfeld/liquidsfz/blob/0.4.2/lib/pugixml.cc) · [Download](https://github.com/swesterfeld/liquidsfz/releases/download/0.4.2/liquidsfz-0.4.2.tar.bz2)

## Pcg32Rng (in liquidsfz)

- Version: 2014 source in liquidsfz 0.4.2
- License: MPL-2.0
- Used as: runtime
- Authors / origin credits: Tim Janik
- [Project](https://github.com/swesterfeld/liquidsfz) · [Source](https://github.com/swesterfeld/liquidsfz/blob/0.4.2/lib/pcg32rng.hh) · [Download](https://github.com/swesterfeld/liquidsfz/releases/download/0.4.2/liquidsfz-0.4.2.tar.bz2)

## Munt / libmt32emu

- Version: 2.8.3
- License: LGPL-2.1-or-later
- Used as: optional
- Authors / origin credits: Jerome Fisher; Sergiy Danychenko; Munt contributors
- [Project](https://github.com/munt/munt) · [Source](https://github.com/munt/munt/tree/libmt32emu_2_8_3) · [Download](https://github.com/munt/munt/archive/refs/tags/libmt32emu_2_8_3.tar.gz)

## cJSON

- Version: 1.7.19
- License: MIT
- Used as: runtime
- Authors / origin credits: Dave Gamble; cJSON contributors
- [Project](https://github.com/DaveGamble/cJSON) · [Source](https://github.com/DaveGamble/cJSON/tree/v1.7.19) · [Download](https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz)

## FatFs

- Version: R0.16 (80386)
- License: FatFs permissive license
- Used as: runtime
- Authors / origin credits: ChaN
- [Project](https://elm-chan.org/fsw/ff/) · [Source](https://elm-chan.org/fsw/ff/arc/ff16.zip) · [Download](https://elm-chan.org/fsw/ff/arc/ff16.zip)

## crypto-algorithms SHA-256

- Version: unversioned vendored snapshot (see file hash)
- License: Public domain
- Used as: runtime
- Authors / origin credits: Brad Conte
- [Project](https://github.com/B-Con/crypto-algorithms) · [Source](https://github.com/B-Con/crypto-algorithms) · [Download](https://github.com/B-Con/crypto-algorithms/archive/refs/heads/master.tar.gz)
- Sha256: 4cbc93d30c28cee2a056d6f52cbce6e66ff9450f1d712ee4f59761b9b4486fad

## Deark PACK decoders

- Version: c19b47c80a5ff8a7b41eb4f578e1afdda99202a2
- License: MIT; retained public-domain dskdcmps component
- Used as: runtime
- Authors / origin credits: Jason Summers; Dimitriy Ryazantcev (FTCOMP implementation); anonymous dskdcmps author
- [Project](https://entropymine.com/deark/) · [Source](https://github.com/jsummers/deark/tree/c19b47c80a5ff8a7b41eb4f578e1afdda99202a2) · [Download](https://github.com/jsummers/deark/archive/c19b47c80a5ff8a7b41eb4f578e1afdda99202a2.tar.gz)

## libarchive API headers

- Version: 3.7.4
- License: BSD-2-Clause and per-file notices
- Used as: runtime
- Authors / origin credits: Tim Kientzle; libarchive contributors
- [Project](https://www.libarchive.org/) · [Source](https://github.com/libarchive/libarchive/tree/v3.7.4) · [Download](https://github.com/libarchive/libarchive/releases/download/v3.7.4/libarchive-3.7.4.tar.xz)

## libarchive

- Version: 3.8.5-1ubuntu2.3 (host selected)
- License: BSD-2-Clause and per-file notices
- Used as: host dependency
- Authors / origin credits: Tim Kientzle; libarchive contributors
- [Project](https://www.libarchive.org/) · [Source](https://github.com/libarchive/libarchive) · [Download](https://launchpad.net/ubuntu/+source/libarchive/3.8.5-1ubuntu2.3)

## Nettle

- Version: 3.10.2-1 (host selected)
- License: GPL-2.0-or-later option (alternative LGPL-3.0-or-later)
- Used as: host dependency
- Authors / origin credits: Niels Möller; Nettle contributors
- [Project](https://www.lysator.liu.se/~nisse/nettle/) · [Source](https://launchpad.net/ubuntu/+source/nettle/3.10.2-1) · [Download](https://launchpad.net/ubuntu/+source/nettle/3.10.2-1)

## libacl

- Version: 2.3.2-2 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: Andreas Gruenbacher; Silicon Graphics; ACL contributors
- [Project](https://savannah.nongnu.org/projects/acl/) · [Source](https://git.savannah.nongnu.org/cgit/acl.git) · [Download](https://launchpad.net/ubuntu/+source/acl/2.3.2-2)

## Zstandard

- Version: 1.5.7+dfsg-3 (host selected)
- License: BSD-3-Clause option
- Used as: host dependency
- Authors / origin credits: Yann Collet; Zstandard contributors
- [Project](https://github.com/facebook/zstd) · [Source](https://github.com/facebook/zstd) · [Download](https://launchpad.net/ubuntu/+source/libzstd/1.5.7+dfsg-3)

## LZ4

- Version: 1.10.0-8 (host selected)
- License: BSD-2-Clause (library)
- Used as: host dependency
- Authors / origin credits: Yann Collet; LZ4 contributors
- [Project](https://lz4.org/) · [Source](https://github.com/lz4/lz4) · [Download](https://launchpad.net/ubuntu/+source/lz4/1.10.0-8)

## bzip2

- Version: 1.0.8-6ubuntu0.1 (host selected)
- License: bzip2-1.0.6
- Used as: host dependency
- Authors / origin credits: Julian Seward; Mark Wielaard; Federico Mena Quintero
- [Project](https://sourceware.org/bzip2/) · [Source](https://sourceware.org/git/bzip2.git) · [Download](https://launchpad.net/ubuntu/+source/bzip2/1.0.8-6ubuntu0.1)

## zlib

- Version: 1:1.3.dfsg+really1.3.1-1ubuntu3.2 (host selected)
- License: Zlib
- Used as: host dependency
- Authors / origin credits: Jean-loup Gailly; Mark Adler
- [Project](https://zlib.net/) · [Source](https://github.com/madler/zlib) · [Download](https://launchpad.net/ubuntu/+source/zlib/1:1.3.dfsg+really1.3.1-1ubuntu3.2)

## XZ Utils / liblzma

- Version: 5.8.3-1ubuntu0.1 (host selected)
- License: 0BSD and per-file permissive terms
- Used as: host dependency
- Authors / origin credits: Lasse Collin; Igor Pavlov; XZ contributors
- [Project](https://tukaani.org/xz/) · [Source](https://github.com/tukaani-project/xz) · [Download](https://launchpad.net/ubuntu/+source/xz-utils/5.8.3-1ubuntu0.1)

## libxml2

- Version: 2.15.2+dfsg-0.1ubuntu0.3 (host selected)
- License: MIT and ISC
- Used as: host dependency
- Authors / origin credits: Daniel Veillard; libxml2 contributors
- [Project](https://gitlab.gnome.org/GNOME/libxml2) · [Source](https://gitlab.gnome.org/GNOME/libxml2) · [Download](https://launchpad.net/ubuntu/+source/libxml2/2.15.2+dfsg-0.1ubuntu0.3)

## xxHash

- Version: 0.8.3-2build1 (host selected)
- License: BSD-2-Clause (library)
- Used as: host dependency
- Authors / origin credits: Yann Collet
- [Project](https://xxhash.com/) · [Source](https://github.com/Cyan4973/xxHash) · [Download](https://launchpad.net/ubuntu/+source/xxhash/0.8.3-2build1)

## ncurses / form / tinfo

- Version: 6.6+20251231-1 (host selected)
- License: X11-style permissive license
- Used as: host dependency
- Authors / origin credits: Thomas E. Dickey; Zeyd M. Ben-Halim; Eric S. Raymond; ncurses contributors
- [Project](https://invisible-island.net/ncurses/) · [Source](https://invisible-island.net/ncurses/) · [Download](https://launchpad.net/ubuntu/+source/ncurses/6.6+20251231-1)

## GLib / GObject / GIO

- Version: 2.88.0-1ubuntu0.1 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: GLib contributors
- [Project](https://gitlab.gnome.org/GNOME/glib) · [Source](https://gitlab.gnome.org/GNOME/glib) · [Download](https://launchpad.net/ubuntu/+source/glib2.0/2.88.0-1ubuntu0.1)

## PCRE2

- Version: 10.46-1build1 (host selected)
- License: BSD-3-Clause
- Used as: host dependency
- Authors / origin credits: Philip Hazel; Zoltan Herczeg; PCRE2 contributors
- [Project](https://www.pcre.org/) · [Source](https://github.com/PCRE2Project/pcre2) · [Download](https://launchpad.net/ubuntu/+source/pcre2/10.46-1build1)

## libffi

- Version: 3.5.2-4 (host selected)
- License: MIT
- Used as: host dependency
- Authors / origin credits: Anthony Green; libffi contributors
- [Project](https://sourceware.org/libffi/) · [Source](https://github.com/libffi/libffi) · [Download](https://launchpad.net/ubuntu/+source/libffi/3.5.2-4)

## libsndfile

- Version: 1.2.2-4 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: Erik de Castro Lopo; libsndfile contributors
- [Project](https://libsndfile.github.io/libsndfile/) · [Source](https://github.com/libsndfile/libsndfile) · [Download](https://launchpad.net/ubuntu/+source/libsndfile/1.2.2-4)

## FLAC

- Version: 1.5.0+ds-5 (host selected)
- License: BSD-3-Clause (library)
- Used as: host dependency
- Authors / origin credits: Josh Coalson; Xiph.Org Foundation; FLAC contributors
- [Project](https://xiph.org/flac/) · [Source](https://github.com/xiph/flac) · [Download](https://launchpad.net/ubuntu/+source/flac/1.5.0+ds-5)

## libogg

- Version: 1.3.6-2 (host selected)
- License: BSD-3-Clause
- Used as: host dependency
- Authors / origin credits: Xiph.Org Foundation; libogg contributors
- [Project](https://xiph.org/ogg/) · [Source](https://github.com/xiph/ogg) · [Download](https://launchpad.net/ubuntu/+source/libogg/1.3.6-2)

## libvorbis

- Version: 1.3.7-3build2 (host selected)
- License: BSD-3-Clause
- Used as: host dependency
- Authors / origin credits: Xiph.Org Foundation; libvorbis contributors
- [Project](https://xiph.org/vorbis/) · [Source](https://github.com/xiph/vorbis) · [Download](https://launchpad.net/ubuntu/+source/libvorbis/1.3.7-3build2)

## Opus

- Version: 1.6.1-1 (host selected)
- License: BSD-3-Clause and retained notices
- Used as: host dependency
- Authors / origin credits: Xiph.Org Foundation; Opus contributors
- [Project](https://opus-codec.org/) · [Source](https://github.com/xiph/opus) · [Download](https://launchpad.net/ubuntu/+source/opus/1.6.1-1)

## mpg123

- Version: 1.33.3-2 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: Michael Hipp; Thomas Orgis; mpg123 contributors
- [Project](https://www.mpg123.de/) · [Source](https://www.mpg123.de/) · [Download](https://launchpad.net/ubuntu/+source/mpg123/1.33.3-2)

## LAME

- Version: 3.101~svn6525+dfsg-2ubuntu0.1 (host selected)
- License: LGPL-2.0-or-later
- Used as: host dependency
- Authors / origin credits: Mike Cheng; Mark Taylor; LAME contributors
- [Project](https://lame.sourceforge.io/) · [Source](https://sourceforge.net/projects/lame/) · [Download](https://launchpad.net/ubuntu/+source/lame/3.101~svn6525+dfsg-2ubuntu0.1)

## Python

- Version: 3.14.4-1ubuntu0.2 (host selected)
- License: PSF-2.0 and retained notices
- Used as: host dependency
- Authors / origin credits: Guido van Rossum; Python Software Foundation; CPython contributors
- [Project](https://www.python.org/) · [Source](https://github.com/python/cpython) · [Download](https://launchpad.net/ubuntu/+source/python3.14/3.14.4-1ubuntu0.2)

## PyGObject

- Version: 3.56.2-1 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: James Henstridge; Johan Dahlin; PyGObject contributors
- [Project](https://pygobject.gnome.org/) · [Source](https://gitlab.gnome.org/GNOME/pygobject) · [Download](https://launchpad.net/ubuntu/+source/pygobject/3.56.2-1)

## GTK

- Version: 4.22.4+ds-0ubuntu0.1 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: GTK contributors
- [Project](https://www.gtk.org/) · [Source](https://gitlab.gnome.org/GNOME/gtk) · [Download](https://launchpad.net/ubuntu/+source/gtk4/4.22.4+ds-0ubuntu0.1)

## libadwaita

- Version: 1.9.1-0ubuntu0.2 (host selected)
- License: LGPL-2.1-or-later
- Used as: host dependency
- Authors / origin credits: Alexander Mikhaylenko; Adrien Plazas; GNOME contributors
- [Project](https://gnome.pages.gitlab.gnome.org/libadwaita/) · [Source](https://gitlab.gnome.org/GNOME/libadwaita) · [Download](https://launchpad.net/ubuntu/+source/libadwaita-1/1.9.1-0ubuntu0.2)

## Mesa (including Zink/KosmicKrisp)

- Version: 26.2.6
- License: MIT and per-file notices
- Used as: platform
- Authors / origin credits: Mesa contributors
- [Project](https://www.mesa3d.org/) · [Source](https://www.mesa3d.org/) · [Download](https://archive.mesa3d.org/mesa-26.2.6.tar.xz)

## Vulkan Loader

- Version: installed version; captured in Mac package inventory (host selected)
- License: Apache-2.0 (majority); retained per-file permissive exceptions
- Used as: platform
- Authors / origin credits: Khronos Group; Vulkan Loader contributors
- [Project](https://github.com/KhronosGroup/Vulkan-Loader) · [Source](https://github.com/KhronosGroup/Vulkan-Loader) · [Download](https://github.com/KhronosGroup/Vulkan-Loader/releases)

## Swift compiler and runtime

- Version: 6.4 required; actual Mac compiler is build-selected (host selected)
- License: Apache-2.0 WITH Swift runtime library exception
- Used as: platform
- Authors / origin credits: Apple; Swift contributors
- [Project](https://www.swift.org/) · [Source](https://github.com/swiftlang/swift) · [Download](https://www.swift.org/download/)

## GNU C Library (host)

- Version: 2.43 (host selected)
- License: LGPL-2.1-or-later and per-file terms
- Used as: platform
- Authors / origin credits: GNU C Library contributors
- [Project](https://www.gnu.org/software/libc/) · [Source](https://www.gnu.org/software/libc/) · [Download](https://ftp.gnu.org/gnu/glibc/)

## GCC runtime libraries (host)

- Version: 15.2.0 (host selected)
- License: GPL-3.0-or-later WITH GCC-exception-3.1
- Used as: platform
- Authors / origin credits: GCC contributors
- [Project](https://gcc.gnu.org/) · [Source](https://gcc.gnu.org/) · [Download](https://ftp.gnu.org/gnu/gcc/)

## LLVM/Clang runtime (host)

- Version: 20.1.8 (host selected)
- License: Apache-2.0 WITH LLVM-exception
- Used as: platform
- Authors / origin credits: LLVM contributors
- [Project](https://llvm.org/) · [Source](https://llvm.org/) · [Download](https://github.com/llvm/llvm-project/releases)

## CMake

- Version: 4.2.3 (minimum 3.24) (host selected)
- License: BSD-3-Clause
- Used as: build tool
- Authors / origin credits: Kitware; CMake contributors
- [Project](https://cmake.org/) · [Source](https://cmake.org/) · [Download](https://cmake.org/)

## Ninja

- Version: 1.13.2 (host selected)
- License: Apache-2.0
- Used as: build tool
- Authors / origin credits: Evan Martin; Ninja contributors
- [Project](https://github.com/ninja-build/ninja) · [Source](https://github.com/ninja-build/ninja) · [Download](https://github.com/ninja-build/ninja/releases)

## GNU Make

- Version: 4.4.1 (host selected)
- License: GPL-3.0-or-later
- Used as: build tool
- Authors / origin credits: Richard Stallman; Roland McGrath; Paul D. Smith; GNU Make contributors
- [Project](https://www.gnu.org/software/make/) · [Source](https://www.gnu.org/software/make/) · [Download](https://www.gnu.org/software/make/)

## GNU Autoconf / Automake / Libtool

- Version: Autoconf 2.72; Automake 1.18.1; release-generated Libtool scripts (host selected)
- License: GPL with applicable output exceptions
- Used as: build tool
- Authors / origin credits: GNU build-system contributors
- [Project](https://www.gnu.org/software/autoconf/) · [Source](https://www.gnu.org/software/autoconf/) · [Download](https://www.gnu.org/software/autoconf/)

## Snapcraft / GNOME extension

- Version: 9.2; core26; gnome-50-2604 (host selected)
- License: GPL-3.0 and per-component content-snap terms
- Used as: build tool
- Authors / origin credits: Canonical; GNOME contributors
- [Project](https://github.com/canonical/snapcraft) · [Source](https://github.com/canonical/snapcraft) · [Download](https://github.com/canonical/snapcraft/releases)

## Axiom development guidance

- Version: 58ae15ac93b1fa59db628f634a6c257e86e794b2 (host selected)
- License: MIT
- Used as: build tool
- Authors / origin credits: Charles Wiltgen; Axiom contributors
- [Project](https://github.com/CharlesWiltgen/Axiom) · [Source](https://github.com/CharlesWiltgen/Axiom/tree/58ae15ac93b1fa59db628f634a6c257e86e794b2) · [Download](https://github.com/CharlesWiltgen/Axiom/archive/58ae15ac93b1fa59db628f634a6c257e86e794b2.tar.gz)

## Apple Icon Composer skill

- Version: 43b96bf200e088149abef29a1b999c70589bed81 (host selected)
- License: Apache-2.0 (development-time guidance; not linked into the runtime)
- Used as: build tool
- Authors / origin credits: Kohki Miki (giginet)
- [Project](https://github.com/giginet/apple-icon-composer-skill) · [Source](https://github.com/giginet/apple-icon-composer-skill/tree/43b96bf200e088149abef29a1b999c70589bed81) · [Download](https://github.com/giginet/apple-icon-composer-skill/archive/43b96bf200e088149abef29a1b999c70589bed81.tar.gz)

## GeneralUser GS

- Version: 2.0.3
- License: GeneralUser GS License v2.0
- Used as: optional user download; not bundled
- Authors / origin credits: S. Christian Collins
- [Project](https://www.schristiancollins.com/generaluser.php) · [Source](https://github.com/mrbumpy409/GeneralUser-GS/tree/684543d5e5efaef08d02be50dcda8d552478fa60) · [Download](https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d5e5efaef08d02be50dcda8d552478fa60/GeneralUser-GS.sf2)
- Sha256: 9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
- Notes: Optional user-selected General MIDI/Roland GS bank. The author discloses incomplete sample-origin records; it is not bundled.

## TimGM6mb

- Version: musescore-6bf0564e
- License: GPL-2.0-only
- Used as: optional user download; not bundled
- Authors / origin credits: Tim Brechbill; MuseScore distribution
- [Project](https://www.timbrechbill.com/saxguru/Timidity.php) · [Source](https://github.com/musescore/MuseScore/tree/6bf0564e3fa76d5e1d7dd7d96f49124fef1074a8/share/sound) · [Download](https://raw.githubusercontent.com/musescore/MuseScore/6bf0564e3fa76d5e1d7dd7d96f49124fef1074a8/share/sound/TimGM6mb.sf2)
- Sha256: c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854
- Notes: Small complete General MIDI bank retained by the official historical MuseScore source. A neutral low-memory alternative.

## HIDAPI (in SDL3)

- Version: source snapshot in SDL 3.4.18
- License: BSD-3-Clause option
- Used as: SDL3 incorporated component
- Authors / origin credits: Alan Ott; Signal 11 Software; HIDAPI contributors
- [Project](https://github.com/libusb/hidapi) · [Source](https://github.com/libsdl-org/SDL/tree/release-3.4.18/src/hidapi) · [Download](https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.tar.gz)

## SwiftUI / AppKit / macOS SDK

- Version: macOS 27+ target; exact SDK is build-selected (host selected)
- License: Apple SDK terms; implementation source is not published
- Used as: OS-provided frameworks; not redistributed
- Authors / origin credits: Apple
- [Project](https://developer.apple.com/) · [Source](https://developer.apple.com/documentation/swiftui) · [Download](https://developer.apple.com/xcode/)
- Notes: The source link is API documentation, not implementation source. Apple frameworks come from the operating system/SDK and are not included in our source archives.

## Git

- Version: 1:2.53.0-1ubuntu1.1 (host selected)
- License: GPL-2.0-only
- Used as: build/packaging tool; not bundled
- Authors / origin credits: Linus Torvalds; Junio C Hamano; Git contributors
- [Project](https://git-scm.com/) · [Source](https://launchpad.net/ubuntu/+source/git/1:2.53.0-1ubuntu1.1) · [Download](https://launchpad.net/ubuntu/+source/git/1:2.53.0-1ubuntu1.1)

## pkgconf / pkg-config

- Version: 2.5.1-4 (host selected)
- License: ISC
- Used as: build/packaging tool; not bundled
- Authors / origin credits: Ariadne Conill; pkgconf contributors
- [Project](https://github.com/pkgconf/pkgconf) · [Source](https://launchpad.net/ubuntu/+source/pkgconf/2.5.1-4) · [Download](https://launchpad.net/ubuntu/+source/pkgconf/2.5.1-4)

## dpkg

- Version: 1.23.7ubuntu1 (host selected)
- License: GPL-2.0-or-later and retained notices
- Used as: build/packaging tool; not bundled
- Authors / origin credits: Ian Jackson; Ian Murdock; Guillem Jover; dpkg contributors
- [Project](https://www.dpkg.org/) · [Source](https://launchpad.net/ubuntu/+source/dpkg/1.23.7ubuntu1) · [Download](https://launchpad.net/ubuntu/+source/dpkg/1.23.7ubuntu1)

## RPM

- Version: 6.0.1-1build1 (host selected)
- License: GPL-2.0-or-later; LGPL-2.0-or-later and BSD-3-Clause components
- Used as: build/packaging tool; not bundled
- Authors / origin credits: Erik Troan; Marc Ewing; RPM contributors
- [Project](https://rpm.org/) · [Source](https://launchpad.net/ubuntu/+source/rpm/6.0.1-1build1) · [Download](https://launchpad.net/ubuntu/+source/rpm/6.0.1-1build1)

## Ruff

- Version: 0.15.20 (host selected)
- License: MIT
- Used as: development-time validation; not bundled
- Authors / origin credits: Charlie Marsh; Astral and Ruff contributors
- [Project](https://github.com/astral-sh/ruff) · [Source](https://github.com/astral-sh/ruff/tree/0.15.20) · [Download](https://github.com/astral-sh/ruff/archive/refs/tags/0.15.20.tar.gz)

## ty

- Version: 0.0.57 (host selected)
- License: MIT
- Used as: development-time validation; not bundled
- Authors / origin credits: Astral and ty contributors
- [Project](https://github.com/astral-sh/ty) · [Source](https://github.com/astral-sh/ty/tree/0.0.57) · [Download](https://github.com/astral-sh/ty/archive/refs/tags/0.0.57.tar.gz)

## clang-format

- Version: 21.1.8 (host selected)
- License: Apache-2.0 WITH LLVM-exception
- Used as: development-time validation; not bundled
- Authors / origin credits: LLVM contributors
- [Project](https://github.com/llvm/llvm-project) · [Source](https://github.com/llvm/llvm-project/tree/llvmorg-21.1.8) · [Download](https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-21.1.8.tar.gz)
