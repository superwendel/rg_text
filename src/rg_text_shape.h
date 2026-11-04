// rg_text_shape - Unicode text shaping over kb_text_shape
//
// Part of the Reverse Gravity (rg_) libraries. Opt-in: rg_text.h is not
// touched and keeps its bitmap path; include this header only where shaped
// text is wanted. It wraps the vendored third_party/kb/kb_text_shape.h
// (Jimmy Lefevre, zlib) with internal linkage and arena memory.
//
// What it does
//   - Loads TrueType/OpenType faces from memory into an arena (RgTextFace).
//   - Shapes UTF-8 into positioned glyph ids with font fallback across a set of
//     faces (RgTextFontSet), script and direction segmentation included.
//   - Copies the result out of kb's reused run memory into caller-owned
//     storage (RgTextLayout), in visual order, with each glyph's source byte
//     offset for caret placement and hit testing.
//
// What it does not do
//   - Rasterize: glyph ids go to rg_text_raster.h (or any rasterizer).
//   - Line breaking or wrapping: one layout is one line.
//
// SECURITY: kb provides no guarantee on malformed fonts. Load only fonts you
// trust (system fonts, bundled fonts).
//
// USAGE:
//   #include "rg_text_shape.h"           // once, in the unity TU, after rg_mem.h
//
//   RgTextFace ui, cjk;
//   rg_text_face_load(&ui, &arena, segoeui_bytes, segoeui_size, 0);
//   rg_text_face_load(&cjk, &arena, yugothic_bytes, yugothic_size, 0);
//   RgTextFontSet set = {0};
//   rg_text_font_set_add(&set, &ui);     // primary first
//   rg_text_font_set_add(&set, &cjk);    // fallbacks after
//
//   RgTextShapeContext shaper;           // one per thread
//   rg_text_shape_context_init(&shaper, &arena, MB(2));
//
//   RgTextLayout layout;
//   rg_text_shape_utf8(&shaper, &set, "fi السلام 日本", len, 14.0f, &frame_arena, &layout);
//   for (u32 i = 0; i < layout.count; i++) draw(layout.glyphs[i].face, layout.glyphs[i].glyph, layout.glyphs[i].x, ...);
//
// NOTES:
//   - All functions have internal linkage and work in unity builds.
//   - Positions are in pixels at the requested size; kb works in font units.
//   - Line metrics follow Windows: hhea unless OS/2 USE_TYPO_METRICS is set
//     (Segoe UI at 14 px: 18.6 px line height, as DirectWrite lays it out).
//   - A shape context is single-threaded; its arena-backed allocations are
//     never freed individually (kb's free ops are no-ops), so size the arena
//     for the longest line and reset the context's arena between frames with
//     rg_text_shape_context_reset if memory must not accumulate.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_TEXT_SHAPE_H
#define RG_TEXT_SHAPE_H

#include "rg_defs.h"
#include "rg_mem.h"

#include <string.h>

#define KB_TEXT_SHAPE_STATIC
#define KB_TEXT_SHAPE_IMPLEMENTATION
#define KB_TEXT_SHAPE_NO_CRT
#define KBTS_MEMSET memset
#define KBTS_MEMCPY memcpy
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4127 4189 4244 4245 4267 4456 4457 4459 4701 4702 4706 4505 4310 4296 4366)
#endif
#include "../third_party/kb/kb_text_shape.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifndef RG_TEXT_SHAPE_MAX_FACES
#define RG_TEXT_SHAPE_MAX_FACES 12
#endif

// =============================================================================
// TYPES
// =============================================================================

typedef struct RgTextFace
{
	kbts_font font;
	u16 units_per_em;
	i16 ascent;   // font units, positive up
	i16 descent;  // font units, usually negative
	i16 line_gap;
	i16 cap_height;
	int valid;
} RgTextFace;

typedef struct RgTextFontSet
{
	RgTextFace* faces[RG_TEXT_SHAPE_MAX_FACES]; // faces[0] is primary; later ones are fallbacks in order
	u32 count;
} RgTextFontSet;

