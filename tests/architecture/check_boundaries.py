#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Validate component ownership, include style, public HAL contracts and a source-free root."""

import os
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
failures = []

SOURCE_SUFFIXES = {".c", ".h", ".m", ".mm", ".cpp", ".inc"}

# Every top-level directory that holds sources has a role, so a new component is
# placed under the rules below before it can pass.
PORTABLE_COMPONENTS = {"formats", "storage", "emulator", "runtime", "loader", "guest", "os2libs"}
HOST_COMPONENTS = {"hal", "ball"}
SUPPORT_DIRECTORIES = {"tests", "tools", "macos", "cmake", "docs", "snap"}
# Sources built by other toolchains or kept only as reference material.
FOREIGN_DIRECTORIES = {"ports", "research"}

# Paths whose sources follow another project's or toolchain's conventions:
# vendored libraries and OS/2 guest programs built with OS/2 compilers.
FOREIGN_SOURCE_PATHS = ("ball/vendor/", "storage/vendor/", "tests/programs/")

# Execution backends run guest code on the host CPU or an emulator, so they
# use host APIs, CPU-specific assembly and emulator headers.
EXECUTION_BACKENDS = ("emulator/execution/native_i386/", "emulator/execution/unicorn/")

# Files inside portable components that adapt them to the host: the only
# place such a component may use host APIs.
PORTABLE_HOST_ADAPTERS = ("storage/common/host",) + EXECUTION_BACKENDS

# Portable-component paths that may also branch on the host OS. The native
# backend is Linux-only; the Unicorn backend runs on every host.
PORTABLE_OS_ADAPTERS = ("storage/common/host", "emulator/execution/native_i386/")

# Includes each component may not use: formats/ is self-contained, storage/
# builds on formats/ only, and nothing outside ball/ depends on ball/.
FORBIDDEN_INCLUDES = {
    "formats": ("storage/", "ball/", "hal/", "runtime/", "loader/", "guest/", "os2libs/"),
    "storage": ("ball/", "hal/", "runtime/", "loader/", "guest/", "os2libs/"),
    "emulator": ("ball/",),
    "hal": ("ball/",),
    "runtime": ("ball/",),
    "loader": ("ball/",),
    "guest": ("ball/",),
    "os2libs": ("ball/",),
}

# HAL directories that may branch on the host operating system.
HAL_PLATFORM_DIRECTORIES = ("hal/linux/", "hal/macos/", "hal/win/")

# Host-OS branches that remain outside the platform directories, with the
# number of branch directives each one holds. A count change fails, so an entry
# is removed together with its branches.
TEMPORARY_OS_BRANCH_ALLOWANCES = {
    # Operation supervision is unavailable on Windows; replaced by HAL stubs.
    "loader/main.c": 2,
    # strncasecmp spelling for Windows C runtimes.
    "os2libs/printer_catalogue.h": 1,
}

# Headers generated into the build tree rather than the source tree.
GENERATED_INCLUDES = {"twoine_ball_icon_png.h"}

INCLUDE_ROOTS = (root, root / "hal/include", root / "storage/include")


def excluded_top_level(name):
    return name.startswith((".", "build", "cmake-")) or name in {"_deps", "third_party"}


def sources(directory="", suffixes=SOURCE_SUFFIXES):
    """Yield source files below directory without following symbolic links."""
    base = root / directory
    for current, directories, files in os.walk(base):
        here = Path(current)
        if here == root:
            directories[:] = [name for name in directories if not excluded_top_level(name)]
        directories.sort()
        for name in sorted(files):
            path = here / name
            if path.suffix in suffixes:
                yield path


def relative(path):
    return path.relative_to(root).as_posix()


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
    found = 0
    for pattern in patterns:
        for match in re.finditer(pattern, text):
            found += 1
            line = text[: match.start()].count("\n") + 1
            failures.append(f"{relative(path)}:{line}: {match.group(0)}")
    return found


def check_include_style(path):
    """Project includes name their path from the project root or hal/include."""
    text = path.read_text(errors="replace")
    pattern = r'^[ \t]*#[ \t]*(?:include|import)[ \t]*(?:"([^"]+)"|<([^>]+)>)'
    for match in re.finditer(pattern, text, flags=re.M):
        name = match.group(1) or match.group(2)
        line = text[: match.start()].count("\n") + 1
        where = f"{relative(path)}:{line}: {match.group(0).strip()}"
        if ".." in Path(name).parts:
            failures.append(f"{where}: include by path from the project root, not '../'")
            continue
        if match.group(2) is not None:
            continue
        if name.startswith("hal/include/"):
            failures.append(f'{where}: public HAL headers are included as "hal/<name>.h"')
            continue
        if name.startswith("storage/include/"):
            failures.append(f'{where}: public storage headers are included as "storage/<name>.h"')
            continue
        rooted = [base / name for base in INCLUDE_ROOTS if (base / name).is_file()]
        local = path.parent / name
        if local.is_file() and not any(local.samefile(candidate) for candidate in rooted):
            failures.append(f"{where}: include by path from the project root")
        elif not rooted and not local.is_file() and name not in GENERATED_INCLUDES:
            failures.append(f"{where}: unresolved project include")


