# rg_text

Bitmap-font loading, UTF-8 measurement, quad generation, and SDL3 GPU drawing
for the Reverse Gravity ecosystem.

`rg_text` is intentionally direct: it depends on the sibling
[`rg_core`](../rg_core) repository, uses the RGFONT text format in `.font`
files, and uses raw RGBA8 atlas images produced by the bake tool. It is basic
left-to-right bitmap text, not a Unicode shaping engine.

## Contents

- `src/rg_text.h` — renderer-neutral metrics parsing, glyph lookup, kerning,
  measurement, alignment, and quad generation.
- `src/rg_text_gpu.h` — SDL3 GPU atlas upload, batching, upload-ring staging,
  and indexed drawing.
- `tools/rg_text_bake.c` — optional offline `.ttf`/`.otf` converter using
  FreeType and HarfBuzz.
- `shaders/` — HLSL source for the SDL3 GPU path. Generated backend binaries
  are intentionally ignored.
- `examples/hello_text.c` — runnable SDL3 GPU example with an included pixel font.
- `benchmarks/bench_text.c` — layout benchmark, with optional RGFONT input.

See [the API and format notes](docs/rg_text.md) for usage.

## Build and test

From a Visual Studio Developer Command Prompt:

```bat
build.bat test
build.bat test_ci
build.bat shaders
build.bat example
build.bat bench
```

`test` runs the renderer-neutral suite only and never silently skips GPU work.
`test_ci` additionally requires SDL3, compiles the GPU device test, example,
and benchmark, and translates every shader backend. `test_release` is the
strict local release gate: it also builds the optional baker, bakes the Inter
test font named by `RG_TEXT_TEST_FONT`, runs the SDL_GPU pixel-readback tests,
and smoke-tests the example. The device test accepts a backend name, for example
`test_text_gpu_device.exe vulkan`, when SDL3's DLL is on `PATH`.

Run `build.bat example` from this repository's root. The example includes a
small original bitmap font under this project's MIT license, so it needs no
baker or downloaded font. Escape or closing the window exits; use
`build.bat example --smoke-test` for one hidden offscreen frame.

`build.bat bench` reports layout costs for a generated font. To benchmark a
real asset, use `build.bat bench path\to\font.font`. It measures complete
left-aligned lines and a one-quad output limit; timing is informational and
is not used as a CI pass/fail threshold.

The public baker round-trip uses renderer-version-tolerant structural checks.
Pinned CI also sets `RG_TEXT_BAKER_GOLDEN=1` to lock selected Inter 4.1 glyph,
metric, atlas, and kerning invariants against the manifest's pinned Windows
dependency versions.

`RG_CORE_DIR` defaults to `..\rg_core`. SDL-dependent checks use `SDL3_DIR`.
CI pins `rg_core` to `27d5475a4af221813977f4b7d62e4e3f88cffab2`.
Shader compilation uses `SHADERCROSS_EXE` or an SDL_shadercross installation
under `C:\libs`.

The bake tool is optional at runtime:

```bat
vcpkg install --x-feature=baker
build.bat rg_text_bake
```

The manifest's optional `baker` feature provides FreeType and HarfBuzz. The
build automatically finds the default `vcpkg_installed\x64-windows` prefix.
Other installations can be supplied with `FREETYPE_DIR` and `HARFBUZZ_DIR`, or
with the corresponding `*_INCLUDE_DIR`, `*_LIB_DIR`, and optional `*_BIN_DIR`
variables.

Only `rg_text_bake.exe` links the font libraries; neither the runtime headers
nor the generated assets depend on them. FreeType produces normally hinted
grayscale glyphs and metrics. HarfBuzz selects the active OpenType `kern`
feature and evaluates each candidate pair with kerning enabled and disabled,
so legacy `kern` data and modern GPOS positioning follow the font's declared
script and feature structure.

Kerning-enabled bakes are limited to 512 supported glyphs, bounding the
quadratic offline shaping pass. Use the leading `--no-kerning` option for
larger atlases (up to the 2,048-codepoint selection limit); it emits no pair
records and skips HarfBuzz shaping.

The baker writes `<output_base>.font` and a tightly packed, row-major
`<output_base>.rgba` straight-alpha atlas. The atlas byte size is
`width * height * 4`, using the dimensions stored in the `.font` file. RGFONT
remains a basic codepoint-to-glyph format: substitutions are disabled during
pair extraction,
and placement or advance changes that cannot be represented by one horizontal
pair adjustment are omitted. Runtime ligatures, bidirectional text, and
complex-script shaping remain outside this library's scope.

The SDL3 GPU renderer premultiplies atlas pixels during upload for correct
linear filtering, and uses matching shaders and blending. Rebuild the supplied
shaders when updating the renderer; custom fragment shaders must follow the
[premultiplied output contract](docs/rg_text.md#sdl3-gpu-path). Caller-provided
atlas bytes and vertex colors remain straight-alpha.

Both outputs are fully staged in unique same-directory files before either
final path is replaced. The atlas is replaced first and `.font` last as the
commit marker. Each replacement is atomic, but the two-file format cannot make
the pair crash-atomic across a power loss; applications should not read a bake
while it is being replaced.

The baker holds an output-base lock through staging and commit. A second baker
targeting the same base exits without touching the outputs; retry it after the
first bake finishes.

The public runtime headers target ISO C99. Release CI compiles and runs them in
strict C99 mode with both GCC and Clang. MSVC builds use `/std:c11` because MSVC
does not provide a C99 language-mode switch.

The optional baker uses C11 with MSVC and GNU11 with GCC or Clang. Its C11
exclusive-create file mode and platform locking protect staged output from name
collisions and concurrent bakes. Those build requirements do not affect
applications that consume the runtime headers or baked assets.

## License and trademark

The software and documentation are available under the [MIT License](LICENSE).
Reverse Gravity is a registered trademark of Steven Wendel in the United
States. The license grants rights to the software and documentation, but not
to the Reverse Gravity name or trademark except to identify the origin of this
software. See the
[USPTO trademark record](https://tmsearch.uspto.gov/search/search-results/86371513)
(Serial No. `86371513`, Registration No. `4805325`).
