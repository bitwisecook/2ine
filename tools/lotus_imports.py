#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Read-only LX/NE import census, static closures, and source export evidence.

Fixup layouts follow tools/lx_dump.c, loader/load_lx.c, loader/load_ne.c and
formats/{lx,ne}.h. --lx-dump optionally cross-checks their existing text parser.
No guest code or native API module is loaded or executed.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import json
import ntpath
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.os2binary import inspect_file, module_name  # noqa: E402
from tools.os2binary import parse_image as parse_image  # noqa: E402

APPS = {
    "approach": r"C:\LOTUSW4\APPROACH\APPROACH.EXE",
    "freelance": r"C:\LOTUSW4\FLG\FLG.EXE",
    "organizer": r"C:\LOTUSW4\ORGANIZE\ORG32.EXE",
    "123": r"C:\LOTUSW4\123\123W.EXE",
    "wordpro": r"C:\LOTUSW4\WORDPRO\WORDPRO.EXE",
}


KNOWN_DYNAMIC = {
    "123": [
        {
            "path": r"C:\LOTUSW4\123\MAIN123W.DLL",
            "source": "rea-notes/lotus-123-startup.md",
            "kind": "recorded-user-startup-chain",
            "detail": "User-reported MAIN123W -> LTSIO40 -> LTOLEO20 startup dependency chain.",
        }
    ],
    "organizer": [
        {
            "path": r"C:\LOTUSW4\ORGANIZE\OR1C30EN.DLL",
            "source": "rea-notes/lotus-startup-exports.md",
            "kind": "recorded-runtime-initialization",
            "detail": "Prior isolated Organizer probe observed initialization of OR1C30EN.DLL.",
        }
    ],
    "freelance": [
        {
            "path": ntpath.join(r"C:\LOTUSW4\FLG", name),
            "source": "rea-notes/lotus-startup-exports.md",
            "kind": "recorded-runtime-module-load",
            "detail": "Prior isolated Freelance probes observed FLWRSEN.DLL/FLWRS8EN.DLL loading.",
        }
        for name in ("FLWRSEN.DLL", "FLWRS8EN.DLL")
    ],
}


def guest_key(name: str) -> str:
    return ntpath.normpath(name).upper()


def verify_dump(path: Path, executable: Path, parsed: dict) -> dict:
    """Cross-check the existing parser; NE name offsets are decoded above."""
    if any("forwarder" in item["origins"] for item in parsed["imports"]):
        return {
            "status": "not-run",
            "reason": "existing lx_dump forwarder bundle decoder does not advance its cursor",
        }
    try:
        process = subprocess.run([str(executable), str(path)], capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"status": "failed", "error": str(error)}
    text = process.stdout.decode("latin1")
    ordinal_pairs = set()
    modules = {}
    reading = False
    for line in text.splitlines():
        if re.match(r"(?:Import modules|Module reference table) \(", line):
            reading = True
            continue
        match = re.fullmatch(r"\s*(\d+):\s*(.*?)", line) if reading else None
        if match:
            modules[int(match[1])] = module_name(match[2])
        elif reading:
            reading = False
    for module, ordinal in re.findall(
        r"Import by ordinal fixup record:\s*Module ordinal:\s*(\d+)\s*Import ordinal:\s*(\d+)", text
    ):
        ordinal_pairs.add((int(module), int(ordinal)))
    for module, ordinal in re.findall(r"IMPORTORDINAL \(module (\d+), ordinal (\d+)\)", text):
        ordinal_pairs.add((int(module), int(ordinal)))
    expected = {
        (item["module"], item["ordinal"])
        for item in parsed["imports"]
        if item["kind"] == "ordinal" and "fixup" in item["origins"]
    }
    observed = {
        (modules.get(module, f"index:{module}"), ordinal) for module, ordinal in ordinal_pairs
    }
    expected_names = {
        (item["module"], item["name"])
        for item in parsed["imports"]
        if item["kind"] == "name" and "fixup" in item["origins"]
    }
    observed_names = {
        (modules.get(int(module), f"index:{module}"), name)
        for module, name in re.findall(
            r"Import by name fixup record:\s*Module ordinal:\s*(\d+)\s*Name offset:\s*\d+ \('([^']*)'\)",
            text,
        )
    }
    names_match = parsed["format"] == "NE" or expected_names == observed_names
    status = (
        "matched"
        if process.returncode == 0 and expected == observed and names_match
        else "mismatch"
    )
    return {
        "status": status,
        "nameComparison": "LX named imports compared"
        if parsed["format"] != "NE"
        else "NE dump emits offsets only; names covered by bounded parser fixtures",
        "missingNamesFromDump": sorted(expected_names - observed_names)
        if parsed["format"] != "NE"
        else [],
        "extraNamesInDump": sorted(observed_names - expected_names)
        if parsed["format"] != "NE"
        else [],
        "exitCode": process.returncode,
        "missingFromDump": sorted(expected - observed),
        "extraInDump": sorted(observed - expected),
        "stderr": process.stderr.decode("latin1")[:4000],
    }