def foreign(name):
    return name.split("/", 1)[0] in FOREIGN_DIRECTORIES or name.startswith(FOREIGN_SOURCE_PATHS)


def portable_source(path):
    """Component code that must build on any host without host APIs."""
    name = relative(path)
    return not foreign(name) and not name.startswith(PORTABLE_HOST_ADAPTERS)


def os_branch_scope(path):
    name = relative(path)
    if foreign(name) or name.startswith(PORTABLE_OS_ADAPTERS):
        return False
    top = name.split("/", 1)[0]
    if top in PORTABLE_COMPONENTS:
        return True
    return top == "hal" and not name.startswith(HAL_PLATFORM_DIRECTORIES)


for path in sorted(root.iterdir()):
    if not path.is_dir() or excluded_top_level(path.name):
        continue
    known = PORTABLE_COMPONENTS | HOST_COMPONENTS | SUPPORT_DIRECTORIES | FOREIGN_DIRECTORIES
    if path.name not in known and next(sources(path.name), None):
        failures.append(
            f"{path.name}/: unclassified source directory; add it to check_boundaries.py"
        )

for path in sources():
    name = relative(path)
    if name.split("/", 1)[0] in FOREIGN_DIRECTORIES or name.startswith(FOREIGN_SOURCE_PATHS):
        continue
    check_include_style(path)

platform = [
    r"#\s*include\s*<(?:sys/|asm/|pthread\.h|windows\.h|unicorn/|SDL3/|EGL/|GL/|OpenGL/|vulkan/|Metal/|d3d\w*\.h|curses\.h|arpa/|netinet/|netdb\.h)",
    r"\b(?:ucontext_t|uc_engine|pthread_\w+|SDL_\w+)\b",
]
hardware = platform + [
    r"\b(?:__asm__|syscall|mmap|munmap|mprotect|dlopen|dlsym|dlclose|fork|waitpid)\s*\("
]
for directory in sorted(PORTABLE_COMPONENTS):
    for path in sources(directory, {".c", ".h"}):
        if not portable_source(path):
            continue
        check(
            path,
            hardware
            + [
                r"#\s*include\s*<[^>]*hal/(?!abi\.h|audio\.h|clipboard\.h|video\.h|video_frame\.h|print\.h|font\.h|opengl\.h|gl_(?:dispatch|constants)\.h|socket\.h|network\.h|tls\.h|terminal\.h|host(?:_\w+)?\.h)[^>]+>"
            ],
        )
for directory, prefixes in sorted(FORBIDDEN_INCLUDES.items()):
    for path in sources(directory):
        if foreign(relative(path)):
            continue
        check(
            path, [rf"#\s*(?:include|import)\s*<{re.escape(prefix)}[^>]*>" for prefix in prefixes]
        )
for path in sources("hal/include", {".h"}):
    check(path, platform + [r"\bHANDLE\b"])
for path in list(sources("hal")) + list(sources("emulator/execution")):
    check(
        path,
        [
            r"\b(?:GLoaderState|LxModule|LxMmaps)\b",
            r"#\s*include\s*<[^>]*(?:os2libs/|loader/|runtime/(?:runtime|process)\.h)",
        ],
    )
os_branches = [
    r"#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*(?:_WIN32|__APPLE__|__linux__)",
    r"#\s*include\s*<(?:windows\.h|mach/|mach-o/|sys/sysinfo\.h)",
]
for path in sources():
    if not os_branch_scope(path):
        continue
    name = relative(path)
    allowed = TEMPORARY_OS_BRANCH_ALLOWANCES.get(name)
    if allowed is None:
        check(path, os_branches)
        continue
    before = len(failures)
    found = check(path, os_branches)
    if found == allowed:
        del failures[before:]
    else:
        failures.append(f"{name}: {found} host-OS branches; temporary allowance is {allowed}")
for name in TEMPORARY_OS_BRANCH_ALLOWANCES:
    if not (root / name).is_file():
        failures.append(f"{name}: temporary host-OS branch allowance names a missing file")
for directory in sorted(PORTABLE_COMPONENTS):
    base = root / directory
    for path in sorted(base.rglob("CMakeLists.txt")) + sorted(base.rglob("*.cmake")):
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
    failures.append(f"{relative(path)}: generated exports belong in os2libs/generated/")
if failures:
    raise SystemExit("\n".join(failures))
print("Source ownership, include style, host-OS branches, public HAL and root layout passed")
