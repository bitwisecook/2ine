# Component provider policy, destination folders and media validation

## Tree policy

The tree is a presentation/selection model distinct from the flat extraction
plan. Its existing provider records gain these fields:

| Field | Meaning |
| --- | --- |
| `nativeSelected` | The selected provider setting uses 2ine rather than an original engine. Availability is separate. |
| `choiceSupported` | Choosing an original engine is supported by the provider model. |
| `originalAvailable` | At least one original file is present in this media group's tree. |

`originalAvailable` describes source payload presence, not whether an original
engine is qualified or selectable. Original printer and multimedia support
files can exist while `choiceSupported` remains false. OpenGL permits provider
selection; printing and multimedia currently use 2ine only.

Mesa, including an absent saved preference, marks the original OpenGL group
disabled. Selecting Original enables it. Off is stageable and reports
`nativeSelected:false`: files may be installed before enabling an engine.
Printer and multimedia groups stay disabled under their native providers.

After construction, native provider policy recursively marks component groups,
semantic subgroups, directories and file leaves `disabled:true` and
`selectable:false`. The provider's `kind:"setting"` child remains explicitly
`disabled:false`, `selectable:false`: it is an action, not an extraction
checkbox. Whole-tree/component selection must exclude disabled descendants.
Frontend adapters own their selection logic and replan when settings change.

This does not change `plan.files`, source priority, protected-module policy,
explicit `fileSelection`, or CLI staging. A deliberate request to copy original
OpenGL files while Mesa is selected still works without changing the selected
engine. Native engine availability and original-engine qualification remain
separate runtime capability concerns.

## Guest destination hierarchy

Each semantic group now contains naturally nested destination directories:
for example `fonts.truetype → C:\ → OS2 → DLL → FACE.TTF`. Semantic font/help/
SOM subgroups remain intact. Directory nodes have `kind:"directory"`, an
inherited `category`, the canonical guest directory in `path`, its component
name in `title`, and their own `children`.

IDs use `<semantic-group-ID>.directory.<UPPERCASE-GUEST-DIRECTORY>`. Thus folder
matching is case insensitive, repeated plans are stable, and identical paths
in separate semantic subgroups do not collapse together. Directories select
eligible descendants, but their kind does not imply whole-component dependency
selection. Component requirements remain on semantic component groups. File
leaf IDs, exact sources and full destination paths are unchanged. Drive rows
are titled `C:\`.

## Read-only media verification

`validate-os2-media` accepts `{sources:[...]}` and returns:

```json
{
  "sources": ["verified normalized source paths"],
  "physicalSources": ["canonical top-level archives/images/folders"],
  "media": [{"path":"...", "status":"verified-os2-media", "selected":true}],
  "complete": true,
  "errors": [],
  "skippedSources": [],
  "readStats": {}
}
```

Media records also contain the existing command/version signature and search
root metadata. The action normalizes each explicit input through the existing
ISO/archive opener, then deduplicates the normalized set. Finding an ISO in one
input cannot silently omit a separately selected folder or archive.

Verification reuses the existing actual CMD.EXE and SYSLEVEL.OS2 signature
checks. Partial installation layouts use the existing combined floppy-set
rule, including explicitly discovered sibling component directories from the
same OS2IMAGE container. An unrelated application image cannot inherit proof
from a separately selected OS/2 CD. A normal unverified selection returns
`complete:false`; parser failures may still return an error. The returned
`sources` contains only verified members, and `complete` requires every
normalized selected member to be verified. Global media preferences should
only be updated after `complete:true`.

The action disables dependency index cache reads/writes and does not import,
remember media, save manifests/configuration, or write logs. It does not use
the ball's remembered sources or drive mappings as substitute inputs. Physical
source paths resolve the outer archive, including ZIP→ISO nesting, for native
picker initialization. Existing import/remember actions retain their behavior.

## Original multimedia boundary

`os2libs/video_decode.c` uses the native FFmpeg decoder directly. The native
MDM digital-video frontend feeds it and the retained display/audio paths.
`os2libs/mmio.c` currently implements the memory-file IOProc, not an installable
original video IOProc/codec dispatcher. MDM/MMIO remain native-provided system
modules. Extracting original codec DLLs or tools does not create an operational
original multimedia engine; `choiceSupported:false` is therefore intentional.
No new decoder/backend or driver qualification is claimed by this UI metadata.

## Verification

```sh
cmake -S ball -B build-provider-tree-current -DCMAKE_BUILD_TYPE=Debug
cmake --build build-provider-tree-current -j6
TWOINE_BALL_LIBRARY="$PWD/build-provider-tree-current/libtwoine_ball.dylib" \
  python3 -m unittest tests.tools.test_os2_tree tests.tools.test_os2_dependencies tests.tools.test_os2_support
```

All 58 focused tests pass in 35.943 seconds. They check default/Mesa/Original/
Off policy, disabled descendant eligibility with live settings links, original
file staging without provider mutation, nested destination folders, case
insensitive grouping, independent font subgroups and unchanged exact leaves.

Read-only tests cover nested ZIP→ISO physical paths, valid media, signature-
split sets, normalized CID siblings, a Lotus ISO alone and mixed with valid
OS/2 media, and byte-identical contents of every ball file before/after
verification. No actual ball or desktop UI is changed. Ruff, ty, pinned
clang-format dry-run and diff checks pass. Mac and GTK adapters are separate
integration work owned by their respective agents; this checkpoint establishes
the shared core contracts and behavior.