typedef struct RgTextShapeContext
{
	kbts_shape_context* ctx;
	RgArena* arena;         // every kb allocation comes from here
	size_t arena_base;      // arena->used right after the context was placed
	const RgTextFontSet* bound; // the set whose faces are currently pushed
	u32 error;              // kbts_shape_error after the last shape, 0 when none
} RgTextShapeContext;

typedef struct RgTextShapedGlyph
{
	u16 glyph;    // glyph index in `face`; 0 is .notdef
	u8 face;      // index into the font set
	u8 rtl;       // the run this glyph came from is right-to-left
	u8 grapheme;  // the source codepoint starts a Unicode grapheme cluster (kb's UAX #29 break)
	f32 x;        // pen position + GPOS offset, pixels, relative to the layout origin
	f32 y;        // positive up
	f32 advance;  // pixels
	f32 pen_x;    // pen position before this glyph (x without the GPOS offset); caret edges use it
	u32 cluster;  // byte offset of the source codepoint in the UTF-8 input
} RgTextShapedGlyph;

/* A caret group: the glyphs of one grapheme cluster, between two caret
 * positions. kb clusters are per codepoint, but a vowel sign (reordered or
 * spacing), a mark or a ZWJ continuation is not a caret position; groups merge
 * them with their base using the grapheme break kb computes per codepoint. */
typedef struct RgTextCaretGroup
{
	u32 start;   // first source byte of the group
	f32 left;    // pen extent, pixels
	f32 right;
	int rtl;
} RgTextCaretGroup;

typedef struct RgTextCaretGroupIter
{
	u32 next;  // lower bound (source byte) of the next group
	int done;
} RgTextCaretGroupIter;

typedef struct RgTextLayout
{
	RgTextShapedGlyph* glyphs; // visual order (left to right)
	u32 count;
	f32 width;      // total advance, pixels
	f32 px_size;
	u32 source_len; // bytes of input
	int has_rtl;
	int notdef_count;
} RgTextLayout;

// =============================================================================
// API
// =============================================================================

/** Parse a TrueType/OpenType font (or the `index`th font of a collection)
 *  held in `data` (kept alive by the caller) into `face`, allocating the
 *  shaping blob from `arena`. Returns 1 on success. */
RGINLINE int rg_text_face_load(RgTextFace* face, RgArena* arena, const void* data, size_t size, int index);

RGINLINE int rg_text_font_set_add(RgTextFontSet* set, RgTextFace* face);

/** Create a shaping context whose allocations come from `arena`. `reserve`
 *  bytes are taken up front for the context itself; shaping allocates more as
 *  lines demand. One context per thread. */
RGINLINE int rg_text_shape_context_init(RgTextShapeContext* sc, RgArena* arena, size_t reserve);
/** Discard the context's allocations (the arena is rewound to just after the
 *  context) and forget the bound font set. Call between frames or batches. */
RGINLINE void rg_text_shape_context_reset(RgTextShapeContext* sc);

/** Shape one line of UTF-8 at `px_size` with fallback across `set`. The
 *  layout's glyph array is allocated from `out`. Returns 1 on success, 0 when
 *  the shaper reported an error (rg_text_shape_context.error says which) or
 *  `out` is full; the layout is then empty. */
RGINLINE int rg_text_shape_utf8(RgTextShapeContext* sc, const RgTextFontSet* set, const char* utf8, size_t len, f32 px_size,
                                RgArena* out, RgTextLayout* layout);

/** Caret groups in logical order (a zeroed iterator starts). A group is the
 *  source byte range from one grapheme start to the next (the grapheme starts
 *  are the clusters of glyphs flagged by kb); every glyph whose cluster falls
 *  in the range belongs to it wherever it was reordered visually. One scan per
 *  group; layouts are line-sized. */
RGINLINE int rg_text_layout_caret_group_next(const RgTextLayout* layout, RgTextCaretGroupIter* it, RgTextCaretGroup* out);
/** Byte offset just past the group starting at `start`: the next group start,
 *  else the source length. */
