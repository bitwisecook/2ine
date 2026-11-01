# Sampled MIDI synthesis

MMPM's original host implementation played all MIDI notes as sine tones. This
preserved note timing but lost Doom's instrument program changes, percussion and
pitch controls. The default application profile now uses the unmodified
TinySoundFont 0.9 implementation and the complete GeneralUser GS 2.0.3 bank.
No guest instructions or WAD data were changed for this audio implementation.

Official source identities and SHA-256:

| File | Upstream revision | SHA-256 |
|---|---|---|
| tsf.h | schellingb/TinySoundFont `853a0a171759f1ddba0de1442133a75912bbeffa` | `70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197` |
| TinySoundFont-LICENSE | same | `ef10cb839de73fb6d5d32ea7151979e87809a6df70dc7e7ee8457ae1c9e7aa7e` |
| GeneralUser-GS.sf2 | mrbumpy409/GeneralUser-GS `684543d5e5efaef08d02be50dcda8d552478fa60` | `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe` |
| GeneralUser-GS-LICENSE.txt | same, documentation/LICENSE.txt | `7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be` |

Direct upstream files and API metadata are retained in `.state/2ine/doom/midi/`.
The upstream bank's license explicitly permits software-project packaging and
modification, while describing uncertainty about some historical sample origins;
retain the full upstream text. TinySoundFont is MIT-licensed. No upstream issue,
patch or pull request is involved. Product source contains neither copied vendor
code nor opaque binary dependencies; CMake downloads or reads hash-verified
inputs and stages licenses with the output bank.

CMake's default sampled profile fails on unavailable dependencies instead of
silently downgrading playback. Offline provider:
`-DTWOINE_MIDI_DATA_DIR=$PWD/.state/2ine/doom/midi`. Explicit
`-DTWOINE_MIDI_SYNTH=OFF` retains a clearly labeled oscillator fallback. Runtime
`TWOINE_SOUNDFONT` permits user-supplied complete SF2 banks; each opened synth
checks bank-0 programs 0..127 and bank-128 percussion preset 0 before playback.

MDM retains program/controller/pitchwheel events in addition to notes and tempo.
Same-tick events preserve input order. The audio callback dispatches events at
sample boundaries and renders blocks up to the next event, with 256-frame
stack scratch buffers in the wrapper. Voice/channel resources are preallocated.
Channel volume, expression, sustain, bank/program and pitchwheel messages use
TinySoundFont's channel API. MCI play resets channels and releases old voices;
close releases the SoundFont. MCI volume scales current PCM while mixing.

A synthesis contract caught that TinySoundFont's global gain update does not
change already-started voices; the wrapper now scales produced PCM, making mute
and live MCI volume changes immediate without waiting for the next note.

Standalone offline configure/build of the actual `cmake/Midi.cmake` and
`tests/os2libs/test_midi_synth.c` passes. It checks nonzero finite rendered audio
from every one of the 128 melodic programs, distinct piano/guitar samples,
distinct kick/snare samples, channel volume and expression mute, pitchwheel
waveform changes, all-sound-off, master mute and sustain release. The test's
initial host stub supplies only executable-path discovery; it is not used for
any audio synthesis. Integrated native/portable results belong in the parent
run ledger after their current application harness finishes.

Limits: the shared host stream is mono; this does not provide stereo positioning.
TinySoundFont explicitly lacks complete SF2 modulation, chorus and reverb.
GeneralUser GS uses such features, so sampled program/percussion playback is
verified without claiming exact bank-authoring or hardware MIDI fidelity.

The default network provider also configured successfully in isolated
`.state/2ine/doom/midi/fetch-test`, fetching all four files from their pinned
upstream revisions and verifying the declared hashes. Offline provider builds
and tests pass independently. The fallback wrapper compiles with
`TWOINE_MIDI_SYNTH=0`; native i386 GCC and portable Clang syntax checks of the
integrated MDM changes pass (the extra `-Wextra` native probe reports existing
partial LxExport initializers in generated glue).

## SoundFont catalog and user selection

The maintained `tools/fetch_soundfonts.py` and `tools/data/soundfonts.json`
now list/download/select complete banks and link primary author resources.
GeneralUser GS stays the catalog default, accurately described as Roland GS
compatible rather than as a Roland ROM/SC-55/MT-32 hardware clone. The second
pinned bank is TimGM6mb from the official MuseScore source immediately before
its replacement by FluidR3, revision `6bf0564e3fa76d5e1d7dd7d96f49124fef1074a8`:

- `share/sound/TimGM6mb.sf2`: 5,969,788 bytes, SHA-256
  `c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854`.
- `LICENSE.GPL`: 18,770 bytes, SHA-256
  `5911e49c99b4ee7e211daaae06b742821650dc501bff7c9a82fbba7811de01e8`.

MuseScore's official handbook identifies this bank as GPL version 2. Tim
Brechbill's own current direct SF2 differs (5,994,284 bytes, SHA-256
`82475b91a76de15cb28a104707d3247ba932e228bada3f47bba63c6b31aaf7a1`);
that author download was inspected, not silently substituted for the pinned
historical redistribution. Both fetched catalog banks pass RIFF bounds and
all-128-program plus standard-percussion inventory validation.

Actual script commands fetched GeneralUser GS and TimGM into
`.state/2ine/doom/soundfonts-cache`, including hash-verified license notices.
The initial workspace selection used GeneralUser GS. After the user's explicit
SC-55 upload, `use-file` imported the unchanged supplied file into
`user/ae2e56711dc40835d3f97d975d80eadb2abdc76dc9da8b055f7a3c18dab20974/bank.sf2`
and selected it in `.state/2ine/doom/audio.conf`. Its `receipt.json` records
user origin, original path/name, size and digest. No user SF2 was placed in the
redistributable catalog or product source, and no global user config changed.
The launcher can consume this setting through `TWOINE_AUDIO_CONFIG`.

