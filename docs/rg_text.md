# rg_text

`rg_text.h` parses bitmap-font metrics from caller-owned memory, measures UTF-8
text, and emits renderer-neutral quads. The runtime performs no file I/O or
allocation.

The runtime uses `rg_defs.h` and typed sorting from `rg_algo.h` in `rg_core`.
Font loading uses a fixed local sort stack (about 1.5 KiB per sort with the
default core configuration on 64-bit platforms); layout does not use it.

## Scope

- Basic left-to-right bitmap text
- UTF-8 decoding and glyph fallback
- Pair kerning
- Multi-line measurement and horizontal alignment
- Caller-owned glyph, kerning, and quad storage
- Optional SDL3 GPU batching through `rg_text_gpu.h`

Complex shaping, bidirectional text, fallback font chains, and runtime TTF
loading are intentionally outside this path.

## Loading and measurement

```c
#include "rg_text.h"

RgTextGlyph glyphs[256];
RgTextKerning kernings[128];
RgTextFontLoadDesc desc = {0};
desc.data = metrics_data;
desc.data_size = metrics_size;
desc.glyphs = glyphs;
desc.glyph_capacity = RG_ARRAY_COUNT(glyphs);
desc.kernings = kernings;
desc.kerning_capacity = RG_ARRAY_COUNT(kernings);

RgTextFont font;
if (!rg_text_font_load_rgfont(&font, &desc))
{
	return 1;
}

RgTextSize size = rg_text_measure_cstr(&font, "Hello", 1.0f);
```

The loader sorts the caller's glyph and kerning arrays in place without
allocating, then uses binary searches during layout. Duplicate glyph
codepoints and duplicate kerning pairs are rejected. Array contents and order
may change even if loading fails; use a font only after a successful load.
The arrays must outlive the font and remain unchanged while it is in use.
The source RGFONT bytes can be released after loading.

Manually assembled fonts remain supported: zero-initialize `RgTextFont`, fill
its arrays and counts, and leave its internal lookup flags at zero. Those
fonts use linear lookup and may have unsorted arrays. Do not copy a loaded
font and replace or reorder its arrays without clearing its internal lookup
flags or reloading it.

Fallback glyphs use the kerning pairs for the glyphs actually displayed.
Left-aligned quad generation does not premeasure lines and stops when the
output capacity is reached. Center and right alignment measure each line;
when `align_width` is not positive, they also measure the full text to choose
an alignment width. A build call returns the number of quads written and may
truncate to the provided capacity.

## RGFONT format

```text
rgfont 1
atlas <width> <height>
line_height <pixels>
ascent <pixels>
descent <pixels>
fallback <codepoint>
glyph <codepoint> <x> <y> <w> <h> <x_offset> <y_offset> <x_advance>
kerning <left_codepoint> <right_codepoint> <x_advance>
```

Glyph offsets are relative to the top-left text pen. Pair the metrics with the
raw `.rgba` atlas emitted by `rg_text_bake`. It is tightly packed row-major
straight-alpha RGBA8 data; the `atlas` record supplies its width and height.

## SDL3 GPU path

`rg_text_gpu.h` depends directly on `rg_gpu.h`. Its descriptor takes a shader
root using the compiled layout documented by `rg_core`. The included HLSL
expects `RgTextGpuUniforms`, containing projection and model-transform
matrices, in vertex uniform slot zero.

The frame flow is:

1. `rg_text_gpu_begin`
2. `rg_text_gpu_queue` or `rg_text_gpu_queue_ex`
3. Map an `RgGpuUploadRing` and call `rg_text_gpu_stage_upload`
4. Encode the staged copies with `rg_text_gpu_encode_upload`
5. Call `rg_text_gpu_flush` inside the caller's render pass

End the upload ring mapping before encoding copies. Keep the queue unchanged
between staging and drawing. The renderer's pipeline expects one color target
with the format supplied at creation, single sampling, and no depth attachment.
See [the runnable example](../examples/hello_text.c) for the complete setup,
frame ordering, projection, and cleanup.

