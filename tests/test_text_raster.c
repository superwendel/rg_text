// rg_text_raster tests (both backends: build with -DRG_TEXT_RASTER_BACKEND=1 or 2):
// glyph bitmaps for shaped Segoe UI text at 14 px, bearings, packing without
// overlap, lookup hits, dirty tracking, reset and generation, a full tiny atlas,
// and a 10k-glyph timing. With --sheet <out.bmp> it also writes a sample sheet
// of a mixed-script line at 14 / 17.5 / 21 / 28 px for the quality gate.
//
// Usage:
//   test_text_raster.exe [--sheet out.bmp]

#define RGINLINE static inline
#include "rg_defs.h"
#include "rg_mem.h"
#include "rg_time.h"
#include "rg_text_shape.h"
#include "rg_text_raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg)                               \
	do {                                                     \
		if (!(cond))                                         \
		{                                                    \
			printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
			g_tests_failed++;                                \
			return;                                          \
		}                                                    \
	} while (0)

#define TEST_PASS()       \
	do {                  \
		g_tests_passed++; \
	} while (0)

static RgArena arena, scratch, raster_scratch;
static RgTextShapeContext shaper;
static RgTextRaster raster;
static RgTextFace shape_faces[4];
static RgTextRasterFace raster_faces[4];
static RgTextRasterFace* face_ptrs[4];
static RgTextFontSet set;
static RgTextAtlas atlas;

static void* read_file(const char* path, size_t* size)
{
	FILE* f = fopen(path, "rb");
	void* data;
	long n;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = rg_arena_alloc_aligned(&arena, (size_t)n, 16);
	if (!data || fread(data, 1, (size_t)n, f) != (size_t)n)
	{
		fclose(f);
		return NULL;
	}
	fclose(f);
	*size = (size_t)n;
	return data;
}

static int load(u32 i, const char* path, int index)
{
	size_t size = 0;
	void* data = read_file(path, &size);
	if (!data) return 0;
	if (!rg_text_face_load(&shape_faces[i], &arena, data, size, index)) return 0;
	if (!rg_text_raster_face_load(&raster, &raster_faces[i], data, size, index)) return 0;
	face_ptrs[i] = &raster_faces[i];
	return 1;
}

static void test_glyphs_and_bearings(void)
{
	RgTextLayout l;
	u32 i;
	u32 x0, y0, x1, y1;
	scratch.used = 0;
	TEST_ASSERT(rg_text_shape_utf8(&shaper, &set, "Hello", 5, 14.0f, &scratch, &l), "shape Hello");
	for (i = 0; i < l.count; i++)
	{
		RgTextGlyphKey key = rg_text_glyph_key(l.glyphs[i].face, l.glyphs[i].glyph, 14.0f, l.glyphs[i].x);
		const RgTextAtlasEntry* e = rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, key);
		TEST_ASSERT(e && e->present, "glyph rasterized");
		TEST_ASSERT(e->w > 0 && e->h > 0 && e->w < 20 && e->h < 20, "plausible 14 px glyph size");
		TEST_ASSERT(e->top > 5 && e->top <= 12, "top bearing within the ascent");
	}
	/* H is a tall glyph: about 10 px high at 14 px, top near the cap height. */
	{
		const RgTextAtlasEntry* e = rg_text_atlas_lookup(&atlas, rg_text_glyph_key(0, l.glyphs[0].glyph, 14.0f, 0.0f));
		TEST_ASSERT(e && e->h >= 9 && e->h <= 11 && e->top >= 9 && e->top <= 11, "H is ~10 px tall with its top at the cap height");
		/* Ink exists: the bitmap is not blank and not fully opaque. */
		{
			u32 sum = 0, px = 0, x, y;
			for (y = 0; y < e->h; y++)
				for (x = 0; x < e->w; x++)
				{
					sum += atlas.pixels[(e->y + y) * atlas.width + e->x + x];
					px++;
				}
			TEST_ASSERT(sum > px * 40u && sum < px * 230u, "H coverage is partial (stems and gaps)");
		}
	}
	/* Five glyphs; the two l share a bitmap when their pen fractions round to the same quarter. */
	TEST_ASSERT(atlas.count >= 4 && atlas.count <= 5 && atlas.rasterized == atlas.count, "one bitmap per distinct key");
	TEST_ASSERT(rg_text_atlas_take_dirty(&atlas, &x0, &y0, &x1, &y1) && x1 > x0 && y1 > y0 && x1 <= atlas.width, "dirty rectangle covers the new glyphs");
	TEST_ASSERT(!rg_text_atlas_take_dirty(&atlas, &x0, &y0, &x1, &y1), "clean after take");
	TEST_PASS();
}