Seven owning unit tests pass for atomic verified streaming/cache repair,
partial/oversized/mismatched downloads, malformed or incomplete SF2 rejection,
preserving unrelated settings, config/environment selection, catalog traversal
and insecure-URL rejection, and content-addressed user imports/receipts.
Pinned Ruff 0.15.20 lint/format and ty 0.0.57 checks pass on these files.

## VOGONS external collection automation

The explicitly selected public topic was inspected directly:
<https://www.vogons.org/viewtopic.php?t=88648>, title
“New SC55 Soundfont 266MB (all new 44.1k samples)”. Actual topic HTML contains
five posts on one page, with 21 unique outgoing/attachment links. Author
`zz_denis` describes RA90/SC55mk2 recordings and SC55 v0.5 dated 2022/7/9,
claimed size 284 MB. A later post links a KORG i3 (X3R) GM bank. Other Drive
links include audio comparisons, MIDI test files and a configuration example;
they were not reclassified as SoundFonts merely because of their host.

Ordinary curl with a normal browser User-Agent successfully read the public
page; urllib alone received HTTP403 and the web browser tool HTTP503. The tool's
ordinary-curl page fallback now succeeds. Both the live fetch and retained-HTML
parse produce complete five-post/21-link inventories in
`.state/2ine/doom/vogons/live-inventory/` and `inventory/`. Exact HTML files and
hashes are retained there. Author labels use username anchors and exclude
status such as “Offline”. Post text is source data, not executed instructions.

The selected SC55 v0.5 link ID is `cf041549c96f83c7`, pointing to the author's
Google Drive file `1wdYpwoCka8r7ZuIzPn1CHy13_4aP9oSq`. The actual automation
followed Google's normal public large-file confirmation and retrieved
`SC55_zzdenis_v0.5.sf2`, **298,185,428 bytes**, SHA-256
`c8c2aad1fe4cc55a7cfdf66307cf2fbe7e9e5407a8c8d4e8101ab96444c3c00d`.
It is a real RIFF SF2, with all 128 GM programs and standard percussion present.
The receipt is under `.state/2ine/doom/vogons/downloads/<sha>/receipt.json` and
retains selected post observations and the resolved download URL. Provenance
labels were refreshed after correcting the username/status parser; file bytes
and hash were unchanged. This is an observed first-download identity, not a
publisher-provided expected digest.

The selected forum bank stays private in scratch cache. It was **not selected**
as the runtime default and was not added to the pinned redistributable catalog.
The user's separately uploaded SC55 bank remains selected and has a different
SHA-256; no claim that it is the same bank is made. The forum's CC-BY-4.0 post
footer and a public-domain screenshot attachment do not establish an external
bank license; the receipt explicitly leaves the bank license unknown.

Owning tests now total 13 across `test_soundfonts` and
`test_vogons_soundfonts`. New coverage includes actual-style forum markup,
username/status separation, topic-bound pagination and incomplete caps,
ordinary curl fallback, public Drive confirmation, binary receipt/hash/coverage,
rejected landing pages and byte/hash limits, and selected ZIP extraction with
path limits. No bank download occurs in the unit suite. Actual live discovery
and one selected 284 MiB-class fetch provide the separate network/runtime proof.

## mt32-pi recommended-source choices

The user's latest preferred source is
<https://github.com/dwhinham/mt32-pi/wiki/Recommended-SoundFonts>.
Its actual raw Markdown was retrieved to
`.state/2ine/doom/recommended/Recommended-SoundFonts.md`. The curated index
`tools/data/recommended-soundfonts.json` records that snapshot's digest and
retains the named sources and five categories, with locally authored descriptions.
There are 19 choices: eight quality banks, four Roland sources, three
Creative/Ensoniq entries, one Voice Crystal purchase source and three Yamaha
choices. This index does not build or redistribute another bank.

`tools/recommended_soundfonts.py` provides list/details/fetch by these names.
GeneralUser uses the existing pinned/licensed bank fetcher. The wiki's direct
8mbgm and Logue Yamaha XG SF2 links use private selected-file downloads and
observed receipts, preserving unknown bank license status. Source-only entries
report their original page and reason rather than pretending an automatic
file download is available. Current ordinary public checks returned HTTP404
for trevor0402/SC55Soundfont's GitHub releases and latest-release API, and
HTTP403 for Patch93 and Phoenix on Musical Artifacts. These negative results
are dated observations, not permanent claims that a source cannot return.
Commercial purchase and driver-CD sources are not fetched automatically.

Five focused tests pass for complete named/category inventory, exact source
URLs, source-only rejection without downloading, existing pinned-provider
selection/byte caps, external source receipts and explicit selection, and
unsupported provider rejection. The new files pass pinned Ruff and ty checks.

The actual named `yamaha-xg-logue` fetch succeeds against the linked upstream
repository: 56,456,276 bytes, SHA-256
`01ae3f8826b73b7acc757f8f90f0ee99e2770b01c4ba70be029361d1a6311ce0`.
The inspected file is a RIFF SF2 with all 128 GM programs and standard percussion.
Its private receipt is in `.state/2ine/doom/recommended/downloads/external/<sha>/`.
The user-uploaded SC55 selection remains unchanged. Combined source/fetcher/forum
tests now pass 18/18, with no network operations in the unit suite.
