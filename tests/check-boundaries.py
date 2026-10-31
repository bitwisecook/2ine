#!/usr/bin/env python3
# Public domain. No rights reserved.
# Wholly AI-authored.
"""Check the source separation between guest semantics, execution and host APIs."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
failures = []

def check(path, patterns, cmake=False):
    if cmake:
        text = re.sub(r'#[^\n]*', '', path.read_text())
    else:
        text = re.sub(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",
                      lambda m: '\n' * m.group(0).count('\n') if not m.group(0).startswith(('"', "'")) else 'STRING',
                      re.sub(r'(#\s*include\s*)"([^"]+)"', r'\1<\2>', path.read_text()), flags=re.S)
    for pattern in patterns:
        for match in re.finditer(pattern, text):
            line = text[:match.start()].count('\n') + 1
            failures.append(f'{path.relative_to(root)}:{line}: {match.group(0)}')

platform = [r'#\s*include\s*[<"](?:sys/|asm/|pthread\.h|windows\.h|unicorn/)',
            r'\b(?:ucontext_t|HANDLE|uc_engine|pthread_\w+)\b']
for path in [root / 'hal/emulator/cpu.h', *sorted((root / 'runtime').glob('*.h'))]:
    check(path, platform)
for path in sorted((root / 'hal').rglob('*.c')):
    check(path, [r'\b(?:GLoaderState|LxModule|LxMmaps)\b',
                 r'#\s*include\s*<[^>]*(?:lib2ine\.h|os2libs/)'])
for directory in ['hal/common', 'hal/posix', 'hal/emulator']:
    for path in sorted((root / directory).rglob('*.[ch]')):
        check(path, [r'#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*(?:_WIN32|__APPLE__|__linux__)',
                     r'#\s*include\s*<(?:windows\.h|mach/|mach-o/|sys/sysinfo\.h)'])
hardware = platform + [r'\b(?:__asm__|syscall|mmap|munmap|mprotect|dlopen|dlsym|dlclose|fork|waitpid)\s*\(']
for name in ['lx_loader.c', 'lib2ine.c', 'os2libs/doscalls.c', 'os2libs/msg.c', 'os2libs/nls.c']:
    check(root / name, hardware)
for directory in ['runtime', 'guest']:
    for path in sorted((root / directory).rglob('*.[ch]')):
        check(path, hardware + [r'#\s*include\s*<[^>]*hal/'])
# Implementation locations are part of the HAL boundary, including build input.
for path in [*sorted((root / 'runtime').glob('host*.c')),
             *sorted((root / 'os2libs').glob('execution_*.c')),
             *sorted((root / 'os2libs').glob('abi_linux_*.c'))]:
    failures.append(f'{path.relative_to(root)}: hardware implementation belongs in hal/')
if (root / 'emulator').exists():
    failures.append('emulator/: execution implementations belong in hal/emulator/')
for path in [root / 'CMakeLists.txt', root / 'runtime/CMakeLists.txt']:
    check(path, [r'(?<!tests/)\bhal/(?:common|posix|linux|macos|win|emulator)/[^\s"\)]*\.[ch]\b',
                 r'\bTWOINE_HOST_SOURCES\b'], cmake=True)
if failures:
    raise SystemExit('\n'.join(failures))
print('Guest semantics, shared runtime and HAL source boundaries passed')