static void test_space_and_subpixel(void)
{
	RgTextLayout l;
	scratch.used = 0;
	TEST_ASSERT(rg_text_shape_utf8(&shaper, &set, "a b", 3, 14.0f, &scratch, &l), "shape a b");
	{
		const RgTextAtlasEntry* sp = rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, rg_text_glyph_key(0, l.glyphs[1].glyph, 14.0f, 0.0f));
		TEST_ASSERT(sp && sp->present && sp->w == 0 && sp->h == 0, "space has no ink but is present");
	}
	/* Four subpixel variants of the same glyph are four entries. */
	{
		u32 before = atlas.count, q;
		for (q = 0; q < 4; q++)
		{
			const RgTextAtlasEntry* e = rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, rg_text_glyph_key(0, l.glyphs[0].glyph, 14.0f, (f32)q * 0.25f));
			TEST_ASSERT(e && e->present, "subpixel variant");
		}
		TEST_ASSERT(atlas.count >= before + 3, "distinct subpixel entries");
		TEST_ASSERT(rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, rg_text_glyph_key(0, l.glyphs[0].glyph, 14.0f, 0.3f)) == rg_text_atlas_lookup(&atlas, rg_text_glyph_key(0, l.glyphs[0].glyph, 14.0f, 0.3f)), "hit returns the stored entry");
	}
	TEST_PASS();
}

static void test_packing_and_reset(void)
{
	/* 1500 distinct keys: glyph ids 1..375 at 14 px x 4 subpixels, into a map
	 * that must show no overlaps. */
	u8* occupancy;
	u32 g, q, placed = 0, overlaps = 0;
	u32 gen = atlas.generation;
	occupancy = (u8*)rg_arena_alloc_aligned(&arena, (size_t)atlas.width * atlas.height, 16);
	memset(occupancy, 0, (size_t)atlas.width * atlas.height);
	rg_text_atlas_reset(&atlas);
	TEST_ASSERT(atlas.generation == gen + 1 && atlas.count == 0, "reset bumps the generation and empties the table");
	for (g = 1; g <= 375; g++)
		for (q = 0; q < 4; q++)
		{
			RgTextGlyphKey key;
			const RgTextAtlasEntry* e;
			key.face = 0;
			key.glyph = (u16)g;
			key.size_q6 = 14 * 64;
			key.subpixel = (u8)q;
			key.flags = 0;
			e = rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, key);
			TEST_ASSERT(e != NULL, "packed (2048^2 is far from full)");
			if (e->w)
			{
				u32 x, y;
				placed++;
				for (y = 0; y < e->h; y++)
					for (x = 0; x < e->w; x++)
					{
						u8* cell = &occupancy[(e->y + y) * atlas.width + e->x + x];
						if (*cell) overlaps++;
						*cell = 1;
					}
			}
		}
	TEST_ASSERT(overlaps == 0, "no two glyph rectangles overlap");
	TEST_ASSERT(placed > 1000 && atlas.count == 1500, "most glyphs have ink; all keys stored");
	printf("  packed %u glyph bitmaps; shelf cursor y %u of %u\n", placed, atlas.shelf_y + atlas.shelf_h, atlas.height);
	TEST_PASS();
}

