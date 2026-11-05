# 2ine agent instructions

## Version numbering

- Use `x.y(build)` for 2ine releases, for example `0.5(27)`. Do not use
  three-part semantic versions such as `0.5.1`.
- Keep `VERSION` and macOS `CFBundleShortVersionString` in `x.y` form. Store
  the build number separately in `CFBundleVersion`; include it in displayed
  release versions as `x.y(build)`.
- Allocate macOS build numbers through `tools/macos_build_number.py`. Build
  numbers must increase; never reset, decrement or reuse them. Fix releases
  advance the build number without adding a patch component.

## Build locations and cleanup

- Put every build, test, package, temporary worktree and build-specific scratch
  output beneath the repository's single `build/` directory. Use named subtrees
  such as `build/runtime`, `build/swift` and `build/package`. Do not create
  top-level `build-*` or `cmake-*` directories or leave build trees in `/tmp`.
- Before starting a build, identify its purpose and reuse an appropriate active
  subtree where possible. Keep parallel configurations in separate subtrees.
- A build is temporary. Remove its subtree immediately after its purpose is
  complete, including copied apps, test balls, intermediate dependency builds,
  scratch worktrees and compiler caches created for that build. Successful
  installation or completed verification is the cleanup point; do not retain
  old builds or rollback app copies indefinitely.
- Before cleanup, put any reusable source changes in their maintained source
  location and any requested RE/documentation metadata in its persistent cache.
  Preserve only the small reports or receipts needed for review, outside the
  disposable build tree. A documentation/interface cache is data, not a build.
- Never delete user balls, original installation media, source checkouts with
  unrelated edits, or another active job's inputs as build cleanup. Check for
  active consumers before removal; when the user explicitly confirms none are
  active, proceed with removal of the obsolete build trees.
- End each build task with cleanup and report material artifacts intentionally
  retained and why. Do not leave cleanup as an unperformed suggestion.