def masked_c(text: str) -> str:
    return re.sub(
        r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m: re.sub(r"[^\n]", " ", m[0]),
        text,
        flags=re.S,
    )


def source_functions(root: Path) -> dict:
    functions = defaultdict(list)
    for path in sorted((root / "os2libs").rglob("*.c")):
        text = path.read_text()
        masked = masked_c(text)
        for match in re.finditer(
            r"(?m)^(?:[A-Za-z_]\w*[\t *]+)+(?P<name>\w+)\s*\([^;{}]*\)\s*\{", masked
        ):
            at, depth = match.end(), 1
            while at < len(masked) and depth:
                depth += (masked[at] == "{") - (masked[at] == "}")
                at += 1
            body = text[match.start() : at]
            markers = sorted(
                set(
                    re.findall(
                        r"\b(?:FIXME|STUBBED|UNIMPLEMENTED|UNSUPPORTED|ERROR_NOT_SUPPORTED|ERROR_INVALID_FUNCTION)\b",
                        body,
                    )
                )
            )
            functions[match["name"]].append(
                {
                    "path": path.relative_to(root).as_posix(),
                    "line": text.count("\n", 0, match.start()) + 1,
                    "sha256": hashlib.sha256(text.encode()).hexdigest(),
                    "markers": markers,
                }
            )
    return functions


def export_inventory(root: Path) -> dict:
    inventories = {}
    functions = source_functions(root)
    api32 = json.loads((root / "guest/bindings/api32.json").read_text())
    api16 = json.loads((root / "guest/bindings/api16.json").read_text())
    for path in sorted((root / "os2libs/generated").glob("*-lx.h")):
        module = module_name(path.name.removesuffix("-lx.h"))
        entries = {}
        text = path.read_text()
        pattern = r'LX_NATIVE_EXPORT(?:16|32)?(?:_BRIDGED)?(?P<different>_DIFFERENT_NAME)?\((?P<symbol>\w+),\s*(?:"(?P<name>[^"]+)",\s*)?(?P<ordinal>\d+)\)'
        for match in re.finditer(pattern, text):
            symbol, ordinal = match["symbol"], int(match["ordinal"])
            locations = [
                entry
                for entry in functions.get(symbol, [])
                if entry["path"].startswith(f"os2libs/{module.lower()}/")
                or entry["path"] == f"os2libs/{module.lower()}.c"
            ]
            entries[ordinal] = {
                "ordinal": ordinal,
                "symbol": symbol,
                "name": match["name"] or symbol,
                "generatedSource": path.relative_to(root).as_posix(),
                "generatedLine": text.count("\n", 0, match.start()) + 1,
                "generatedSha256": hashlib.sha256(text.encode()).hexdigest(),
                "abi32Policy": symbol in api32.get(module.lower(), api32.get(module, {})),
                "abi16Policy": symbol in api16.get(module.lower(), api16.get(module, {})),
                "sourceLocations": locations,
                "sourceMarkers": sorted(
                    {marker for entry in locations for marker in entry["markers"]}
                ),
            }
        inventories[module] = entries
    # Empty native SOM is built, while genuine OS/2 SOM is supplied by user media.
    inventories.setdefault("SOM", {})
    runtime_path = root / "loader/imports.c"
    runtime_text = runtime_path.read_text()
    special = re.search(
        r'if \(module->nativelib && !strcasecmp\(module->name, "DOSCALLS"\) && ordinal == 295\)',
        runtime_text,
    )
    if (
        special
        and "lx_execution_flat_data_selector"
        in runtime_text[special.start() : special.start() + 500]
    ):
        inventories.setdefault("DOSCALLS", {})[295] = {
            "ordinal": 295,
            "symbol": "Thk32R3DS",
            "name": "Thk32R3DS",
            "runtimeSpecial": "absolute-flat-data-selector",
            "generatedSource": None,
            "generatedLine": None,
            "abi32Policy": False,
            "abi16Policy": False,
            "sourceLocations": [
                {
                    "path": "loader/imports.c",
                    "line": runtime_text.count("\n", 0, special.start()) + 1,
                    "sha256": hashlib.sha256(runtime_text.encode()).hexdigest(),
                    "markers": [],
                }
            ],
            "sourceMarkers": [],
        }
    if "DOSCALLS" in inventories:
        inventories["DOSCALL1"] = inventories["DOSCALLS"]
    return inventories