static void test_full_atlas(void)
{
	RgTextAtlas tiny;
	u32 g, got = 0;
	TEST_ASSERT(rg_text_atlas_init(&tiny, &arena, 48, 48, 64), "tiny atlas");
	for (g = 1; g <= 60; g++)
	{
		RgTextGlyphKey key;
		key.face = 0;
		key.glyph = (u16)(g + 30);
		key.size_q6 = 14 * 64;
		key.subpixel = 0;
		key.flags = 0;
		if (rg_text_atlas_get(&tiny, &raster, face_ptrs, 4, key)) got++;
		else break;
	}
	TEST_ASSERT(got > 3 && got < 60, "a 48x48 atlas fills after a few glyphs and then refuses");
	rg_text_atlas_reset(&tiny);
	TEST_ASSERT(rg_text_atlas_get(&tiny, &raster, face_ptrs, 4, rg_text_glyph_key(0, 36, 14.0f, 0.0f)) != NULL, "accepts again after reset");
	TEST_PASS();
}

static void test_timing(void)
{
	u64 t0;
	u32 i, n = 0;
	f64 ms;
	rg_text_atlas_reset(&atlas);
	t0 = rg_time_ticks();
	for (i = 0; i < 10000; i++)
	{
		RgTextGlyphKey key;
		key.face = (u16)(i % 3 == 2 ? 1 : 0);
		key.glyph = (u16)(1 + (i * 7919u) % 1500u);
		key.size_q6 = (u16)((i & 1) ? 14 * 64 : 21 * 64);
		key.subpixel = (u8)(i & 3);
		key.flags = 0;
		if (rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, key)) n++;
	}
	ms = rg_time_ticks_to_ms(rg_time_ticks() - t0);
	printf("  %u glyph requests, %u rasterized, %.1f ms (%.1f us per rasterization)\n", 10000u, atlas.rasterized, ms,
	       atlas.rasterized ? ms * 1000.0 / atlas.rasterized : 0.0);
	TEST_ASSERT(n == 10000, "every request served");
	TEST_PASS();
}

/* --------------------------------------------------------------- sheet */

