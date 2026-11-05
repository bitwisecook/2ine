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