def config_environment(ball: Path, manifest: dict) -> dict:
    values = {}
    config = ball / manifest.get("drives", {}).get("C", "drive_c") / "CONFIG.SYS"
    if config.is_file():
        for raw in config.read_text(encoding="latin1").splitlines():
            line = re.sub(r"^SET\s+", "", raw.strip(), flags=re.I)
            if "=" in line and not line.upper().startswith("REM "):
                key, value = line.split("=", 1)
                values[key.upper().strip()] = value.strip()
    values.update(
        {key.upper(): str(value) for key, value in manifest.get("environment", {}).items()}
    )
    return values


def scan(ball: Path, source: Path, lx_dump: Path | None = None) -> dict:
    manifest_bytes = (ball / "ball.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    drive = manifest.get("drives", {}).get("C", "drive_c")
    if not isinstance(drive, str):
        raise ValueError("the scanner currently requires a directory-backed C: drive")
    drive_c = (ball / drive).resolve()
    paths = {
        guest_key("C:\\" + path.relative_to(drive_c).as_posix().replace("/", "\\")): path
        for path in sorted(drive_c.rglob("*"))
        if path.is_file()
        and not path.is_symlink()
        and path.suffix.upper() in {".EXE", ".DLL", ".DRV", ".PDR", ".OCX", ".SYS"}
        and "/OS2/DLL/2INE/" not in path.as_posix().upper()
    }
    parsed = {}
    exports = export_inventory(source)
    env = config_environment(ball, manifest)
    catalogue = json.loads((source / "tools/data/os2imports.json").read_text())
    builtin_text = (source / "ball/system_modules.h").read_text()
    builtin_block = re.search(r"static const char \*modules\[\].*?\{(.*?)NULL", builtin_text, re.S)
    provided = set(re.findall(r'"([A-Z0-9]+)"', builtin_block[1])) if builtin_block else set()
    provided |= (
        {"OPENGL", "PGL"} if manifest.get("system", {}).get("opengl", "mesa") == "mesa" else set()
    )
    if manifest.get("system", {}).get("tls_upgrade"):
        provided.add("SSLEAY32")
    native_candidates = set(exports) | {"OPENGL", "PGL"}

    def binary(path):
        if path not in parsed:
            entry = inspect_file(paths[path])
            entry.update(
                path=path, applications=[], staticApplications=[], knownDynamicApplications=[]
            )
            if lx_dump and entry["format"] in {"LX", "NE"}:
                entry["parserCrossCheck"] = verify_dump(paths[path], lx_dump, entry)
            parsed[path] = entry
        return parsed[path]

    def closure(root, environment):
        queue, seen, unresolved = [root], set(), []
        search = [
            part
            for key in ("BEGINLIBPATH", "LIBPATH", "ENDLIBPATH")
            for part in environment.get(key, "").split(";")
            if part and part != "."
        ]
        for path in queue:
            if path in seen:
                continue
            seen.add(path)
            if path not in paths:
                unresolved.append({"binary": path, "module": None, "reason": "root-not-installed"})
                continue
            entry = binary(path)
            for module in entry["modules"]:
                # Explicitly native-only providers take precedence over arbitrary
                # copies. Original HELPMGR/SOM and third-party DLLs are followed.
                if module in provided:
                    continue
                candidates = [
                    guest_key(ntpath.join(folder, module + ".DLL"))
                    for folder in [ntpath.dirname(path), *search]
                ]
                found = next((candidate for candidate in candidates if candidate in paths), None)
                if found:
                    if found not in seen:
                        queue.append(found)
                elif module not in native_candidates:
                    other = sorted(
                        candidate
                        for candidate in paths
                        if ntpath.basename(candidate) == module + ".DLL"
                    )
                    unresolved.append(
                        {
                            "binary": path,
                            "module": module,
                            "reason": "unreachable" if other else "missing",
                            "otherPaths": other,
                        }
                    )
        return sorted(seen), sorted(
            unresolved, key=lambda item: (item["binary"], item["module"] or "")
        )

    applications: list[dict[str, Any]] = []
    for identity, default in sorted(APPS.items()):
        root = guest_key(default)
        saved = next(
            (
                app
                for app in manifest.get("applications", {}).values()
                if guest_key(app.get("program", "")) == root
            ),
            {},
        )
        app_env = {
            **env,
            **{key.upper(): str(value) for key, value in saved.get("environment", {}).items()},
        }
        members, missing = closure(root, app_env)
        applications.append(
            {
                "id": identity,
                "root": root,
                "binaries": members,
                "unresolvedModules": missing,
                "knownDynamicRoots": [],
                "searchPaths": {
                    key: app_env.get(key, "") for key in ("BEGINLIBPATH", "LIBPATH", "ENDLIBPATH")
                },
            }
        )
        for path in members:
            if path in parsed:
                parsed[path]["applications"].append(identity)
                parsed[path]["staticApplications"].append(identity)
        for observed in KNOWN_DYNAMIC.get(identity, []):
            dynamic_root = guest_key(observed["path"])
            if dynamic_root not in paths:
                dynamic_root = next(
                    (
                        candidate
                        for candidate in paths
                        if ntpath.basename(candidate) == ntpath.basename(dynamic_root)
                    ),
                    dynamic_root,
                )
            dynamic_members, dynamic_missing = closure(dynamic_root, app_env)
            provenance = {
                **observed,
                "sourceSha256": hashlib.sha256(
                    (source / observed["source"]).read_bytes()
                ).hexdigest()
                if (source / observed["source"]).is_file()
                else None,
            }
            applications[-1]["knownDynamicRoots"].append(
                {
                    "path": dynamic_root,
                    "provenance": provenance,
                    "binaries": dynamic_members,
                    "unresolvedModules": dynamic_missing,
                }
            )
            for path in dynamic_members:
                if path in parsed:
                    if identity not in parsed[path]["applications"]:
                        parsed[path]["applications"].append(identity)
                    if identity not in parsed[path]["knownDynamicApplications"]:
                        parsed[path]["knownDynamicApplications"].append(identity)
    optional_roots = sorted(
        path
        for path in paths
        if path.startswith("C:\\LOTUSW4\\")
        and paths[path].suffix.upper() in {".EXE", ".DLL", ".DRV", ".PDR", ".OCX"}
    )
    optional_missing = []
    for root in optional_roots:
        _members, missing = closure(root, env)
        optional_missing.extend(missing)
    rows = {}
    for path, entry in sorted(parsed.items()):
        entry["scope"] = (
            "static-closure"
            if entry["staticApplications"]
            else "verified-dynamic-closure"
            if entry["knownDynamicApplications"]
            else "optional-lotus-closure"
        )
        for imported in entry["imports"]:
            key = imported["module"], imported["kind"], imported[imported["kind"]]
            if key not in rows:
                module, kind, value = key
                candidates = exports.get(module, {})
                export = (
                    candidates.get(value)
                    if kind == "ordinal"
                    else next((item for item in candidates.values() if item["name"] == value), None)
                )
                row: dict[str, Any] = {
                    "module": module,
                    "kind": kind,
                    kind: value,
                    "twoineLibrary": module in native_candidates,
                    "policyProvided": module in provided,
                    "exportPresent": export is not None,
                    "generatedExportPresent": bool(export and not export.get("runtimeSpecial")),
                    "selectedProvider": "2ine" if module in provided else "original-or-third-party",
                    "exportSymbol": export["symbol"] if export else None,
                    "catalogueName": catalogue.get(module, {}).get(str(value))
                    if kind == "ordinal"
                    else value,
                    "export": export,
                    "implementationEvidence": "export-present-body-located"
                    if export and export["sourceLocations"]
                    else "export-present-body-not-located"
                    if export
                    else "export-missing",
                    "semanticCompleteness": "not-assessed",
                    "interfaceKind": "absolute-data"
                    if export and export.get("runtimeSpecial") == "absolute-flat-data-selector"
                    else "function-or-data-not-validated",
                    "importers": [],
                    "applications": [],
                    "staticApplications": [],
                    "knownDynamicApplications": [],
                    "optionalImporters": [],
                    "fixupCount": 0,
                }
                rows[key] = row
            row = rows[key]
            row["importers"].append(path)
            row["applications"] = sorted(set(row["applications"]) | set(entry["applications"]))
            for category in ("staticApplications", "knownDynamicApplications"):
                row[category] = sorted(set(row[category]) | set(entry[category]))
            if not entry["applications"]:
                row["optionalImporters"].append(path)
            row["fixupCount"] += imported["fixupCount"]
    return {
        "schema": 1,
        "sourceRevision": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=source, check=True, capture_output=True, text=True
        ).stdout.strip(),
        "manifestSha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "configurationSha256": hashlib.sha256((drive_c / "CONFIG.SYS").read_bytes()).hexdigest()
        if (drive_c / "CONFIG.SYS").is_file()
        else None,
        "parserSourceHashes": {
            name: hashlib.sha256((source / name).read_bytes()).hexdigest()
            for name in (
                "tools/lotus_imports.py",
                "tools/os2binary.py",
                "tools/lx_dump.c",
                "loader/load_lx.c",
                "loader/load_ne.c",
                "formats/executable/lx.h",
                "formats/executable/ne.h",
                "ball/system_modules.h",
            )
        },
        "parserOrigins": [
            "tools/lx_dump.c",
            "loader/load_lx.c",
            "loader/load_ne.c",
            "formats/executable/lx.h",
            "formats/executable/ne.h",
        ],
        "limits": [
            "Counted static module tables, relocations and forwarded-export targets plus separately provenance-tagged previously observed startup DLL roots. Other computed DosLoadModule/DosQueryProcAddr choices and execution frequency are not inferred.",
            "fixupCount counts encoded relocation records/forwarders, not dynamic calls or all links in a source chain.",
            "Export presence, ABI policies, located source bodies and explicit markers do not establish complete semantics.",
            "Internal-entry fixups are decoded using the primary IBM LX specification; opaque parameter-type payloads, compressed chain expansion and unsupported format records remain explicit rather than invented.",
            "Provider precedence follows ball/system_modules.h plus selected Mesa/TLS policy; directory lookup uses importer directory then configured LIBPATH. Optional Lotus DLL closures are a separate conservative superset.",
        ],
        "applications": applications,
        "optionalRoots": optional_roots,
        "optionalUnresolvedModules": [
            json.loads(item)
            for item in sorted({json.dumps(item, sort_keys=True) for item in optional_missing})
        ],
        "binaries": [parsed[path] for path in sorted(parsed)],
        "imports": [rows[key] for key in sorted(rows, key=lambda key: (key[0], key[1], key[2]))],
        "summary": {
            "binaries": len(parsed),
            "staticClosureBinaries": sum(
                bool(item["staticApplications"]) for item in parsed.values()
            ),
            "verifiedDynamicOnlyBinaries": sum(
                bool(item["knownDynamicApplications"]) and not item["staticApplications"]
                for item in parsed.values()
            ),
            "optionalClosureBinaries": sum(not item["applications"] for item in parsed.values()),
            "parseErrors": sum(bool(item["errors"]) for item in parsed.values()),
            "importParseErrors": sum(
                bool(item["errors"]) and not item.get("importsComplete", False)
                for item in parsed.values()
            ),
            "exportMetadataErrors": sum(
                bool(item["errors"])
                and bool(item.get("importsComplete"))
                and not item.get("exportsComplete", False)
                for item in parsed.values()
            ),
            "unsupportedFormats": dict(
                Counter(
                    item["format"]
                    for item in parsed.values()
                    if item["format"] not in {"LX", "LE", "NE"}
                )
            ),
            "uniqueImports": len(rows),
            "uniqueTwoineImports": sum(item["twoineLibrary"] for item in rows.values()),
            "missingTwoineExports": sum(
                item["twoineLibrary"] and not item["exportPresent"] for item in rows.values()
            ),
            "uniqueSelectedNativeImports": sum(item["policyProvided"] for item in rows.values()),
            "missingSelectedNativeExports": sum(
                item["policyProvided"] and not item["exportPresent"] for item in rows.values()
            ),
        },
    }