static void write_sheet(const char* path)
{
	static const char* lines[] = {"The quick brown fox jumps over the lazy dog 0123456789", "résumé Архив 日本語 نمونه नमस्ते emoji_🙂 Ünïcödé.txt"};
	static const f32 sizes[] = {14.0f, 17.5f, 21.0f, 28.0f};
	u32 W = 1100, H = 260, x, y, s, li;
	u8* img = (u8*)rg_arena_alloc_aligned(&arena, (size_t)W * H, 16);
	FILE* f;
	u32 pen_y = 8;
	memset(img, 0x17, (size_t)W * H); /* near the app's canvas */
	rg_text_atlas_reset(&atlas);
	for (s = 0; s < 4; s++)
		for (li = 0; li < 2; li++)
		{
			RgTextLayout l;
			u32 i;
			f32 line_h = rg_text_face_line_height(&shape_faces[0], sizes[s]);
			f32 baseline = (f32)pen_y + rg_text_face_ascent(&shape_faces[0], sizes[s]);
			scratch.used = 0;
			if (!rg_text_shape_utf8(&shaper, &set, lines[li], strlen(lines[li]), sizes[s], &scratch, &l)) continue;
			for (i = 0; i < l.count; i++)
			{
				f32 px = 8.0f + l.glyphs[i].x;
				RgTextGlyphKey key = rg_text_glyph_key(l.glyphs[i].face, l.glyphs[i].glyph, sizes[s], px);
				const RgTextAtlasEntry* e = rg_text_atlas_get(&atlas, &raster, face_ptrs, 4, key);
				i32 ox, oy, gx, gy;
				if (!e || !e->w) continue;
				ox = (i32)floorf(px) + e->left;
				oy = (i32)(baseline + 0.5f) - e->top - (i32)l.glyphs[i].y;
				for (gy = 0; gy < e->h; gy++)
					for (gx = 0; gx < e->w; gx++)
					{
						i32 X = ox + gx, Y = oy + gy;
						u8 c = atlas.pixels[(e->y + gy) * atlas.width + e->x + gx];
						if (X >= 0 && Y >= 0 && (u32)X < W && (u32)Y < H)
						{
							/* straight alpha over the canvas towards near-white text */
							u8* d = &img[(u32)Y * W + (u32)X];
							*d = (u8)((*d * (255 - c) + 0xE7 * c) / 255);
						}
					}
			}
			pen_y += (u32)(line_h + 2.0f);
		}
	/* 24-bit BMP, bottom-up. */
	f = fopen(path, "wb");
	if (!f) return;
	{
		u32 row = (W * 3 + 3) & ~3u, size = 54 + row * H;
		u8 hdr[54] = {'B', 'M'};
		hdr[2] = (u8)size; hdr[3] = (u8)(size >> 8); hdr[4] = (u8)(size >> 16); hdr[5] = (u8)(size >> 24);
		hdr[10] = 54; hdr[14] = 40;
		hdr[18] = (u8)W; hdr[19] = (u8)(W >> 8); hdr[22] = (u8)H; hdr[23] = (u8)(H >> 8);
		hdr[26] = 1; hdr[28] = 24;
		fwrite(hdr, 1, 54, f);
		for (y = H; y-- > 0;)
		{
			u8 pad[4] = {0};
			for (x = 0; x < W; x++)
			{
				u8 v = img[y * W + x];
				u8 bgr[3] = {v, v, v};
				fwrite(bgr, 1, 3, f);
			}
			fwrite(pad, 1, row - W * 3, f);
		}
		fclose(f);
	}
	printf("  sheet written to %s (%s)\n", path, RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB ? "stb_truetype" : "FreeType light");
}

int main(int argc, char** argv)
{
	rg_time_init();
	if (rg_malloc(MB(320)) != 0) return 1;
	arena = rg_arena_create(MB(256));
	scratch = rg_arena_create(MB(8));
	raster_scratch = rg_arena_create(MB(16));
	printf("rg_text_raster tests (%s)\n", RG_TEXT_RASTER_BACKEND == RG_TEXT_RASTER_STB ? "stb_truetype" : "FreeType");
	if (!rg_text_raster_init(&raster, &raster_scratch)) return 1;
	if (!load(0, "C:\\Windows\\Fonts\\segoeui.ttf", 0) || !load(1, "C:\\Windows\\Fonts\\YuGothR.ttc", 0) || !load(2, "C:\\Windows\\Fonts\\Nirmala.ttc", 0) ||
	    !load(3, "C:\\Windows\\Fonts\\seguiemj.ttf", 0))
	{
		printf("  system fonts not found: raster tests skipped\n");
		return 0;
	}
	memset(&set, 0, sizeof set);
	rg_text_font_set_add(&set, &shape_faces[0]);
	rg_text_font_set_add(&set, &shape_faces[1]);
	rg_text_font_set_add(&set, &shape_faces[2]);
	rg_text_font_set_add(&set, &shape_faces[3]);
	if (!rg_text_shape_context_init(&shaper, &arena, 0)) return 1;
	if (!rg_text_atlas_init(&atlas, &arena, 2048, 2048, 16384)) return 1;
	test_glyphs_and_bearings();
	test_space_and_subpixel();
	test_packing_and_reset();
	test_full_atlas();
	test_timing();
	if (argc >= 3 && strcmp(argv[1], "--sheet") == 0) write_sheet(argv[2]);
	printf("%d passed, %d failed\n", g_tests_passed, g_tests_failed);
	rg_free();
	return g_tests_failed ? 1 : 0;
}
