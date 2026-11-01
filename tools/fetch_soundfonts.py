#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Fetch pinned complete GM sound banks and select the default for 2ine.

Only the catalog's freely redistributable SF2 files and license notices are
fetched. MT-32 emulation uses separately supplied ROMs rather than an SF2 bank.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, cast

CATALOG = Path(__file__).with_name("data") / "soundfonts.json"
CHUNK = 1024 * 1024


@dataclass(frozen=True)
class Artifact:
    name: str
    url: str
    size: int
    sha256: str


@dataclass(frozen=True)
class Bank:
    identity: str
    name: str
    version: str
    author: str
    description: str
    homepage: str
    source: str
    license: str
    bank: str
    files: tuple[Artifact, ...]


def object_fields(value: object) -> dict[str, object]:
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise ValueError("catalog object expected")
    return cast(dict[str, object], value)


def string(value: object) -> str:
    if not isinstance(value, str) or not value or "\n" in value or "\r" in value:
        raise ValueError("nonempty one-line catalog string expected")
    return value


def filename(value: object) -> str:
    text = string(value)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", text) or text in (".", ".."):
        raise ValueError(f"invalid catalog filename: {text!r}")
    return text


def load_catalog(path: Path = CATALOG) -> tuple[dict[str, Bank], str, list[dict[str, str]]]:
    root = object_fields(json.loads(path.read_text(encoding="utf-8")))
    if root.get("schema") != 1 or not isinstance(root.get("banks"), list):
        raise ValueError("unsupported soundfont catalog schema")
    banks: dict[str, Bank] = {}
    for raw in cast(list[object], root["banks"]):
        entry = object_fields(raw)
        identity = filename(entry.get("id"))
        if identity in banks or not isinstance(entry.get("files"), list):
            raise ValueError("duplicate bank or invalid artifact list")
        artifacts = []
        for item in cast(list[object], entry["files"]):
            item = object_fields(item)
            url, size, sha = string(item.get("url")), item.get("size"), string(item.get("sha256"))
            if urllib.parse.urlparse(url).scheme != "https":
                raise ValueError("catalog downloads require HTTPS")
            if not isinstance(size, int) or isinstance(size, bool) or not 0 < size <= 1024**3:
                raise ValueError("invalid artifact size")
            if not re.fullmatch("[0-9a-f]{64}", sha):
                raise ValueError("invalid artifact SHA-256")
            artifacts.append(Artifact(filename(item.get("name")), url, size, sha))
        bankfile = filename(entry.get("bank"))
        names = [item.name for item in artifacts]
        if bankfile not in names or len(set(names)) != len(names) or len(names) < 2:
            raise ValueError("bank and unique license artifacts are required")
        banks[identity] = Bank(
            identity,
            string(entry.get("name")),
            filename(entry.get("version")),
            string(entry.get("author")),
            string(entry.get("description")),
            string(entry.get("homepage")),
            string(entry.get("source")),
            string(entry.get("license")),
            bankfile,
            tuple(artifacts),
        )
    default = string(root.get("default"))
    if default not in banks:
        raise ValueError("catalog default is not an available bank")
    sources = []
    for item in cast(list[object], root.get("sources", [])):
        item = object_fields(item)
        sources.append({key: string(item.get(key)) for key in ("name", "url", "description")})
    return banks, default, sources


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            sha.update(chunk)
    return sha.hexdigest()


def fetch_artifact(
    artifact: Artifact,
    directory: Path,
    opener: Callable[..., object] = urllib.request.urlopen,
) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / artifact.name
    if (
        destination.is_file()
        and destination.stat().st_size == artifact.size
        and digest(destination) == artifact.sha256
    ):
        return destination
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f".{artifact.name}.", dir=directory, delete=False
        ) as out:
            temporary = Path(out.name)
            sha, size = hashlib.sha256(), 0
            request = urllib.request.Request(
                artifact.url, headers={"User-Agent": "2ine-soundfonts/1"}
            )
            response = cast(BinaryIO, opener(request, timeout=60))
            with response:
                for chunk in iter(lambda: response.read(CHUNK), b""):
                    size += len(chunk)
                    if size > artifact.size:
                        raise ValueError(f"download exceeds pinned size: {artifact.name}")
                    sha.update(chunk)
                    out.write(chunk)
            if size != artifact.size or sha.hexdigest() != artifact.sha256:
                raise ValueError(f"download size/SHA-256 mismatch: {artifact.name}")
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, destination)
        temporary = None
        return destination
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def validate_sf2(path: Path) -> None:
    """Check RIFF bounds and the complete GM/percussion preset header inventory."""
    size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(12)
        if (
            len(header) != 12
            or header[:4] != b"RIFF"
            or header[8:] != b"sfbk"
            or struct.unpack_from("<I", header, 4)[0] + 8 != size
        ):
            raise ValueError(f"not a complete RIFF SoundFont2 file: {path}")
        presets: set[tuple[int, int]] = set()
        offset = 12
        while offset + 8 <= size:
            stream.seek(offset)
            kind, length = struct.unpack("<4sI", stream.read(8))
            end = offset + 8 + length
            if end > size:
                raise ValueError("SoundFont RIFF chunk exceeds file")
            if kind == b"LIST" and length >= 4 and stream.read(4) == b"pdta":
                sub = offset + 12
                while sub + 8 <= end:
                    stream.seek(sub)
                    subkind, sublength = struct.unpack("<4sI", stream.read(8))
                    if sub + 8 + sublength > end:
                        raise ValueError("SoundFont preset chunk exceeds list")
                    if subkind == b"phdr":
                        if sublength < 38 or sublength % 38 or sublength > 38 * 65536:
                            raise ValueError("invalid SoundFont preset header table")
                        for record in range(sublength // 38 - 1):
                            stream.seek(sub + 8 + record * 38 + 20)
                            program, bank = struct.unpack("<HH", stream.read(4))
                            presets.add((bank, program))
                    sub += 8 + sublength + (sublength & 1)
            offset = end + (length & 1)
    if not all((0, program) in presets for program in range(128)) or (128, 0) not in presets:
        raise ValueError("SoundFont must contain all 128 GM programs and standard percussion")


def fetch_bank(bank: Bank, cache: Path) -> Path:
    directory = cache / bank.identity / bank.version
    for artifact in bank.files:
        fetch_artifact(artifact, directory)
    path = directory / bank.bank
    validate_sf2(path)
    notice = (
        f"{bank.name} {bank.version}\nAuthor: {bank.author}\nLicense: {bank.license}\n"
        f"Source: {bank.source}\nHomepage: {bank.homepage}\n\n{bank.description}\n"
    )
    atomic_write(directory / "SOURCE.txt", notice)
    return path.resolve()


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent, delete=False
        ) as out:
            temporary = Path(out.name)
            out.write(text)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def select_bank(bank: Bank, path: Path, config: Path) -> None:
    path = path.resolve()
    validate_sf2(path)
    artifact = next(item for item in bank.files if item.name == bank.bank)
    if digest(path) != artifact.sha256:
        raise ValueError("selected bank differs from catalog hash")
    write_selection(path, bank.identity, artifact.sha256, config)


