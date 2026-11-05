# OpenGL/PGL and DIVE completion

User request: finish both implementations. This is host compatibility tooling,
not a reconstructed OS/2 artifact or a reconstruction milestone claim.

Authority: the retained Toolkit 4.5 `h/dive.h`, `h/fourcc.h`, `h/gl/pgl.h`,
`samples/mm/dive/show.c`, `samples/mm/fsdive/fsdive.c`, and the GL/PGL wire
catalogue in `tools/data/opengl.json`. Preserve fixed-width guest layouts and
checked memory access on native and emulated workers.

Acceptance requires actual behavior for public exports, supported-format
capabilities matching the implementation, short blitter setup records,
offscreen conversion, palette selection, transparent and partial-line blits,
framebuffer acquisition lifetime, PGL context copying, font lists and bitmap
exchange. Verify pixel results, invalid inputs, ownership and cleanup through
host and guest tests. Rendering/acceleration claims require real drivers and
runtime evidence on each claimed host; compilation alone does not qualify them.

Existing unrelated working-tree edits are preserved. Keep remaining work and
unavailable prerequisites explicit; do not replace missing behavior with a
success-only export or declare general OpenGL conformance from entry counts.


## Implemented and checked

- DIVE public ordinals 1–19 have native declarations and checked guest bridges.
  Non-screen ownership, 20 input/11 output formats, planar allocation extents,
  destination palettes, short setup records, transparent and line-mask blits,
  linear framebuffer acquisition/addressing and unload cleanup are implemented.
  Linear displays reject bank switching rather than reporting a fictitious bank.
  Pending partial updates preserve prior pixels and visible-region clipping is
  bounded by the rectangle and pixel counts.
- PGL server attribute copying includes raster state, light/material parameters,
  eye-space planes, pixel maps, evaluator data, texture state and bindings.
  Client state/matrix stacks are independent. Font helpers generate real glyph
  bitmap lists. Front-bitmap acquisition owns a PM PS and bitmap until release,
  blocks guest GL calls while held and does not write edits back to GL.
- GL attribute-pop handling restores the read buffer under PIXEL_MODE_BIT and
  the draw buffer under COLOR_BUFFER_BIT. Both ordinal profiles exercise this.
- The macOS Mesa helper now locates the Vulkan loader and embeds its directory
  on Mesa's own libraries. Linking the application or preloading the loader did
  not fix Zink's `@rpath` lookup from its dynamically loaded driver library.

`docs/opengl.md` and `docs/dive.md` describe the implemented contracts and limits.

## Authority hashes (SHA-256)

Paths are relative to `/Users/jimd/src/os2`.

| Input | SHA-256 |
| --- | --- |
| sdk/os2tk45-master/h/dive.h | 29a35cb52af1062ad0153b791945aaaa19fa28b490d4211d2919dbf94895c966 |
| sdk/os2tk45-master/h/fourcc.h | d9758d19921fac5d088d2870de27eb8ab2534741f6ff352f6c6298b1e12764b3 |
| sdk/os2tk45-master/h/gl/pgl.h | e7f8e4154ab263a2164c9feeacf76d06abc0cba0e8d77cc74f7d3c9f8e4b530a |
| ddk/components-2004/mmpmetc/DDK/MME/SRC/MM/VSD/IBMVID/initdll.c | 58ac295eaec6680bea0309ad83309d5e2c3d438f3256c92581809d5f0b4727d6 |

The IBMVID `The784Palette` table supplies the default 256-entry indexed palette.
The host adapter is C99 with fixed-width guest ABI decoding; native host pointers
never enter guest caps, blitter records or buffer pointers.

## Runtime evidence

Local build: `build-graphics-completion`, ARM64 macOS 27.2, Apple M5 Max,
Unicorn 2.1.4, SDL 3.4.18, Mesa commit
`6c4d70f284cbad6e7aa1ce15be104bf480cadd94` (27.0.0-devel), Zink/KosmicKrisp.
Mesa was built from a clean workspace-local checkout with LLVM/libclc 23.1.5,
SPIR-V translator 23.1.3, SPIR-V tools and Vulkan loader 1.4.363.0, and current
Meson/Bison/Flex. The source checkout was not modified. Build logs are retained
under `build-graphics-completion/evidence`; Mesa build files and libraries are
under `build-graphics-mesa*`.

Commands:

```
cmake --build build-graphics-completion --target twoine_opengl_tests twoine_opengl_pm_tests twoine_dive_tests twoine_dive_format_tests twoine_visibility_tests lx_loader doscalls dive -j8
ctest --test-dir build-graphics-completion -R '^(opengl_(mesa|pm|guest)|dive_(formats|video|guest_apis)|pm_visible_regions)$' --output-on-failure
python3 -m unittest tests.tools.test_opengl_tools
python3 tools/gen_opengl.py --check
python3 tools/lxapigen.py --check
```

The final seven-test GL/DIVE/visible-region run passed together, using the real GPU for GL and
actual LX callers for both GL ordinal profiles and DIVE. The packed/planar codec
suite, including saturated YUV key preservation, also passed standalone AddressSanitizer and UndefinedBehaviorSanitizer.
The owning generator/build-helper Python suite passed 5/5. Full source-boundary
validation reports unrelated networking/diagnostics entries in the dirty tree;
these changes do not qualify that check or the entire repository.

The shared working tree's concurrent desktop work temporarily omitted two
sources from its link. This test build uses a build-local CMake deferred include
for `hal/common/desktop_cycle.c` and `hal/posix/desktop_cycle.c`; the desktop
implementation and its production source list were not changed by this task.

## Outstanding broader completion

This is not full completion of every gap in the original survey. Still absent:
GPU DIVE conversion and shared-surface GL-to-PM presentation; color-index,
stereo/auxiliary/accumulation/overlay visuals; full physical desktop/VRAM or VMAN
emulation; YUV output/GREY/GY16; supplied GLU/GLUT/AUX; newer guest GL/extensions;
and a qualified Windows renderer. Linux native and portable runtime regression
has not been rerun here. The current GPU reports a missing Zink base requirement
for custom border colors; passing these tests is not a conformance certificate.
The scope clarification about Toolkit compatibility versus all newer APIs/hosts
remains unanswered. Retain these limits rather than marking the broad user
request complete.
