# Word Pro reference cycle

The reference set uses a stock OS/2 Warp 4.52 VM and the original SmartSuite
1.7 installer followed by Fixpack 3. The VM disk and application/font binaries
are private fixtures. `manifest.json` records the exact artifacts and stages.

The matching 2ine harness is `tests/integration/smartsuite.py`. Its cycle enters
`word pro editing test`, chooses Courier and adds ` courier text`, saves,
closes/reopens, chooses Helv on a new paragraph, adds `edited after reopen`,
saves and closes/reopens again. The screenshots cover the blank page, editing,
font selection, Save As, saved document, reopening and further editing.

Generate a local comparison report with:

```sh
python3 tools/compare_wordpro.py \
  --masters research/reference_screenshots/wordpro \
  --actual /private/wordpro-results --output /private/wordpro-comparison.html
```

Review control presence, menu contents, toolbar/ruler/scrollbar layout, dialog
fields, selection state and page content. Allow glyph shapes, antialiasing and
small text-metric differences. Host window decorations also differ. The report
has side-by-side views and adjustable opacity/alignment; it does not assign an
automatic pass based on pixel similarity.
