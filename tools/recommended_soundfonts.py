#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Choose external SoundFont sources named by mt32-pi's recommended list.

The index links the existing publishers and collections. Direct files remain
private downloads with observed identities; no SoundFont is built by this tool.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tools import fetch_soundfonts as sf
    from tools import vogons_soundfonts as external
else:
    try:
        from tools import fetch_soundfonts as sf
        from tools import vogons_soundfonts as external
    except ModuleNotFoundError:
        import fetch_soundfonts as sf
        import vogons_soundfonts as external

INDEX = Path(__file__).with_name("data") / "recommended-soundfonts.json"


def load_index(path: Path = INDEX) -> dict:
    index = json.loads(path.read_text(encoding="utf-8"))
    if index.get("schema") != 1 or not isinstance(index.get("choices"), list):
        raise ValueError("unsupported recommended-source index")
    seen = set()
    for choice in index["choices"]:
        identity = sf.filename(choice.get("id"))
        if identity in seen or choice.get("provider") not in {"source", "catalog", "direct"}:
            raise ValueError("duplicate choice or unsupported provider")
        seen.add(identity)
        for field in ("name", "category", "source_url", "notes"):
            sf.string(choice.get(field))
        if choice["provider"] == "direct" and not choice.get("download_url", "").startswith(
            "https://"
        ):
            raise ValueError("direct recommended downloads require HTTPS")
    return index


def fetch_choice(
    index: dict,
    identity: str,
    cache: Path,
    config: Path | None = None,
    max_bytes: int = 512 * 1024**2,
    member: str | None = None,
) -> Path:
    choices = [choice for choice in index["choices"] if choice["id"] == identity]
    if len(choices) != 1:
        raise ValueError(f"unknown choice {identity!r}; use list")
    choice = choices[0]
    if choice["provider"] == "source":
        raise ValueError(f"Source-only choice: {choice['source_url']}\n{choice['notes']}")
    if choice["provider"] == "catalog":
        banks, _, _ = sf.load_catalog()
        bank = banks[choice["bank_id"]]
        if any(artifact.size > max_bytes for artifact in bank.files):
            raise ValueError("catalog artifact exceeds download byte limit")
        path = sf.fetch_bank(bank, cache)
        if config:
            sf.select_bank(bank, path, config)
        return path
    inventory = {
        "topic": index["source"],
        "links": [
            {
                "id": identity,
                "url": choice["download_url"],
                "label": choice["name"],
                "source_url": choice["source_url"],
                "collection_source": index["source"],
                "observations": [{"source_url": index["source"], "name": choice["name"]}],
            }
        ],
    }
    receipt = external.selected_fetch(inventory, identity, cache / "external", max_bytes)
    receipt["collection_source"] = index["source"]
    receipt["bank_license"] = (
        "unknown; recommendation and project code licenses do not establish bank rights"
    )
    path = Path(receipt["path"])
    sf.atomic_write(path.with_name("receipt.json"), json.dumps(receipt, indent=2) + "\n")
    if member:
        path = external.extract_member(receipt, member, path.parent / "selected", max_bytes)
    if config:
        sf.use_file(path, cache / "selected-banks", config)
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    listing = commands.add_parser("list")
    listing.add_argument(
        "--category", choices=("high-quality", "sc55", "creative-ensoniq", "gus", "yamaha-xg")
    )
    details = commands.add_parser("details")
    details.add_argument("choice")
    fetch = commands.add_parser("fetch")
    fetch.add_argument("choice")
    fetch.add_argument("--cache", type=Path, default=sf.default_cache())
    fetch.add_argument("--use", action="store_true", help="select a complete GM SF2 after fetching")
    fetch.add_argument("--config", type=Path, default=sf.default_config())
    fetch.add_argument("--max-mib", type=int, default=512)
    fetch.add_argument("--member", help="extract exactly one named member if the download is a ZIP")
    args = parser.parse_args()
    try:
        index = load_index()
        if args.command == "list":
            print(index["source"])
            for choice in index["choices"]:
                if args.category and choice["category"] != args.category:
                    continue
                status = "fetch" if choice["provider"] != "source" else "source-only"
                print(
                    f"{choice['id']} [{choice['category']}; {status}] {choice['name']}\n  {choice['source_url']}"
                )
            return 0
        if args.command == "details":
            choice = next(
                (choice for choice in index["choices"] if choice["id"] == args.choice), None
            )
            if not choice:
                raise ValueError(f"unknown choice {args.choice!r}")
            print(json.dumps(choice, indent=2))
            return 0
        if not 1 <= args.max_mib <= 4096:
            raise ValueError("max-mib must be 1..4096")
        path = fetch_choice(
            index,
            args.choice,
            args.cache,
            args.config if args.use else None,
            args.max_mib * 1024**2,
            args.member,
        )
        print(path.resolve())
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"recommended_soundfonts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
