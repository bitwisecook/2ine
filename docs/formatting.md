<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Source formatting

Every maintained source language has one pinned formatter and one configuration
file. `tools/format.py` runs them all:

```sh
python3 tools/format.py --write          # format everything
python3 tools/format.py --check          # verify, as CI does
python3 tools/format.py --check c swift  # selected languages only
```

| Language | Formatter | Configuration | Pin |
|---|---|---|---|
| C, C++, Objective-C | clang-format | `.clang-format`, `.clang-format-ignore` | `tools/clang-format-version` |
| Python | ruff format + ruff check | `pyproject.toml` | `RUFF` in `tools/format.py` |
| Swift | swift-format | `.swift-format` | Swift toolchain 6.4 |
| CMake | gersemi | `.gersemirc` | `GERSEMI` in `tools/format.py` |

Ruff and gersemi run through `uvx`, so only `uv` needs installing. Install the
pinned clang-format with `python3 -m pip install clang-format==21.1.8`, or set
`CLANG_FORMAT` to a matching executable. `tools/format-c.py` remains the C entry
point and accepts individual filenames.

## Layout rules

All languages use four-space indentation and a 100-column target. Top-level
definitions (functions, types, tables) are separated by a blank line.

C and Objective-C use Allman braces with pointer stars beside variable names.
Include order, comment text and string literals are preserved. The formatter
does not insert or remove braces or reorder qualifiers. Long string literals
and protected macros may exceed the column target.

## Exclusions

Third-party sources (`third_party/`), reference experiments (`research/`) and
build output retain their own layout. Generated export glue retains its
generator's layout; validate it with `python3 tools/lxapigen.py --check` and
`python3 tools/gen_opengl.py --check`. Generators emit a blank line between
generated definitions.

CI checks formatting without modifying sources. Build and runtime checks remain
separate from formatting checks.
