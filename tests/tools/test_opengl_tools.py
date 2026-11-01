# Public domain. No rights reserved. Wholly AI-authored.
"""Check GL ABI widths, pointer policies and the macOS dependency build plan."""

import json
import tempfile
import unittest
from pathlib import Path

from tools import build_mesa_macos, gen_opengl

ROOT = Path(__file__).resolve().parents[2]


class OpenGLGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.schema = json.loads((ROOT / "tools/data/opengl.json").read_text())
        self.output = gen_opengl.render(self.schema)

    def test_distinct_ordinal_profiles_and_double_stack_width(self):
        entries = self.output["os2libs/generated/opengl-entries.h"]
        self.assertIn('{"glBegin", call_glBegin, 4, 22, 275}', entries)
        self.assertIn('{"glOrtho", call_glOrtho, 48,', entries)
        wire = self.output["guest/bindings/opengl.h"]
        self.assertTrue("double far_val = (double)gl_double(a, 40)" in wire)
        self.assertIn("float value = (float)gl_float(a, 4)", wire)

    def test_pixel_spans_include_pack_state_and_queries_have_explicit_extents(self):
        wire = self.output["guest/bindings/opengl.h"]
        self.assertIn("gl_pixel_bytes(ctx, width, height, format, type, 1)", wire)
        self.assertIn("gl_texture_bytes(ctx, target, level, format, type)", wire)
        self.assertIn("gl_query_count(pname)", wire)
        self.assertIn("gl_map_query_count(ctx, target, query)", wire)
        with self.assertRaisesRegex(ValueError, "missing pointer extent"):
            gen_opengl.pointer_size("glUnknownPointer", "data")

    def test_no_host_sized_longs_in_dispatch_and_generated_outputs_match(self):
        self.assertNotIn("long", self.output["hal/include/hal/gl_dispatch.h"])
        for relative, generated in self.output.items():
            self.assertEqual((ROOT / relative).read_text(), generated, relative)

    def test_kosmickrisp_build_includes_opengl_egl_and_zink_without_moltenvk(self):
        plan = build_mesa_macos.commands(Path("/mesa"), Path("/build"), Path("/prefix"), 3)
        setup = plan[0]
        for value in (
            "-Dopengl=true",
            "-Degl=enabled",
            "-Dgallium-drivers=zink",
            "-Dvulkan-drivers=kosmickrisp",
            "-Dglx=disabled",
        ):
            self.assertIn(value, setup)
        self.assertFalse(any("moltenvk" in value for value in setup))
        self.assertEqual(plan[1], ["meson", "compile", "-C", "/build", "-j", "3"])

    def test_unimplemented_graphics_adapters_fail_configuration(self):
        import subprocess

        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "probe.cmake"
            for backend in ("vulkan", "directx"):
                script.write_text(
                    "cmake_minimum_required(VERSION 3.24)\nset(CMAKE_SYSTEM_NAME Linux)\nset(TWOINE_OPENGL ON)\n"
                    f"set(TWOINE_OPENGL_BACKEND {backend})\n"
                    f'include("{ROOT / "cmake/OpenGL.cmake"}")\n'
                )
                result = subprocess.run(
                    ["cmake", "-P", str(script)], capture_output=True, text=True, timeout=10
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("is not implemented", result.stderr)


if __name__ == "__main__":
    unittest.main()
