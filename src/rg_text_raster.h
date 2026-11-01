// rg_text_raster - glyph rasterization into an R8 shelf-packed atlas
//
// Part of the Reverse Gravity (rg_) libraries. Opt-in companion to
// rg_text_shape.h: shaping produces (face, glyph id, size) requests; this
// header turns them into coverage bitmaps packed into one 8-bit atlas that a
// renderer uploads as a single texture. rg_text.h's baked bitmap path is not
// touched.
//
// Backends (pick one before including):
//   #define RG_TEXT_RASTER_BACKEND RG_TEXT_RASTER_STB       // vendored stb_truetype, no link step (default)
//   #define RG_TEXT_RASTER_BACKEND RG_TEXT_RASTER_FREETYPE  // FreeType 2 (FT_LOAD_TARGET_LIGHT), link freetype
// Both produce the same RgTextAtlasEntry contract; quality differs and is the
// subject of the 14 px gate in the RG Explorer plan.
//
// Model
//   - RgTextGlyphKey {face, glyph, size in 1/64 px, subpixel quarter} identifies
//     a bitmap. The atlas is a hash table from key to RgTextAtlasEntry (x, y,
//     w, h in the atlas; left/top bearings in pixels).
//   - Shelf packing, no per-glyph eviction. When the atlas is full,
//     rg_text_atlas_get returns NULL; the owner resets the atlas (generation
//     bumps so cached quads referencing old UVs are invalidated) and the next
//     frame repopulates. 2048x2048 holds tens of thousands of UI glyphs.
//   - A dirty rectangle accumulates for the renderer's partial upload.
//   - GUI thread only. Fonts must be trusted (both backends say so).
//
// USAGE:
//   #include "rg_text_raster.h"      // once, in the unity TU, after rg_mem.h
//   RgTextRaster raster;  rg_text_raster_init(&raster, &scratch_arena);
//   RgTextRasterFace face; rg_text_raster_face_load(&raster, &face, bytes, size, 0);
//   RgTextAtlas atlas;    rg_text_atlas_init(&atlas, &arena, 2048, 2048, 16384);
//   RgTextGlyphKey key = rg_text_glyph_key(0, glyph_id, 14.0f, pen_x);
//   const RgTextAtlasEntry* e = rg_text_atlas_get(&atlas, &raster, faces, key);
//   // draw e->w x e->h from atlas (e->x, e->y) at (floor(pen_x) + e->left, baseline - e->top)
//
// Author: Steven Wendel (superwendel)

#ifndef RG_TEXT_RASTER_H
#define RG_TEXT_RASTER_H

#include "rg_defs.h"
#include "rg_mem.h"

#include <math.h>
#include <string.h>

#define RG_TEXT_RASTER_STB 1
#define RG_TEXT_RASTER_FREETYPE 2
#ifndef RG_TEXT_RASTER_BACKEND
#define RG_TEXT_RASTER_BACKEND RG_TEXT_RASTER_STB
#endif

#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x, u) rg_text_raster_stb_alloc((x), (u))
#define STBTT_free(x, u) rg_text_raster_stb_free((x), (u))
#define STBTT_assert(x) ((void)0)
static void* rg_text_raster_stb_alloc(size_t size, void* user);
static void rg_text_raster_stb_free(void* ptr, void* user);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4127 4244 4245 4267 4456 4457 4459 4701 4702 4706 4505 4310 4296 4996 4189)
#endif
#include "../third_party/stb/stb_truetype.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#else
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#endif

// =============================================================================
// TYPES
// =============================================================================

typedef struct RgTextRaster
{
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_FREETYPE
	FT_Library library;
#endif
	RgArena* scratch; // per-glyph bitmap staging; rewound after each rasterization
	int valid;
} RgTextRaster;

typedef struct RgTextRasterFace
{
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB
	stbtt_fontinfo info;
#else
	FT_Face face;
	u32 size_q6; // last FT_Set_Char_Size applied, to skip redundant calls
#endif
	const void* data;
	size_t size;
	int valid;
} RgTextRasterFace;

