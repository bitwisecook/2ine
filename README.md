<!-- Derived from the original 2ine README; platform information updated in this fork. See LICENSE.txt. -->

# 2ine

*What is this?*

It's a program that loads and runs OS/2 .exe files on Linux and macOS.

*Does it work?*

It runs command line OS/2 apps, including Microsoft, IBM and Watcom developer tools,
and small Presentation Manager GUI programs such as PMStars.

*Why?*

Because.

*Does it work on Mac OS X?*

Yes. After applying copious amounts of AI to the problem, ARM64 macOS uses Unicorn
for mixed 16/32-bit x86 execution. Linux x86 can run it natively. No Linux VM or
Rosetta needed. Windows is not supported but probably wouldn't be hard for a clanker
to add.

*I have questions.*

For the original https://github.com/icculus/2ine
I have answers: icculus@icculus.org
--ryan.

For this abomination
Raise an issue on GH or find a way to contact me.
--[bitwisecook]

[Build](docs/build.md) · [Architecture](docs/architecture.md) · [HAL](docs/hal.md) ·
[Tools](docs/tools.md) · [Tests](docs/testing.md) · [GUI applications](docs/gui.md) ·
[C/H formatting](docs/formatting.md) · [Licensing](docs/licensing.md)
