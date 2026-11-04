# CMD bootstrap and existing-ball landing facts

The reported Lotus `D:\INSTALL.CMD` failure initially came from selection without installation. The first read-only inspection of `Documents/Lotus.2ineball` found only private `CONFIG.SYS`: no CMD/REXX payload, no ownership ledger, and no component-operation record. The manifest had no COMSPEC override; PATH/LIBPATH defaults were normal. Remembered OS/2 ZIP→ISO source folders did not prove an import had happened.

While investigation continued, user-driven imports populated the ball. CMD.EXE's modification time was 17:11:28 on 2026-11-04; a later ownership record at 17:16:25 included REXXCRT.DLL. The agent did not import files, alter this ball, or run its installer. The new read-only check then correctly identified INSTALL.CMD as REXX and reported it ready with `C:\OS2\CMD.EXE`.

## Read-only frontend contract

`launch-prerequisites` accepts ordinary `arguments` (`run`, `install`, or saved `launch`) and application environment/working-directory fields. A runtime build is not required. It returns:

```json
{
  "program": "D:\\INSTALL.CMD",
  "commandFile": true,
  "rexx": true,
  "ready": false,
  "requiredCategories": ["cmd"],
  "requirements": [{
    "id": "cmd",
    "title": "CMD and REXX Support",
    "missingPaths": ["C:\\OS2\\CMD.EXE"],
    "reason": "..."
  }],
  "issues": [],
  "reason": "..."
}
```

An available interpreter adds `interpreter`. CMD/REXX and verified SmartSuite support requirements point only at the `cmd` extraction group. The endpoint does not install files or expand into a full OS/2 extraction. A missing configured standard `C:\OS2\CMD.EXE` remains recoverable through that group. An invalid nonstandard COMSPEC or malformed environment returns a configuration `issue` rather than creating an endless extraction/retry loop. Missing selected programs or unavailable media remain ordinary actionable errors.

The existing launch path still enforces bootstrap requirements. Error text now names OS/2 Components and CMD and REXX Support, and asks users to wait for installation to finish. It does not refer to the retired CLI-only path or require an obsolete Install Selected button. Frontends can retain the pending launch, direct the one-shot CMD group choice, and recheck readiness after the component queue becomes idle before offering continuation.

Readiness is bootstrap presence and command-file detection, not proof of every dynamic DLL import or OS/2 API. Normal dependency checking still supplies that separate evidence. Script detection and interpreter resolution match the actual launch logic; a REXX comment prefix on an attached ISO is read without extracting or executing the script.

## Existing-ball navigation

`ball-navigation-state` returns `validApplications`, `installedComponents`, `page`, and `truncated`.

Priority is:

1. A valid saved application → `applications`.
2. Existing OS/2 payload with no valid saved application → `application-installation`.
3. Neither → `system-components`.

The frontend retains the distinct new-ball intent and opens Drives immediately after creation. Existing-ball checks resolve saved paths with drive/case handling, recognize OS/2 executable headers while excluding DLL modules, and require CMD/REXX bootstrap for command launchers. Missing/ejected/invalid records do not make the whole navigation request fail. This is not a full executable/import compatibility guarantee.

Installed component facts use actual owned destinations first, then bounded known private OS/2 payload roots for legacy imports. CONFIG.SYS, local INI/registration files, native `OS2\DLL\2INE` caches and remembered or checked media choices do not count. Directory/application processing is bounded and reports truncation. The read-only actions resolve attached images directly: loading the request must not create `devices/*.media` records as an incidental side effect.

## Verification

- 48 prerequisite, ball-engine and support tests passed: `/tmp/2ine-prereq-tests-final.log`.
- Fixtures prove selected media differs from installed support; only CMD/REXX is imported; later command preparation uses `/C CALL`; REXX on an ISO is detected without execution; bad COMSPEC does not request another extraction; modern error text matches current UI; navigation excludes stale app records/profiles/cache and accepts actual applications.
- Byte snapshots prove the status/navigation calls do not mutate the ball, including an attached-image fixture whose device-control record did not already exist.
- Source: `ball/launch_prerequisites.h` and bounded hooks in `ball/ball.c`; tests: `tests/tools/test_launch_prerequisites.py`.