typedef struct RgTextGlyphKey
{
	u16 face;     // index into the caller's face array
	u16 glyph;    // glyph id
	u16 size_q6;  // pixel size * 64 (14.0 px -> 896)
	u8 subpixel;  // 0..3: horizontal quarter-pixel offset
	u8 flags;     // reserved (0)
} RgTextGlyphKey;

typedef struct RgTextAtlasEntry
{
	u16 x, y, w, h; // atlas rectangle; w == 0 for glyphs with no ink (spaces)
	i16 left;       // pixels from the (floored) pen x to the bitmap's left edge
	i16 top;        // pixels from the baseline up to the bitmap's top edge
	u8 present;
} RgTextAtlasEntry;

typedef struct RgTextAtlas
{
	u8* pixels;
	u32 width, height;
	u32 shelf_y, shelf_h, cursor_x; // shelf packer
	u64* keys;                       // open addressing; 0 = empty slot
	RgTextAtlasEntry* entries;
	u32 capacity;                    // power of two
	u32 count;
	u32 generation;                  // bumps on reset; renderers key their caches on it
	u32 dirty_x0, dirty_y0, dirty_x1, dirty_y1; // exclusive max; x0 > x1 means clean
	u32 rasterized;                  // misses since the last reset (diagnostics)
	u32 padding;                     // pixels kept between glyphs (1)
} RgTextAtlas;

// =============================================================================
// API
// =============================================================================

RGINLINE int rg_text_raster_init(RgTextRaster* raster, RgArena* scratch);
RGINLINE void rg_text_raster_destroy(RgTextRaster* raster);
RGINLINE int rg_text_raster_face_load(RgTextRaster* raster, RgTextRasterFace* face, const void* data, size_t size, int index);
RGINLINE void rg_text_raster_face_destroy(RgTextRasterFace* face);

/** Key for a glyph at `px_size` whose pen x is `pen_x` (the fractional part
 *  picks the subpixel variant). */
RGINLINE RgTextGlyphKey rg_text_glyph_key(u16 face, u16 glyph, f32 px_size, f32 pen_x);

RGINLINE int rg_text_atlas_init(RgTextAtlas* atlas, RgArena* arena, u32 width, u32 height, u32 capacity);
/** Forget every glyph, clear the pixels, bump the generation. */
RGINLINE void rg_text_atlas_reset(RgTextAtlas* atlas);
RGINLINE const RgTextAtlasEntry* rg_text_atlas_lookup(const RgTextAtlas* atlas, RgTextGlyphKey key);
/** Lookup, rasterizing and packing on a miss. NULL when the atlas or its table
 *  is full (reset and retry), or the face is invalid. */
RGINLINE const RgTextAtlasEntry* rg_text_atlas_get(RgTextAtlas* atlas, RgTextRaster* raster, RgTextRasterFace* const* faces, u32 face_count,
                                                   RgTextGlyphKey key);
/** The rectangle changed since the last call (exclusive max), and clear it. 0 when clean. */
RGINLINE int rg_text_atlas_take_dirty(RgTextAtlas* atlas, u32* x0, u32* y0, u32* x1, u32* y1);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB
static RgArena* rg_text_raster_stb_arena; /* set around stb calls; GUI thread only */
static void* rg_text_raster_stb_alloc(size_t size, void* user)
{
	(void)user;
	return rg_text_raster_stb_arena ? rg_arena_alloc_aligned(rg_text_raster_stb_arena, size, 16) : NULL;
}
static void rg_text_raster_stb_free(void* ptr, void* user)
{
	(void)ptr;
	(void)user; /* the scratch arena is rewound after each glyph */
}
#endif

RGINLINE int rg_text_raster_init(RgTextRaster* raster, RgArena* scratch)
{
	memset(raster, 0, sizeof *raster);
	raster->scratch = scratch;
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_FREETYPE
	if (FT_Init_FreeType(&raster->library)) return 0;
#endif
	raster->valid = 1;
	return 1;
}

