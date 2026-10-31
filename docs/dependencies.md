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
