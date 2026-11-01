#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Validate component ownership, public HAL contracts and a source-free project root."""

import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
failures = []


def check(path, patterns, cmake=False):
    if cmake:
        text = re.sub(r"#[^\n]*", "", path.read_text())
    else:
        text = path.read_text()
        text = re.sub(r'(#\s*include\s*)"([^"]+)"', r"\1<\2>", text)
        text = re.sub(
            r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' """.rstrip(),
            lambda m: (
                "\n" * m.group(0).count("\n") if not m.group(0).startswith(('"', "'")) else "STRING"
            ),
            text,
            flags=re.S,
        )
    for pattern in patterns:
        for match in re.finditer(pattern, text):
            line = text[: match.start()].count("\n") + 1
            failures.append(f"{path.relative_to(root)}:{line}: {match.group(0)}")


platform = [
    r"#\s*include\s*<(?:sys/|asm/|pthread\.h|windows\.h|unicorn/|SDL3/|curses\.h|arpa/|netinet/|netdb\.h)",
    r"\b(?:ucontext_t|uc_engine|pthread_\w+|SDL_\w+)\b",
]
hardware = platform + [
    r"\b(?:__asm__|syscall|mmap|munmap|mprotect|dlopen|dlsym|dlclose|fork|waitpid)\s*\("
]
for directory in ["runtime", "guest", "loader", "formats", "os2libs"]:
    for path in sorted((root / directory).rglob("*.[ch]")):
        check(
            path,
            hardware
            + [
                r"#\s*include\s*<[^>]*hal/(?!abi\.h|audio\.h|video\.h|font\.h|socket\.h|terminal\.h|host(?:_\w+)?\.h)[^>]+>"
            ],
        )
for path in sorted((root / "hal/include").rglob("*.h")):
    check(path, platform + [r"\bHANDLE\b"])
for path in sorted((root / "hal").rglob("*.c")):
    check(
        path,
        [
            r"\b(?:GLoaderState|LxModule|LxMmaps)\b",
            r"#\s*include\s*<[^>]*(?:os2libs/|loader/|runtime/(?:runtime|process)\.h)",
        ],
    )
for directory in ["hal/common", "hal/posix", "hal/emulator", "hal/sdl"]:
    for path in sorted((root / directory).rglob("*.[ch]")):
        check(
            path,
            [
                r"#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*(?:_WIN32|__APPLE__|__linux__)",
                r"#\s*include\s*<(?:windows\.h|mach/|mach-o/|sys/sysinfo\.h)",
            ],
        )
for directory in ["runtime", "loader", "os2libs"]:
    for path in (root / directory).glob("CMakeLists.txt"):
        check(
            path,
            [
                r'(?<!tests/)\bhal/(?:common|posix|linux|macos|win|emulator|sdl)/[^\s"\)]*\.[ch]\b',
                r"\bTWOINE_HOST_SOURCES\b",
            ],
            cmake=True,
        )
for pattern in ["*.c", "*.h", "*.py", "lib*.so*", "*.dll", "os2imports.json"]:
    for path in root.glob(pattern):
        failures.append(f"{path.name}: implementation/tool/data belongs in its component directory")
for path in (root / "os2libs").glob("*-lx.h"):
    failures.append(f"{path.relative_to(root)}: generated exports belong in os2libs/generated/")
if failures:
    raise SystemExit("\n".join(failures))
print("Source ownership, guest/runtime isolation, public HAL and root layout passed")