RGINLINE void rg_text_raster_destroy(RgTextRaster* raster)
{
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_FREETYPE
	if (raster->library) FT_Done_FreeType(raster->library);
#endif
	memset(raster, 0, sizeof *raster);
}

RGINLINE int rg_text_raster_face_load(RgTextRaster* raster, RgTextRasterFace* face, const void* data, size_t size, int index)
{
	memset(face, 0, sizeof *face);
	face->data = data;
	face->size = size;
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB
	{
		int offset;
		(void)raster;
		offset = stbtt_GetFontOffsetForIndex((const unsigned char*)data, index);
		if (offset < 0) return 0;
		rg_text_raster_stb_arena = raster->scratch;
		if (!stbtt_InitFont(&face->info, (const unsigned char*)data, offset)) return 0;
	}
#else
	if (FT_New_Memory_Face(raster->library, (const FT_Byte*)data, (FT_Long)size, index, &face->face)) return 0;
#endif
	face->valid = 1;
	return 1;
}

RGINLINE void rg_text_raster_face_destroy(RgTextRasterFace* face)
{
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_FREETYPE
	if (face->face) FT_Done_Face(face->face);
#endif
	memset(face, 0, sizeof *face);
}

RGINLINE RgTextGlyphKey rg_text_glyph_key(u16 face, u16 glyph, f32 px_size, f32 pen_x)
{
	RgTextGlyphKey key;
	f32 frac = pen_x - floorf(pen_x);
	key.face = face;
	key.glyph = glyph;
	key.size_q6 = (u16)(px_size * 64.0f + 0.5f);
	key.subpixel = (u8)((int)(frac * 4.0f) & 3);
	key.flags = 0;
	return key;
}

RGINLINE u64 rg_text_glyph_key_bits(RgTextGlyphKey key)
{
	return ((u64)key.face << 48) | ((u64)key.glyph << 32) | ((u64)key.size_q6 << 16) | ((u64)key.subpixel << 8) | (u64)key.flags | (1ull << 63);
}

RGINLINE int rg_text_atlas_init(RgTextAtlas* atlas, RgArena* arena, u32 width, u32 height, u32 capacity)
{
	u32 cap = 1;
	memset(atlas, 0, sizeof *atlas);
	while (cap < capacity * 2u) cap <<= 1; /* load factor <= 0.5 */
	atlas->pixels = (u8*)rg_arena_alloc_aligned(arena, (size_t)width * height, 64);
	atlas->keys = (u64*)rg_arena_alloc_aligned(arena, (size_t)cap * sizeof(u64), 16);
	atlas->entries = (RgTextAtlasEntry*)rg_arena_alloc_aligned(arena, (size_t)cap * sizeof(RgTextAtlasEntry), 16);
	if (!atlas->pixels || !atlas->keys || !atlas->entries) return 0;
	atlas->width = width;
	atlas->height = height;
	atlas->capacity = cap;
	atlas->padding = 1;
	atlas->generation = 1;
	memset(atlas->pixels, 0, (size_t)width * height);
	memset(atlas->keys, 0, (size_t)cap * sizeof(u64));
	atlas->dirty_x0 = 1;
	atlas->dirty_x1 = 0;
	return 1;
}

RGINLINE void rg_text_atlas_reset(RgTextAtlas* atlas)
{
	memset(atlas->pixels, 0, (size_t)atlas->width * atlas->height);
	memset(atlas->keys, 0, (size_t)atlas->capacity * sizeof(u64));
	atlas->count = 0;
	atlas->shelf_y = 0;
	atlas->shelf_h = 0;
	atlas->cursor_x = 0;
	atlas->rasterized = 0;
	atlas->generation++;
	/* Everything is dirty: the renderer re-uploads the whole texture. */
	atlas->dirty_x0 = 0;
	atlas->dirty_y0 = 0;
	atlas->dirty_x1 = atlas->width;
	atlas->dirty_y1 = atlas->height;
}