RGINLINE u32 rg_text_layout_caret_group_end(const RgTextLayout* layout, u32 start);
/** Caret x for a byte offset into the source (0..source_len), in pixels: the
 *  trailing edge of the group owning the last byte before the offset (left of
 *  an RTL group, right of an LTR one), or the leading edge of the first group
 *  at offset 0. An offset inside a group carets after that group. */
RGINLINE f32 rg_text_layout_caret_x(const RgTextLayout* layout, u32 byte_offset);
/** Source byte offset whose caret is nearest to pixel x (mouse hit testing):
 *  the near edge of the group under x, mapped through its direction. */
RGINLINE u32 rg_text_layout_hit_test(const RgTextLayout* layout, f32 x);
/** Number of leading glyphs whose right edge stays within `max_width`. */
RGINLINE u32 rg_text_layout_fit(const RgTextLayout* layout, f32 max_width);
/** Line height in pixels for a face at a size (ascent - descent + gap). */
RGINLINE f32 rg_text_face_line_height(const RgTextFace* face, f32 px_size);
RGINLINE f32 rg_text_face_ascent(const RgTextFace* face, f32 px_size);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

typedef struct RgTextShapeAllocator
{
	RgArena* arena;
} RgTextShapeAllocator;

static void rg_text_shape_allocator_fn(void* data, kbts_allocator_op* op)
{
	RgArena* arena = (RgArena*)data;
	if (op->Kind == KBTS_ALLOCATOR_OP_KIND_ALLOCATE)
	{
		op->Allocate.Pointer = rg_arena_alloc_aligned(arena, op->Allocate.Size ? op->Allocate.Size : 1u, 16);
	}
	/* FREE: arena memory is reclaimed by rg_text_shape_context_reset. */
}

/* Big-endian reads for the sfnt directory walk below. */
RGINLINE u32 rg_text_sfnt_u32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
RGINLINE u16 rg_text_sfnt_u16(const u8* p) { return (u16)(((u16)p[0] << 8) | p[1]); }

/* Locate a table of face `index` in a TrueType/OpenType file or collection.
 * Returns the table bytes and length, or NULL when absent or out of bounds. */
RGINLINE const u8* rg_text_sfnt_table(const void* data, size_t size, int index, const char* tag, u32* out_length)
{
	const u8* bytes = (const u8*)data;
	size_t offset = 0;
	u32 i, count;
	if (!bytes || size < 12) return NULL;
	if (memcmp(bytes, "ttcf", 4) == 0)
	{
		u32 fonts = rg_text_sfnt_u32(bytes + 8);
		if (index < 0 || (u32)index >= fonts || size < 12u + 4u * fonts) return NULL;
		offset = rg_text_sfnt_u32(bytes + 12 + 4 * (u32)index);
		if (offset + 12 > size) return NULL;
	}
	count = rg_text_sfnt_u16(bytes + offset + 4);
	if (offset + 12 + 16u * count > size) return NULL;
	for (i = 0; i < count; i++)
	{
		const u8* rec = bytes + offset + 12 + 16u * i;
		if (memcmp(rec, tag, 4) == 0)
		{
			u32 table_offset = rg_text_sfnt_u32(rec + 8);
			u32 length = rg_text_sfnt_u32(rec + 12);
			if (table_offset > size || length > size - table_offset) return NULL;
			*out_length = length;
			return bytes + table_offset;
		}
	}
	return NULL;
}

/* Windows (DirectWrite, GDI) lays text out with the hhea ascender/descender/gap
 * unless OS/2 fsSelection sets USE_TYPO_METRICS; kb reports the OS/2 typo
 * metrics first. Mirror Windows so line boxes match what the platform draws. */
RGINLINE void rg_text_face_windows_metrics(RgTextFace* face, const void* data, size_t size, int index)
{
	u32 length = 0;
	const u8* os2 = rg_text_sfnt_table(data, size, index, "OS/2", &length);
	const u8* hhea;
	if (os2 && length >= 64 && (rg_text_sfnt_u16(os2 + 62) & 0x80u)) return; /* fsSelection.USE_TYPO_METRICS */
	hhea = rg_text_sfnt_table(data, size, index, "hhea", &length);
	if (!hhea || length < 10) return;
	face->ascent = (i16)rg_text_sfnt_u16(hhea + 4);
	face->descent = (i16)rg_text_sfnt_u16(hhea + 6);
	face->line_gap = (i16)rg_text_sfnt_u16(hhea + 8);
}

