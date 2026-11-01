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
allocating, then builds small indexes inside the font: a direct table for the
128 ASCII codepoints, the fallback glyph's index, and each glyph's range of
kerning pairs (`kerning_first` and `kerning_count` in `RgTextGlyph`). Layout
then resolves an ASCII glyph with one table load, any other glyph with a
binary search, and a kerning pair by searching only the left glyph's pairs.
Duplicate glyph codepoints and duplicate kerning pairs are rejected. Array
contents and order may change even if loading fails; use a font only after a
successful load. The arrays must outlive the font and remain unchanged while
it is in use. The source RGFONT bytes can be released after loading.

Manually assembled fonts remain supported: zero-initialize `RgTextFont`, fill
its arrays and counts, and leave its internal lookup flags at zero. Those
fonts use linear lookup and may have unsorted arrays. Do not copy a loaded
font and replace or reorder its arrays without clearing its internal lookup
flags or reloading it. Changing `fallback_codepoint` after loading is allowed.

Fallback glyphs use the kerning pairs for the glyphs actually displayed.
Left-aligned quad generation stops when the output capacity is reached.
Center and right alignment resolve each glyph once: a line's quads are emitted
relative to the pen and placed when the line ends. When `align_width` is not
positive, the widest line sets the alignment width, so every line is placed
after the whole text has been scanned, and scanning continues past the output
capacity to find that width. A build call returns the number of quads written
and may truncate to the provided capacity; output is identical to measuring
each line first.

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

## Shaping (`rg_text_shape.h`)

Opt-in and independent of the bitmap path: include it once in the unity
translation unit after `rg_mem.h`. It wraps kb_text_shape (vendored under
`third_party/kb`, zlib) with internal linkage and arena memory.

```c
RgTextFace ui, cjk;                       // faces parse from caller-held font bytes
rg_text_face_load(&ui, &arena, segoe_bytes, segoe_size, 0);
rg_text_face_load(&cjk, &arena, yugothic_bytes, yugothic_size, 0);
RgTextFontSet set = {0};
rg_text_font_set_add(&set, &ui);          // primary first, fallbacks after
rg_text_font_set_add(&set, &cjk);

RgTextShapeContext shaper;                // one per thread, one font set at a time
rg_text_shape_context_init(&shaper, &shaper_arena, 0);

RgTextLayout layout;                      // glyphs in visual order, pixels at px_size
rg_text_shape_utf8(&shaper, &set, utf8, len, 14.0f, &frame_arena, &layout);
```

Each `RgTextShapedGlyph` carries the glyph id, the face index it came from,
pen-relative x/y and advance in pixels, whether its run is right-to-left, and
`cluster`, the byte offset of its source codepoint, which drives
`rg_text_layout_caret_x`, `rg_text_layout_hit_test` and `rg_text_layout_fit`.
`notdef_count` tells the caller when no face in the set covered a codepoint.

The context's allocations come from its arena and are never freed one by one;
call `rg_text_shape_context_reset` between batches (a reset costs about 60 us,
so not per line). Switching the bound font set also resets. The arena given
to the context must hold nothing else after init. Fonts must be trusted.

`tests/test_text_shape.c` checks glyph ids against DirectWrite dumps for
Latin (Calibri ligatures), Arabic joining, Devanagari conjuncts, Thai, CJK and
emoji fallback, and times 10,000 file-name-like strings (about 6 us each).

## Rasterization (`rg_text_raster.h`)

Turns (face, glyph id, pixel size, subpixel quarter) into coverage bitmaps in
one 8-bit atlas. Choose the backend before the include:

```c
#define RG_TEXT_RASTER_BACKEND RG_TEXT_RASTER_FREETYPE   // or RG_TEXT_RASTER_STB (default, no link step)
#include "rg_text_raster.h"

RgTextRaster raster;       rg_text_raster_init(&raster, &scratch_arena);
RgTextRasterFace face;     rg_text_raster_face_load(&raster, &face, bytes, size, 0);
RgTextAtlas atlas;         rg_text_atlas_init(&atlas, &arena, 2048, 2048, 16384);
RgTextGlyphKey key = rg_text_glyph_key(face_index, glyph_id, 14.0f, pen_x);
const RgTextAtlasEntry* e = rg_text_atlas_get(&atlas, &raster, faces, face_count, key);
// blit e->w x e->h from (e->x, e->y) at (floor(pen_x) + e->left, baseline - e->top)
```

Shelf packing, no per-glyph eviction: when `rg_text_atlas_get` returns NULL
the owner calls `rg_text_atlas_reset` (generation bumps; caches keyed on it
drop) and the next frame repopulates. `rg_text_atlas_take_dirty` hands the
renderer the rectangle to upload. GUI thread only; fonts must be trusted.

Measured on Segoe UI at 14 px: stb 3.2 us per glyph, FreeType light 7.2 us.
Against a DirectWrite natural-grayscale rendering of the same lines, total
ink is within 2.4% (FreeType) and 3.4% (stb); FreeType's vertical hinting
keeps stems on the pixel grid, stb is visibly softer, so the explorer uses
FreeType and keeps stb as the dependency-free fallback.