def write_selection(path: Path, identity: str, sha256: str, config: Path) -> None:
    settings = {
        "schema": "1",
        "midi_backend": "gm",
        "soundfont": str(path),
        "soundfont_id": identity,
        "soundfont_sha256": sha256,
    }
    if any("\n" in value or "\r" in value for value in settings.values()):
        raise ValueError("configuration values cannot contain newlines")
    previous = config.read_text(encoding="utf-8").splitlines() if config.exists() else []
    kept = [line for line in previous if line.partition("=")[0].strip() not in settings]
    text = "\n".join(kept + [f"{key}={value}" for key, value in settings.items()]) + "\n"
    atomic_write(config, text)


def use_file(source: Path, cache: Path, config: Path) -> Path:
    source = source.resolve()
    validate_sf2(source)
    sha = digest(source)
    directory = cache / "user" / sha
    item = Artifact("bank.sf2", source.as_uri(), source.stat().st_size, sha)
    destination = fetch_artifact(item, directory, lambda *args, **kwargs: source.open("rb"))
    validate_sf2(destination)
    receipt = {
        "schema": 1,
        "source": str(source),
        "name": source.name,
        "size": item.size,
        "sha256": sha,
        "bank": str(destination.resolve()),
        "origin": "user-supplied",
    }
    atomic_write(directory / "receipt.json", json.dumps(receipt, indent=2) + "\n")
    write_selection(destination.resolve(), "user-" + sha[:16], sha, config)
    return destination.resolve()


def default_cache() -> Path:
    return Path(os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")) / "2ine/soundfonts"


def default_config() -> Path:
    override = os.environ.get("TWOINE_AUDIO_CONFIG")
    if override:
        return Path(override)
    return (
        Path(os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")) / "2ine/audio.conf"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=CATALOG)
    parser.add_argument("--cache", type=Path, default=default_cache())
    parser.add_argument("--config", type=Path, default=default_config())
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "details", "fetch", "use", "use-file"):
        sub = commands.add_parser(name)
        if name == "use-file":
            sub.add_argument("path", type=Path, help="a user-supplied complete GM SF2 bank")
        elif name != "list":
            sub.add_argument("bank", nargs="?", help="bank id; defaults to GeneralUser GS")
        for option in ("catalog", "cache", "config"):
            sub.add_argument(f"--{option}", type=Path, default=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        if args.command == "use-file":
            path = use_file(args.path, args.cache, args.config)
            print(path)
            print(f"Selected user bank in {args.config.resolve()}")
            return 0
        banks, default, sources = load_catalog(args.catalog)
        if args.command == "list":
            settings = {}
            if args.config.is_file():
                settings = dict(
                    line.split("=", 1)
                    for line in args.config.read_text(encoding="utf-8").splitlines()
                    if "=" in line and not line.startswith("#")
                )
            override = os.environ.get("TWOINE_SOUNDFONT")
            if override:
                print(f"Active bank override: {override}\n")
            elif settings.get("soundfont"):
                print(f"Selected bank: {settings['soundfont']}\n")
            for identity, bank in banks.items():
                suffix = " (bundled default)" if identity == default else ""
                print(f"{identity}{suffix}: {bank.name} {bank.version} — {bank.license}")
                print(f"  {bank.description}\n  {bank.homepage}")
            for source in sources:
                print(f"\n{source['name']}: {source['url']}\n  {source['description']}")
            return 0
        identity = args.bank or default
        if identity not in banks:
            raise ValueError(f"unknown bank {identity!r}; use 'list'")
        bank = banks[identity]
        if args.command == "details":
            print(
                json.dumps(
                    {**bank.__dict__, "files": [item.__dict__ for item in bank.files]}, indent=2
                )
            )
            return 0
        path = fetch_bank(bank, args.cache)
        print(path)
        if args.command == "use":
            select_bank(bank, path, args.config)
            print(f"Selected {bank.name} in {args.config.resolve()}")
        return 0
    except (OSError, ValueError, urllib.error.URLError) as error:
        print(f"fetch_soundfonts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
