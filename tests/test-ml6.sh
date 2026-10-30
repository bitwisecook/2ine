#!/usr/bin/env bash
# Exercise the 16-bit MASM reader thread with a separately supplied ML.EXE.
set -euo pipefail

if [ "$#" -ne 2 ] && [ "$#" -ne 4 ]; then
    echo "usage: $0 /path/to/ML.EXE /path/to/2ine/build [/path/to/LINK.EXE /path/to/OS2.LIB]" >&2
    exit 2
fi
ml=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
build=$(cd "$2" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$ml" "$tmp/ML.EXE"
if [ -f "$(dirname "$ml")/ML.ERR" ]; then
    cp "$(dirname "$ml")/ML.ERR" "$tmp/ML.ERR"
fi
if [ "$#" -eq 4 ]; then
    cp "$3" "$tmp/LINK.EXE"
    cp "$4" "$tmp/OS2.LIB"
fi
cat > "$tmp/sample.asm" <<'EOF'
include extra.inc
example segment
    mov ax, value
    ret
example ends
end
EOF
printf 'value equ 1234h\n' > "$tmp/extra.inc"
cd "$tmp"
export LD_LIBRARY_PATH="$build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export TWOINE_LIBPATH="$build${TWOINE_LIBPATH:+:$TWOINE_LIBPATH}"
runner=("$build/lx_loader")
if command -v setarch >/dev/null && setarch "$(uname -m)" -R true 2>/dev/null; then
    runner=(setarch "$(uname -m)" -R "${runner[@]}")
fi
timeout 30 "${runner[@]}" ML.EXE /c sample.asm >output.log 2>&1
grep -qi 'Assembling: sample.asm' output.log
python3 - <<'PY'
from pathlib import Path
data = Path('sample.obj').read_bytes()
offset = 0
types = []
while offset < len(data):
    assert offset + 3 <= len(data), 'truncated OMF header'
    length = int.from_bytes(data[offset + 1:offset + 3], 'little')
    end = offset + 3 + length
    assert length and end <= len(data), 'truncated OMF record'
    assert sum(data[offset:end]) % 256 == 0, 'bad OMF checksum'
    types.append(data[offset])
    offset = end
assert types[0] == 0x80 and types[-1] in (0x8a, 0x8b), types
assert any(t in (0xa0, 0xa1) for t in types), types
PY
echo 'MASM 6 assembly and OMF record checks passed'

# Exercise repeated reader-buffer allocations on a source larger than 300 KiB.
python3 - <<'PY'
from pathlib import Path
with Path('large.asm').open('w') as out:
    out.write('include extra.inc\n')
    for _ in range(4200):
        out.write('; ' + 'large source reader regression ' * 3 + '\n')
    out.write('example segment\n mov ax,value\n ret\nexample ends\nend\n')
PY
for iteration in 1 2 3 4 5; do
    rm -f large.obj
    timeout 30 "${runner[@]}" ML.EXE /c large.asm >large.log 2>&1
    test -s large.obj
done
echo 'MASM 6 large-source reader checks passed'

if [ -f ML.ERR ]; then
    printf 'example segment\ninvalidopcode\nexample ends\nend\n' > bad.asm
    if timeout 30 "${runner[@]}" ML.EXE /c bad.asm >bad.log 2>&1; then
        echo 'invalid source unexpectedly assembled' >&2
        exit 1
    fi
    grep -q 'error A2008' bad.log
    test ! -e bad.obj
    echo 'MASM 6 diagnostic and failure exit checks passed'
fi

if [ "$#" -eq 4 ]; then
    cat > program.asm <<'EOF'
.286
.model small
.stack 4096
.data
message db "hello from 2ine",13,10
written dw 0
.code
extrn DosWrite:far
extrn DosExit:far
main proc
    mov ax,@data
    mov ds,ax
    push 1
    push ds
    push offset message
    push 17
    push ds
    push offset written
    call DosWrite
    xor ax,ax
    push ax
    push ax
    call DosExit
main endp
end main
EOF
    timeout 30 "${runner[@]}" ML.EXE /c program.asm >program-asm.log 2>&1
    LIB=. timeout 30 "${runner[@]}" LINK.EXE program.OBJ,program.exe,NUL,OS2.LIB '/PM:VIO;' >program-link.log 2>&1
    timeout 30 "${runner[@]}" program.exe >program-run.log 2>&1
    grep -q 'hello from 2ine' program-run.log
    echo 'MASM 6 assemble, link and DOSCALLS execution checks passed'
fi
