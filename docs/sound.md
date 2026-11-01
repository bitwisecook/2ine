<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Sound and SoundFont sources

[mt32-pi](https://github.com/dwhinham/mt32-pi) maintains a useful
[Recommended SoundFonts list](https://github.com/dwhinham/mt32-pi/wiki/Recommended-SoundFonts).
[Named choices and download availability](recommended-soundfonts.md).

List the named upstream choices with:

```sh
python3 tools/recommended_soundfonts.py list
python3 tools/recommended_soundfonts.py list --category sc55
python3 tools/recommended_soundfonts.py details generaluser-gs
python3 tools/recommended_soundfonts.py fetch generaluser-gs --cache ./soundfonts --use --config ./audio.conf
```

In the OS/2 workspace, `scripts/2ine-soundfonts recommended` exposes the same
commands. `fetch` downloads a selected supported source; `--use` also selects
it. Source pages and unavailable/unsupported downloads remain named choices
with their status and original links.

Use that upstream list to compare Roland Sound Canvas, Yamaha XG and other
General MIDI banks. The downloader exposes named choices for those external
sources and retains the author/download links; bank data lives in your cache.

2ine plays digital effects through DART and sampled MIDI through a selected SF2
bank. GeneralUser GS is the bundled default source. A local SC-55 bank can be
selected with the [SoundFont tool](soundfonts.md); it writes the saved choice
used by subsequent application launches. An explicit `TWOINE_SOUNDFONT` path
overrides that saved selection.

The [VOGONS SoundFont topic downloader](vogons-soundfonts.md) inventories a
community topic and downloads one selected link, including normal Google Drive
confirmation pages. Download receipts preserve URL, size and SHA-256; ZIP files
can be inspected and a specified bank member extracted.

[Munt](https://github.com/munt/munt) provides actual MT-32/CM-32L emulation.
The [optional Munt backend](midi-mt32.md) requires separately supplied control
and PCM ROMs. Sound Canvas GM/GS banks and native MT-32 synthesis are separate
choices, with different instrument mappings.

See [audio behavior and MIDI controls](audio.md), [bank fetching and selection](soundfonts.md),
and [OS/2 Doom setup](doom.md) for commands and execution requirements.
