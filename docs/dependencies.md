# Dependency notices

The 2ine source retains its zlib-style license in `LICENSE.txt`. This altered
fork is identified in the README. Optional dependencies keep their own licenses.

The portable adapter was tested with **Unicorn 2.1.4**, revision
`8028ec436f2d9376525352dd38ed9ed6b9f6be10`. CI fetches that exact source revision.
The source archive used locally had SHA-256
`9d2a2003f0307d0fc239af439654564fe6ef17b67f7c5a39b45fad4c69d6aec2`.

Unicorn is distributed under GPL version 2; see its
[README and license](https://github.com/unicorn-engine/unicorn/tree/2.1.4).
It includes QEMU-derived code and component-specific notices in its source.
Linking the optional portable runtime does not make these dependencies subject
to 2ine's zlib license. Preserve the dependency licenses/notices and provide the
corresponding source as required when distributing a linked binary. This fork
does not vendor Unicorn source or publish built binaries.

SDL2 and ncurses remain application dependencies. Their notices and licenses
accompany their respective packages. The repository's pre-existing i386 SDL2
shared library is unchanged by the portability work.

Historical compiler/SDK executables and libraries are separately supplied test
fixtures. They are not added to this repository or included in the published
verification record. A test recipe is not a redistribution license for its inputs.
