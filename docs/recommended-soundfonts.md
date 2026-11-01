<!-- Public domain. No rights reserved. Wholly AI-authored index descriptions. -->

# mt32-pi recommended external SoundFonts

The named choices come from [mt32-pi's Recommended SoundFonts](https://github.com/dwhinham/mt32-pi/wiki/Recommended-SoundFonts).
They point to existing publishers, repositories and collections. The tool downloads
selected external files; it does not build another SoundFont.

```sh
python3 tools/recommended_soundfonts.py list
python3 tools/recommended_soundfonts.py list --category sc55
python3 tools/recommended_soundfonts.py details sc55-patch93
python3 tools/recommended_soundfonts.py fetch generaluser-gs --cache ./soundfonts
python3 tools/recommended_soundfonts.py fetch yamaha-xg-logue --cache ./soundfonts
```

The index includes all five groups from the inspected wiki page: eight general
quality choices; four Roland SC-55/Sound Canvas sources; Creative/Ensoniq archives;
Voice Crystal/Gravis Ultrasound; and three Yamaha XG choices. Each list entry
states whether it supports `fetch` or is `source-only`. `details NAME` prints its
original source and availability notes.

Automatic downloads currently include:

| Choice | External source | Handling |
|---|---|---|
| `generaluser-gs` | S. Christian Collins's official GeneralUser GS repository | Existing pinned bank and license checks |
| `yamaha-xg-logue` | Logue's sf2synth.js repository, linked by mt32-pi | Direct SF2, private cache and observed digest/receipt |
| `8mbgm-enhanced` | The direct archived SF2 linked by mt32-pi | Private cache and observed digest/receipt |

Source-only entries retain the actual wiki links rather than inventing direct
bank URLs. For example, the listed trevor0402 SC55 repository returned HTTP404
when checked on 2026-11-01, while the Patch93 and Phoenix pages returned HTTP403
to ordinary public requests. The creator video, publisher landing pages, driver
CD collections and commercial Voice Crystal purchase page also remain source
choices. Open the original source and import a supplied file using
`tools/fetch_soundfonts.py use-file PATH` when appropriate.

Fetching leaves the active bank unchanged. Add `--use --config ./audio.conf`
only when you want to select a downloaded complete GM SF2. Download receipts
retain source URLs, size, hash and coverage. A recommendation or project code
license is not treated as the license of an external bank. These private source
downloads stay separate from the pinned redistributable bank catalog.

The sampled backend uses GM program and controller messages. Labels such as
GS, XG, GM2 or MT-32 on a bank do not by themselves provide complete device or
SysEx emulation. Actual MT-32 playback remains a separate synthesis backend.
