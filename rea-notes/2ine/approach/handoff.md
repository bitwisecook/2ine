# Approach handoff: source boundary

The user requested handoff. Do not resume implementation or launch probes until
continuation is requested. No commits or pushes were made by this agent.

The priority, Unicode and interlocked sources are frozen. Parent owns public
headers, api32 metadata, generated bridges and CMake integration. Another task
has extensive unrelated dirty Open32 text/metafile/batch edits in the shared
checkout: preserve them and coordinate before changing shared interfaces.

## Verified state

- Priority636/337: native scheduling, mocked policy/serialization, real macOS
  child and synthetic LX gates passed under parent integration. See priority.md.
- Unicode2073/2079: native conversion and clipboard checks passed. The corrected
  synthetic fixture explicitly passes `data=guest.data`, builds EXEs with a data
  page, and covers counted UTF16, guest guards and24/32-byte caller cleanup.
  Parent confirmation: `open32_unicode` passed after the EXE/data-page fix,
  together with the strengthened priority ESP check. The retained log is in
  `/Users/jimd/src/os2/.cache/os2-reports/2026-11-06/lotus-handoff/unicode-bridge-ctest.log`.
- Interlocked421 Decrement,422 Exchange,423 Increment: original raw bytes and
  SDK agree on4/8/4-byte caller-cleaned public arguments. INC/DEC return the sign
  of the new wrapping DWORD; Exchange returns the previous DWORD. Adapter uses
  real guest-backed runtime atomic operations, not shadow-copy RMW.
- `open32_atomic_backing` passed in the integrated runtime CTest at23:38 on
  November5 (0.23s). It verifies432,000 competing guest CPU/host API updates,
  unaligned and intra-mapping cross-page words, original return signs, wrap and
  guarded failures. **The integrated `open32_atomic` LX gate is still pending.**
- Ruff/ty and C formatting passed for this agent's new atomic/Unicode tests and
  sources. Nativei386 provider was source-reviewed; acceptance ran on Darwin
  with Unicorn. Cross-process Unicorn atomicity is not established.

## Last Approach probe

The private COW ball reached ten actual CP0/flags0/NUL-terminated Unicode calls,
then stopped at missing PMWINX423. It ran3.75s, exited1, had no preceding CPU or
MEM_BAD_POINTER fault, and left original tracked fingerprints unchanged.
RuntimeID was `e74c1feb4746479208421e7433c7c80e02587b45602722147eaea0c33fa8c74a`.
That plan predates integrated atomic exports: **reprepare from current runtime
before the next probe**. This is startup progress, not a working Approach UI.

No owned guest, SOMDD, Ghidra, Unicode or atomic worker remains running. A
read-only process check found only its own ps/rg commands. Do not kill unrelated
processes or launch the real Documents ball.

## Inputs and cleanup

Keep `build/lotus/approach-startup`: private COW ball, prepare/probe helpers,
request/plan, source fingerprints, current logs and receipts are next-probe
inputs. `prepare.py` initially asserts the clone does not already exist; use a
bounded reprepare of the existing private clone rather than rerunning it blindly.

This agent no longer consumes `build/lotus/baseline` or
`build/lotus/ordinal-audit`. Parent removed both completed trees after all
consumers released them and archived the evidence; see the main handoff.
Original PMWINX full analysis is durable at
`~/.cache/os2-re-module/ghidra-jobs/analysis-9lwae4ej/analysis.json`; source SHA and
analysis identity are recorded in unicode.md/interlocked.md. No staging cache is
needed to reuse those contracts. Completed owned compiler intermediates,
standalone test binaries and local duplicate RE derivatives have been removed.
The approach private ball and current probe logs are intentionally retained for
continuation. Do not remove shared build/runtime or another active job's inputs.

## Continuation prompt

Continue Approach and Freelance from the frozen shared checkout. Read AGENTS.md,
rea-notes/2ine/approach/{priority,unicode,interlocked,handoff}.md and the parent's
main handoff. Preserve unrelated dirty Open32 work. First ensure the runtime and
PMWINX export table coherently include atomic421/422/423 and Unicode2073/2079;
run the pending integrated open32_atomic LX gate and confirm the corrected
open32_unicode gate outside the macOS sandbox. Do not infer success from the
passing native unit alone. Reprepare the existing private Approach COW ball's
launch plan using current build/runtime, backend unicorn, resolve:false, then
run only its bounded dummy SDL probe and owned SOMDD process. Record exact next
blocker and original-ball fingerprint preservation. Use installed os2-re-module
with the exact original DLL hash and permanent cached declarations/raw evidence
before implementing another ordinal. Parent owns generated interfaces and shared
builds. Never modify or launch the user's original ball, claim UI acceptance
from a missing-import advance, commit, push, or discard unrelated work without
explicit continuation authorization. Keep all scratch under build/ and clean
completed owned scratch after durable evidence retention.
