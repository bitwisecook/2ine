<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# C and header formatting

Use clang-format 21.1.8, pinned in `tools/clang-format-version`. The same version
runs locally and in CI. Install it in your development Python environment:

```sh
python3 -m pip install clang-format==21.1.8
python3 tools/format-c.py --write
python3 tools/format-c.py --check
```

Both modes also accept individual C/H filenames. `CLANG_FORMAT` selects an
executable explicitly; its version must match the pin. Without an override,
the wrapper searches for `clang-format-21`, then `clang-format`.

The default file list includes tracked and untracked C/H sources recognized by
Git. `.clang-format-ignore` excludes generated export headers, reference
experiments, third-party sources and build directories. Generated export glue
retains its generator's layout; validate it with `python3 lxapigen.py --check`.

`.clang-format` specifies four spaces, Allman braces, a 100-column target and
pointer stars beside variable names. Include order, comment text and string
literals are preserved. The formatter does not insert/remove braces or reorder
qualifiers. Long string literals and protected macros may exceed the column
target. Preprocessor directives and assembly strings remain part of the source
contract.

Editors can use `.clang-format` directly with the pinned executable; observe
`.clang-format-ignore` when formatting generated or third-party files.
CI checks formatting without modifying sources. Build and runtime checks remain
separate from formatting checks.

[LLVM clang-format documentation](https://clang.llvm.org/docs/ClangFormat.html)
describes editor integration and ignore-file behavior.
