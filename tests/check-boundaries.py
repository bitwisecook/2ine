#!/usr/bin/env python3
"""Check the source separation between guest semantics, execution and host APIs."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
failures = []

def check(path, patterns):
    text = re.sub(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",
                  lambda m: '\n' * m.group(0).count('\n') if not m.group(0).startswith(('"', "'")) else 'STRING',
                  re.sub(r'(#\s*include\s*)"([^"]+)"', r'\1<\2>', path.read_text()), flags=re.S)
    for pattern in patterns:
        for match in re.finditer(pattern, text):
            line = text[:match.start()].count('\n') + 1
            failures.append(f'{path.relative_to(root)}:{line}: {match.group(0)}')

platform = [r'#\s*include\s*[<"](?:sys/|asm/|pthread\.h|windows\.h|unicorn/)',
            r'\b(?:ucontext_t|HANDLE|uc_engine|pthread_\w+)\b']
for name in ['runtime/cpu.h', 'runtime/execution.h']:
    check(root / name, platform)
for name in ['runtime/execution_native_linux_i386.c', 'runtime/execution_unicorn.c', 'runtime/cpu.c', 'runtime/cpu_unicorn.c']:
    check(root / name, [r'\b(?:GLoaderState|LxModule|LxMmaps)\b'])
for name in ['lx_loader.c', 'lib2ine.c', 'native/doscalls.c', 'native/msg.c', 'native/nls.c', 'guest/handles.c', 'guest/arguments.c']:
    check(root / name, platform + [r'\b(?:__asm__|syscall|mmap|munmap|mprotect|dlopen|dlsym|dlclose|fork|waitpid)\s*\('])
if failures:
    raise SystemExit('\n'.join(failures))
print('Guest semantics, execution and host-service source boundaries passed')