RGINLINE u32 rg_text_atlas_slot(const RgTextAtlas* atlas, u64 bits, int* found)
{
	u64 h = bits * 0x9E3779B97F4A7C15ull;
	u32 mask = atlas->capacity - 1u;
	u32 i = (u32)(h >> 40) & mask;
	for (;;)
	{
		if (atlas->keys[i] == bits)
		{
			*found = 1;
			return i;
		}
		if (atlas->keys[i] == 0)
		{
			*found = 0;
			return i;
		}
		i = (i + 1u) & mask;
	}
}

RGINLINE const RgTextAtlasEntry* rg_text_atlas_lookup(const RgTextAtlas* atlas, RgTextGlyphKey key)
{
	int found;
	u32 i = rg_text_atlas_slot(atlas, rg_text_glyph_key_bits(key), &found);
	return found ? &atlas->entries[i] : NULL;
}

/* Reserve w x h in the shelf packer; 0 when it does not fit. */
RGINLINE int rg_text_atlas_pack(RgTextAtlas* atlas, u32 w, u32 h, u32* out_x, u32* out_y)
{
	u32 pw = w + atlas->padding, ph = h + atlas->padding;
	if (w == 0 || h == 0)
	{
		*out_x = 0;
		*out_y = 0;
		return 1;
	}
	if (pw > atlas->width || ph > atlas->height) return 0;
	if (atlas->cursor_x + pw > atlas->width || ph > atlas->shelf_h)
	{
		/* Open a new shelf when the glyph does not fit the current one (taller
		 * glyphs than the shelf also open a new one: shelves grow, never shrink). */
		if (atlas->cursor_x + pw > atlas->width || ph > atlas->shelf_h)
		{
			if (atlas->shelf_h && atlas->cursor_x + pw <= atlas->width && ph <= atlas->shelf_h)
			{
			}
			else
			{
				u32 next_y = atlas->shelf_y + atlas->shelf_h;
				if (atlas->shelf_h == 0) next_y = 0;
				if (next_y + ph > atlas->height) return 0;
				atlas->shelf_y = next_y;
				atlas->shelf_h = ph;
				atlas->cursor_x = 0;
			}
		}
	}
	*out_x = atlas->cursor_x;
	*out_y = atlas->shelf_y;
	atlas->cursor_x += pw;
	return 1;
}

RGINLINE void rg_text_atlas_mark_dirty(RgTextAtlas* atlas, u32 x, u32 y, u32 w, u32 h)
{
	if (atlas->dirty_x0 > atlas->dirty_x1)
	{
		atlas->dirty_x0 = x;
		atlas->dirty_y0 = y;
		atlas->dirty_x1 = x + w;
		atlas->dirty_y1 = y + h;
		return;
	}
	if (x < atlas->dirty_x0) atlas->dirty_x0 = x;
	if (y < atlas->dirty_y0) atlas->dirty_y0 = y;
	if (x + w > atlas->dirty_x1) atlas->dirty_x1 = x + w;
	if (y + h > atlas->dirty_y1) atlas->dirty_y1 = y + h;
}

RGINLINE int rg_text_atlas_take_dirty(RgTextAtlas* atlas, u32* x0, u32* y0, u32* x1, u32* y1)
{
	if (atlas->dirty_x0 > atlas->dirty_x1) return 0;
	*x0 = atlas->dirty_x0;
	*y0 = atlas->dirty_y0;
	*x1 = atlas->dirty_x1;
	*y1 = atlas->dirty_y1;
	atlas->dirty_x0 = 1;
	atlas->dirty_x1 = 0;
	return 1;
}

/* Rasterize one glyph into the scratch arena; returns the bitmap and its
 * bearings. w == 0 for empty glyphs. */
