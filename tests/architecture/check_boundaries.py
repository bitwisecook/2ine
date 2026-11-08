#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Validate component ownership, include style, public host contracts and a source-free root."""

import os
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
failures = []

SOURCE_SUFFIXES = {".c", ".h", ".m", ".mm", ".cpp", ".inc"}

# Every top-level directory that holds sources has a role, so a new component is
# placed under the rules below before it can pass.
PORTABLE_COMPONENTS = {"formats", "storage", "emulator", "runtime", "loader", "guest", "os2libs"}
HOST_COMPONENTS = {"host", "ball"}
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
    "formats": ("storage/", "ball/", "host/", "runtime/", "loader/", "guest/", "os2libs/"),
    "storage": ("ball/", "host/", "runtime/", "loader/", "guest/", "os2libs/"),
    "emulator": ("ball/",),
    "host": ("ball/", "emulator/"),
    "runtime": ("ball/",),
    "loader": ("ball/",),
    "guest": ("ball/",),
    "os2libs": ("ball/",),
}

# Host adapters, one directory each below host/, mirroring host/core/ by
# function. Only the OS adapters may branch on the host operating system.
HOST_OS_ADAPTERS = {"linux", "macos", "win"}
HOST_ADAPTERS = HOST_OS_ADAPTERS | {"posix", "sdl", "openssl", "freetype", "curses", "mesa", "none"}
HOST_FUNCTIONS = {
    "system",
    "process",
    "thread",
    "ipc",
    "fs",
    "net",
    "fatal",
    "window",
    "input",
    "desktop",
    "audio",
    "clipboard",
    "print",
    "terminal",
    "font",
    "opengl",
}

# An adapter may include public headers, host/core/ and itself. The Linux and
# macOS adapters also build on the shared POSIX adapter.
HOST_ADAPTER_INCLUDES = {"linux": {"posix"}, "macos": {"posix"}}

# Host-OS branches that remain outside the platform directories, with the
# number of branch directives each one holds. A count change fails, so an entry
# is removed together with its branches.
TEMPORARY_OS_BRANCH_ALLOWANCES = {
    # strncasecmp spelling for Windows C runtimes.
    "os2libs/printer_catalogue.h": 1,
}

# Public host contracts, the only host headers portable components include.
PUBLIC_HOST_HEADERS = (
    r"(?:abi|audio|clipboard|clock|codepage_tables|desktop|fatal|font|fs|input|library|mailbox"
    r"|memory|network|opengl|print|process|process_supervisor|shared_memory|socket|sync"
    r"|system|terminal|thread|tls|video_frame|window|gl/(?:dispatch|constants))\.h"
)

# Headers generated into the build tree rather than the source tree.
GENERATED_INCLUDES = {"twoine_ball_icon_png.h"}

INCLUDE_ROOTS = (root, root / "host/include", root / "storage/include", root / "emulator/include")


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
    """Project includes name their path from the project root or host/include."""
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
        if name.startswith("host/include/"):
            failures.append(f'{where}: public host headers are included as "host/<name>.h"')
            continue
        if name.startswith("emulator/include/"):
            failures.append(f'{where}: public emulator headers are included as "emulator/<name>.h"')
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
    if top != "host":
        return False
    return name.split("/")[1] not in HOST_OS_ADAPTERS


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
            hardware + [r"#\s*include\s*<host/(?!" + PUBLIC_HOST_HEADERS + r")[^>]+>"],
        )
for directory, prefixes in sorted(FORBIDDEN_INCLUDES.items()):
    for path in sources(directory):
        if foreign(relative(path)):
            continue
        check(
            path, [rf"#\s*(?:include|import)\s*<{re.escape(prefix)}[^>]*>" for prefix in prefixes]
        )
for path in sources("host/include", {".h"}):
    check(path, platform + [r"\bHANDLE\b"])
# The core holds the platform-free half of each function: no host APIs.
for path in sources("host/core"):
    check(path, hardware)
for path in sources("host"):
    name = relative(path)
    parts = name.split("/")
    if parts[1] == "include" or len(parts) < 4:
        continue
    if parts[1] != "core" and parts[1] not in HOST_ADAPTERS:
        failures.append(f"{name}: unknown host adapter {parts[1]!r}; add it to check_boundaries.py")
    elif parts[2] not in HOST_FUNCTIONS:
        failures.append(
            f"{name}: unknown host function {parts[2]!r}; add it to check_boundaries.py"
        )
    allowed = {"core", parts[1]} | HOST_ADAPTER_INCLUDES.get(parts[1], set())
    text = path.read_text(errors="replace")
    for match in re.finditer(r'^[ \t]*#[ \t]*(?:include|import)[ \t]*"host/(\w+)/', text, re.M):
        if match.group(1) not in allowed and (root / "host" / match.group(1)).is_dir():
            line = text[: match.start()].count("\n") + 1
            failures.append(f"{name}:{line}: {parts[1]} may not include host/{match.group(1)}/")
# Outside host/, only the public contracts are visible.
for path in sources():
    name = relative(path)
    if name.startswith(("host/", "tests/")) or foreign(name):
        continue
    check(
        path,
        [
            r"#\s*(?:include|import)\s*<host/(?:core|"
            + "|".join(sorted(HOST_ADAPTERS))
            + r")/[^>]*>"
        ],
    )
for path in list(sources("host")) + list(sources("emulator/execution")):
    check(
        path,
        [
            r"\b(?:GProcessState|LxModule|LxMmaps)\b",
            r"#\s*include\s*<[^>]*(?:os2libs/|loader/|emulator/(?:runtime|process)\.h)",
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
                r"(?<!tests/)\bhost/(?:core|"
                + "|".join(sorted(HOST_ADAPTERS))
                + r')/[^\s"\)]*\.[chm]\b',
                r"\btwoine_host_(?:function|requires)\b",
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
print(
    "Source ownership, include style, host-OS branches, public host contracts and root layout passed"
)
