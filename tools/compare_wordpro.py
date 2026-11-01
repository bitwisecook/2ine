#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Build a local, interactive comparison of Warp Word Pro masters and 2ine captures."""

import argparse
import base64
import html
import json
from pathlib import Path

STAGES = (
    "blank-document",
    "new-document",
    "font-selector-0",
    "save-as",
    "saved",
    "reopened",
    "edited-after-reopen",
    "final-reopened",
)


def image(path):
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--masters", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.masters / "manifest.json").read_text())
    sections = []
    for stage in STAGES:
        master, actual = args.masters / f"{stage}.png", args.actual / f"{stage}.png"
        if not master.is_file() or not actual.is_file():
            parser.error(f"missing stage: {stage}")
        sections.append(
            f'<section><h2>{html.escape(stage)}</h2><div class="pair">'
            f'<figure><figcaption>Warp 4.52 master</figcaption><img src="{image(master)}"></figure>'
            f'<figure><figcaption>2ine</figcaption><img src="{image(actual)}"></figure></div>'
            f"<details><summary>Opacity overlay (adjust alignment with the controls below)</summary>"
            f'<label>Opacity <input type="range" min="0" max="1" step=".05" value=".5" '
            f"oninput=\"this.closest('details').querySelector('.actual').style.opacity=this.value\"></label>"
            f'<label>X <input type="number" value="0" '
            f"oninput=\"this.closest('details').querySelector('.actual').style.left=this.value+'px'\"></label>"
            f'<label>Y <input type="number" value="0" '
            f"oninput=\"this.closest('details').querySelector('.actual').style.top=this.value+'px'\"></label>"
            f'<div class="overlay"><img src="{image(master)}"><img class="actual" '
            f'style="opacity:.5" src="{image(actual)}"></div></details></section>'
        )
    provenance = html.escape(json.dumps(manifest, indent=2))
    document = """<!doctype html><html lang="en"><meta charset="utf-8">
<title>Word Pro: Warp and 2ine</title><style>
body{font:16px system-ui;margin:2rem;background:#eee;color:#111}section{margin:3rem 0}
.pair{display:flex;gap:1rem;align-items:start}figure{margin:0;width:50%}figure img{width:100%}
figcaption{font-weight:bold;margin-bottom:.5rem}.overlay{position:relative;overflow:hidden}
.overlay img{display:block}.overlay .actual{position:absolute;top:0;left:0}
label{display:inline-block;margin:1rem}input[type=number]{width:5em}pre{white-space:pre-wrap}
</style><h1>Word Pro: Warp 4.52 and 2ine</h1>
<p>Inspect menu items, toolbar buttons, rulers, scrollbars, dialogs, selection and page content.
Allow different glyph shapes, antialiasing and small text-metric differences. Missing controls,
wrong state, clipped content and incorrect placement remain defects. Host decorations may differ.</p>
<p>This report supports visual review; it does not infer a passing result from pixel similarity.</p>
"""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        document
        + "\n".join(sections)
        + f"<details><summary>Master provenance</summary><pre>{provenance}</pre></details></html>\n"
    )
    print(args.output.resolve())


if __name__ == "__main__":
    main()