RGINLINE const u8* rg_text_raster_glyph(RgTextRaster* raster, RgTextRasterFace* face, RgTextGlyphKey key, u32* w, u32* h, i32* left, i32* top,
                                        u32* stride)
{
	f32 px = (f32)key.size_q6 / 64.0f;
	f32 shift = (f32)key.subpixel * 0.25f;
	*w = *h = 0;
	*left = *top = 0;
	*stride = 0;
#if RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB
	{
		f32 scale = stbtt_ScaleForPixelHeight(&face->info, px);
		int x0, y0, x1, y1;
		u8* bitmap;
		/* ScaleForPixelHeight maps ascent-descent to px; UI fonts are sized by
		 * em, so use the em scale instead for parity with FreeType/DirectWrite. */
		scale = stbtt_ScaleForMappingEmToPixels(&face->info, px);
		rg_text_raster_stb_arena = raster->scratch;
		stbtt_GetGlyphBitmapBoxSubpixel(&face->info, key.glyph, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
		if (x1 <= x0 || y1 <= y0) return NULL;
		*w = (u32)(x1 - x0);
		*h = (u32)(y1 - y0);
		*left = x0;
		*top = -y0;
		*stride = *w;
		bitmap = (u8*)rg_arena_alloc_aligned(raster->scratch, (size_t)*w * *h, 16);
		if (!bitmap) return NULL;
		stbtt_MakeGlyphBitmapSubpixel(&face->info, bitmap, (int)*w, (int)*h, (int)*w, scale, scale, shift, 0.0f, key.glyph);
		return bitmap;
	}
#else
	{
		FT_GlyphSlot slot;
		(void)raster;
		(void)px;
		(void)shift; /* FreeType takes the quarter-pixel shift in 26.6 units below */
		if (face->size_q6 != key.size_q6)
		{
			if (FT_Set_Char_Size(face->face, 0, (FT_F26Dot6)key.size_q6, 72, 72)) return NULL;
			face->size_q6 = key.size_q6;
		}
		if (FT_Load_Glyph(face->face, key.glyph, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)) return NULL;
		slot = face->face->glyph;
		if (slot->format == FT_GLYPH_FORMAT_OUTLINE && key.subpixel) FT_Outline_Translate(&slot->outline, (FT_Pos)(key.subpixel * 16), 0);
		if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL)) return NULL;
		if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) return NULL;
		*w = slot->bitmap.width;
		*h = slot->bitmap.rows;
		*left = slot->bitmap_left;
		*top = slot->bitmap_top;
		*stride = (u32)slot->bitmap.pitch;
		return slot->bitmap.buffer;
	}
#endif
}

RGINLINE const RgTextAtlasEntry* rg_text_atlas_get(RgTextAtlas* atlas, RgTextRaster* raster, RgTextRasterFace* const* faces, u32 face_count,
                                                   RgTextGlyphKey key)
{
	u64 bits = rg_text_glyph_key_bits(key);
	int found;
	u32 slot = rg_text_atlas_slot(atlas, bits, &found);
	RgTextAtlasEntry* e;
	if (found) return &atlas->entries[slot];
	if (atlas->count * 2u >= atlas->capacity) return NULL; /* table full */
	if (key.face >= face_count || !faces[key.face] || !faces[key.face]->valid) return NULL;
	{
		size_t mark = raster->scratch->used;
		u32 w, h, stride, x, y;
		i32 left, top;
		const u8* bitmap = rg_text_raster_glyph(raster, faces[key.face], key, &w, &h, &left, &top, &stride);
		if (w && h && bitmap)
		{
			if (!rg_text_atlas_pack(atlas, w, h, &x, &y))
			{
				raster->scratch->used = mark;
				return NULL; /* atlas full */
			}
			{
				u32 row;
				for (row = 0; row < h; row++) memcpy(atlas->pixels + (size_t)(y + row) * atlas->width + x, bitmap + (size_t)row * stride, w);
			}
			rg_text_atlas_mark_dirty(atlas, x, y, w, h);
		}
		else
		{
			x = y = 0;
			w = h = 0;
		}
		raster->scratch->used = mark;
		e = &atlas->entries[slot];
		e->x = (u16)x;
		e->y = (u16)y;
		e->w = (u16)w;
		e->h = (u16)h;
		e->left = (i16)left;
		e->top = (i16)top;
		e->present = 1;
		atlas->keys[slot] = bits;
		atlas->count++;
		atlas->rasterized++;
		return e;
	}
}

#endif // RG_TEXT_RASTER_H
