# Shared ball package rename (2026-11-03)

`rename-ball` accepts `{ball,text,expected?}` and returns `{path,snapshot}`.
Text is a new basename, with or without `.2ineball`; the parent directory stays
the same. The optional expected value is SHA256 of the original ball.json.
Mac/GTK frontends own stopped-guest/busy guards and URL-keyed preference/draft
migration; the shared engine has no process-manager state. Trash remains a
native frontend operation rather than recursive backend deletion.

The source must be a regular `.2ineball` package with a regular version1 JSON
manifest and a private C: directory. Package/manifest symlinks, home/root paths,
unsafe names and existing destinations are rejected. Case-only renames on a
case-insensitive filesystem report a collision. Atomic no-replace primitives
are macOS renamex_np(RENAME_EXCL), Linux renameat2(RENAME_NOREPLACE), and Windows
MoveFileEx without replacement. Unsupported hosts/filesystems fail instead of
using an overwrite-prone fallback.

Only supported, absolute package-owned host fields are rebased: directory/image
drive mappings, runtime.build, and USER_INI/SYSTEM_INI/COMSPEC overrides in global
or application environments. Installed sound-bank file paths inside soundfonts
are normalized to their supported package-relative form. Relative paths, guest
DOS paths, external mappings and opaque/unknown metadata remain unchanged.
CONFIG.SYS, binary profiles and documents are never searched/replaced. Generated
2ine.cfg/audio.conf/runtime launch paths are refreshed by the existing launch
preparation path, as for an externally moved ball; cached binaries are retained.

The engine stages the original/prepared manifests privately, verifies unchanged
source bytes before and after the atomic package move, then commits any path
changes. A post-move snapshot failure rolls the package and manifest back.
Rollback never replaces an item that appeared at the old name, and does not
overwrite a manifest changed again by another writer. In those exceptional
cases it retains the package/recovery data and reports its current location.
The existing engine's compare-before-write model cannot make arbitrary external
writers participate in an atomic multi-file transaction.

Acceptance:

- `TWOINE_BALL_LIBRARY=$PWD/build-image-app-runtime/libtwoine_ball.dylib python3 -m unittest tests.tools.test_ball_rename -v`:7/7 pass. Covers names/extensions, exact guest/document/profile preservation, internal/external path boundaries, removable image paths, opaque large integers, environment paths, collisions, stale revisions, symlinks, non-package sources and a real post-commit rollback. The rollback fixture uses an absolute internal C: symlink that becomes inaccessible after moving; no synthetic production fault hook is added.
- `ctest --test-dir build-image-app-runtime -R '^ball_relocation$' --output-on-failure`:passes. The integration now performs the shared rename action before the subsequent cross-directory move and actual generated LX application launch, preserving guest state and the foreign cache.
- Ruff, ty, architecture boundaries and diff checks pass. Logs:
  `/tmp/2ine-ball-rename-{build,tests,lx-tests}.log`.

Actual host rename and rollback acceptance was on macOS. Linux/Windows adapter
source is provided, with their runtime/GTK UI checks belonging to those hosts.
Root separately reports44 Swift model/core tests passing with the shared action;
that frontend acceptance is not substituted for the engine tests above.
