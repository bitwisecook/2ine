# OS/2 component operations

The shared ball engine supports background installation and conservative removal without tying the native UI to a long-running request. The engine itself remains synchronous; a frontend starts an operation on a worker and polls from independent requests.

## Request contract

- `import-os2-support` retains its existing category/exact-source fields and accepts optional `operationID`.
- `os2-component-state` accepts `paths: [guestDestination]` and optional `operationID`. Empty paths and no source selection reads progress only, without a media plan or ownership scan. An unknown ID returns `progress: null`, including the interval before the worker starts.
- `remove-os2-components` accepts `paths`, or exact `fileSelection` plus `sources`. The latter makes one media plan and resolves selected sources to their actual owned destinations, including preserved variants. Selecting an uninstalled alternative never authorizes deleting a canonical file owned by another source; that file is reported as protected with the selected source identified. The result contains `removedPaths`, `preserved` file records, and final `progress`.
- `cancel-os2-component-operation` accepts `operationID`, returns `cancelRequested: true` and current progress or null. A request made before operation startup is retained.

An explicit operation ID contains 1–64 ASCII letters, digits, underscores or hyphens. Existing operation IDs cannot be reused. Callers normally supply UUIDs. An omitted ID receives a unique generated token.

Progress has `operationID`, `kind` (`install` or `remove`), `status` (`running`, `complete`, `failed`, `cancelled`), `phase`, `activeSources`, `activePaths`, `completedPaths`, `completedCount`, `totalCount`, `updated`, and `logPath`; failures also have `error`. Phases include `planning`, `extracting`, `staged`, `installing`, `removing`, `finishing`, and final `finished`. The recent completed-path list is capped at 256 and sets `completedPathsTruncated` when older paths are dropped. The count remains cumulative. Frontends should refresh full visible state at completion, even if their polls missed intermediate paths.

A failed or cancelled synchronous mutation returns the normal diagnostic error. Its final progress and log remain readable using the supplied operation ID. Cancellation is cooperative between file/container work; it cannot interrupt a host filesystem or decompressor call already in progress.

## Live file state

The state response contains `files`, `installedPaths`, `managedPaths`, `modifiedPaths`, `unownedPaths`, `protectedPaths`, and `progress`. Each file has `path`, `present`, `managed`, and one stable `status`:

| Status | Meaning |
| --- | --- |
| `missing` | No regular file exists at the private destination. |
| `managed` | A file installed by this mechanism still matches its ownership record. |
| `modified` | A hash check established that the installed bytes changed. |
| `unowned` | A present file has no usable installation ownership record. |
| `protected` | The path is redirected, outside private C:, a runtime-cache path, unreadable, or otherwise unsafe. |

Only `present && managed && status == "managed"` is eligible for a Remove button. A status record may contain `reason`, `category`, `source`, and `sha256`. A protected path is never followed to read external contents.

Normal polling compares the complete saved file identity (device/file ID, size, modification/change times and fractions). Changed identities require a hash check; a bounded thread-local cache retains hash-verified verdicts for unchanged current identities without writing files. Removal performs a fresh content hash and validates identity again before deletion. State does not write manifests, logs, caches or media records and does not rescan an ISO.

### Source-aware retained variants

For a known media plan, state also accepts `selection: [{source, path}]`, where `source` is an exact opaque source ID and `path` is its planned guest destination. This is independent of optional old `paths`; both can be supplied. No `sources` media reopening or verification is involved.

Each matching ownership record contributes a `files` item whose `selectedSource` identifies the requested source, `plannedPath` retains the requested canonical tree destination, and `path` identifies its actual installed destination. Preserved alternatives therefore resolve to their real `C:\OS2\INSTALL\MEDIA\<sha>\...` paths. More than one actual record for a source produces multiple items; frontends group by `selectedSource`. Source lookup uses a sorted in-memory ledger index rather than scanning the ledger once for every selected source. A request accepts at most 100000 pairs and returns at most 100000 resolved file records, including any old path records.

When a selected source has no ownership record, the canonical destination is checked. An unowned existing file remains `unowned`, because its provenance is unknown. A destination owned by another source does **not** make this alternative installed: its source-specific item is `missing`, `present: false`, `managed: false`. Paths-only state still truthfully reports that canonical file as present. Unsafe canonical paths remain protected. The parallel installed/managed arrays contain actual destinations from the corresponding records.

## Ownership, partial operations, and concurrency

`os2-components.json` stores ownership by canonical guest destination. Records include exact source, category, SHA-256, operation ID and file identity. Every newly copied file appends a complete record to `logs/os2-components/ownership.jsonl` immediately. Finishing atomically snapshots the ledger and clears that journal. A later operation folds complete rows from an interrupted job and discards its unfinished trailing row before appending new records. Readers retry the snapshot/journal handoff if the snapshot changed around their read.

Successful copies remain installed and owned if a later file fails or the user cancels. No rollback is claimed. Legacy CMD/REXX import and automatic dependency recovery use this mechanism as well. Existing files are never adopted simply because they resemble media contents; files imported by older versions therefore remain unowned and protected. Original CMD import now stages and creates destinations exclusively, preserving files that appeared after its initial scan.

Progress lives in fixed `cache/component-operations/<ID>.json` records; the latest ID lives alongside it. Cancellation uses a fixed `<ID>.cancel` marker. Final reports, including failures and partial completion, are in `logs/os2-components/<ID>.json`. Read progress uses the current ball's log location after relocation.

A nonblocking per-ball filesystem lock excludes other component installs/removals across threads and processes. Polling and cancellation do not acquire that lock. A process exit releases it. Frontends additionally prevent guest launches and other mutations of the affected ball while a job is active, and keep other balls usable. Requests have independent contexts, thread-local parser/media state, and exclusive shared-media cache leases.

Destinations are restricted to private C:, checked segment by segment without symbolic links; runtime cache paths are excluded. Removal opens and verifies a regular file and checks the directory-entry identity before unlinking. POSIX does not provide compare-inode-and-unlink atomically: an uncooperative external process could rename a replacement into place after the final check. The cooperative component lock excludes other participating jobs, not arbitrary external filesystem mutations. Windows has handle-based deletion source code, but this project still has no Windows runtime qualification.

Removing an essential tracked SOM DLL, daemon/server, or repository removes only the `owner: os2-som` daemon startup record. Manual startup records and message-only removals are preserved. Existing configuration search paths are retained.

## Validation

All fixtures use disposable temporary balls and generated media; no user ball or desktop clipboard is accessed.

```sh
cmake -S ball -B build-component-operations-current
cmake --build build-component-operations-current -j4
TWOINE_BALL_LIBRARY=$PWD/build-component-operations-current/libtwoine_ball.dylib \
  python -m unittest tests.tools.test_os2_component_operations \
    tests.tools.test_os2_tree tests.tools.test_os2_support tests.tools.test_os2_dependencies
```

The focused cases cover changed/unowned preservation, exclusive removal, edited metadata with unchanged bytes, pre-start cancellation, unknown progress IDs, token validation, symlink/traversal/cache protection, deterministic partial commit failure, interrupted-journal restart, exact-source variant removal, dependency recovery ownership, and managed-only SOM service cleanup. A FIFO-gated source proves the job remains running while another request reads `extracting` progress and a second mutation receives the busy diagnostic. The helper's disposable POSIX harness additionally qualified cross-process contention, process-death release, stale-inode refusal and symlink ancestry refusal.
