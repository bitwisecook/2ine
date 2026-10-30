# 2ine

*What is this?*

It's a program that loads and runs OS/2 .exe files on Linux.

*Does it work?*

Barely. It runs some command line OS/2 apps.

*Why?*

Because.

*Does it work on Mac OS X?*

No.

*I have questions.*

I have answers: icculus@icculus.org

--ryan.

## About this fork

This fork contains changes made for the OS/2 reconstruction project. AI tools
were used to research and write those changes, including the GCC build fix.
The original 2ine project and the README above are by Ryan C. Gordon and its
other contributors; this disclosure applies to this fork's changes.

This fork extends the loader and DOSCALLS and MSG shims for historical OS/2
toolchains. Earlier Linux x86 tests exercised IBM VisualAge C++ 3.6.5, Microsoft
C 6.0, MASM 5.10A and LINK 5.13. These are compatibility experiments, not a
claim that all programs or toolchain configurations work. MASM 6.0 can
assemble a small source file with `/c`; broader compatibility and thread
lifecycle behavior remain unverified. An optional regression test is available
as `tests/test-ml6.sh`, using a separately supplied OS/2 `ML.EXE`.

