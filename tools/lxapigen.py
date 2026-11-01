#!/usr/bin/env python3
# 2ine-derived; zlib-style license. See LICENSE.txt.
"""Generate native OS/2 export glue from headers and explicit guest ABI policies."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import cast

TYPE_SIZES = {"LONG": 4, "ULONG": 4, "SHORT": 2, "USHORT": 2, "HVIO": 2, "HKBD": 2}


@dataclass(frozen=True)
class Argument:
    type: str
    name: str

    @property
    def size(self) -> int:
        if self.type.startswith("P"):
            return 4
        if self.type not in TYPE_SIZES:
            raise ValueError(f"unknown argument type {self.type!r}")
        return TYPE_SIZES[self.type]


@dataclass(frozen=True)
class Function:
    result: str
    name: str
    arguments: tuple[Argument, ...]
    ordinal: int
    exported_name: str | None
    use16: bool
    variadic: bool = False


@dataclass(frozen=True)
class PointerPolicy:
    name: str
    size: str
    permissions: int
    kind: str
    nullable: bool


@dataclass(frozen=True)
class Abi32:
    argument_bytes: int
    callee_pop: int
    convention: str


Policies16 = dict[str, dict[str, tuple[PointerPolicy, ...]]]
Policies32 = dict[str, dict[str, Abi32]]


def object_fields(value: object, location: str) -> dict[str, object]:
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise ValueError(f"{location}: expected an object with string keys")
    return cast(dict[str, object], value)


def string(value: object, location: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{location}: expected string")
    return value


def integer(value: object, location: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{location}: expected integer")
    return value


def read_policies16(path: Path) -> Policies16:
    result: Policies16 = {}
    for module, raw in object_fields(json.loads(path.read_text()), str(path)).items():
        if module == "_notice":
            continue
        functions: dict[str, tuple[PointerPolicy, ...]] = {}
        for name, items in object_fields(raw, module).items():
            if not isinstance(items, list):
                raise ValueError(f"{module}.{name}: expected pointer policy list")
            policies: list[PointerPolicy] = []
            for item in cast(list[object], items):
                fields = object_fields(item, name)
                kind = string(fields.get("kind"), name)
                permissions = integer(fields.get("permissions"), name)
                nullable = fields.get("nullable")
                if kind not in {"string", "command", "environment", "live", "copy"}:
                    raise ValueError(f"{name}: invalid pointer kind {kind!r}")
                if permissions not in range(8) or not isinstance(nullable, bool):
                    raise ValueError(f"{name}: invalid permissions or nullable flag")
                policies.append(
                    PointerPolicy(
                        string(fields.get("name"), name),
                        string(fields.get("size"), name),
                        permissions,
                        kind,
                        nullable,
                    )
                )
            functions[name] = tuple(policies)
        result[module] = functions
    return result


def read_policies32(path: Path) -> Policies32:
    result: Policies32 = {}
    for module, raw in object_fields(json.loads(path.read_text()), str(path)).items():
        if module == "_notice":
            continue
        functions: dict[str, Abi32] = {}
        for name, item in object_fields(raw, module).items():
            fields = object_fields(item, name)
            size = integer(fields.get("argument_bytes"), name)
            pop = integer(fields.get("callee_pop"), name)
            convention = string(fields.get("convention"), name)
            if not 0 <= pop <= size <= 65535 or convention not in {
                "LX_ABI_STACK32",
                "LX_ABI_EAX32",
            }:
                raise ValueError(f"{name}: invalid wire ABI")
            if size % 4 or pop % 4 or (convention == "LX_ABI_EAX32" and (size != 4 or pop)):
                raise ValueError(f"{name}: invalid wire argument alignment or EAX convention")
            functions[name] = Abi32(size, pop, convention)
        result[module] = functions
    return result


def parse_functions(text: str) -> list[Function]:
    functions: dict[int, Function] = {}
    # Export prototypes may wrap across lines and contain parameter comments.
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    for declaration in re.split(r"(?<=;)", text):
        line = " ".join(declaration.split())
        if "OS2APIINFO" not in line:
            continue
        start = line.find("OS2EXPORT")
        if start >= 0:
            line = line[start:]
        match = re.fullmatch(
            r"OS2EXPORT\s+(.+?)\s*(OS2API|OS2API16)\s+(.*?)\s*\((.*?)\)\s+OS2APIINFO\((.*?)\);",
            line,
        )
        if match is None:
            raise ValueError(f"could not parse export: {line}")
        result, api, name, parameters, info = (value.strip() for value in match.groups())
        options: dict[str, str] = {}
        if info.isdecimal():
            options["ord"] = info
        else:
            for option in info.split(","):
                key, value = option.split("=", 1)
                key, value = key.strip(), value.strip()
                if key not in {"ord", "name"}:
                    raise ValueError(f"{name}: unknown OS2APIINFO key {key!r}")
                options[key] = value
        ordinal = int(options["ord"])
        arguments: list[Argument] = []
        variadic = False
        if parameters not in {"void", "VOID"}:
            parts = parameters.split(",")
            for index, parameter in enumerate(parts):
                if parameter.strip() == "...":
                    if index != len(parts) - 1 or not index or api == "OS2API16":
                        raise ValueError(f"{name}: invalid variadic parameter list")
                    variadic = True
                    continue
                type_name, argument_name = parameter.strip().rsplit(None, 1)
                arguments.append(Argument(type_name, argument_name))
        if ordinal in functions:
            raise ValueError(f"duplicate ordinal #{ordinal}: {name} and {functions[ordinal].name}")
        functions[ordinal] = Function(
            result,
            name,
            tuple(arguments),
            ordinal,
            options.get("name"),
            api == "OS2API16",
            variadic,
        )
    return [functions[key] for key in sorted(functions)]


def bridge16(function: Function, policy: tuple[PointerPolicy, ...] | None) -> str:
    name = function.name
    lines: list[str] = []
    if policy is None:
        lines.append("#if !TWOINE_PORTABLE_LOADER\n")
    lines.append(f"static uint32 bridge16to32_{name}(uint8 *args) {{\n")
    pointers: dict[str, str] = {}
    for argument in reversed(function.arguments):
        kind = "PTRARG" if argument.type.startswith("P") else "ARG"
        if kind == "PTRARG":
            pointers[argument.name] = argument.type
        lines.append(
            f"    LX_NATIVE_MODULE_16BIT_BRIDGE_{kind}({argument.type}, {argument.name});\n"
        )
    if policy is not None:
        listed = {entry.name for entry in policy}
        if listed != pointers.keys() or len(listed) != len(policy):
            raise ValueError(f"{name}: incomplete USE16 pointer policy")
    if policy:
        lines.append(f"    LxGuestArgument __abi_args[{len(policy)}] = {{0}};\n")
        lines.append(
            "    uint32 __abi_result = ERROR_INVALID_PARAMETER;\n    int __abi_copy = 0;\n"
        )
        for index, entry in enumerate(policy):
            nullable = int(entry.nullable)
            if entry.kind in {"string", "command", "environment"}:
                count = {"string": 1, "command": 2, "environment": 0}[entry.kind]
                call = f"lx_guest_argument_strings(GLoaderState.execution, {entry.name}_guest, {count}, {entry.size}, {nullable}, &__abi_args[{index}])"
            else:
                live = int(entry.kind == "live")
                call = f"lx_guest_argument_far(GLoaderState.execution, {entry.name}_guest, {entry.size}, {entry.permissions}, {live}, {nullable}, &__abi_args[{index}])"
            lines.append(f"    if (!{call}) goto __abi_done;\n")
            lines.append(f"    {entry.name} = ({pointers[entry.name]}) __abi_args[{index}].host;\n")
    returns = function.result not in {"void", "VOID"}
    prefix = ("__abi_result = " if policy else "return ") if returns else ""
    arguments = ", ".join(argument.name for argument in function.arguments)
    lines.append(f"    {prefix}{name}({arguments});\n")
    if policy:
        if not returns:
            lines.append("    __abi_result = 0;\n")
        lines.append("    __abi_copy = 1;\n__abi_done:\n")
        for index in range(len(policy)):
            lines.append(f"    lx_guest_argument_finish(&__abi_args[{index}], __abi_copy);\n")
        lines.append("    return __abi_result;\n")
    elif not returns:
        lines.append("    return 0;\n")
    lines.append("}\n")
    if policy is None:
        lines.append("#endif\n")
    return "".join(lines) + "\n"


def render(
    module: str,
    functions: list[Function],
    api16: dict[str, tuple[PointerPolicy, ...]],
    api32: dict[str, Abi32],
) -> str:
    lines = [
        "/* 2ine. Zlib-style license; see LICENSE.txt. */\n"
        "/* Generated by lxapigen.py. */\n\n#if LX_LEGACY\n\n"
    ]
    if api32:
        lines.append('#include "os2libs/os2native32.h"\n#if TWOINE_PORTABLE_LOADER\n')
        lines.append(f'#include "guest/bindings/{module}.h"\nLX_NATIVE_MODULE_32BIT_SUPPORT()\n')
        for name in sorted(api32):
            lines.append(f"LX_NATIVE_MODULE_32BIT_API({name})\n")
        lines.append("#endif\n\n")
    use16 = [function for function in functions if function.use16]
    for function in use16:
        lines.append(bridge16(function, api16.get(function.name)))
    if use16:
        lines.append("LX_NATIVE_MODULE_16BIT_SUPPORT()\n")
        for function in use16:
            if function.name not in api16:
                lines.append("#if !TWOINE_PORTABLE_LOADER\n")
            lines.append(f"    LX_NATIVE_MODULE_16BIT_API({function.name})\n")
            if function.name not in api16:
                lines.append("#endif\n")
        lines.append(
            "LX_NATIVE_MODULE_16BIT_SUPPORT_END()\n\nLX_NATIVE_MODULE_DEINIT({\n    LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT();\n"
        )
        if api32:
            lines.append("    LX_NATIVE_MODULE_DEINIT_32BIT_SUPPORT();\n")
        lines.append(
            f"}})\n\nstatic int init16_{module}(void) {{\n    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT()\n"
        )
        for function in use16:
            size = sum(argument.size for argument in function.arguments)
            if function.name not in api16:
                lines.append("#if !TWOINE_PORTABLE_LOADER\n")
            lines.append(f"        LX_NATIVE_INIT_16BIT_BRIDGE({function.name}, {size})\n")
            if function.name not in api16:
                lines.append("#endif\n")
        lines.append("    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END()\n    return 1;\n}\n\n")
    if api32:
        if not use16:
            lines.append("LX_NATIVE_MODULE_DEINIT({ LX_NATIVE_MODULE_DEINIT_32BIT_SUPPORT(); })\n")
        lines.append(
            f"#if TWOINE_PORTABLE_LOADER\nstatic int init32_{module}(void) {{\n    LX_NATIVE_MODULE_INIT_32BIT_SUPPORT()\n"
        )
        for name, abi in sorted(api32.items()):
            lines.append(
                f"    LX_NATIVE_INIT_32BIT_BRIDGE({name}, {abi.argument_bytes}, {abi.callee_pop}, {abi.convention})\n"
            )
        lines.append("    LX_NATIVE_MODULE_INIT_32BIT_SUPPORT_END()\n    return 1;\n}\n#endif\n")
    lines.append("LX_NATIVE_MODULE_INIT({\n")
    if use16:
        lines.append(f"    if (!init16_{module}()) return 0;\n")
    if api32:
        lines.append(f"    LX_NATIVE_INIT_32BIT_MODULE({module})\n")
    lines.append("})\n")
    for function in functions:
        name = function.name
        suffix = "16" if function.use16 else "32" if name in api32 else ""
        hidden = name not in (api16 if function.use16 else api32)
        if hidden:
            lines.append("#if !TWOINE_PORTABLE_LOADER\n")
        macro = f"LX_NATIVE_EXPORT{suffix}"
        arguments = f"{name}, {function.ordinal}"
        if function.exported_name is not None:
            macro += "_DIFFERENT_NAME"
            arguments = f'{name}, "{function.exported_name}", {function.ordinal}'
        lines.append(f"    {macro}({arguments}),\n")
        if hidden:
            lines.append("#endif\n")
    lines.append(
        f"\nLX_NATIVE_MODULE_INIT_END()\n\n#endif /* LX_LEGACY */\n\n/* end of {module}-lx.h ... */\n\n"
    )
    return "".join(lines).rstrip() + "\n"


def generate(root: Path, *, check: bool = False) -> bool:
    policies16 = read_policies16(root / "guest/bindings/api16.json")
    policies32 = read_policies32(root / "guest/bindings/api32.json")
    outputs: dict[Path, str] = {}
    for header in sorted((root / "os2libs").glob("*.h")):
        if header.name.startswith("os2") or header.name.endswith("-lx.h"):
            continue
        functions = parse_functions(header.read_text())
        if functions:
            module = header.stem
            outputs[root / "os2libs/generated" / f"{module}-lx.h"] = render(
                module,
                functions,
                policies16.get(module, {}),
                policies32.get(module, {}),
            )
    if not check:
        (root / "os2libs/generated").mkdir(parents=True, exist_ok=True)
    changed = False
    for path, text in outputs.items():
        if path.exists() and path.read_text() == text:
            continue
        changed = True
        print(f"{path.name}: {'out of date' if check else 'updated'}")
        if not check:
            path.write_text(text)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="report stale glue without changing files"
    )
    check = bool(parser.parse_args().check)
    try:
        changed = generate(Path(__file__).resolve().parents[1], check=check)
    except (OSError, ValueError, KeyError) as error:
        print(f"lxapigen: {error}", file=sys.stderr)
        return 1
    return int(check and changed)


if __name__ == "__main__":
    raise SystemExit(main())
