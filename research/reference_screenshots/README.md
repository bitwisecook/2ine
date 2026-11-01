# Application reference screenshots

Keep references for every working GUI application in this directory, grouped by
application. Root PNG files in each app directory are genuine stock OS/2 Warp
4.52 captures; `manifest.json` records media and program hashes, VM configuration,
input steps and screenshot hashes. Matching 2ine captures and receipts belong in
`<app>/2ine/native/` and `<app>/2ine/unicorn/`. Retain a stock reference and review
visible differences before qualifying another application.

| Application | Stock captures | 2ine captures |
|---|---|---|
| [Word Pro](wordpro/review.md) | New/edit/font/save/reopen/edit cycle | Native and Unicorn cycles |
| [PMStars](pmstars/README.md) | Original release animation | Animation, pointer input, dot mode and resizing |
| [RunBox](runbox/README.md) | Archived source rebuilt with Watcom, moving window | Movement and pointer avoidance |
| [Lotus 1-2-3](123/README.md) | New workbook, values/formula, Courier/Helv, save/reopen and recalculation | Application cycle unverified |

Allow differences in font shapes, antialiasing and small text metrics. Missing
controls, incorrect colors/state, clipped content and layout errors remain
defects. Stock captures include the desktop and OS/2 window frame; 2ine captures
usually contain the host window client area. Align the app content before
assessing positions. The Word Pro review records remaining differences.

Application binaries, VM disks and original font files are supplied separately
and are not stored here. Capturing stock OS/2 does not verify reconstructed OS/2
artifacts or qualify an entire application.

Current SmartSuite priority: Word Pro, then 1-2-3. Other suite apps are deferred.
