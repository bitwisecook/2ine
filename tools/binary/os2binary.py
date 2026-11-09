#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Standalone read-only NE/LX/LE import/fixup scanner and offline API annotator.

No 2ine runtime, compiler, Ghidra, network, or non-stdlib dependency is required.
parse_image(bytes) and inspect_file(Path) retain exact encoded references.
scan(paths, catalogue) accepts the portable os2api schema1 modules/ordinals map.
Also decodes PE32/64 imports/delay imports/export forwarders, OMF IMPORTDEF and
EXPDEF, aligned OMF libraries, ar/COFF import libraries, and COFF symbol evidence.
Unknown DLL associations and unimplemented payload/parameter metadata stay
explicit; static records do not establish dynamically computed loads.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from collections import Counter, defaultdict
from itertools import islice
from pathlib import Path
from typing import Any

LIMIT = 2_000_000


class InvalidImage(ValueError):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data

    def take(self, at: int, size: int) -> bytes:
        if at < 0 or size < 0 or at + size > len(self.data):
            raise InvalidImage(f"metadata outside file: offset {at}, size {size}")
        return self.data[at : at + size]

    def number(self, at: int, size: int) -> int:
        return int.from_bytes(self.take(at, size), "little")

    def text(self, at: int, mask=255) -> str:
        size = self.number(at, 1) & mask
        return self.take(at + 1, size).decode("latin1")


def module_name(name: str) -> str:
    return name.upper().removesuffix(".DLL")


