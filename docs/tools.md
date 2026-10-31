<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Python tools

For maintained C/H sources, use `python3 tools/format-c.py --write` or
`--check`. The pinned formatter, exclusions and CI check are described in
[C/H formatting](formatting.md).

Python 3.11+. No runtime dependencies outside the standard library.

| Tool | Input → output |
|---|---|
| `lxapigen.py` | API headers + binding policies → `os2libs/*-lx.h` |
| `cvtasm.py` | ndisasm text → C byte emissions |
| `os2imports.py` | lx_dump text → named NE/LX imports |
| `unimplemented.py` | Named imports → services absent from export tables |

```sh
python3 lxapigen.py          # regenerate changed headers
python3 lxapigen.py --check  # check without writing
build/lx_dump program.exe | python3 os2imports.py | python3 unimplemented.py
ndisasm -b32 code.bin | python3 cvtasm.py
```

The import catalogue is `os2imports.json`. `cvtasm.py` and `os2imports.py` accept
files as arguments or read stdin. `unimplemented.py` reads stdin.

```sh
uvx ruff check .
uvx ruff format --check .
uvx ty check
python3 -m unittest discover -s tests -p test_tools.py -v
```

Ruff and ty cover the four tools and their tests. Their scope is in
`pyproject.toml`. The tools and ordinal catalogue derive from 2ine and retain
its [license](../LICENSE.txt).
