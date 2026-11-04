# Desktop 0.4 acceptance

The component UI work was completed before the application-list/session phase.
Media selection and component trees stay inline, filesystem state follows exact
sources without reopening media, and per-ball background operations expose
progress/cancellation and ownership-safe removal. Native-provider links, cached
media defaults, destination folders and one-shot settings navigation are covered
by the native control/model regressions and offscreen light/dark captures.

Application lists retain genuine Workplace Shell license/document shortcuts,
sort real icons first by default, preserve explicit column order, and remove one
launcher through the existing undoable command. New WPS tombstones identify the
removed object rather than hiding an unrelated object after handle reuse.

macOS and GNOME session controllers allow ordinary applications in one ball to
run concurrently with independent logs and targeted Stop. Startup services have
manager-local ownership/refcounts and are released after their last client.
Installers exclude other runs in the same ball. Session IDs do not change the
ball's IPC namespace. Host-backed PM profile transactions now refresh under a
cross-process lock before applying mutations; reads refresh stale snapshots with
an identity-based fast path. Native filesystem layouts remain in the selected
macOS/Linux HAL adapters.

Final combined verification before committing:

- 79 Swift tests pass (41 model/native-control and 38 BallCore), with
  `TWOINE_TEST_RUNTIME=build-image-app-runtime` enabling the real generated-LX
  shared-memory exchange test. This includes separate logs, captured Stop
  targets, cross-ball navigation, installer exclusion, shared daemon ownership,
  cancellation without abandoning a child, and the source-aware component UI.
- 73/73 runtime/source-ownership checks pass in 43.40 seconds. The profile
  regression uses separate long-lived processes, binary values, concurrent
  writes, deletion, stale reads, lock-owner death and damaged-file preservation.
  A baseline implementation fails its first cross-process read assertion.
- 282/282 tooling tests pass in 74.925 seconds. A previous supervision assertion
  was updated for the intentional new owned-startup supervision contract;
  unsupported older runtimes and ordinary applications remain unsupervised.
- Ruff check/format (155 files), ty, pinned ClangFormat21, export generation and
  source diff checks pass.

Logs are `/tmp/2ine-final-{swift-tests,ctest-clean,tools-clean}.log`; native UI
captures and harness limits remain in `build-tree-current/inline-activity-qa`.
Tests used disposable balls, generated guests and owned helpers. No actual user
ball, proprietary application workflow or desktop clipboard content was used.

Limits remain explicit: live GTK interaction needs a Linux desktop; offscreen
macOS rendering does not qualify desktop input or a fresh SmartSuite install.
DDE/OLE document embedding and full SmartSuite cooperation are not implemented
or qualified. Startup leases/install exclusion cover one manager process,
not separate manager processes or externally started SOMDD instances. Profile
transaction guarantees cover host-backed profiles, including the normal ball
USER/SYSTEM INIs; image-member profiles and non-cooperating writers are excluded.
The receiving runtime profile retains Unicorn/system SDL/system FFmpeg with
video, PCM, MIDI and DIVE enabled; Mesa is not compiled into this profile.