Quad packing uses portable C by default. On x86 with SSE2 enabled (including
x64), define `RG_TEXT_GPU_USE_SSE2=1` before including `rg_text_gpu.h` to use
the optional SIMD kernel. It needs no extra object file, runtime CPU dispatch,
or 16-byte buffer alignment; the target must support SSE2. Other architectures
use the default C implementation. Source quads must not overlap the renderer
or its output buffers. Both paths preserve the same vertex/index layout.
See [the packing benchmark](quad_packing.md) for measured costs and how to
repeat the C, intrinsics, and assembly comparison.

Windows x64 can instead define `RG_TEXT_GPU_USE_ASM=1` and link the object
assembled from `src/asm/rg_text_gpu_pack_quads_x64.asm` with MASM (`ml64`).
The handwritten batch kernel uses baseline SSE2 and requires no CPU dispatch.
Do not enable both options. `build.bat` assembles and links the object for
application, GPU compile, and device targets when the `RG_TEXT_GPU_USE_ASM`
environment variable is `1`. Dedicated packing tests exercise their named path;
the benchmark compares all supported paths. The portable C default supports
other architectures.

Atlas input pixels use straight alpha and are copied during upload. The GPU
copy stores premultiplied RGB so interpolation across transparent padding
preserves edge brightness. The supplied fragment shader also multiplies tint
RGB by tint alpha; blending uses `ONE, ONE_MINUS_SRC_ALPHA` for both color and
alpha. Custom fragment shaders must output premultiplied color and account
for tint opacity the same way. Recompile the shaders when updating from the
old straight-alpha GPU path. The on-disk atlas format does not change.

## Bake tool

```bat
rg_text_bake.exe [--no-kerning] input.ttf output_base [pixel_size] [first] [last] [padding] [atlas_width]
rg_text_bake.exe [--no-kerning] input.ttf output_base [pixel_size] [range_list] [padding] [atlas_width]
```

The tool writes `output_base.rgba` and `output_base.font`. It uses FreeType for
normally hinted grayscale rasterization and glyph metrics, and HarfBuzz for
OpenType kerning selection. These are dependencies of the optional baker
executable only; the runtime library and generated files do not include or link
either library.

The baker composites white foreground coverage over its built-in dark shadow
using source-over alpha, then stores the result as straight-alpha RGBA8.

For every candidate codepoint pair, the baker compares HarfBuzz output with the
active `kern` feature enabled and disabled. This respects the font's OpenType
script, language-system, feature, and lookup structure and supports both modern
GPOS positioning and legacy `kern` data through HarfBuzz. Common substitution
and contextual features are disabled, and a pair is emitted only when it stays
as the two nominal glyphs and its effect can be represented exactly by RGFONT's
single horizontal pair adjustment.

Pairs are shaped as isolated, non-boundary text. Contextual or chained
positioning that needs surrounding glyphs is therefore omitted, as are
language-specific effects outside the baker's `und` language selection. The
basic RGFONT format cannot represent those effects at runtime.

This does not turn the runtime into a general shaper. Ligatures, contextual
substitutions, per-pair placement, bidirectional layout, and complex-script
shaping remain outside the basic left-to-right runtime.

`range_list` is comma-separated and accepts decimal, hexadecimal, or `U+`
codepoints, for example `32-126,1024-1279`. Overlapping ranges are deduplicated,
and surrogate codepoints are skipped. A bake is capped at 2,048 unique Unicode
scalar values and 65,536 nonzero output pairs. Because pair extraction is
quadratic, kerning-enabled bakes are additionally capped at 512 supported
glyphs. Pass `--no-kerning` to bake a larger selection without pair records or
HarfBuzz shaping.

The two outputs are written completely to unique files in the destination
directory before replacement. The atlas is atomically replaced first and the
`.font` file last as the commit marker; ordinary second-replacement failures
roll the atlas back. The pair is not crash-atomic because it consists of two
files, so readers should not open it concurrently with a bake.

Only one process may bake a given output base at a time. The baker holds a
per-base operating-system lock and rejects a concurrent writer before staging
any output.
