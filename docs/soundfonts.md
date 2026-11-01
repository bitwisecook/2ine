<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# SoundFont sources and selection

For named choices from the external collection, see
[mt32-pi recommended sources and downloads](recommended-soundfonts.md).

[GeneralUser GS](https://www.schristiancollins.com/generaluser.php) by S. Christian
Collins is the default complete General MIDI and Roland GS compatible bank.
It supplies sampled instruments and percussion; it does not contain a licensed
Roland ROM set or emulate the SC-55 or MT-32 hardware. The author's
[official repository](https://github.com/mrbumpy409/GeneralUser-GS) provides the
bank, documentation and its license.

[TimGM6mb](https://www.timbrechbill.com/saxguru/Timidity.php) by Tim Brechbill is a
small complete GM alternative. The catalog pins the GPL-2.0 copy retained in
MuseScore's official historical source, together with that distribution's
license. The author's current download is a different file, so these identities
are kept separate.

[MuseScore's SoundFont guide](https://musescore.org/en/handbook/3/soundfonts-and-sfz-files)
is another primary source for banks and formats, including FluidR3.
[FreePats](https://freepats.zenvoid.org/) publishes openly licensed instrument
samples and banks. Check each bank's format, instrument coverage and license:
2ine's sampled backend requires a complete SF2 GM set. SF3 and SFZ use different
formats.

For actual MT-32/CM-32L synthesis, [Munt](https://github.com/munt/munt) emulates
those pre-GM devices using separately supplied control and PCM ROM images.
An SF2 does not reproduce the MT-32's LA synthesis or programmable SysEx patches.
The fetcher obtains no Roland ROMs. MT-32 playback is a separate backend and
requires compatible music and supplied ROM files.

From the checkout:

```sh
python3 tools/fetch_soundfonts.py list
python3 tools/fetch_soundfonts.py details generaluser-gs
python3 tools/fetch_soundfonts.py fetch generaluser-gs
python3 tools/fetch_soundfonts.py use generaluser-gs
python3 tools/fetch_soundfonts.py use timgm6mb
python3 tools/fetch_soundfonts.py use-file /path/to/your/SC-55.sf2
```

`fetch` downloads only the selected catalog bank and its license, verifies
pinned byte counts and SHA-256 hashes, and retains a source notice beside it.
`use` additionally writes the selected default to the audio configuration.
`use-file` verifies a supplied SF2's complete GM/percussion preset inventory,
caches a content-addressed copy, retains an import receipt and selects that copy.
User imports are separate from the redistributable catalog.

The default cache is `$XDG_CACHE_HOME/2ine/soundfonts`, falling back to
`~/.cache/2ine/soundfonts`. The default configuration is
`$XDG_CONFIG_HOME/2ine/audio.conf`, falling back to `~/.config/2ine/audio.conf`.
`TWOINE_AUDIO_CONFIG` or `--config` selects another configuration;
`--cache` selects another cache. Options work before or after the command.
For an isolated application setup:

```sh
python3 tools/fetch_soundfonts.py use-file /path/to/bank.sf2 \
  --cache ./soundfonts-cache --config ./audio.conf
TWOINE_AUDIO_CONFIG="$PWD/audio.conf" path/to/lx_loader application.exe
```

Selections preserve unrelated configuration entries and write
`midi_backend=gm`, `soundfont`, `soundfont_id`, `soundfont_sha256` and `schema=1`.
`TWOINE_SOUNDFONT` overrides the configured sound bank at runtime. Downloads and
configuration updates use temporary files and atomic replacement; incomplete or
mismatched downloads do not become selected banks.

See [external VOGONS collections](vogons-soundfonts.md) for topic discovery and selected downloads.