def parse_image(data: bytes, *, exports_only=False, allow_foreign=False, _depth=0) -> dict:
    """Bounded adaptation of the repository's counted LX/NE metadata parsers."""
    reader = Reader(data)
    result: dict[str, Any] = {
        "format": "unsupported",
        "domain": "unknown",
        "modules": [],
        "imports": [],
        "exports": [],
        "internalReferences": [],
        "importsComplete": None if exports_only else False,
        "exportsComplete": False,
        "warnings": [],
        "errors": [],
        "scope": "exports" if exports_only else "imports-and-exports",
        "supported": False,
        "complete": False,
    }
    counts: Counter = Counter()
    origins: dict = defaultdict(set)
    references: dict = defaultdict(list)
    reference_count = 0

    def imported(module: int, value: int | str, by_name=False, origin="fixup", reference=None):
        if not 1 <= module <= len(result["modules"]):
            raise InvalidImage(f"invalid import module index {module}")
        nonlocal reference_count
        reference_count += 1
        if reference_count > LIMIT:
            raise InvalidImage("import reference count exceeds bound")
        key = (result["modules"][module - 1], "name" if by_name else "ordinal", value)
        counts[key] += 1
        origins[key].add(origin)
        if reference is not None:
            reference.update(moduleIndex=module, form=origin)
            references[key].append(reference)

    try:
        initial = reader.take(0, 2)
        if initial == b"MZ":
            h = reader.number(60, 4)
        elif initial in (b"LX", b"LE", b"NE"):
            h = 0
        else:
            # Recognize other offline binary families without inventing a decoder.
            if initial == b"PE":
                return parse_pe(data, 0, exports_only)
            elif data[:8] == b"!<arch>\n":
                return parse_archive(data, _depth, exports_only, allow_foreign)
            elif data[:3] == b"\xf3\x15\0":
                return parse_f3_library(data, exports_only)
            elif data[:4] == b"\0\0\xff\xff":
                if reader.number(4, 2) == 0:
                    return parse_coff_import(data)
                result["format"] = "COFF-bigobj-unsupported"
            elif len(data) >= 20 and reader.number(0, 2) in (
                0x14C,
                0x8664,
                0x1C0,
                0x1C4,
                0xAA64,
                0x200,
            ):
                return parse_coff_object(data)
            elif (
                initial[0] in (0x80, 0x82, 0x88, 0xF0)
                and len(data) >= 3
                and 0 < reader.number(1, 2) <= len(data) - 3
            ):
                return parse_omf(data)
            return result
        magic = reader.take(h, 2)
        result["format"] = magic.decode("ascii", errors="replace")
        if magic == b"PE":
            return parse_pe(data, h, exports_only)
        if magic in (b"LX", b"LE"):
            reader.take(h, 196)
            result["targetOSCode"] = reader.number(h + 10, 2)
            result["domain"] = (
                "os2" if result["targetOSCode"] == 1 else f"linear-target-{result['targetOSCode']}"
            )
            if reader.take(h + 2, 2) != bytes(2):
                raise InvalidImage("non-little-endian linear executable unsupported")
            if result["targetOSCode"] != 1 and not (exports_only or allow_foreign):
                result["format"] += "-other"
                return result
            result["moduleFlags"] = reader.number(h + 16, 4)
            resident = reader.number(h + 88, 4)
            if resident:
                result["moduleName"] = reader.text(h + resident)
            modules = reader.number(h + 116, 4)
            if modules > 65535:
                raise InvalidImage("module count exceeds 65535")
            at = h + reader.number(h + 112, 4)
            for _ in range(modules):
                name = reader.text(at)
                if not name:
                    raise InvalidImage("empty module name")
                result["modules"].append(module_name(name))
                at += 1 + reader.number(at, 1)
            names = h + reader.number(h + 120, 4)
            pages = reader.number(h + 20, 4)
            table = h + reader.number(h + 104, 4)
            records = h + reader.number(h + 108, 4)
            if pages > LIMIT:
                raise InvalidImage("page count exceeds bound")
            offsets = (
                [reader.number(table + i * 4, 4) for i in range(pages + 1)]
                if not exports_only
                else []
            )
            object_count = reader.number(h + 68, 4)
            if object_count > 65535:
                raise InvalidImage("object count exceeds bound")
            object_table = h + reader.number(h + 64, 4)
            owners = defaultdict(list)
            page_size = reader.number(h + 40, 4)
            for obj in range(object_count):
                object_at = object_table + obj * 24
                base = reader.number(object_at + 4, 4)
                first, length = reader.number(object_at + 12, 4), reader.number(object_at + 16, 4)
                if length > pages or length and (first == 0 or first + length - 1 > pages):
                    raise InvalidImage("object page range exceeds module pages")
                for local in range(length):
                    owners[first - 1 + local].append(
                        {
                            "object": obj + 1,
                            "objectPage": local,
                            "preferredBase": base + local * page_size,
                        }
                    )
            if any(a > b for a, b in zip(offsets, offsets[1:], strict=False)):
                raise InvalidImage("nonmonotonic fixup page table")
            for page in range(0 if exports_only else pages):
                at, end = records + offsets[page], records + offsets[page + 1]
                reader.take(at, end - at)
                while at < end:
                    start = at
                    source, flags = reader.number(at, 1), reader.number(at + 1, 1)
                    at += 2
                    count = reader.number(at, 1) if source & 0x20 else 0
                    source_offsets = (
                        []
                        if source & 0x20
                        else [int.from_bytes(reader.take(at, 2), "little", signed=True)]
                    )
                    reference = {
                        "recordOffset": start,
                        "recordOffsetInFixupTable": start - records,
                        "pageIndex": page,
                        "sourceType": source & 15,
                        "sourceFlags": source,
                        "targetFlags": flags,
                        "sourceList": bool(source & 0x20),
                        "internalChain": bool(flags & 8),
                        "alias16": bool(source & 0x10),
                        "sourceOffsets": source_offsets,
                        "preferredSources": [],
                        "sourceAddressesAreRuntimeAddresses": False,
                    }
                    at += 1 if source & 0x20 else 2
                    kind = flags & 3
                    module_size = 2 if flags & 0x40 else 1
                    module = reader.number(at, module_size)
                    at += module_size
                    if kind == 3:
                        reference.update(
                            form="internal-entry",
                            entryOrdinal=module,
                            targetFieldOffset=at - module_size,
                        )
                        result["internalReferences"].append(reference)
                    elif kind == 0:
                        if source & 15 != 2:
                            at += 4 if flags & 0x10 else 2
                    else:
                        size = 1 if kind == 1 and flags & 0x80 else 4 if flags & 0x10 else 2
                        value = reader.number(at, size)
                        at += size
                        reference["targetFieldOffset"] = at - size
                        if kind == 2:
                            reference["targetNameOffset"] = names + value
                        imported(
                            module,
                            reader.text(names + value, 127) if kind == 2 else value,
                            kind == 2,
                            reference=reference,
                        )
                    if flags & 4:
                        at += 4 if flags & 0x20 else 2
                    if count:
                        source_offsets.extend(
                            int.from_bytes(reader.take(at + i * 2, 2), "little", signed=True)
                            for i in range(count)
                        )
                    reference["preferredSources"] = [
                        {
                            "object": obj["object"],
                            "objectPage": obj["objectPage"],
                            "addresses": [
                                obj["preferredBase"] + offset for offset in source_offsets
                            ],
                        }
                        for obj in owners[page]
                    ]
                    reference["recordSize"] = at + count * 2 - start
                    reference["additionalChainSourcesDecoded"] = not bool(flags & 8)
                    at += count * 2
                    if at <= start or at > end:
                        raise InvalidImage("fixup record exceeds its page boundary")
            result["importsComplete"] = None if exports_only else True
            # Forwarded export references are static imports too, even when no
            # ordinary relocation uses that target. Keep their provenance.
            at = h + reader.number(h + 92, 4)
            if reader.number(h + 92, 4):
                export_ordinal = 1
                for _ in range(65536):
                    count = reader.number(at, 1)
                    if not count:
                        break
                    kind = reader.number(at + 1, 1)
                    at += 2
                    typed = bool(kind & 0x80)
                    if typed:
                        result["warnings"].append(
                            "LX entry bundle has opaque parameter type metadata; fixed entry layout follows the existing loader"
                        )
                    kind &= 0x7F
                    if kind == 0:
                        export_ordinal += count
                        continue
                    obj = reader.number(at, 2)
                    at += 2
                    if kind in (1, 2, 3):
                        for _entry in range(count):
                            flags = reader.number(at, 1)
                            value = reader.number(at + 1, 2 if kind in (1, 2) else 4)
                            base = (
                                reader.number(object_table + (obj - 1) * 24 + 4, 4)
                                if 1 <= obj <= object_count
                                else None
                            )
                            result["exports"].append(
                                {
                                    "ordinal": export_ordinal,
                                    "object": obj,
                                    "offset": value,
                                    "preferredAddress": base + value if base is not None else None,
                                    "recordOffset": at,
                                    "recordSize": 3 if kind == 1 else 5,
                                    "flags": flags,
                                    "kind": "call-gate" if kind == 2 else "entry",
                                    "callGateSelector": reader.number(at + 3, 2)
                                    if kind == 2
                                    else None,
                                    "parameterTypesDecoded": not typed,
                                }
                            )
                            at += 3 if kind == 1 else 5
                            export_ordinal += 1
                    elif kind == 4:
                        for _entry in range(count):
                            record_at = at
                            flags = reader.number(at, 1)
                            mod = reader.number(at + 1, 2)
                            target = reader.number(at + 3, 4)
                            at += 7
                            imported(
                                mod,
                                target if flags & 1 else reader.text(names + target, 127),
                                not flags & 1,
                                "forwarder",
                                {
                                    "recordOffset": record_at,
                                    "recordSize": 7,
                                    "exportOrdinal": export_ordinal,
                                    "targetFlags": flags,
                                    "targetFieldOffset": record_at + 3,
                                    "targetNameOffset": None if flags & 1 else names + target,
                                },
                            )
                            result["exports"].append(
                                {
                                    "ordinal": export_ordinal,
                                    "recordOffset": record_at,
                                    "recordSize": 7,
                                    "flags": flags,
                                    "kind": "forwarder",
                                    "forwarder": {
                                        "module": result["modules"][mod - 1],
                                        "kind": "ordinal" if flags & 1 else "name",
                                        "ordinal" if flags & 1 else "name": target
                                        if flags & 1
                                        else reader.text(names + target, 127),
                                    },
                                    "parameterTypesDecoded": not typed,
                                }
                            )
                            export_ordinal += 1
                    else:
                        raise InvalidImage(f"unknown entry bundle {kind}")
                else:
                    raise InvalidImage("entry bundle bound reached")
        elif magic == b"NE":
            reader.take(h, 64)
            result["targetOSCode"] = reader.number(h + 54, 1)
            result["domain"] = {1: "os2", 2: "win16", 3: "dos4", 4: "win386"}.get(
                result["targetOSCode"], "unknown"
            )
            if result["targetOSCode"] > 1 and not (exports_only or allow_foreign):
                result["format"] = "NE-other"
                return result
            result["moduleFlags"] = reader.number(h + 12, 2)
            resident = reader.number(h + 38, 2)
            if resident:
                result["moduleName"] = reader.text(h + resident)
            names = h + reader.number(h + 42, 2)
            refs = h + reader.number(h + 40, 2)
            for i in range(reader.number(h + 30, 2)):
                result["modules"].append(
                    module_name(reader.text(names + reader.number(refs + i * 2, 2)))
                )
            segments = h + reader.number(h + 34, 2)
            shift = reader.number(h + 50, 2)
            if shift > 31:
                raise InvalidImage("invalid NE sector shift")
            for i in range(0 if exports_only else reader.number(h + 28, 2)):
                at = segments + i * 8
                sector, size, flags = (reader.number(at + n, 2) for n in (0, 2, 4))
                if not flags & 0x100 or not sector:
                    continue
                if not size:
                    raise InvalidImage(
                        "NE zero-sized segment relocation offset is not decoded (repository uses historical 0xFFFF convention)"
                    )
                segment_file = sector << shift
                at = segment_file + size
                count = reader.number(at, 2)
                at += 2
                reader.take(at, count * 8)
                for fixup in range(count):
                    record = at + fixup * 8
                    kind = reader.number(record + 1, 1) & 3
                    if kind in (1, 2):
                        module, value = reader.number(record + 4, 2), reader.number(record + 6, 2)
                        source_offset = reader.number(record + 2, 2)
                        source_offsets = [source_offset]
                        chained = not bool(reader.number(record + 1, 1) & 4)
                        expanded = not chained
                        if chained and not flags & 8:
                            visited = {source_offset}
                            current = source_offset
                            while current + 2 <= size:
                                following = reader.number(segment_file + current, 2)
                                if following in (0, 65535):
                                    expanded = True
                                    break
                                if following in visited:
                                    break
                                visited.add(following)
                                source_offsets.append(following)
                                current = following
                        imported(
                            module,
                            reader.text(names + value) if kind == 2 else value,
                            kind == 2,
                            reference={
                                "recordOffset": record,
                                "recordSize": 8,
                                "segment": i + 1,
                                "segmentDataOffset": segment_file,
                                "sourceType": reader.number(record, 1),
                                "targetFlags": reader.number(record + 1, 1),
                                "sourceOffsets": source_offsets,
                                "sourceFileOffsets": [
                                    segment_file + offset for offset in source_offsets
                                ]
                                if not flags & 8
                                else [],
                                "sourceSegmentOffsets": [
                                    f"{i + 1}:{offset}" for offset in source_offsets
                                ],
                                "iteratedSegment": bool(flags & 8),
                                "additionalChainSourcesDecoded": expanded,
                                "targetFieldOffset": record + 6,
                                "targetNameOffset": names + value if kind == 2 else None,
                            },
                        )
        else:
            return result
        if magic == b"NE":
            result["importsComplete"] = None if exports_only else True
            at = h + reader.number(h + 4, 2)
            end = at + reader.number(h + 6, 2)
            ordinal = 1
            while at < end:
                count = reader.number(at, 1)
                if not count:
                    break
                kind = reader.number(at + 1, 1)
                at += 2
                if kind == 0:
                    ordinal += count
                    continue
                for _entry in range(count):
                    record = at
                    flags = reader.number(at, 1)
                    segment = reader.number(at + 3, 1) if kind == 255 else kind
                    value = reader.number(at + 4 if kind == 255 else at + 1, 2)
                    at += 6 if kind == 255 else 3
                    if at > end:
                        raise InvalidImage("NE export bundle exceeds entry table")
                    result["exports"].append(
                        {
                            "ordinal": ordinal,
                            "segment": segment,
                            "offset": value,
                            "recordOffset": record,
                            "recordSize": at - record,
                            "flags": flags,
                            "kind": "movable" if kind == 255 else "fixed",
                        }
                    )
                    ordinal += 1
        if magic in (b"LX", b"LE", b"NE"):
            resident_at = reader.number(
                h + (38 if magic == b"NE" else 88), 2 if magic == b"NE" else 4
            )
            nonresident_at = reader.number(h + (44 if magic == b"NE" else 136), 4)
            nonresident_size = reader.number(
                h + (32 if magic == b"NE" else 140), 2 if magic == b"NE" else 4
            )
            named = defaultdict(list)
            for name_at, name_end, scope in (
                (h + resident_at, len(data), "resident"),
                (nonresident_at, nonresident_at + nonresident_size, "nonresident"),
            ):
                if (
                    scope == "resident"
                    and not resident_at
                    or scope == "nonresident"
                    and not nonresident_at
                ):
                    continue
                for _name in range(65536):
                    size = reader.number(name_at, 1)
                    if not size:
                        break
                    if name_at + size + 3 > name_end:
                        raise InvalidImage("export name exceeds counted name table")
                    value = reader.text(name_at)
                    number = reader.number(name_at + size + 1, 2)
                    if number:
                        named[number].append(
                            {"name": value, "scope": scope, "recordOffset": name_at}
                        )
                    elif scope == "nonresident":
                        result["moduleDescription"] = value
                    name_at += size + 3
                else:
                    raise InvalidImage("export name count exceeds bound")
            for entry in result["exports"]:
                entry["names"] = named.get(entry["ordinal"], [])
            own_exports = {entry["ordinal"]: entry for entry in result["exports"]}
            for reference in result["internalReferences"]:
                reference["targetExport"] = own_exports.get(reference["entryOrdinal"])
                if reference["targetExport"] is None:
                    result["warnings"].append(
                        f"internal entry ordinal{reference['entryOrdinal']} has no decoded entry"
                    )
        result["exportsComplete"] = True
    except (InvalidImage, UnicodeError) as error:
        result["errors"].append(str(error))
    for (module, kind, value), count in sorted(counts.items()):
        result["imports"].append(
            {
                "module": module,
                "kind": kind,
                kind: value,
                "fixupCount": count,
                "origins": sorted(origins[module, kind, value]),
                "references": references[module, kind, value],
            }
        )
    result["supported"] = result["format"] in {"LX", "LE", "NE"}
    result["complete"] = result["supported"] and not result["errors"]
    return result


