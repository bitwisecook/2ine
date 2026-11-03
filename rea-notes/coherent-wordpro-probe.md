# Coherent Word Pro diagnostic runtime

The private Word Pro diagnostic previously mixed modules copied from a development build
with guest native-module lookup in a ball cache. Two PMWIN instances can maintain different
window and handle state even when their bytes and Mach-O UUIDs match.

A separate immutable snapshot was packaged with `tools.macos_bundle` dependency relocation,
audit and signing. The current diagnostic app is
`build-lotus-visuals/Word Pro Coherent Probe.app`; its launcher executes its own
`Contents/Resources/Runtime/lx_loader`, pins the working directory and all native-library
search paths to that closure, clears inherited tracing, and bounds diagnostic logging.
Runtime capabilities are beside the loader, never in `Contents/MacOS`. Only the private
`build-lotus-visuals/WordPro.2ineball` was prepared. The user's Documents ball was untouched.

Headless generated LX imports and the actual visibly launched Word Pro process both loaded
PMWIN, PMGPI, PMWINX, VIOCALLS, PMSPL and lib2ine exactly once, from the isolated closure.
Every non-system image was inside that closure. Evidence and reproducible diagnostic scripts
are under the ignored `build-lotus-visuals` directory. The root agent owns desktop interaction.

A second, frozen source-RPATH control proved the product bug independently: ordinary
prepare-launch cache lookup loaded PMWIN and PMGPI both from the cache and via source-build
Mach-O dependencies. Adding cache-first `DYLD_LIBRARY_PATH` eliminated the duplicate native
state. The portable ball engine now emits that path on macOS, appends the receiving build
and inherited host value, reserves it against guest environment overrides, and clears it
between launches. Linux and Windows do not receive this macOS key. This does not change
hardened app entitlements; packaged providers already have relative dependency closures.

`tests/integration/macos_native_cache.py` creates a fresh source-style Mach-O mirror and a
private ball, imports PMWINX before PMWIN in generated LX code, and checks actual dyld load
events, not merely UUID equality. Five key native images must each load exactly once from
the cache. The targeted regression passed on macOS ARM64 with Unicorn; evidence:
`/tmp/2ine-dyld-cache-regression.log`.
