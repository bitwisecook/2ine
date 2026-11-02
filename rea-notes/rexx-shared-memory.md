# REXX address-based shared memory

Scope: the native DOSCALLS 302/303 imports discovered while loading original
REXX support. This is emulator behavior, not a byte-identical reconstruction
of original OS/2 code. The source interface follows the local original
`bsedos.h` declarations; the portable bridge passes the guest numeric address
before an attachment exists.

`DosAllocSharedMem` now allocates actual host shared backing even without an
OS/2 name. Generated names include the original process ID and a locked
process serial and are hashed with the existing ball isolation. Numeric slots
are 64 KiB aligned in the existing shared range, preserving the same guest
address in each attached process.

The HAL can find an existing object by exact numeric slot. It checks the
recorded backing inode/device identity and length while retaining the live
object. A lookup does not create a missing data object or its coordination
files. The generic access sidecar stores owner/attributes and bounded peer
grants against the backing identity. New backing clears retired access records,
so a reused name does not inherit old grants.

DOSCALLS stores allocation/access flags. Address-based attachment accepts the
owner, OBJ_GETTABLE regions, or explicit peer permissions, rejects excess
access and unknown flags, and maps the backing at the recorded address.
Existing valid local mappings can be revived after DosFreeMem's retirement.
DosGiveSharedMem rejects private allocations, unknown targets and excess
permissions and records a grant; it does not claim to have already attached
memory in the remote process. Anonymous regions require OBJ_GIVEABLE or
OBJ_GETTABLE before giving. Named regions retain their public discoverability.

Target IDs resolve to self, parent, retained child processes, or a live runtime
peer in the same ball/IPC/C: scope. Each loader worker retains a per-PID shared
lifetime lease; a read-only probe rejects idle or different-scope records. This
allows an independently started original REXX service to exchange grants with
all of its clients, including detached workers. It does not probe arbitrary host
processes. Named system objects remain discoverable within the same IPC/ball
context. User-provided ordinary PID values are not host pointers.

Validation on 2026-11-02:

- `shared_lifetime` native test passes, including exact-slot lookup, missing
  slots, owner/attribute publication, accumulated peer grants, and retired
  backing identity rejection.
- `legacy_core`, `legacy_sessions` pass after rebuilding all dependent native
  libraries against the current loader state layout.
- `python3 tests/integration/shared_memory.py --build build-image-app-runtime --backend unicorn`
  passes outside the sandbox. An LX parent and separately spawned child verify
  anonymous OBJ_GIVEABLE grants and OBJ_GETTABLE attachment, private-memory and
  excess-access rejection, invalid flags, raw address attachment before a local
  mapping exists, shared changes visible in both workers, and retired mapping
  revival. Each case uses an isolated IPC directory.
- The generated USE16 parent/LX child session test passes with the same backend,
  including the segmented termination payload's lifetime.
- The source ownership checker and Ruff checks for the new integration files
  pass. These synthetic guests do not by themselves prove the original REXX
  interpreter or Lotus installer completes.

Independent-peer regression: two workers launched separately by the host exchange
an anonymous memory grant and observe each other's writes. A third live worker
registers in another ball while using the same IPC directory; granting to it
returns ERROR_INVALID_PROCID. This case exercises the process lease rather than
the pre-existing parent/child exceptions. The shared lifetime test also verifies
live observation while handles and views remain, and an idle result after the
last reference disappears, without retaining an observer reference.
