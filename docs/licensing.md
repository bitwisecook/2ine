<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Licensing

**Read the file header. This tree has two source-license categories.**

| File origin | Notice / license |
|---|---|
| Wholly AI-authored | `Public domain. No rights reserved.` at the top |
| Original 2ine or derived from it | Original notices and [2ine's zlib-style license](../LICENSE.txt) |
| Third-party code or binaries | Their own notices and licenses |

AI-assisted editing does not make an upstream file public domain. Extracting or
moving original code does not change its license. Generated upstream glue
retains the original generator/template license.

The extracted native execution adapter, EAX bridges and adapted 32-bit module
macros remain under 2ine's license. The new CPU adapter, host contracts, guest
marshalling and tests are marked individually.

Notices use comments for source, scripts, CMake, YAML and Markdown. JSON uses
its first `_notice` field so the file remains valid JSON. A script's shebang
stays first; its notice follows immediately.

**Unicorn is GPLv2, even though the adapter is public domain.** The adapter's
notice does not relicense Unicorn or a linked distribution. Preserve dependency
notices and meet their source/distribution requirements.
[Versions and dependency licenses](dependencies.md).

Historical compiler/SDK fixtures are supplied separately and are not licensed
or redistributed by this project. `LICENSE.txt` is preserved unchanged.

Sampled MIDI defaults to FluidR3 GM under Frank Wen's MIT license, with his
recording/contributor account retained. FluidSynth uses LGPL-2.1-or-later;
libInstPatch is LGPL-2.1-only. LiquidSFZ retains MPL-2.0, with its
secondary-license-compatible terms used when combined with the GPLv2 runtime;
its embedded pugixml retains MIT. The explicit minimal TinySoundFont fallback
retains MIT. GeneralUser GS is an optional user download and is not bundled. [Doom source builds](doom.md)
use separately supplied third-party source under its existing notices; neither
that source nor game WADs are relicensed or distributed here.

Optional Munt/libmt32emu retains its LGPL-2.1-or-later license. Preserve its
notices and applicable source/relinking requirements when distributing linked
builds. SoundFont forum discovery records author and license claims separately
from the forum's own post license. Fetched/user-imported banks stay in the
user's cache with receipts; the downloader does not change their licenses.

The [project source and credit catalogue](project-sources.md) is generated from
`tools/data/project-sources.json` and shared by both native About views. Packaged
inventories record actual host dependency versions. Source/download links and
license texts do not by themselves replace the complete corresponding-source
distribution required for GPL/LGPL binary releases.

The pinned FFmpeg source profile reports LGPL 2.1 or later. System FFmpeg
providers can enable GPL or other options: packaged `ffmpeg-build.json` and
the installed catalogue record the actual linked library's license and build
configuration. Retain the applicable FFmpeg notices and source/relinking
materials for the chosen distribution; the source profile's license does not
describe every possible system provider.