RGINLINE int rg_text_face_load(RgTextFace* face, RgArena* arena, const void* data, size_t size, int index)
{
	kbts_load_font_state state;
	kbts_load_font_error err;
	int scratch_size = 0, output_size = 0;
	memset(face, 0, sizeof *face);
	memset(&state, 0, sizeof state);
	if (!data || size == 0 || size > 0x7FFFFFFFu) return 0;
	err = kbts_LoadFont(&face->font, &state, (void*)data, (int)size, index, &scratch_size, &output_size);
	if (err == KBTS_LOAD_FONT_ERROR_NEED_TO_CREATE_BLOB)
	{
		size_t mark = arena->used;
		void* scratch = rg_arena_alloc_aligned(arena, (size_t)scratch_size + 16u, 16);
		void* output;
		if (!scratch) return 0;
		output = rg_arena_alloc_aligned(arena, (size_t)output_size + 16u, 16);
		if (!output)
		{
			arena->used = mark;
			return 0;
		}
		err = kbts_PlaceBlob(&face->font, &state, scratch, output);
		/* The scratch block sits below the output block, so it cannot be
		 * reclaimed without moving the blob; it is a one-time cost per face. */
	}
	if (err != KBTS_LOAD_FONT_ERROR_NONE || !kbts_FontIsValid(&face->font)) return 0;
	{
		kbts_font_info2_2 info;
		memset(&info, 0, sizeof info);
		info.Base.Size = sizeof info;
		kbts_GetFontInfo2(&face->font, &info.Base);
		face->units_per_em = info.UnitsPerEm ? info.UnitsPerEm : 1000u;
		face->ascent = info.Ascent;
		face->descent = info.Descent;
		face->line_gap = info.LineGap;
		face->cap_height = info.CapitalHeight;
	}
	rg_text_face_windows_metrics(face, data, size, index);
	face->valid = 1;
	return 1;
}

RGINLINE int rg_text_font_set_add(RgTextFontSet* set, RgTextFace* face)
{
	if (!face || !face->valid || set->count >= RG_TEXT_SHAPE_MAX_FACES) return 0;
	set->faces[set->count++] = face;
	return 1;
}

RGINLINE int rg_text_shape_context_init(RgTextShapeContext* sc, RgArena* arena, size_t reserve)
{
	void* memory;
	memset(sc, 0, sizeof *sc);
	sc->arena = arena;
	memory = rg_arena_alloc_aligned(arena, (size_t)kbts_SizeOfShapeContext() + 16u, 16);
	if (!memory) return 0;
	/* Fonts pushed earlier win: faces[0] is primary. */
	sc->ctx = kbts_PlaceShapeContext2(rg_text_shape_allocator_fn, arena, memory, KBTS_SHAPE_CONTEXT_FLAG_FONT_PRIORITY_BOTTOM_TO_TOP);
	if (!sc->ctx) return 0;
	(void)reserve;
	sc->arena_base = arena->used;
	return 1;
}

RGINLINE void rg_text_shape_context_reset(RgTextShapeContext* sc)
{
	if (!sc->ctx) return;
	/* Re-place the context on its original memory so kb forgets the blocks
	 * it allocated past arena_base, then give those bytes back. */
	{
		void* memory = (void*)sc->ctx;
		sc->ctx = kbts_PlaceShapeContext2(rg_text_shape_allocator_fn, sc->arena, memory, KBTS_SHAPE_CONTEXT_FLAG_FONT_PRIORITY_BOTTOM_TO_TOP);
	}
	sc->arena->used = sc->arena_base;
	sc->bound = NULL;
	sc->error = 0;
}

/* Make `set` the context's font stack. Switching sets re-places the context
 * (kb's pop does not reliably empty a stack that has shaped), which also
 * discards the context's allocations; a context should therefore serve one
 * set at a time, and its arena must hold nothing else past init. */
