#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Generate the shared native credit catalogue and its readable source index."""

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "tools/data/project-sources.json"


def installed_catalogue(sources):
    """Bind host-supplied projects to actual package versions and source pages."""
    data = json.loads(CATALOGUE.read_text())
    records = []
    known = {
        item.get("sourcePackage"): item for item in data["projects"] if item.get("sourcePackage")
    }
    seen = set()
    for source in sources:
        source = Path(source).resolve()
        if shutil.which("dpkg-query"):
            owner = subprocess.run(
                ["dpkg-query", "-S", str(source)], capture_output=True, text=True
            )
            if owner.returncode:
                continue
            package = owner.stdout.split(": ", 1)[0]
            query = subprocess.run(
                ["dpkg-query", "-W", "-f=${source:Package}|${source:Version}", package],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
            name, version = query.split("|", 1)
            if (name, version) in seen:
                continue
            seen.add((name, version))
            download = "https://launchpad.net/ubuntu/+source/" + name + "/" + version
            record = {"package": name, "version": version, "source": download, "download": download}
            if name in known:
                known[name].update(version=version, download=download)
            else:
                copyright_file = Path("/usr/share/doc") / package.split(":")[0] / "copyright"
                text = (
                    copyright_file.read_text(errors="replace") if copyright_file.is_file() else ""
                )
                source_match = re.search(r"^Source:\s*(https?://\S+)", text, re.M)
                homepage = source_match[1] if source_match else download
                licenses = sorted(set(re.findall(r"^License:\s*(.+)", text, re.M)))
                data["projects"].append(
                    {
                        "id": "package-" + name,
                        "name": name,
                        "version": version,
                        "versionPolicy": "host",
                        "sourcePackage": name,
                        "license": "; ".join(licenses)
                        or "See bundled copyright and upstream source",
                        "authors": [
                            "Contributors credited in the retained package copyright and upstream source"
                        ],
                        "homepage": homepage,
                        "source": download,
                        "download": download,
                        "role": "bundled transitive dependency",
                    }
                )
            records.append(record)
        else:
            for parent in source.parents:
                if not (parent / "INSTALL_RECEIPT.json").is_file():
                    continue
                name, version = parent.parent.name, parent.name
                if (name, version) in seen:
                    break
                seen.add((name, version))
                formula = parent / ".brew" / (name + ".rb")
                text = formula.read_text() if formula.is_file() else ""
                url = re.search(r'^\s*url "([^"]+)"', text, re.M)
                homepage = re.search(r'^\s*homepage "([^"]+)"', text, re.M)
                download = url[1] if url else "https://formulae.brew.sh/formula/" + name
                record = {
                    "package": name,
                    "version": version,
                    "source": download,
                    "download": download,
                }
                aliases = {
                    "vulkan-loader": "vulkan",
                    "sdl3": "sdl3",
                    "glib": "glib",
                    "zstd": "zstd",
                    "xz": "xz",
                    "libvorbis": "vorbis",
                    "libogg": "ogg",
                    "libsndfile": "sndfile",
                }
                entry = next(
                    (item for item in data["projects"] if item["id"] == aliases.get(name, name)),
                    None,
                )
                if entry:
                    entry.update(version=version, download=download)
                else:
                    license_match = re.search(r'^\s*license "([^"]+)"', text, re.M)
                    data["projects"].append(
                        {
                            "id": "brew-" + name,
                            "name": name,
                            "version": version,
                            "versionPolicy": "host",
                            "role": "bundled transitive dependency",
                            "license": license_match[1]
                            if license_match
                            else "See bundled formula notices",
                            "authors": ["Contributors credited in the retained upstream notices"],
                            "homepage": homepage[1] if homepage else download,
                            "source": download,
                            "download": download,
                        }
                    )
                records.append(record)
                break
    for source in sources:
        info = Path(source).parent / "2ine-build.json"
        if info.is_file():
            build = json.loads(info.read_text())
            revision = build.get("sourceRevision", "unknown")
            data["runtimeBuild"] = build
            if re.fullmatch(r"[0-9a-f]{40}", revision):
                entry = next(item for item in data["projects"] if item["id"] == "2ine")
                entry.update(
                    version=build.get("sourceVersion", revision),
                    source="https://github.com/bitwisecook/2ine/tree/" + revision,
                    download="https://github.com/bitwisecook/2ine/archive/" + revision + ".tar.gz",
                )
            break
    for source in sources:
        metadata = Path(source).parent / "ffmpeg-build.json"
        if metadata.is_file():
            multimedia = json.loads(metadata.read_text())
            data["multimediaBuild"] = multimedia
            entry = next(item for item in data["projects"] if item["id"] == "ffmpeg")
            entry.update(
                version=multimedia["version"],
                license=multimedia["license"],
                provider=multimedia["provider"],
                buildConfiguration=multimedia["configuration"],
            )
            break
    data["installedInputs"] = records
    return data


def generate():
    data = json.loads(CATALOGUE.read_text())
    ids = set()
    lines = [
        "<!-- Public domain. No rights reserved. Catalogue text only; component licenses remain unchanged. -->",
        "",
        "# Project sources, versions and credits",
        "",
        "**2ine was created by [Ryan C. Gordon (icculus)](https://icculus.org/).**",
        "This fork builds on Ryan’s original OS/2 compatibility layer. His copyright",
        "and zlib-style license are retained. Fork extensions do not change the",
        "ownership or licenses of upstream code.",
        "",
        "This index covers incorporated code, bundled assets, optional backends,",
        "platform dependencies and direct build tools. Small projects’ authors are",
        "named; larger projects retain their contributor lists in the linked source.",
        "Host versions below are the Ubuntu 26.04 reference build, not universal",
        "pins. Each packaged build carries its actual dependency inventory in",
        "`project-sources.json` alongside the runtime and exposes it in About.",
        "OS graphics drivers and desktop services remain distribution components.",
        "",
        "**Source availability:** links identify upstream source and downloads.",
        "They do not replace a release’s complete corresponding-source distribution",
        "for GPL/LGPL components, including distribution patches and build scripts.",
        "See [licensing](licensing.md). Original OS/2/Lotus media and MT-32 ROMs",
        "are user-supplied and are not licensed or redistributed by this project.",
        "",
    ]
    for item in data["projects"]:
        if item["id"] in ids:
            raise ValueError("Duplicate project: " + item["id"])
        ids.add(item["id"])
        for key in ("homepage", "source", "download"):
            if not item[key].startswith(("https://", "http://")):
                raise ValueError("Missing source/download link: " + item["id"])
        lines += [
            "## " + item["name"],
            "",
            "- Version: "
            + item["version"]
            + (" (host selected)" if item["versionPolicy"] == "host" else ""),
            "- License: " + item["license"],
            "- Used as: " + item["role"],
            "- Authors / origin credits: " + "; ".join(item["authors"]),
            f"- [Project]({item['homepage']}) · [Source]({item['source']}) · [Download]({item['download']})",
        ]
        for key in ("sha256", "notes", "provenance"):
            if item.get(key):
                lines += ["- " + key.title() + ": " + item[key]]
        for key, label in (("licenseUrl", "License text"), ("creditsUrl", "Full credits")):
            if item.get(key):
                lines += [f"- [{label}]({item[key]})"]
        lines += [""]
    encoded = json.dumps(data, ensure_ascii=True, separators=(",", ":"))
    header = "/* Public domain. No rights reserved. Generated by tools/project_sources.py. */\n"
    header += "static const char tb_project_sources[] =\n"
    # Short adjacent literals remain compatible with C compilers' token limits.
    header += "\n".join(json.dumps(encoded[i : i + 96]) for i in range(0, len(encoded), 96)) + ";\n"
    return {
        ROOT / "docs/project-sources.md": "\n".join(lines),
        ROOT / "ball/project_sources.h": header,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for path, content in generate().items():
        if args.check:
            if not path.is_file() or path.read_text() != content:
                raise SystemExit(
                    f"Regenerate {path.relative_to(ROOT)} with tools/project_sources.py"
                )
        else:
            path.write_text(content)


if __name__ == "__main__":
    main()
