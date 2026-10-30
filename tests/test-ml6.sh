#!/usr/bin/env bash
# Exercise the 16-bit MASM reader thread with a separately supplied ML.EXE.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 /path/to/ML.EXE /path/to/2ine/build" >&2
    exit 2
fi
ml=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
build=$(cd "$2" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$ml" "$tmp/ML.EXE"
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
