#!/usr/bin/env python3
# Public domain. No rights reserved.
# Wholly AI-authored.
"""Run configured OS/2 build pipelines with separately supplied historical tools.

Every fixture is required. Work directories must be new, so stale outputs cannot
satisfy assertions. JSON records retain commands, inputs, outputs and log hashes.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def os2path(path):
    return 'C:' + str(path.resolve()).replace('/', '\\')


def omf(path):
    data = path.read_bytes()
    offset, types, omitted = 0, [], 0
    while offset < len(data):
        assert offset + 3 <= len(data), f'{path}: truncated OMF header'
        length = int.from_bytes(data[offset + 1:offset + 3], 'little')
        end = offset + 3 + length
        assert length and end <= len(data), f'{path}: truncated OMF record'
        # TIS OMF 1.1 permits zero when a producer omits checksum calculation.
        if data[end - 1] == 0: omitted += 1
        else: assert sum(data[offset:end]) % 256 == 0, f'{path}: OMF checksum at {offset}'
        types.append(data[offset]); offset = end
    assert types[0] == 0x80 and types[-1] in (0x8a, 0x8b), (path, types)
    assert any(t in (0xa0, 0xa1) for t in types), (path, types)
    return {'records': len(types), 'omitted_checksums': omitted, 'sha256': digest(path)}


def executable(path, kind):
    data = path.read_bytes()
    assert len(data) >= 64 and data[:2] == b'MZ', f'{path}: missing MZ header'
    header = struct.unpack_from('<I', data, 60)[0]
    actual = data[header:header + 2]
    if kind == 'MZ':
        assert actual not in (b'LX', b'NE', b'LE', b'PE'), f'{path}: unexpected extended executable'
    else:
        assert actual == kind.encode(), f'{path}: expected {kind}'
    if kind == 'LX':
        assert header + 196 <= len(data), f'{path}: short LX header'
        pages, ip_object, ip, sp_object, sp = struct.unpack_from('<IIIII', data, header + 20)
        objects, count = struct.unpack_from('<II', data, header + 64)
        assert count and header + objects + 24 * count <= len(data), f'{path}: object table extent'
        assert 1 <= ip_object <= count and 1 <= sp_object <= count, f'{path}: entry objects'
        entries = [struct.unpack_from('<IIIIII', data, header + objects + 24 * i) for i in range(count)]
        assert all(item[0] for item in entries), f'{path}: empty object'
        assert ip < entries[ip_object - 1][0] and sp <= entries[sp_object - 1][0], f'{path}: entry bounds'
        assert all(item[4] <= pages for item in entries), f'{path}: object page count'
        page_table = struct.unpack_from('<I', data, header + 72)[0]
        assert header + page_table + 8 * pages <= len(data), f'{path}: page table extent'
    elif kind == 'NE':
        assert header + 64 <= len(data), f'{path}: short NE header'
        count = struct.unpack_from('<H', data, header + 28)[0]
        table = struct.unpack_from('<H', data, header + 34)[0]
        assert count and header + table + count * 8 <= len(data), f'{path}: NE segment table'
    return {'format': kind, 'size': len(data), 'sha256': digest(path)}


class Suite:
    def __init__(self, args):
        self.build = args.build.resolve()
        self.root = args.work.resolve(); self.root.mkdir(parents=True, exist_ok=False)
        self.config = json.loads(args.fixtures.read_text())
        self.base_env = os.environ.copy()
        for key in ('TRACE_NATIVE', 'TWOINE_TRACE_RET', 'TWOINE_TRACE_SEGMENTS', 'MSC_CMD_FLAGS'):
            self.base_env.pop(key, None)
        self.base_env.update(TWOINE_BACKEND=args.backend, TWOINE_LIBPATH=str(self.build),
                        LD_LIBRARY_PATH=str(self.build))
        source = Path(__file__).resolve().parents[1]
        inventory = [p for p in source.iterdir() if p.is_file() and p.name != '.git']
        for directory in ['runtime', 'hal', 'native', 'guest', 'tests', '.github']:
            inventory.extend(p for p in (source / directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
        source_hashes = {str(p.relative_to(source)): digest(p) for p in inventory}
        try:
            base = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True, stderr=subprocess.DEVNULL).strip()
        except (subprocess.CalledProcessError, OSError):
            base = None
        self.record = dict(_notice='Public domain. No rights reserved. Wholly AI-authored.', source_base=base, source_sha256=source_hashes,
            observed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
            platform=platform.platform(), machine=platform.machine(), backend=args.backend,
            runner=str(self.build / 'lx_loader'), runner_sha256=digest(self.build / 'lx_loader'),
            inputs={}, commands=[], artifacts={}, pipelines=[], passed=False)

    def fixture(self, name):
        path = Path(self.config[name]).resolve()
        assert path.exists(), f'missing fixture {name}: {path}'
        return path

    def copy(self, source, dest):
        assert source.is_file(), f'missing fixture {source}'
        shutil.copy2(source, dest)
        self.record['inputs'][str(source)] = digest(source)

    def stage(self, name, directory):
        work = self.root / name; work.mkdir()
        for source in directory.iterdir():
            if source.is_file() and source.suffix.lower() in ('.exe', '.dll', '.msg', '.cat', '.err', '.lnk'):
                self.copy(source, work / source.name)
        return work

    def env(self, work, **values):
        return dict(self.base_env, OS2PATH=os2path(work), **values)

    def run(self, work, arguments, env=None, expected=0, contains=None, host=False):
        command = list(map(str, arguments if host else [self.build / 'lx_loader', *arguments]))
        log = work / ('command-%03d.log' % len(self.record['commands']))
        environment = env or self.env(work)
        process = subprocess.run(command, cwd=work, env=environment, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=180)
        log.write_bytes(process.stdout)
        self.record['commands'].append(dict(command=command, cwd=str(work),
            environment={k: v for k, v in environment.items() if k in
                         ('TWOINE_BACKEND', 'TWOINE_LIBPATH', 'LD_LIBRARY_PATH', 'OS2PATH', 'LIB', 'INCLUDE', 'TMP', 'WATCOM')},
            exit=process.returncode, log=str(log), log_sha256=digest(log)))
        ok = process.returncode == expected if expected is not None else 0 < process.returncode < 128
        assert ok, f'{arguments}: exit {process.returncode}; see {log}'
        if contains is not None:
            assert contains.lower().encode() in process.stdout.lower(), f'{arguments}: expected {contains!r}; see {log}'
        return process.stdout

    def artifact(self, path, kind='OMF'):
        self.record['artifacts'][str(path)] = omf(path) if kind == 'OMF' else executable(path, kind)

    def passed(self, name):
        self.record['pipelines'].append(name); print(f'PASS {name}', flush=True)

    def microsoft(self):
        tree = self.fixture('msc'); work = self.stage('msc', tree / 'bin')
        env = self.env(work, INCLUDE=os2path(tree / 'include'), LIB=os2path(tree / 'lib') + ';' + os2path(self.fixture('msasm') / 'lib'))
        for target, flags in [('NE', ['/Lp']), ('MZ', [])]:
            name = target.lower()
            (work / (name + '.c')).write_text('extern int puts(const char *);\nint main(void) { puts("hello from portable msc"); return 0; }\n')
            self.run(work, ['cl.exe', *flags, name + '.c'], env)
            self.artifact(work / (name + '.obj')); self.artifact(work / (name + '.exe'), target)
            if target == 'NE': self.run(work, [name + '.exe'], contains='hello from portable msc')
        # Exercise more than ten live search IDs and reuse through USE16 glue.
        (work / 'dirs.c').write_text('extern unsigned far pascal DosFindFirst(char far *, short far *, unsigned, void far *, unsigned, unsigned far *, unsigned long);\nextern unsigned far pascal DosFindClose(unsigned);\nint main(void) {\n    short handles[12]; unsigned count; int i; char buffer[300];\n    for (i = 0; i < 12; i++) {\n        handles[i] = -1; count = 1;\n        if (DosFindFirst("*.c", &handles[i], 0, buffer, sizeof buffer, &count, 0L)) return 10+i;\n    }\n    count = 1;\n    if (DosFindFirst("*.c", &handles[0], 0, buffer, sizeof buffer, &count, 0L)) return 30;\n    for (i = 0; i < 12; i++) if (DosFindClose(handles[i])) return 40+i;\n    if (!DosFindClose(handles[0])) return 60;\n    return 0;\n}\n')
        self.run(work, ['cl.exe', '/Lp', 'dirs.c'], env)
        self.artifact(work / 'dirs.obj'); self.artifact(work / 'dirs.exe', 'NE')
        self.run(work, ['dirs.exe'], env)
        self.passed('Microsoft C 6.0: compile/link NE, run NE, compile/link DOS MZ')
        tree = self.fixture('msasm'); work = self.stage('masm51', tree / 'bin')
        env = self.env(work, LIB=os2path(tree / 'lib'))
        (work / 'greet.asm').write_text('.286\n.model small\n.stack 4096\n.data\nmessage db "hello from portable masm",13,10\nwritten dw 0\n.code\nextrn DosWrite:far\nextrn DosExit:far\nstart proc\nmov ax,@data\nmov ds,ax\npush 1\npush ds\npush offset message\npush 26\npush ds\npush offset written\ncall DosWrite\npush 1\npush 0\ncall DosExit\nstart endp\nend start\n')
        self.run(work, ['MASM51.EXE', 'greet.asm,greet.obj,NUL;'], env)
        self.artifact(work / 'greet.obj')
        self.run(work, ['LINK.EXE', 'greet.obj,greet.exe,NUL,OS2.LIB', '/PM:VIO;'], env)
        self.artifact(work / 'greet.exe', 'NE'); self.run(work, ['greet.exe'], contains='hello from portable masm')
        self.passed('MASM 5.10A + LINK: assemble/link/run NE')
        ml = self.fixture('ml6'); self.copy(ml, self.root / 'ML.EXE'); self.copy(ml.with_name('ML.ERR'), self.root / 'ML.ERR')
        script = Path(__file__).resolve().with_name('test-ml6.sh')
        self.run(self.root, [script, ml, self.build, tree / 'bin/LINK.EXE', tree / 'lib/OS2.LIB'], host=True,
                 contains='MASM 6 assemble, link and DOSCALLS execution checks passed')
        self.passed('MASM 6: OMF, repeated large sources, diagnostic failure, assemble/link/run NE')

    def ddk(self):
        work = self.stage('ddk386', self.fixture('msc386') / 'bin')
        self.copy(self.fixture('ml6'), work / 'ML.EXE')
        self.copy(self.fixture('ml6').with_name('ML.ERR'), work / 'ML.ERR')
        (work / 'entry.c').write_text('int portable_entry(void) { return 42; }\n')
        (work / 'start.asm').write_text('.386p\n.model flat\n.stack 65536\n.code\nextrn _portable_entry:near\nstart proc\ncall _portable_entry\nret\nstart endp\nend start\n')
        self.run(work, ['cl386.exe', '/c', '/Zl', '/Gs', '/Foentry.obj', 'entry.c'])
        self.run(work, ['ML.EXE', '/c', 'start.asm'])
        for name in ['entry.obj', 'start.obj']: self.artifact(work / name)
        self.run(work, ['link386.exe', '/nol', 'start.obj+entry.obj,entry.exe,NUL,,;'])
        self.artifact(work / 'entry.exe', 'LX'); self.run(work, ['entry.exe'], expected=42)
        self.passed('DDK CL386 + MASM 6 + LINK386: compile/assemble/link/run LX without a C runtime')

    def ibm(self):
        work = self.stage('ibm365', self.fixture('ibm_bin'))
        self.copy(self.fixture('os2386'), work / 'os2386.lib')
        libraries = self.fixture('ibm_lib')
        for path in libraries.iterdir():
            if path.is_file(): self.record['inputs'][str(path)] = digest(path)
        env = self.env(work, LIB=os2path(libraries), TMP=os2path(work))
        (work / 'portable.c').write_text('extern int puts(const char *);\nint main(void) { puts("hello from portable icc"); return 0; }\n')
        self.run(work, ['ICC.EXE', 'portable.c'], env)
        self.artifact(work / 'portable.obj'); self.artifact(work / 'portable.exe', 'LX')
        self.run(work, ['portable.exe'], env, contains='hello from portable icc')
        self.passed('IBM ICC/ILINK 3.6.5: child compiler stages, worker thread, compile/link/run LX')

    def watcom(self):
        tree = self.fixture('watcom'); work = self.stage('watcom11', tree / 'binp')
        for source in (tree / 'binp/dll').iterdir():
            if source.is_file() and source.suffix.lower() == '.dll': self.copy(source, work / source.name)
        self.copy(self.fixture('os2386'), work / 'os2386.lib')
        for directory in [tree / 'lib386', tree / 'lib386/os2']:
            for path in directory.iterdir():
                if path.is_file(): self.record['inputs'][str(path)] = digest(path)
        env = self.env(work, WATCOM=os2path(tree))
        (work / 'answer.asm').write_text('.386\n.model flat\n.code\npublic portable_value_\nportable_value_ proc\nmov eax,42\nret\nportable_value_ endp\nend\n')
        (work / 'portable.c').write_text('extern int puts(const char *);\nextern int portable_value(void);\nint main(void) { if (portable_value() != 42) return 1; puts("hello from portable watcom"); return 0; }\n')
        self.run(work, ['wcc386.exe', '-bt=os2', 'portable.c'], env)
        self.run(work, ['wasm.exe', 'answer.asm'], env)
        for name in ['portable.obj', 'answer.obj']: self.artifact(work / name)
        self.run(work, ['wlib.exe', '-q', 'answer.lib', '+answer.obj'], env)
        assert (work / 'answer.lib').stat().st_size > (work / 'answer.obj').stat().st_size
        self.run(work, ['wlink.exe', 'system', 'os2v2', 'name', 'portable.exe', 'file', 'portable.obj', 'library', 'answer.lib'], env)
        self.artifact(work / 'portable.exe', 'LX'); self.run(work, ['portable.exe'], env, contains='hello from portable watcom')
        (work / 'broken.c').write_text('int main(void) { invalid syntax here }\n')
        self.run(work, ['wcc386.exe', '-bt=os2', 'broken.c'], env, expected=None, contains='error')
        assert not (work / 'broken.obj').exists()
        self.passed('Watcom 11.0c: WCC386/WASM/WLIB/WLINK compile/assemble/archive/link/run and diagnostic failure')

    def execute(self):
        try:
            self.microsoft(); self.ddk(); self.ibm(); self.watcom()
            self.record['passed'] = True
            print('All configured developer pipelines passed', flush=True)
        finally:
            (self.root / 'results.json').write_text(json.dumps(self.record, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--backend', choices=['native', 'unicorn'], required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    Suite(args).execute()


if __name__ == '__main__':
    main()
