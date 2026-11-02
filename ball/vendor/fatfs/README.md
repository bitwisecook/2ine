# FatFs R0.16, patch 2

Source: https://elm-chan.org/fsw/ff/arc/ff16.zip
Official patches: https://elm-chan.org/fsw/ff/patch/ff16p1.diff and
https://elm-chan.org/fsw/ff/patch/ff16p2.diff (2026-07-10).
The original copyright and license notices remain in each source file.

Configuration enables FAT12/16/32, exFAT, UTF-8 long names, dynamic code pages,
64-bit sector addresses and formatting. One library volume is serialized by the
adapter; it is an internal work area, not a host OS mount.

Local interoperability change: exFAT creation and synchronization keep a valid
last-access timestamp rather than zeroing it. The independent exfatfsck checker
rejects zero-month/zero-day timestamps. UTF-8 filename buffers allow the full
255 UTF-16-unit name after conversion. Reader bounds and official patch 2 cover
the known malformed BPB, volume-label and cluster-count cases.
