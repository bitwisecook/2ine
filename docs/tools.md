<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Python tools

Format every language with `python3 tools/format.py --write`, or verify with
`--check`. The pinned formatters, exclusions and CI check are described in
[source formatting](formatting.md).

Python 3.11+. No runtime dependencies outside the standard library.

| Tool | Input → output |
|---|---|
| `tools/lxapigen.py` | Module definitions + API headers + binding policies → `os2/**/generated/*-lx.h` |
| `tools/cvtasm.py` | ndisasm text → C byte emissions |
| `tools/os2imports.py` | lx_dump text → named NE/LX imports |
| `tools/unimplemented.py` | Named imports → services absent from export tables |
| `tools/lotus_imports.py` | Lotus ball → static closures, optional DLL corpus and native-interface gap reports |

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
These names identify required APIs in import reports; Open32 support is partial.
An exported name does not establish complete API behaviour.

For an installed Lotus suite, generate a conservative preflight worklist:

```sh
python3 tools/lotus_imports.py --ball /path/to/Lotus.2ineball --output build/lotus-api-audit
```

`missing-native.csv` lists absent exports of providers selected as native 2ine
services, including optional Lotus/plugin candidates. It excludes interfaces
supplied by original OS/2 providers. `portable-policy-gaps.csv` separately lists
source exports without a recorded portable guest ABI policy.
`missing-libraries.json` records unresolved modules in optional closures; those
modules are not necessarily needed by the selected application. The complete
`imports.json` and compressed reference data retain hashes and exact import
locations. Runtime-computed loads and semantic completeness remain unassessed.
The shared `os2-re-module` catalogue/cache can resolve original ordinal names,
declaration variants and documentation before implementing each API family.

```sh
uvx ruff check .
uvx ruff format --check .
uvx ty check
python3 -m unittest discover -s tests/tools -p test_tools.py -v
```

Ruff and ty cover the four tools and their tests. Their scope is in
`pyproject.toml`. The tools and ordinal catalogue derive from 2ine and retain
its [license](../LICENSE.txt).

The generator reads each DLL's LINK386-style module definition
(`os2/<group>/<dll>/<dll>.def`: `LIBRARY` name and `EXPORTS` entries
`name [= C entry] @ordinal`), the C prototypes and `LX_GUEST_DATA_EXPORT`
sizes in the public `os2/include/` headers (and `os2/thirdparty/*/*.h`), and the
`os2/include/private/api16.json`/`api32.json` policies. It writes
`generated/<dll>-lx.h` beside each definition. Service implementation files can be split without changing
generation inputs. Ordinals, calling conventions and explicit guest marshalling
policies remain defined by those inputs. `--check` reports
stale output without creating or modifying directories.