RGINLINE int rg_text_shape_bind(RgTextShapeContext* sc, const RgTextFontSet* set)
{
	u32 i;
	if (sc->bound == set) return 1;
	if (sc->bound) rg_text_shape_context_reset(sc);
	for (i = 0; i < set->count; i++)
		if (!kbts_ShapePushFont(sc->ctx, &set->faces[i]->font)) return 0;
	sc->bound = set;
	return 1;
}

RGINLINE int rg_text_shape_utf8(RgTextShapeContext* sc, const RgTextFontSet* set, const char* utf8, size_t len, f32 px_size,
                                RgArena* out, RgTextLayout* layout)
{
	kbts_run run;
	u32 capacity = (u32)len + 8u; /* glyphs never exceed codepoints by much; ligatures shrink */
	RgTextShapedGlyph* glyphs;
	u32 count = 0;
	f32 pen = 0.0f;
	memset(layout, 0, sizeof *layout);
	layout->px_size = px_size;
	layout->source_len = (u32)len;
	if (!sc->ctx || !set || !set->count || len > 0x7FFFFFFFu) return 0;
	if (!rg_text_shape_bind(sc, set)) return 0;
	glyphs = (RgTextShapedGlyph*)rg_arena_alloc_aligned(out, (size_t)capacity * sizeof(RgTextShapedGlyph), 8);
	if (!glyphs) return 0;

	kbts_ShapeBegin(sc->ctx, KBTS_DIRECTION_LTR, KBTS_LANGUAGE_DONT_KNOW);
	kbts_ShapeUtf8(sc->ctx, utf8, (int)len, KBTS_USER_ID_GENERATION_MODE_SOURCE_INDEX);
	kbts_ShapeEnd(sc->ctx);
	sc->error = kbts_ShapeError(sc->ctx);
	if (sc->error) return 0;

	while (kbts_ShapeRun(sc->ctx, &run))
	{
		kbts_glyph* g;
		u8 face_index = 0;
		f32 scale;
		u32 i;
		for (i = 0; i < set->count; i++)
			if (&set->faces[i]->font == run.Font)
			{
				face_index = (u8)i;
				break;
			}
		if (i == set->count && run.Font) return 0; /* a face outside the bound set: the context is inconsistent */
		scale = px_size / (f32)set->faces[face_index]->units_per_em;
		while (kbts_GlyphIteratorNext(&run.Glyphs, &g))
		{
			RgTextShapedGlyph* o;
			kbts_shape_codepoint cp;
			if (count >= capacity) return 0;
			o = &glyphs[count++];
			o->glyph = g->Id;
			o->face = face_index;
			o->rtl = run.Direction == KBTS_DIRECTION_RTL;
			o->x = pen + (f32)g->OffsetX * scale;
			o->pen_x = pen;
			o->y = (f32)g->OffsetY * scale;
			o->advance = (f32)g->AdvanceX * scale;
			o->cluster = 0;
			o->grapheme = 1;
			if (kbts_ShapeGetShapeCodepoint(sc->ctx, g->UserIdOrCodepointIndex, &cp))
			{
				o->cluster = (u32)cp.UserId;
				o->grapheme = (cp.BreakFlags & KBTS_BREAK_FLAG_GRAPHEME) != 0;
			}
			if (g->Id == 0) layout->notdef_count++;
			if (o->rtl) layout->has_rtl = 1;
			pen += o->advance;
		}
	}
	layout->glyphs = glyphs;
	layout->count = count;
	layout->width = pen;
	return 1;
}

