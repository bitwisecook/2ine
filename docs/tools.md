<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Python tools

For maintained C/H sources, use `python3 tools/format-c.py --write` or
`--check`. The pinned formatter, exclusions and CI check are described in
[C/H formatting](formatting.md).

Python 3.11+. No runtime dependencies outside the standard library.

| Tool | Input → output |
|---|---|
| `tools/lxapigen.py` | API headers + binding policies → `os2libs/generated/*-lx.h` |
| `tools/cvtasm.py` | ndisasm text → C byte emissions |
| `tools/os2imports.py` | lx_dump text → named NE/LX imports |
| `tools/unimplemented.py` | Named imports → services absent from export tables |

```sh
python3 tools/lxapigen.py          # regenerate changed headers
python3 tools/lxapigen.py --check  # check without writing
build/lx_dump program.exe | python3 tools/os2imports.py | python3 tools/unimplemented.py
ndisasm -b32 code.bin | python3 tools/cvtasm.py
```

The import catalogue is `tools/data/os2imports.json`. `cvtasm.py` and `os2imports.py` accept
files as arguments or read stdin. `unimplemented.py` reads stdin.

The `PMWINX` catalogue records 795 named Open32 exports from the Warp 4.52
`os2image/FI/DAX/DLL/PMWINX.DLL`, SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
These names identify required APIs in import reports; Open32 is not implemented.

```sh
uvx ruff check .
uvx ruff format --check .
uvx ty check
python3 -m unittest discover -s tests/tools -p test_tools.py -v
```

Ruff and ty cover the four tools and their tests. Their scope is in
`pyproject.toml`. The tools and ordinal catalogue derive from 2ine and retain
its [license](../LICENSE.txt).

The generator reads public `os2libs/*.h` API declarations and
`guest/bindings/*.json` policies. Service implementation files can be split
without changing generation inputs. Ordinals, calling conventions and explicit
guest marshalling policies remain defined by those inputs. `--check` reports
stale output without creating or modifying directories.