def render_csv(report: dict) -> str:
    output = io.StringIO(newline="")
    columns = [
        "module",
        "kind",
        "ordinal",
        "name",
        "catalogueName",
        "twoineLibrary",
        "policyProvided",
        "exportPresent",
        "exportSymbol",
        "implementationEvidence",
        "applications",
        "staticApplications",
        "knownDynamicApplications",
        "importers",
        "optionalImporters",
        "sourceLocations",
        "sourceMarkers",
        "fixupCount",
    ]
    writer = csv.DictWriter(output, columns, lineterminator="\n")
    writer.writeheader()
    for row in report["imports"]:
        item = {key: row.get(key, "") for key in columns}
        for key in (
            "applications",
            "staticApplications",
            "knownDynamicApplications",
            "importers",
            "optionalImporters",
        ):
            item[key] = ";".join(row[key])
        export = row.get("export") or {}
        item["sourceLocations"] = ";".join(
            f"{source['path']}:{source['line']}" for source in export.get("sourceLocations", [])
        )
        item["sourceMarkers"] = ";".join(export.get("sourceMarkers", []))
        writer.writerow(item)
    return output.getvalue()


def render_markdown(report: dict) -> str:
    lines = [
        "# Lotus static import inventory",
        "",
        "Source revision: `" + report["sourceRevision"] + "`.",
        "",
        "```json",
        json.dumps(report["summary"], indent=2, sort_keys=True),
        "```",
        "",
        "Export presence is not semantic completeness. Dynamic imports require separate runtime evidence.",
        "",
        "| Application | Static binaries | Verified dynamic roots / closure sizes | Unresolved static modules |",
        "| --- | ---: | --- | ---: |",
    ]
    lines += [
        f"| {app['id']} | {len(app['binaries'])} | {', '.join(ntpath.basename(root['path']) + ': ' + str(len(root['binaries'])) for root in app['knownDynamicRoots']) or 'none inferred'} | {len(app['unresolvedModules'])} |"
        for app in report["applications"]
    ]
    lines += [
        "",
        "## All imported interfaces in 2ine libraries",
        "",
        "| Module | Ordinal/name | Symbol | Export | Source evidence | Applications |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in report["imports"]:
        if row["twoineLibrary"]:
            export = row.get("export") or {}
            source = (
                ", ".join(
                    f"{item['path']}:{item['line']}" for item in export.get("sourceLocations", [])
                )
                or "body not located"
            )
            markers = ", ".join(export.get("sourceMarkers", []))
            lines.append(
                f"| {row['module']} | {row.get('ordinal', row.get('name'))} | {row.get('exportSymbol') or row.get('catalogueName') or '?'} | {'present' if row['exportPresent'] else 'MISSING'} | {source}{': ' + markers if markers else ''} | {', '.join(row['applications']) or 'optional corpus'} |"
            )
    lines += ["", "## Limitations", "", *("- " + limit for limit in report["limits"]), ""]
    return "\n".join(lines)


def missing_native_report(report: dict) -> dict:
    """Exclude original providers and retain optional feature candidates."""
    return {
        **report,
        "imports": [
            item
            for item in report["imports"]
            if item["policyProvided"] and not item["exportPresent"]
        ],
    }


def portable_policy_report(report: dict) -> dict:
    """Keep source exports without any recorded portable guest ABI policy."""
    rows = []
    for item in report["imports"]:
        exported = item.get("export") or {}
        if (
            item["policyProvided"]
            and item["exportPresent"]
            and not exported.get("runtimeSpecial")
            and not exported.get("abi32Policy")
            and not exported.get("abi16Policy")
        ):
            rows.append(item)
    return {**report, "imports": rows}


def write_report(report: dict, output: Path, inline_references=False) -> dict:
    """Write a small index plus every exact reference in streaming gzip JSONL.

    Binary import references become {file, firstLine, count}; line numbers are
    one-based after gzip decompression. parse_image() always returns inline refs.
    """
    output.mkdir(parents=True, exist_ok=True)
    saved = {**report, "binaries": []}
    line = 1
    reference_path = output / "references.jsonl.gz"
    with reference_path.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as stream:
            for binary in report["binaries"]:
                copied = {**binary, "imports": []}
                for item in binary["imports"]:
                    references = item["references"]
                    for reference in references:
                        record = {
                            "binary": binary["path"],
                            "module": item["module"],
                            "kind": item["kind"],
                            item["kind"]: item[item["kind"]],
                            **reference,
                        }
                        stream.write(
                            (
                                json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
                            ).encode()
                        )
                    copied["imports"].append(
                        {
                            **item,
                            "references": references
                            if inline_references
                            else {
                                "file": "references.jsonl.gz",
                                "firstLine": line,
                                "count": len(references),
                            },
                        }
                    )
                    line += len(references)
                saved["binaries"].append(copied)
    saved["referenceData"] = {
        "path": "references.jsonl.gz",
        "format": "gzip-json-lines",
        "records": line - 1,
        "sha256": hashlib.sha256(reference_path.read_bytes()).hexdigest(),
    }
    (output / "imports.json").write_text(
        json.dumps(saved, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
    )
    (output / "imports.csv").write_text(render_csv(saved), encoding="utf-8", newline="")
    (output / "imports.md").write_text(render_markdown(saved), encoding="utf-8", newline="\n")
    (output / "missing-native.csv").write_text(
        render_csv(missing_native_report(saved)), encoding="utf-8", newline=""
    )
    (output / "portable-policy-gaps.csv").write_text(
        render_csv(portable_policy_report(saved)), encoding="utf-8", newline=""
    )
    (output / "missing-libraries.json").write_text(
        json.dumps(
            {
                "scope": "Conservative installed Lotus/plugin closure; not all are needed by a launch.",
                "modules": saved["optionalUnresolvedModules"],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    return saved


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ball", type=Path)
    parser.add_argument(
        "--binary", type=Path, help="inspect one NE/LX/LE file, printing inline-reference JSON"
    )
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=Path("build/lotus-import-inventory"))
    parser.add_argument("--lx-dump", type=Path)
    parser.add_argument("--inline-references", action="store_true")
    args = parser.parse_args()
    if args.binary:
        if args.ball:
            parser.error("choose --binary or --ball")
        result = inspect_file(args.binary)
        if args.lx_dump:
            result["parserCrossCheck"] = verify_dump(args.binary, args.lx_dump, result)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    if not args.ball:
        parser.error("choose --binary or --ball")
    report = scan(
        args.ball.resolve(), args.source.resolve(), args.lx_dump.resolve() if args.lx_dump else None
    )
    write_report(report, args.output, args.inline_references)
    print(json.dumps(report["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
