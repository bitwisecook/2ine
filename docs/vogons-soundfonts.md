<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# External VOGONS SoundFont collections

`tools/vogons_soundfonts.py` inventories a public VOGONS topic and downloads one
selected link. Discovery retains post authors, dates, text, source URLs, links,
attachment claims, page hashes and pagination status. It follows pages only
within the chosen topic, with a configurable page cap. It does not download
linked banks during discovery.

For [zz_denis's SC55 topic](https://www.vogons.org/viewtopic.php?t=88648):

```sh
python3 tools/vogons_soundfonts.py discover \
  'https://www.vogons.org/viewtopic.php?t=88648' --out ./forum-topic
python3 tools/vogons_soundfonts.py list ./forum-topic/inventory.json
python3 tools/vogons_soundfonts.py fetch ./forum-topic/inventory.json \
  cf041549c96f83c7 --cache ./forum-downloads
```

The selected ID above identifies the author's SC55 v0.5 link. IDs derive from
source URLs, so links remain distinguishable when a topic also contains audio
comparisons, MIDI files, configuration files or other banks. Public Google
Drive file links follow the ordinary download confirmation form; login/folder
pages are not treated as files. There is no Google account or cookie integration.
Public forum page requests can use ordinary curl if the server denies urllib.

Downloads have a byte cap and a receipt containing observed SHA-256, exact
source/resolved URLs and source posts. `--sha256` can require an expected digest.
The first observed digest establishes file identity, not author authenticity.
ZIP downloads retain their member inventory; `--member 'path/bank.sf2'` extracts
only that selected member with size/path limits. Files are not executed.
`--use --config ./audio.conf` explicitly imports/selects a complete GM SF2;
fetching alone keeps the current bank selected.

For offline discovery, `discover ... --html saved-topic.html` reads a retained
page and records any unfetched pagination. A forum post's license or a screenshot
attachment's license is recorded as that source's claim. It is not automatically
applied to an external SoundFont. Forum downloads remain separate from the
redistributable pinned catalog.