RGINLINE int rg_text_layout_caret_group_next(const RgTextLayout* layout, RgTextCaretGroupIter* it, RgTextCaretGroup* out)
{
	u32 lower = it->next, upper = UINT32_MAX, i;
	int found = 0;
	if (it->done) return 0;
	/* The group is [lower, upper): upper is the smallest grapheme start above
	 * lower (the first group's lower bound is 0, so it also takes any glyph
	 * below the first flagged cluster). */
	for (i = 0; i < layout->count; i++)
	{
		const RgTextShapedGlyph* g = &layout->glyphs[i];
		if (g->grapheme && g->cluster > lower && g->cluster < upper) upper = g->cluster;
	}
	out->left = out->right = 0.0f;
	out->rtl = 0;
	out->start = lower;
	for (i = 0; i < layout->count; i++)
	{
		const RgTextShapedGlyph* g = &layout->glyphs[i];
		if (g->cluster < lower || g->cluster >= upper) continue;
		if (!found || g->cluster < out->start) out->start = g->cluster;
		if (!found || g->pen_x < out->left) out->left = g->pen_x;
		if (!found || g->pen_x + g->advance > out->right) out->right = g->pen_x + g->advance;
		out->rtl |= g->rtl;
		found = 1;
	}
	if (upper == UINT32_MAX) it->done = 1;
	else it->next = upper;
	if (!found) return rg_text_layout_caret_group_next(layout, it, out); /* an empty range (no glyphs): skip it */
	return 1;
}

RGINLINE u32 rg_text_layout_caret_group_end(const RgTextLayout* layout, u32 start)
{
	RgTextCaretGroupIter it;
	RgTextCaretGroup grp;
	u32 end = layout->source_len;
	memset(&it, 0, sizeof it);
	while (rg_text_layout_caret_group_next(layout, &it, &grp))
		if (grp.start > start && grp.start < end) end = grp.start;
	return end;
}

RGINLINE f32 rg_text_layout_caret_x(const RgTextLayout* layout, u32 byte_offset)
{
	RgTextCaretGroupIter it;
	RgTextCaretGroup grp, before, at;
	int have_before = 0, have_at = 0;
	memset(&it, 0, sizeof it);
	memset(&before, 0, sizeof before);
	memset(&at, 0, sizeof at);
	while (rg_text_layout_caret_group_next(layout, &it, &grp))
	{
		if (grp.start == byte_offset) { at = grp; have_at = 1; }
		else if (grp.start < byte_offset && (!have_before || grp.start > before.start)) { before = grp; have_before = 1; }
	}
	if (have_before) return before.rtl ? before.left : before.right; /* trailing edge of the previous group */
	if (have_at) return at.rtl ? at.right : at.left;                  /* leading edge of the first group */
	return 0.0f;
}

RGINLINE u32 rg_text_layout_hit_test(const RgTextLayout* layout, f32 x)
{
	RgTextCaretGroupIter it;
	RgTextCaretGroup grp, hit, leftmost, rightmost;
	int found = 0, any = 0, right_edge;
	memset(&it, 0, sizeof it);
	memset(&hit, 0, sizeof hit);
	leftmost = rightmost = hit;
	while (rg_text_layout_caret_group_next(layout, &it, &grp))
	{
		if (!found && grp.left <= x && x < grp.right) { hit = grp; found = 1; }
		if (!any || grp.left < leftmost.left) leftmost = grp;
		if (!any || grp.right > rightmost.right) rightmost = grp;
		any = 1;
	}
	if (!any) return 0;
	if (found) right_edge = x >= (hit.left + hit.right) * 0.5f;
	else
	{
		/* Outside the line: the nearest end. */
		hit = x < leftmost.left ? leftmost : rightmost;
		right_edge = x >= leftmost.left;
	}
	/* The near edge of the group; its direction says which offset that edge is. */
	return (right_edge == hit.rtl) ? hit.start : rg_text_layout_caret_group_end(layout, hit.start);
}

RGINLINE u32 rg_text_layout_fit(const RgTextLayout* layout, f32 max_width)
{
	u32 i;
	for (i = 0; i < layout->count; i++)
		if (layout->glyphs[i].x + layout->glyphs[i].advance > max_width) return i;
	return layout->count;
}

RGINLINE f32 rg_text_face_line_height(const RgTextFace* face, f32 px_size)
{
	return (f32)(face->ascent - face->descent + face->line_gap) * px_size / (f32)face->units_per_em;
}

RGINLINE f32 rg_text_face_ascent(const RgTextFace* face, f32 px_size)
{
	return (f32)face->ascent * px_size / (f32)face->units_per_em;
}

#endif // RG_TEXT_SHAPE_H
