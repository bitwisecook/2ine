# Public domain. No rights reserved.
# Wholly AI-authored.
"""Exercise export ABI validation and NE/LX import utility behavior."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import cvtasm
import lxapigen
import os2imports
import unimplemented


class AssemblerTests(unittest.TestCase):
    def test_emits_each_instruction_byte(self) -> None:
        self.assertEqual(
            list(cvtasm.convert(["00000000  90  nop\n", "00000001  31C0  xor eax,eax\n"])),
            [
                "    *(ptr++) = 0x90;  /* nop */\n",
                "    *(ptr++) = 0x31;  /* xor eax,eax */\n",
                "    *(ptr++) = 0xC0;\n",
            ],
        )

    def test_rejects_partial_hex_byte(self) -> None:
        with self.assertRaises(ValueError):
            list(cvtasm.convert(["00000000  9  nop\n"]))


class ImportTests(unittest.TestCase):
    def test_ne_and_lx_imports_and_diagnostics(self) -> None:
        lines = """Module reference table (3):
1: doscalls
2: missing

  Fixup type: IMPORTORDINAL (module 1, ordinal 5)
Import by ordinal fixup record:
Module ordinal: 1
Import ordinal: 5
  Fixup type: IMPORTORDINAL (module 1, ordinal 999)
  Fixup type: IMPORTORDINAL (module 2, ordinal 1)
  Fixup type: IMPORTORDINAL (module 3, ordinal 1)
  Fixup type: IMPORTNAME (module 1, ordinal 10)
""".splitlines(keepends=True)
        output, errors = os2imports.resolve(lines, {"DOSCALLS": {5: "Dos16Exit"}})
        self.assertEqual(
            output, ["WARNING: Saw IMPORTNAME, currently unsupported\n", "Dos16Exit (DOSCALLS@5)\n"]
        )
        self.assertCountEqual(
            errors,
            [
                "*** UNKNOWN MODULE MISSING!\n",
                "*** UNKNOWN MODULE ORDINAL 3!\n",
                "*** UNKNOWN ORDINAL 999 IN MODULE DOSCALLS!\n",
            ],
        )

    def test_catalogue_types_and_symbols(self) -> None:
        root = Path(__file__).resolve().parents[1]
        catalogue = os2imports.load_catalogue(root / "os2imports.json")
        self.assertEqual(catalogue["DOSCALLS"][5], "Dos16Exit")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text('{"DOSCALLS": {"5": false}}')
            with self.assertRaises(ValueError):
                os2imports.load_catalogue(path)

    def test_filters_implemented_exports(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "doscalls-lx.h").write_text(
                "    LX_NATIVE_EXPORT16(Dos16Exit, 5),\n    LX_NATIVE_EXPORT32(DosOpen, 273),\n"
            )
            exports = unimplemented.implemented(path)
            self.assertEqual(exports, {"DOSCALLS@5", "DOSCALLS@273"})
            self.assertEqual(
                list(
                    unimplemented.missing(
                        ["Dos16Exit (DOSCALLS@5)\n", "Dos32Close (DOSCALLS@257)\n"], exports
                    )
                ),
                ["DosClose (DOSCALLS@257)\n"],
            )


class GeneratorTests(unittest.TestCase):
    def test_rejects_duplicate_ordinals(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate ordinal"):
            lxapigen.parse_functions(
                "OS2EXPORT APIRET OS2API First(VOID) OS2APIINFO(1);\nOS2EXPORT APIRET OS2API Second(VOID) OS2APIINFO(1);"
            )

    def test_use16_stack_order_and_full_pointer_policy(self) -> None:
        functions = lxapigen.parse_functions(
            "OS2EXPORT APIRET16 OS2API16 Read(USHORT count, PVOID buf) OS2APIINFO(1); /* output */"
        )
        function = functions[0]
        with self.assertRaisesRegex(ValueError, "pointer policy"):
            lxapigen.bridge16(function, ())
        policy = lxapigen.PointerPolicy("buf", "count", 2, "copy", False)
        output = lxapigen.render("sample", functions, {"Read": (policy,)}, {})
        self.assertLess(
            output.index("BRIDGE_PTRARG(PVOID, buf)"), output.index("BRIDGE_ARG(USHORT, count)")
        )
        self.assertIn("LX_NATIVE_INIT_16BIT_BRIDGE(Read, 6)", output)
        self.assertIn("lx_guest_argument_finish(&__abi_args[0], __abi_copy)", output)

    def test_void_and_native_only_bridges(self) -> None:
        function = lxapigen.parse_functions("OS2EXPORT VOID OS2API16 Exit(VOID) OS2APIINFO(5);")[0]
        output = lxapigen.bridge16(function, None)
        self.assertTrue(output.startswith("#if !TWOINE_PORTABLE_LOADER"))
        self.assertIn("Exit();\n    return 0;", output)

    def test_rejects_bad_wire_abi(self) -> None:
        for values in [(3, 0, "LX_ABI_STACK32"), (8, 0, "LX_ABI_EAX32"), (4, 8, "LX_ABI_STACK32")]:
            with self.subTest(values=values), tempfile.TemporaryDirectory() as directory:
                size, pop, convention = values
                path = Path(directory) / "api32.json"
                path.write_text(
                    json.dumps(
                        {
                            "sample": {
                                "Call": {
                                    "argument_bytes": size,
                                    "callee_pop": pop,
                                    "convention": convention,
                                }
                            }
                        }
                    )
                )
                with self.assertRaises(ValueError):
                    lxapigen.read_policies32(path)


if __name__ == "__main__":
    unittest.main()