def _new_result(kind: str) -> dict:
    return {
        "format": kind,
        "domain": "unspecified",
        "modules": [],
        "imports": [],
        "exports": [],
        "errors": [],
        "warnings": [],
        "supported": True,
        "complete": False,
    }


def _collector(result: dict):
    indexed = {}
    total = 0

    def add(module, kind, value, reference, origin):
        nonlocal total
        total += 1
        module = module_name(module)
        if module not in result["modules"]:
            result["modules"].append(module)
        key = module, kind, value
        if key not in indexed:
            indexed[key] = {
                "module": module,
                "kind": kind,
                kind: value,
                "fixupCount": 0,
                "origins": [],
                "references": [],
            }
            result["imports"].append(indexed[key])
        entry = indexed[key]
        entry["fixupCount"] += 1
        if origin not in entry["origins"]:
            entry["origins"].append(origin)
        entry["references"].append({"form": origin, **reference})
        if total > LIMIT:
            raise InvalidImage("import count exceeds bound")

    return add


def parse_pe(data: bytes, h: int, exports_only=False) -> dict:
    """Microsoft PE/COFF specification: normal/delay imports and export targets."""
    result = _new_result("PE")
    result["domain"] = "win32"
    reader = Reader(data)
    add = _collector(result)
    try:
        if reader.take(h, 4) != b"PE\0\0":
            raise InvalidImage("invalid PE signature")
        section_count = reader.number(h + 6, 2)
        optional_size = reader.number(h + 20, 2)
        optional = h + 24
        reader.take(optional, optional_size)
        magic = reader.number(optional, 2)
        if magic not in (0x10B, 0x20B):
            raise InvalidImage("unsupported PE optional-header magic")
        wide = magic == 0x20B
        word = 8 if wide else 4
        base = reader.number(optional + (24 if wide else 28), word)
        result.update(bits=64 if wide else 32, imageBase=base, machine=reader.number(h + 4, 2))
        directory_at = optional + (112 if wide else 96)
        directory_count = reader.number(optional + (108 if wide else 92), 4)
        if (
            section_count > 4096
            or directory_count > 64
            or directory_at + directory_count * 8 > optional + optional_size
        ):
            raise InvalidImage("PE header table exceeds bounds")
        headers_size = reader.number(optional + 60, 4)
        sections = []
        for i in range(section_count):
            at = optional + optional_size + i * 40
            reader.take(at, 40)
            sections.append(
                (reader.number(at + 12, 4), reader.number(at + 16, 4), reader.number(at + 20, 4))
            )

        def offset(rva, size=1):
            candidates = []
            if rva >= 0 and rva + size <= headers_size:
                candidates.append(rva)
            for va, raw_size, raw in sections:
                if va <= rva and rva + size <= va + raw_size:
                    candidates.append(raw + rva - va)
            if not candidates or len(set(candidates)) != 1:
                raise InvalidImage(f"PE RVA0x{rva:x} has no unique file-backed mapping")
            reader.take(candidates[0], size)
            return candidates[0]

        def string(rva):
            at = offset(rva)
            end = data.find(b"\0", at, min(len(data), at + 65536))
            if end < 0:
                raise InvalidImage("unterminated PE string")
            offset(rva, end - at + 1)
            return data[at:end].decode("latin1")

        def directory(number):
            if number >= directory_count:
                return 0, 0
            return reader.number(directory_at + number * 8, 4), reader.number(
                directory_at + number * 8 + 4, 4
            )

        def imports(number, delay=False):
            address, length = directory(number)
            if not address:
                return
            stride = 32 if delay else 20
            maximum = min(65536, length // stride if length else 65536)
            for index in range(maximum):
                descriptor = offset(address + index * stride, stride)
                values = [reader.number(descriptor + i * 4, 4) for i in range(stride // 4)]
                if not any(values):
                    return
                if delay:
                    attributes = values[0]

                    def convert(value, attributes=attributes):
                        return value if attributes & 1 or not value else value - base

                    name, iat, lookup = convert(values[1]), convert(values[3]), convert(values[4])
                    lookup = lookup or iat
                else:
                    attributes = 0
                    lookup, name, iat = values[0] or values[4], values[3], values[4]
                module = string(name)
                if not lookup:
                    raise InvalidImage("PE import descriptor has no lookup/IAT")
                for item in range(65536):
                    thunk_rva = lookup + item * word
                    thunk = offset(thunk_rva, word)
                    value = reader.number(thunk, word)
                    if not value:
                        break
                    ordinal = bool(value & (1 << (word * 8 - 1)))
                    target = value & 65535 if ordinal else string(value + 2)
                    reference = {
                        "recordOffset": thunk,
                        "recordSize": word,
                        "descriptorOffset": descriptor,
                        "descriptorRva": address + index * stride,
                        "thunkRva": thunk_rva,
                        "iatRva": iat + item * word,
                        "preferredIatAddress": base + iat + item * word,
                        "sourceAddressesAreRuntimeAddresses": False,
                        "targetNameOffset": None if ordinal else offset(value + 2),
                        "hint": None if ordinal else reader.number(offset(value, 2), 2),
                        "delayAttributes": attributes if delay else None,
                    }
                    add(
                        module,
                        "ordinal" if ordinal else "name",
                        target,
                        reference,
                        "delay-import" if delay else "import",
                    )
                else:
                    raise InvalidImage("PE thunk count exceeds bound")
            # Some tools size the directory to nonzero descriptors only.
            if length:
                result["warnings"].append(
                    f"PE directory{number} ended at declared size without an all-zero descriptor"
                )
            else:
                raise InvalidImage("PE descriptor count exceeds bound")

        for number, delay in () if exports_only else ((1, False), (13, True)):
            try:
                imports(number, delay)
            except InvalidImage as error:
                result["errors"].append(str(error))
        try:
            export_rva, export_size = directory(0)
            if export_rva:
                at = offset(export_rva, 40)
                dll_name = reader.number(at + 12, 4)
                result["moduleName"] = string(dll_name) if dll_name else ""
                ordinal_base = reader.number(at + 16, 4)
                count, names = reader.number(at + 20, 4), reader.number(at + 24, 4)
                table, name_table, ord_table = (reader.number(at + n, 4) for n in (28, 32, 36))
                if count > LIMIT or names > LIMIT:
                    raise InvalidImage("PE export count exceeds bound")
                named = defaultdict(list)
                for i in range(names):
                    name_rva = reader.number(offset(name_table + i * 4, 4), 4)
                    slot = reader.number(offset(ord_table + i * 2, 2), 2)
                    if slot >= count:
                        raise InvalidImage("PE export name index exceeds address table")
                    named[slot].append({"name": string(name_rva), "recordOffset": offset(name_rva)})
                for i in range(count):
                    record = offset(table + i * 4, 4)
                    value = reader.number(record, 4)
                    if not value:
                        continue
                    entry = {
                        "ordinal": ordinal_base + i,
                        "rva": value,
                        "recordOffset": record,
                        "recordSize": 4,
                        "names": named[i],
                    }
                    if export_rva <= value < export_rva + export_size:
                        forward = string(value)
                        module, separator, target = forward.rpartition(".")
                        entry.update(kind="forwarder", forwarderString=forward)
                        if separator and module and target:
                            by_ordinal = target.startswith("#") and target[1:].isdecimal()
                            target = int(target[1:]) if by_ordinal else target
                            kind = "ordinal" if by_ordinal else "name"
                            entry["forwarder"] = {
                                "module": module_name(module),
                                "kind": kind,
                                kind: target,
                            }
                            add(
                                module,
                                kind,
                                target,
                                {
                                    "recordOffset": record,
                                    "recordSize": 4,
                                    "exportOrdinal": ordinal_base + i,
                                    "targetNameOffset": offset(value),
                                },
                                "forwarder",
                            )
                        else:
                            result["errors"].append("unrecognized PE forwarder string")
                    else:
                        entry.update(kind="entry", preferredAddress=base + value)
                    result["exports"].append(entry)
        except InvalidImage as error:
            result["errors"].append(str(error))
    except InvalidImage as error:
        result["errors"].append(str(error))
    result["complete"] = not result["errors"]
    return result


def parse_omf(data: bytes) -> dict:
    """TIS OMF1.1 COMENT IMPDEF/EXPDEF and page-aligned library traversal."""
    result = _new_result("OMF-library" if data[:1] == b"\xf0" else "OMF-object")
    result.update(members=[], externals=[], publicSymbols=[])
    reader = Reader(data)
    add = _collector(result)
    at = 0
    page_size = 0
    dictionary = 0
    member = ""
    try:
        for _record in range(LIMIT):
            if at >= len(data) or dictionary and at >= dictionary:
                break
            record = at
            kind = reader.number(at, 1)
            length = reader.number(at + 1, 2)
            reader.take(at, length + 3)
            end = at + length + 3
            payload = at + 3
            if kind == 0xF0:
                page_size = length + 3
                if page_size < 16 or page_size > 32768 or page_size & (page_size - 1):
                    raise InvalidImage("invalid OMF library page size")
                dictionary = reader.number(payload, 4)
                result.update(pageSize=page_size, dictionaryOffset=dictionary)
            elif kind == 0xF1:
                break
            else:
                if length < 1:
                    raise InvalidImage("OMF record has no checksum byte")
                if data[end - 1] and sum(data[at:end]) & 255:
                    result["warnings"].append(f"OMF checksum does not verify at{at}")
                if kind in (0x80, 0x82):
                    member = reader.text(payload)
                    if payload + 1 + reader.number(payload, 1) > end - 1:
                        raise InvalidImage("OMF module name exceeds record")
                    result["members"].append({"name": member, "offset": record})
                    if not page_size:
                        result["moduleName"] = member
                elif kind == 0x88 and length >= 4 and reader.number(payload + 1, 1) == 0xA0:
                    subtype = reader.number(payload + 2, 1)
                    cursor = payload + 3

                    def counted(end=end):
                        nonlocal cursor
                        size = reader.number(cursor, 1)
                        if cursor + size + 1 > end - 1:
                            raise InvalidImage("OMF counted string exceeds record")
                        value = reader.text(cursor)
                        cursor += 1 + size
                        return value

                    if subtype in (1, 2):
                        flags = reader.number(cursor, 1)
                        cursor += 1
                        if subtype == 1:
                            internal = counted()
                            module = counted()
                            target_at = cursor
                            target = reader.number(cursor, 2) if flags else counted() or internal
                            if flags:
                                cursor += 2
                            if cursor > end - 1:
                                raise InvalidImage("OMF import target exceeds record")
                            add(
                                module,
                                "ordinal" if flags else "name",
                                target,
                                {
                                    "recordOffset": record,
                                    "recordSize": end - record,
                                    "targetFieldOffset": target_at,
                                    "member": member,
                                    "internalName": internal,
                                    "commentClass": 0xA0,
                                    "commentSubtype": 1,
                                },
                                "omf-import-definition",
                            )
                        else:
                            exported = counted()
                            internal = counted() or exported
                            ordinal = reader.number(cursor, 2) if flags & 0x80 else None
                            if flags & 0x80:
                                cursor += 2
                            if cursor > end - 1:
                                raise InvalidImage("OMF export ordinal exceeds record")
                            result["exports"].append(
                                {
                                    "ordinal": ordinal,
                                    "names": [{"name": exported}],
                                    "internalName": internal,
                                    "member": member,
                                    "flags": flags,
                                    "residentName": bool(flags & 0x40),
                                    "noData": bool(flags & 0x20),
                                    "parameterWords": flags & 31,
                                    "recordOffset": record,
                                    "recordSize": end - record,
                                    "kind": "omf-export-definition",
                                }
                            )
                elif kind in (0x90, 0x91, 0xB6, 0xB7):
                    cursor = payload

                    def index(end=end):
                        nonlocal cursor
                        first = reader.number(cursor, 1)
                        value = (
                            ((first & 127) << 8) | reader.number(cursor + 1, 1)
                            if first & 128
                            else first
                        )
                        cursor += 2 if first & 128 else 1
                        if cursor > end - 1:
                            raise InvalidImage("OMF public index exceeds record")
                        return value

                    group = index()
                    segment = index()
                    frame = reader.number(cursor, 2) if segment == 0 else None
                    if segment == 0:
                        cursor += 2
                    while cursor < end - 1:
                        symbol_at = cursor
                        name = reader.text(cursor)
                        cursor += 1 + reader.number(cursor, 1)
                        value = reader.number(cursor, 4 if kind & 1 else 2)
                        cursor += 4 if kind & 1 else 2
                        type_index = index()
                        if cursor > end - 1:
                            raise InvalidImage("OMF public symbol exceeds record")
                        result["publicSymbols"].append(
                            {
                                "name": name,
                                "group": group,
                                "segment": segment,
                                "frame": frame,
                                "offset": value,
                                "typeIndex": type_index,
                                "member": member,
                                "recordOffset": record,
                                "symbolOffset": symbol_at,
                                "recordSize": end - record,
                                "local": kind in (0xB6, 0xB7),
                                "moduleUnknown": True,
                            }
                        )
                elif kind in (0x8C, 0xB4):
                    cursor = payload
                    while cursor < end - 1:
                        name = reader.text(cursor)
                        cursor += 1 + reader.number(cursor, 1)
                        first = reader.number(cursor, 1)
                        type_index = (
                            ((first & 127) << 8) | reader.number(cursor + 1, 1)
                            if first & 128
                            else first
                        )
                        cursor += 2 if first & 128 else 1
                        if cursor > end - 1:
                            raise InvalidImage("OMF EXTDEF exceeds record")
                        result["externals"].append(
                            {
                                "name": name,
                                "typeIndex": type_index,
                                "member": member,
                                "recordOffset": record,
                                "moduleUnknown": True,
                            }
                        )
                if kind in (0x8A, 0x8B) and page_size:
                    end = (end + page_size - 1) // page_size * page_size
            at = end
        else:
            raise InvalidImage("OMF record count exceeds bound")
    except InvalidImage as error:
        result["errors"].append(str(error))
    result["complete"] = not result["errors"]
    return result


def parse_coff_import(data: bytes) -> dict:
    result = _new_result("COFF-import-object")
    reader = Reader(data)
    add = _collector(result)
    try:
        if reader.take(0, 4) != b"\0\0\xff\xff":
            raise InvalidImage("invalid COFF import signature")
        length = reader.number(12, 4)
        payload = reader.take(20, length)
        strings = payload.split(b"\0")
        if len(strings) < 3:
            raise InvalidImage("unterminated COFF import strings")
        internal, module = strings[0].decode("latin1"), strings[1].decode("latin1")
        flags = reader.number(18, 2)
        name_type = (flags >> 2) & 7
        ordinal = reader.number(16, 2)
        if name_type == 0:
            kind, target = "ordinal", ordinal
        elif name_type in (1, 2, 3, 4):
            kind, target = "name", internal
            if name_type in (2, 3) and target[:1] in ("_", "@", "?"):
                target = target[1:]
            if name_type == 3:
                target = target.split("@", 1)[0]
            if name_type == 4:
                if len(strings) < 4:
                    raise InvalidImage("COFF EXPORTAS string missing")
                target = strings[2].decode("latin1")
        else:
            raise InvalidImage("unknown COFF import name type")
        add(
            module,
            kind,
            target,
            {
                "recordOffset": 0,
                "recordSize": 20 + length,
                "internalName": internal,
                "importType": flags & 3,
                "nameType": name_type,
                "hint": ordinal if kind == "name" else None,
            },
            "coff-import-definition",
        )
        result.update(machine=reader.number(6, 2), moduleName=module)
    except InvalidImage as error:
        result["errors"].append(str(error))
    result["complete"] = not result["errors"]
    return result


def parse_coff_object(data: bytes) -> dict:
    """COFF public/undefined symbol evidence; DLL associations remain unknown."""
    result = _new_result("COFF-object")
    result.update(externals=[], publicSymbols=[])
    reader = Reader(data)
    try:
        machine = reader.number(0, 2)
        sections = reader.number(2, 2)
        table = reader.number(8, 4)
        count = reader.number(12, 4)
        optional = reader.number(16, 2)
        result.update(machine=machine)
        if sections > 4096 or count > LIMIT:
            raise InvalidImage("COFF table count exceeds bound")
        reader.take(20 + optional, sections * 40)
        if not table or not count:
            result["complete"] = True
            return result
        reader.take(table, count * 18)
        strings = table + count * 18
        string_size = reader.number(strings, 4)
        reader.take(strings, string_size)
        index = 0
        while index < count:
            at = table + index * 18
            raw = reader.take(at, 8)
            if raw[:4] == bytes(4):
                name_at = strings + reader.number(at + 4, 4)
                if name_at < strings + 4 or name_at >= strings + string_size:
                    raise InvalidImage("COFF symbol string offset invalid")
                end = data.find(b"\0", name_at, strings + string_size)
                if end < 0:
                    raise InvalidImage("unterminated COFF symbol")
                name = data[name_at:end].decode("latin1")
            else:
                name = raw.split(b"\0", 1)[0].decode("latin1")
            value = reader.number(at + 8, 4)
            section = int.from_bytes(reader.take(at + 12, 2), "little", signed=True)
            kind = reader.number(at + 16, 1)
            auxiliary = reader.number(at + 17, 1)
            if kind in (2, 105):
                entry = {
                    "name": name,
                    "value": value,
                    "section": section,
                    "recordOffset": at,
                    "recordSize": 18,
                    "symbolIndex": index,
                    "storageClass": kind,
                    "moduleUnknown": True,
                }
                result["externals" if section == 0 else "publicSymbols"].append(entry)
            index += 1 + auxiliary
            if index > count:
                raise InvalidImage("COFF auxiliary symbols exceed table")
        if result["externals"]:
            result["warnings"].append(
                "Undefined COFF symbols have no proven DLL/ordinal association; they are unqualified external evidence only"
            )
    except InvalidImage as error:
        result["errors"].append(str(error))
    result["complete"] = not result["errors"]
    return result


def _container_record(entry: dict, base: int, member: str) -> dict:
    copied = {
        **entry,
        "containerMember": member,
        "memberOffset": base,
        "memberRecordOffset": entry.get("recordOffset"),
    }
    for key in (
        "recordOffset",
        "targetFieldOffset",
        "targetNameOffset",
        "descriptorOffset",
        "segmentDataOffset",
        "symbolOffset",
    ):
        if isinstance(entry.get(key), int):
            copied[key] = base + entry[key]
    if "sourceFileOffsets" in entry:
        copied["sourceFileOffsets"] = [base + value for value in entry["sourceFileOffsets"]]
    if "names" in entry:
        copied["names"] = [
            {**item, "recordOffset": base + item["recordOffset"]}
            if isinstance(item.get("recordOffset"), int)
            else item
            for item in entry["names"]
        ]
    return copied


def parse_f3_library(data: bytes, exports_only=False) -> dict:
    """Bounded IBM SDK F3 container index, verified against contained OMF.

    The names-size/count/offset-array layout is observed in SDK libraries; only
    accepted when every indexed member begins with a complete OMF object. No
    byte-pattern hunting or guessed offsets are used. Other header fields stay
    opaque rather than inventing dictionary semantics.
    """
    result = _new_result("F3-OMF-library")
    result.update(members=[], externals=[], publicSymbols=[])
    reader = Reader(data)
    add = _collector(result)
    try:
        if reader.take(0, 3) != b"\xf3\x15\0":
            raise InvalidImage("unsupported F3 header revision")
        names_size = reader.number(4, 4)
        count = reader.number(8, 2)
        table = 24 + names_size
        reader.take(24, names_size)
        reader.take(table, count * 4)
        names = [name.decode("latin1") for name in data[24:table].split(b"\0") if name]
        if len(names) != count:
            raise InvalidImage("F3 indexed name count does not match member count")
        offsets = [reader.number(table + i * 4, 4) for i in range(count)]
        if any(value < table + count * 4 or value >= len(data) for value in offsets) or any(
            a >= b for a, b in zip(offsets, offsets[1:], strict=False)
        ):
            raise InvalidImage("F3 member offset array invalid")
        for i, start in enumerate(offsets):
            end = offsets[i + 1] if i + 1 < count else len(data)
            if reader.number(start, 1) not in (0x80, 0x82):
                raise InvalidImage("F3 indexed member is not an OMF module")
            child = parse_omf(reader.take(start, end - start))
            result["members"].append(
                {
                    "name": names[i],
                    "offset": start,
                    "size": end - start,
                    "format": child["format"],
                    "complete": child["complete"],
                }
            )
            result["errors"].extend(f"member{names[i]}:{error}" for error in child["errors"])
            result["warnings"].extend(
                f"member{names[i]}:{warning}" for warning in child["warnings"]
            )
            if not exports_only:
                for imported in child["imports"]:
                    for reference in imported["references"]:
                        add(
                            imported["module"],
                            imported["kind"],
                            imported[imported["kind"]],
                            _container_record(reference, start, names[i]),
                            "f3-indexed-omf-member",
                        )
            for category in ("exports", "externals", "publicSymbols"):
                result[category].extend(
                    _container_record(entry, start, names[i]) for entry in child.get(category, [])
                )
        result["containerIndexAuthority"] = (
            "observed IBM SDK F3 layout, structurally validated OMF members"
        )
        result["warnings"].append(
            "F3 ancillary hash/header fields remain opaque; only the indexed OMF member metadata is decoded"
        )
    except InvalidImage as error:
        result["errors"].append(str(error))
    result["complete"] = not result["errors"] and all(
        member["complete"] for member in result["members"]
    )
    return result


def parse_archive(data: bytes, depth=0, exports_only=False, allow_foreign=False) -> dict:
    result = _new_result("ar-library")
    result["members"] = []
    result["externals"] = []
    result["publicSymbols"] = []
    add = _collector(result)
    reader = Reader(data)
    at = 8
    longnames = b""
    try:
        if depth > 4:
            raise InvalidImage("nested archive depth exceeds4")
        for _member in range(100000):
            if at == len(data):
                break
            record = reader.take(at, 60)
            if record[58:60] != b"`\n":
                raise InvalidImage("invalid ar member header")
            size = int(record[48:58].strip())
            name = record[:16].decode("latin1").strip()
            payload = at + 60
            reader.take(payload, size)
            if name == "//":
                longnames = data[payload : payload + size]
            elif name in ("/", "/SYM64/"):
                pass
            else:
                body_size = size
                if name.startswith("#1/"):
                    length = int(name[3:])
                    name = reader.take(payload, length).rstrip(b"\0").decode("latin1")
                    payload += length
                    body_size -= length
                elif name.startswith("/") and name[1:].isdecimal():
                    start = int(name[1:])
                    end = longnames.find(b"\n", start)
                    if end < 0:
                        raise InvalidImage("invalid ar long-name offset")
                    name = longnames[start:end].rstrip(b"/\0").decode("latin1")
                else:
                    name = name.removesuffix("/")
                if body_size < 0:
                    raise InvalidImage("ar extended name exceeds member")
                child = parse_image(
                    reader.take(payload, body_size),
                    exports_only=exports_only,
                    allow_foreign=allow_foreign,
                    _depth=depth + 1,
                )
                result["members"].append(
                    {
                        "name": name,
                        "offset": payload,
                        "size": body_size,
                        "format": child["format"],
                        "complete": child["complete"],
                    }
                )
                if not child["complete"]:
                    result["warnings"].append(
                        f"member{name}:unsupported or incomplete{child['format']}"
                    )
                result["errors"].extend(f"member{name}:{error}" for error in child["errors"])
                for imported in child["imports"]:
                    for reference in imported["references"]:
                        add(
                            imported["module"],
                            imported["kind"],
                            imported[imported["kind"]],
                            {
                                **_container_record(reference, payload, name),
                                "targetDomain": reference.get(
                                    "targetDomain", child.get("domain", "unknown")
                                ),
                            },
                            "archive-member",
                        )
                for category in ("externals", "publicSymbols", "exports"):
                    result[category].extend(
                        _container_record(entry, payload, name) for entry in child.get(category, [])
                    )
            at += 60 + size + (size & 1)
        else:
            raise InvalidImage("ar member count exceeds bound")
    except (InvalidImage, ValueError) as error:
        result["errors"].append(str(error))
    result["complete"] = not result["errors"] and all(
        item["complete"] for item in result["members"]
    )
    return result


def inspect_file(path: Path, *, exports_only=False, allow_foreign=False) -> dict:
    """Hash and decode a file read-only; references are always inline."""
    if path.stat().st_size > 256 * 1024 * 1024:
        raise InvalidImage("file exceeds256MiB input bound")
    data = path.read_bytes()
    return {
        **parse_image(data, exports_only=exports_only, allow_foreign=allow_foreign),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def parse_export_table(data: bytes) -> dict:
    """Decode counted exports/names/forwarders without ordinary import fixups."""
    return parse_image(data, exports_only=True)


def lookup(catalogue: dict, module: str, identifier: int | str) -> list[dict]:
    """Resolve a schema1 API catalogue record without requiring its helper module."""
    normalized = module.upper()
    for suffix in (".DLL", ".EXE", ".DRV"):
        normalized = normalized.removesuffix(suffix)
    records = catalogue.get("modules", {}).get(normalized, {}).get("ordinals", {})
    text = str(identifier).removeprefix("#")
    if isinstance(identifier, int) or text.isdecimal():
        entry = records.get(str(int(text)))
        return [{"module": normalized, "ordinal": int(text), **entry}] if entry else []
    result = []
    for ordinal, entry in records.items():
        if any(
            str(name).casefold() == text.casefold()
            for name in [
                entry.get("name", ""),
                *entry.get("aliases", []),
                *[variant["name"] for variant in entry.get("nameVariants", [])],
            ]
        ):
            result.append({"module": normalized, "ordinal": int(ordinal), **entry})
    if not result:
        for name, evidence in catalogue.get("namedSymbols", {}).get(normalized, {}).items():
            if name.casefold() == text.casefold():
                result.append(
                    {
                        "module": normalized,
                        "ordinal": None,
                        "name": name,
                        "provenance": evidence,
                        "nameOnly": True,
                    }
                )
    return sorted(result, key=lambda item: item["ordinal"] if item["ordinal"] is not None else -1)


def scan(
    paths,
    catalogue: dict | None = None,
    include_unknown=False,
    *,
    exports_only=False,
    allow_foreign=False,
    domain=None,
) -> dict:
    """Scan explicit files/directories by magic, regardless of filename suffix.

    Directory traversal skips symlinks and is bounded at100000 files. Exact
    imports retain preferred addresses, never asserted runtime addresses.
    Catalogue annotation is offline and never changes the target files.
    """
    files = []
    active_catalogue = catalogue or {}
    selected_domain = domain or active_catalogue.get("domain", "os2")
    if active_catalogue.get("domain", "os2") != selected_domain:
        active_catalogue = active_catalogue.get("domains", {}).get(selected_domain, {})
    catalogue_mismatch = bool(catalogue) and (
        not active_catalogue or active_catalogue.get("domain", "os2") != selected_domain
    )
    seen = set()
    errors = []
    skipped = 0
    visited = 0
    for requested in paths:
        requested = Path(requested)
        if not requested.exists():
            errors.append({"path": str(requested), "error": "input path does not exist"})
            continue
        candidates = (
            sorted(islice(requested.rglob("*"), 100001)) if requested.is_dir() else [requested]
        )
        if len(candidates) > 100000:
            errors.append(
                {"path": str(requested), "error": "100000 directory-entry scan limit reached"}
            )
            candidates = candidates[:100000]
        for path in candidates:
            if path.is_symlink() or not path.is_file():
                continue
            identity = str(path.resolve())
            if identity in seen:
                continue
            seen.add(identity)
            visited += 1
            if visited > 100000:
                errors.append({"path": str(requested), "error": "100000 file scan limit reached"})
                break
            try:
                if path.stat().st_size > 256 * 1024 * 1024:
                    errors.append({"path": str(path), "error": "file exceeds256MiB input bound"})
                    continue
                entry = inspect_file(path, exports_only=exports_only, allow_foreign=allow_foreign)
                if entry["format"] == "unsupported" and not include_unknown:
                    skipped += 1
                    continue
                entry["path"] = str(path)
                for imported in entry["imports"]:
                    encoded_domains = {
                        reference.get("targetDomain", entry.get("domain", "unknown"))
                        for reference in imported["references"]
                    }
                    encoded_domains.discard("unspecified")
                    mismatch = catalogue_mismatch or bool(
                        encoded_domains and encoded_domains != {selected_domain}
                    )
                    matches = (
                        []
                        if mismatch
                        else lookup(
                            active_catalogue, imported["module"], imported[imported["kind"]]
                        )
                    )
                    imported["api"] = matches
                    imported["catalogueDomain"] = selected_domain
                    imported["encodedDomains"] = sorted(encoded_domains)
                    imported["domainBinding"] = (
                        "unbound-domain-mismatch"
                        if mismatch
                        else "encoded-domain"
                        if encoded_domains
                        else "domain-not-encoded"
                    )
                    imported["resolutionStatus"] = (
                        "unbound-domain-mismatch"
                        if mismatch
                        else "ambiguous"
                        if any(entry.get("ambiguous") for entry in matches)
                        else "known-ordinal-unnamed"
                        if matches and all(not entry.get("name") for entry in matches)
                        else "known"
                        if matches
                        else "unknown"
                    )
                files.append(entry)
            except (OSError, ValueError) as error:
                errors.append({"path": str(path), "error": str(error)})
        if visited > 100000:
            break
    return {
        "schema": 1,
        "catalogueDomain": selected_domain,
        "files": files,
        "errors": errors,
        "summary": {
            "files": len(files),
            "visitedFiles": visited,
            "skippedUnknownMagic": skipped,
            "formats": dict(sorted(Counter(item["format"] for item in files).items())),
            "incompleteFiles": sum(not item["complete"] for item in files),
        },
        "limitations": [
            "NE/LX/LE/PE and library static metadata only; this does not execute code or infer dynamic module loads.",
            "NE zero-size relocation segments, COFF bigobj, vendor-specific library extensions and parameter-type metadata are not decoded; unqualified object externals are not assigned a DLL.",
            "Preferred source addresses describe object metadata, not observed runtime addresses. Compressed/iterated payloads and internal fixup chains are not materialized; chain expansion coverage is explicit per reference.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--catalogue", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--include-unknown", action="store_true")
    parser.add_argument("--exports-only", action="store_true")
    parser.add_argument(
        "--allow-foreign",
        action="store_true",
        help="Decode encoded foreign target formats without changing API namespace",
    )
    parser.add_argument("--domain", choices=("os2", "win16", "win32"))
    args = parser.parse_args()
    catalogue_bytes = args.catalogue.read_bytes() if args.catalogue else b"{}"
    if args.catalogue and args.catalogue.suffix == ".gz":
        catalogue_bytes = gzip.decompress(catalogue_bytes)
    catalogue = json.loads(catalogue_bytes)
    report = scan(
        args.paths,
        catalogue,
        args.include_unknown,
        exports_only=args.exports_only,
        allow_foreign=args.allow_foreign or args.domain in ("win16", "win32"),
        domain=args.domain,
    )
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    return 0 if not report["errors"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
