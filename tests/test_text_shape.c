// rg_text_shape tests against the Windows system fonts (skipped elsewhere):
// Latin ligatures, Arabic joining, Devanagari conjuncts, CJK and emoji
// fallback across faces, monotonic clusters per direction, zero .notdef,
// copy-out of kb's reused run memory, caret and hit-test round trips, and a
// 10k-name shaping timing.
//
// Usage:
//   test_text_shape.exe

#define RGINLINE static inline
#include "rg_defs.h"
#include "rg_mem.h"
#include "rg_time.h"
#include "rg_text_shape.h"

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

static RgArena arena;
static RgArena scratch;
static RgTextShapeContext shaper;
static RgTextFace segoe, calibri, nirmala, leela, yugothic, emoji;
static RgTextFontSet set;

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

static int load_face(RgTextFace* face, const char* path, int index)
{
	size_t size = 0;
	void* data = read_file(path, &size);
	if (!data) return 0;
	return rg_text_face_load(face, &arena, data, size, index);
}

/* Layouts accumulate in `scratch`; each test resets it on entry so layouts it
 * compares stay alive together. */
static int shape(const char* text, f32 px, RgTextLayout* layout)
{
	return rg_text_shape_utf8(&shaper, &set, text, strlen(text), px, &scratch, layout);
}

static void test_latin(void)
{
	RgTextLayout a, b;
	scratch.used = 0;
	TEST_ASSERT(shape("Hello", 14.0f, &a), "shape Hello");
	TEST_ASSERT(a.count == 5 && a.notdef_count == 0 && a.width > 20.0f && a.width < 60.0f, "five glyphs, plausible width");
	TEST_ASSERT(a.glyphs[0].cluster == 0 && a.glyphs[4].cluster == 4 && !a.has_rtl, "clusters are byte offsets, LTR");
	TEST_ASSERT(a.glyphs[0].face == 0, "primary face");
	/* DirectWrite: ids 43 72 79 79 82, advances 710 523 242 242 586 per 1000 em. */
	TEST_ASSERT(a.glyphs[0].glyph == 43 && a.glyphs[1].glyph == 72 && a.glyphs[2].glyph == 79 && a.glyphs[4].glyph == 82, "Latin ids match DirectWrite");
	TEST_ASSERT(a.glyphs[0].advance > 9.8f && a.glyphs[0].advance < 10.0f, "H advance is 710/1000 em at 14 px");
	/* Calibri has an fi ligature under liga; Segoe UI does not. Use the second set slot. */
	{
		RgTextFontSet cal;
		RgTextLayout l;
		memset(&cal, 0, sizeof cal);
		rg_text_font_set_add(&cal, &calibri);
		TEST_ASSERT(rg_text_shape_utf8(&shaper, &cal, "fi", 2, 14.0f, &scratch, &l), "shape fi in Calibri");
		TEST_ASSERT(l.count == 1 && l.glyphs[0].glyph == 302 && l.glyphs[0].cluster == 0 && l.notdef_count == 0, "fi becomes one ligature glyph (DirectWrite: 302)");
		TEST_ASSERT(rg_text_shape_utf8(&shaper, &set, "fi", 2, 14.0f, &scratch, &l), "shape fi in the main set");
	}
	/* Kerning moves the pen: "AV" narrower than "A" + "V". */
	TEST_ASSERT(shape("AV", 40.0f, &a) && shape("A", 40.0f, &b), "shape AV and A");
	{
		RgTextLayout v;
		TEST_ASSERT(shape("V", 40.0f, &v), "shape V");
		TEST_ASSERT(a.width < b.width + v.width - 0.5f, "AV is kerned tighter than A + V");
	}
	TEST_PASS();
}

static void test_arabic(void)
{
	/* U+0628 BEH: isolated alone, initial/medial/final when joined. */
	RgTextLayout alone, joined;
	scratch.used = 0;
	TEST_ASSERT(shape("\xD8\xA8", 14.0f, &alone), "shape isolated beh");
	TEST_ASSERT(alone.count == 1 && alone.glyphs[0].glyph == 2318, "isolated form matches DirectWrite (2318)");
	TEST_ASSERT(shape("\xD8\xA8\xD8\xA8\xD8\xA8", 14.0f, &joined), "shape beh beh beh");
	TEST_ASSERT(alone.count == 1 && joined.count == 3 && alone.notdef_count == 0 && joined.notdef_count == 0, "glyph counts");
	TEST_ASSERT(alone.has_rtl && joined.has_rtl && joined.glyphs[0].rtl, "runs are RTL");
	TEST_ASSERT(joined.glyphs[0].glyph != joined.glyphs[1].glyph || joined.glyphs[1].glyph != joined.glyphs[2].glyph,
	            "joining forms differ along the word");
	/* DirectWrite logical order: 2251 (initial) 3623 (medial) 3622 (final); visual order reverses it. */
	TEST_ASSERT(joined.glyphs[0].glyph == 3622 && joined.glyphs[1].glyph == 3623 && joined.glyphs[2].glyph == 2251,
	            "joined forms match DirectWrite in visual order (3622 3623 2251)");
	/* Visual order is left to right, so clusters of an RTL word decrease. */
	/* Caret: logical start of an RTL word is its right edge, logical end its left edge. */
	TEST_ASSERT(rg_text_layout_caret_x(&joined, 0) == joined.width && rg_text_layout_caret_x(&joined, 6) == 0.0f,
	            "RTL caret endpoints");
	TEST_ASSERT(rg_text_layout_caret_x(&joined, 2) == joined.glyphs[2].x &&
	                rg_text_layout_caret_x(&joined, 4) == joined.glyphs[1].x,
	            "RTL caret between letters sits left of the earlier letter");
	/* Hit test: the left edge is the logical end, the right edge the start, and
	 * a probe just inside each caret position returns that offset. */
	TEST_ASSERT(rg_text_layout_hit_test(&joined, 0.5f) == 6 && rg_text_layout_hit_test(&joined, -5.0f) == 6,
	            "left edge hits the logical end");
	TEST_ASSERT(rg_text_layout_hit_test(&joined, joined.width - 0.5f) == 0 &&
	                rg_text_layout_hit_test(&joined, joined.width + 5.0f) == 0,
	            "right edge hits the logical start");
	for (u32 off = 0; off < 6; off += 2)
		TEST_ASSERT(rg_text_layout_hit_test(&joined, rg_text_layout_caret_x(&joined, off) - 0.5f) == off,
		            "RTL hit test returns the caret offset");
	TEST_ASSERT(rg_text_layout_hit_test(&joined, rg_text_layout_caret_x(&joined, 6) + 0.5f) == 6,
	            "RTL hit test at the logical end");
	/* Mixed: "ab" then the Arabic word. The caret after 'b' and the caret
	 * before the first Arabic letter coincide at the run boundary. */
	{
		RgTextLayout mixed;
		f32 after_b;
		u32 k, arabic_right = 0;
		TEST_ASSERT(shape("ab\xD8\xA8\xD8\xA8\xD8\xA8", 14.0f, &mixed), "shape mixed");
		TEST_ASSERT(mixed.count == 5 && mixed.has_rtl && !mixed.glyphs[0].rtl && mixed.glyphs[4].rtl, "mixed runs");
		after_b = mixed.glyphs[1].x + mixed.glyphs[1].advance;
		for (k = 0; k < 5; k++)
			if (mixed.glyphs[k].cluster == 2) arabic_right = k;
		TEST_ASSERT(rg_text_layout_caret_x(&mixed, 0) == 0.0f && rg_text_layout_caret_x(&mixed, 2) == after_b,
		            "LTR carets in the mixed line");
		TEST_ASSERT(rg_text_layout_caret_x(&mixed, 8) == mixed.glyphs[2].x &&
		                mixed.glyphs[2].x == after_b,
		            "the Arabic word's logical end is its left edge, at the run boundary");
		TEST_ASSERT(rg_text_layout_hit_test(&mixed, mixed.width - 0.5f) == 2 &&
		                mixed.glyphs[arabic_right].x + mixed.glyphs[arabic_right].advance == mixed.width,
		            "the right edge of the line is the first Arabic letter");
		TEST_ASSERT(rg_text_layout_hit_test(&mixed, 0.5f) == 0 && rg_text_layout_hit_test(&mixed, after_b - 0.5f) == 2,
		            "hits inside the Latin run");
	}
	TEST_ASSERT(joined.glyphs[0].cluster == 4 && joined.glyphs[1].cluster == 2 && joined.glyphs[2].cluster == 0,
	            "RTL clusters run backwards in visual order");
	/* Mixed line: Latin then Arabic then Latin keeps Latin LTR and the word RTL. */
	{
		RgTextLayout m;
		TEST_ASSERT(shape("ab \xD8\xA8\xD8\xA8 cd", 14.0f, &m), "shape mixed");
		TEST_ASSERT(m.count == 8 && m.notdef_count == 0, "eight glyphs");
		TEST_ASSERT(m.glyphs[0].cluster == 0 && !m.glyphs[0].rtl, "a first");
		TEST_ASSERT(m.glyphs[7].cluster == 9 && !m.glyphs[7].rtl, "d last");
		TEST_ASSERT(m.glyphs[3].rtl && m.glyphs[4].rtl && m.glyphs[3].cluster > m.glyphs[4].cluster, "arabic word reversed in the middle");
	}
	TEST_PASS();
}

static void test_devanagari_and_thai(void)
{
	/* नमस्ते : 6 codepoints; the conjunct s+virama+t and the vowel sign shape into fewer glyphs. */
	RgTextLayout l;
	scratch.used = 0;
	TEST_ASSERT(shape("\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87", 16.0f, &l), "shape namaste");
	TEST_ASSERT(l.count > 0 && l.count < 6 && l.notdef_count == 0, "conjunct reduced the glyph count");
	TEST_ASSERT(l.glyphs[0].face == 2, "Devanagari came from the Nirmala fallback face");
	/* Thai: ไทย with the above-base vowel mark positioned (y != 0 somewhere, no notdef). */
	TEST_ASSERT(shape("\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2", 16.0f, &l), "shape thai");
	TEST_ASSERT(l.count == 3 && l.notdef_count == 0 && l.glyphs[0].face == 3, "Thai from the Leelawadee face, no notdef");
	/* DirectWrite parity (tools/dwrite_dump.exe on the same fonts): glyph ids per script. */
	TEST_ASSERT(l.glyphs[0].glyph == 314 && l.glyphs[1].glyph == 273 && l.glyphs[2].glyph == 284, "Thai ids match DirectWrite (314 273 284)");
	TEST_ASSERT(shape("\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87", 16.0f, &l), "shape namaste again");
	TEST_ASSERT(l.count == 4 && l.glyphs[0].glyph == 267 && l.glyphs[1].glyph == 273 && l.glyphs[2].glyph == 721 && l.glyphs[3].glyph == 312,
	            "Devanagari ids match DirectWrite (267 273 721 312)");
	TEST_ASSERT(shape("\xD8\xA7\xD9\x84\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", 14.0f, &l), "shape as-salam");
	TEST_ASSERT(l.count == 6 && l.glyphs[5].glyph == 2317 && l.glyphs[4].glyph == 2286 && l.glyphs[3].glyph == 3634 &&
	                l.glyphs[2].glyph == 3981 && l.glyphs[1].glyph == 3941 && l.glyphs[0].glyph == 2342,
	            "Arabic ids match DirectWrite in logical order (2317 2286 3634 3981 3941 2342)");
	TEST_ASSERT(shape("r\xC3\xA9sum\xC3\xA9", 14.0f, &l), "shape resume");
	TEST_ASSERT(l.count == 6 && l.glyphs[0].glyph == 85 && l.glyphs[1].glyph == 112 && l.glyphs[5].glyph == 112, "precomposed accents match DirectWrite (85 112 86 88 80 112)");
	TEST_ASSERT(shape("\xD0\x90\xD1\x80\xD1\x85\xD0\xB8\xD0\xB2", 14.0f, &l), "shape cyrillic");
	TEST_ASSERT(l.count == 5 && l.glyphs[0].glyph == 487 && l.glyphs[4].glyph == 521, "Cyrillic ids match DirectWrite (487 535 540 527 521)");
	/* Carets of clusters whose glyphs reorder or carry marks: only the group
	 * boundaries exist, at the pen extent, never at an inner glyph. */
	{
		/* Devanagari ka + vowel sign i (the sign is reordered before its base),
		 * Arabic beh + fatha (zero-advance mark, RTL), Thai ko kai + mai tho (mark),
		 * Devanagari ka + vowel sign aa (a spacing sign with its own advance). */
		static const char* const one_group[] = {"\xE0\xA4\x95\xE0\xA4\xBF", "\xD8\xA8\xD9\x8E", "\xE0\xB8\x81\xE0\xB9\x89",
		                                        "\xE0\xA4\x95\xE0\xA4\xBE"};
		static const char* const names[] = {"devanagari ki", "arabic beh+fatha", "thai ko kai+mai tho", "devanagari kaa"};
		static const u32 lens[] = {6, 4, 6, 6};
		u32 t;
		for (t = 0; t < 4; t++)
		{
			RgTextCaretGroupIter it;
			RgTextCaretGroup grp;
			f32 start, end;
			u32 groups = 0, k;
			TEST_ASSERT(shape(one_group[t], 14.0f, &l), names[t]);
			TEST_ASSERT(l.count == 2 && l.notdef_count == 0 && l.width > 4.0f, "two glyphs");
			TEST_ASSERT(l.glyphs[0].grapheme + l.glyphs[1].grapheme == 1, "exactly one glyph starts a grapheme");
			memset(&it, 0, sizeof it);
			while (rg_text_layout_caret_group_next(&l, &it, &grp)) groups++;
			TEST_ASSERT(groups == 1 && grp.start == 0 && grp.left == 0.0f && grp.right == l.width, "one caret group spanning the run");
			start = rg_text_layout_caret_x(&l, 0);
			end = rg_text_layout_caret_x(&l, lens[t]);
			if (l.has_rtl)
				TEST_ASSERT(start == l.width && end == 0.0f, "RTL group: start at the right edge, end at the left edge");
			else
				TEST_ASSERT(start == 0.0f && end == l.width, "LTR group: start at the left edge, end at the right edge");
			TEST_ASSERT(rg_text_layout_caret_x(&l, 3) == end, "an offset inside the group carets after it");
			for (k = 0; k < l.count; k++)
				TEST_ASSERT(l.glyphs[k].cluster == 0 || rg_text_layout_caret_x(&l, l.glyphs[k].cluster) == end,
				            "a mark's cluster start is no caret position");
			TEST_ASSERT(rg_text_layout_hit_test(&l, 0.5f) == (l.has_rtl ? lens[t] : 0u) &&
			                rg_text_layout_hit_test(&l, l.width - 0.5f) == (l.has_rtl ? 0u : lens[t]) &&
			                rg_text_layout_hit_test(&l, -3.0f) == (l.has_rtl ? lens[t] : 0u) &&
			                rg_text_layout_hit_test(&l, l.width + 3.0f) == (l.has_rtl ? 0u : lens[t]),
			            "hits map to the group's two boundaries only");
		}
		/* Two graphemes with a spacing sign in the first: kaa + ka has exactly two
		 * caret positions besides the ends, both at pen edges. */
		TEST_ASSERT(shape("\xE0\xA4\x95\xE0\xA4\xBE\xE0\xA4\x95", 14.0f, &l), "shape kaa ka");
		{
			RgTextCaretGroupIter it;
			RgTextCaretGroup grp;
			u32 groups = 0;
			f32 split = -1.0f;
			memset(&it, 0, sizeof it);
			while (rg_text_layout_caret_group_next(&l, &it, &grp))
			{
				if (groups == 1) split = grp.left;
				groups++;
			}
			TEST_ASSERT(l.count == 3 && groups == 2 && rg_text_layout_caret_group_end(&l, 0) == 6, "kaa|ka: two groups, the first ends at byte 6");
			TEST_ASSERT(rg_text_layout_caret_x(&l, 3) == split && rg_text_layout_caret_x(&l, 6) == split &&
			                split > 4.0f && split < l.width - 4.0f,
			            "the only inner caret is between the two graphemes");
			TEST_ASSERT(rg_text_layout_hit_test(&l, split - 0.5f) == 6 && rg_text_layout_hit_test(&l, split + 0.5f) == 6,
			            "hits around the split return byte 6, never byte 3");
		}
		/* Two reordered graphemes side by side: ki ki. Visually both signs precede
		 * their bases, so the middle caret (byte 6) must come from source identity. */
		TEST_ASSERT(shape("\xE0\xA4\x95\xE0\xA4\xBF\xE0\xA4\x95\xE0\xA4\xBF", 14.0f, &l), "shape ki ki");
		{
			RgTextCaretGroupIter it;
			RgTextCaretGroup grp;
			u32 groups = 0, order_ok = 1;
			f32 split = -1.0f;
			memset(&it, 0, sizeof it);
			while (rg_text_layout_caret_group_next(&l, &it, &grp))
			{
				if (groups == 0) order_ok &= grp.start == 0 && grp.left == 0.0f;
				if (groups == 1) { order_ok &= grp.start == 6 && grp.right == l.width; split = grp.left; }
				groups++;
			}
			TEST_ASSERT(l.count == 4 && groups == 2 && order_ok, "ki|ki: two groups, 0..6 and 6..12");
			TEST_ASSERT(rg_text_layout_caret_x(&l, 6) == split && split > l.width * 0.4f && split < l.width * 0.6f &&
			                rg_text_layout_caret_x(&l, 3) == split && rg_text_layout_caret_x(&l, 9) == l.width &&
			                rg_text_layout_caret_x(&l, 12) == l.width,
			            "the middle caret is at the midpoint; inner offsets caret after their grapheme");
			TEST_ASSERT(rg_text_layout_hit_test(&l, split - 0.5f) == 6 && rg_text_layout_hit_test(&l, split + 0.5f) == 6 &&
			                rg_text_layout_hit_test(&l, 0.5f) == 0 && rg_text_layout_hit_test(&l, l.width - 0.5f) == 12,
			            "hits map to 0, 6 and 12 only");
		}
		/* The same groups inside a word: namaste has a conjunct and a vowel sign;
		 * every caret position is a group boundary and they are monotonic. */
		TEST_ASSERT(shape("\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87", 16.0f, &l), "shape namaste for carets");
		{
			RgTextCaretGroupIter it;
			RgTextCaretGroup grp;
			f32 last_x = -1.0f;
			u32 last_start = 0, groups = 0;
			memset(&it, 0, sizeof it);
			while (rg_text_layout_caret_group_next(&l, &it, &grp))
			{
				f32 cx = rg_text_layout_caret_x(&l, grp.start);
				TEST_ASSERT(cx == grp.left && cx > last_x && (groups == 0 || grp.start > last_start), "LTR group carets increase with the offset");
				TEST_ASSERT(rg_text_layout_hit_test(&l, grp.left + 0.5f) == grp.start &&
				                rg_text_layout_hit_test(&l, grp.right - 0.5f) == rg_text_layout_caret_group_end(&l, grp.start),
				            "hits inside a group return its boundaries");
				last_x = cx;
				last_start = grp.start;
				groups++;
			}
			TEST_ASSERT(groups >= 2 && groups <= 4 && rg_text_layout_caret_x(&l, 18) == l.width, "namaste has 2-4 caret groups ending at the width");
		}
	}
	TEST_PASS();
}

static void test_fallback_cjk_emoji(void)
{
	RgTextLayout l;
	scratch.used = 0;
	TEST_ASSERT(shape("file 日本語.txt", 14.0f, &l), "shape mixed cjk");
	TEST_ASSERT(l.notdef_count == 0, "no notdef with the fallback set");
	{
		u32 i, cjk = 0, latin = 0;
		for (i = 0; i < l.count; i++)
		{
			if (l.glyphs[i].face == 4) cjk++;
			if (l.glyphs[i].face == 0) latin++;
		}
		TEST_ASSERT(cjk == 3 && latin == 9, "three ideographs from Yu Gothic, the rest from Segoe UI");
		TEST_ASSERT(l.glyphs[5].glyph == 3286 && l.glyphs[6].glyph == 3724 && l.glyphs[7].glyph == 1954, "ideograph ids match DirectWrite in Yu Gothic (3286 3724 1954)");
	}
	TEST_ASSERT(shape("emoji_\xF0\x9F\x99\x82", 14.0f, &l), "shape emoji");
	TEST_ASSERT(l.notdef_count == 0 && l.glyphs[l.count - 1].face == 5 && l.glyphs[l.count - 1].cluster == 6, "emoji from Segoe UI Emoji, cluster at byte 6");
	TEST_ASSERT(l.glyphs[l.count - 1].glyph == 2386, "emoji id matches DirectWrite (2386)");
	/* Without a fallback the ideographs are .notdef, and the caller can tell. */
	{
		RgTextFontSet only;
		memset(&only, 0, sizeof only);
		rg_text_font_set_add(&only, &segoe);
		TEST_ASSERT(rg_text_shape_utf8(&shaper, &only, "日本", 6, 14.0f, &scratch, &l), "shape cjk with Segoe only");
		TEST_ASSERT(l.notdef_count == 2 && l.count == 2 && l.glyphs[0].glyph == 0, "two notdef reported (DirectWrite: 0 0)");
		rg_text_shape_utf8(&shaper, &set, "x", 1, 14.0f, &scratch, &l); /* rebind the main set */
	}
	/* Grapheme starts (UAX #29) that kb 2.28g gets wrong; third_party/kb/README.md has the
	 * patch. With Segoe UI alone every codepoint is its own glyph, so the grapheme flags
	 * are all that join a sequence: a ZWJ chain (family) is one grapheme, and after a
	 * consonant + virama the next grapheme keeps its own rules (emoji ZWJ, Hangul L V). */
	{
		static const char* const texts[] = {
		    "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6",
		    "\xE0\xA4\x95\xE0\xA5\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
		    "\xE0\xA4\x95\xE0\xA5\x8D\xE1\x84\x80\xE1\x85\xA1"};
		static const char* const names[] = {"family is one grapheme", "ka+virama | woman ZWJ girl", "ka+virama | Hangul L V"};
		static const u32 starts[] = {1u, 1u | (1u << 6), 1u | (1u << 6)}; /* bit b: a grapheme starts at byte b */
		RgTextFontSet only;
		u32 t;
		memset(&only, 0, sizeof only);
		rg_text_font_set_add(&only, &segoe);
		for (t = 0; t < 3; t++)
		{
			u32 k, got = 0;
			TEST_ASSERT(rg_text_shape_utf8(&shaper, &only, texts[t], strlen(texts[t]), 14.0f, &scratch, &l), names[t]);
			for (k = 0; k < l.count; k++)
				if (l.glyphs[k].grapheme) got |= 1u << l.glyphs[k].cluster;
			TEST_ASSERT(got == starts[t], names[t]);
		}
		rg_text_shape_utf8(&shaper, &set, "x", 1, 14.0f, &scratch, &l); /* rebind the main set */
		TEST_ASSERT(shape(texts[0], 14.0f, &l) && rg_text_layout_caret_group_end(&l, 0) == 25u,
		            "with the emoji face the family is still one caret group (0..25)");
	}
	TEST_PASS();
}

/* Where the direction changes an offset has two places on screen. In "a" + seen
 * lam + "b" the Arabic run starts at byte 1: caret_x(1) is the trailing edge of
 * "a" (the run's left end), but a click at the run's right end also hits byte 1,
 * and hit_caret reports that edge, so the caret can be drawn where the user clicked. */
static void test_hit_caret(void)
{
	RgTextLayout l;
	f32 a_right, run_right, edge = -1.0f;
	u32 i, hit;
	scratch.used = 0;
	TEST_ASSERT(shape("a\xD8\xB3\xD9\x84" "b", 14.0f, &l), "shape a + Arabic + b");
	TEST_ASSERT(l.has_rtl && l.count == 4, "four glyphs, one RTL run");
	a_right = l.glyphs[0].pen_x + l.glyphs[0].advance;
	run_right = a_right;
	for (i = 0; i < l.count; i++)
		if (l.glyphs[i].rtl && l.glyphs[i].pen_x + l.glyphs[i].advance > run_right) run_right = l.glyphs[i].pen_x + l.glyphs[i].advance;
	TEST_ASSERT(run_right > a_right + 4.0f, "the run has width");
	TEST_ASSERT(rg_text_layout_caret_x(&l, 1) == a_right, "caret_x(1) is the left end of the run");
	hit = rg_text_layout_hit_caret(&l, run_right - 0.5f, &edge);
	TEST_ASSERT(hit == 1u && edge == run_right, "a hit at the run's right end is byte 1, drawn at that end");
	hit = rg_text_layout_hit_caret(&l, a_right + 0.5f, &edge);
	TEST_ASSERT(hit == 5u && edge == a_right, "a hit at the run's left end is its logical end, drawn there");
	TEST_ASSERT(rg_text_layout_hit_test(&l, run_right - 0.5f) == 1u, "hit_test agrees");
	TEST_ASSERT(rg_text_layout_hit_caret(&l, l.width + 5.0f, &edge) == 6u && edge == l.width, "past the end: the end, at the width");
	TEST_PASS();
}

static void test_copy_out_and_queries(void)
{
	/* Two layouts shaped back to back stay distinct: results were copied out of kb's reused memory. */
	RgTextLayout a, b;
	u32 i;
	scratch.used = 0;
	TEST_ASSERT(rg_text_shape_utf8(&shaper, &set, "first", 5, 14.0f, &scratch, &a), "shape first");
	TEST_ASSERT(rg_text_shape_utf8(&shaper, &set, "second one", 10, 14.0f, &scratch, &b), "shape second");
	TEST_ASSERT(a.count == 5 && b.count == 10 && a.glyphs != b.glyphs, "separate storage");
	TEST_ASSERT(a.glyphs[0].cluster == 0 && a.glyphs[4].cluster == 4 && a.glyphs[4].x > a.glyphs[0].x, "first layout intact");
	/* Caret and hit test round trip on LTR text. */
	for (i = 0; i <= 5; i++)
	{
		f32 x = rg_text_layout_caret_x(&a, i);
		u32 back = rg_text_layout_hit_test(&a, x + 0.5f);
		TEST_ASSERT(back == i, "hit test returns the caret offset");
	}
	TEST_ASSERT(rg_text_layout_caret_x(&a, 5) == a.width, "caret at the end is the width");
	TEST_ASSERT(rg_text_layout_fit(&a, a.width + 1.0f) == 5 && rg_text_layout_fit(&a, a.glyphs[2].x + 0.5f) == 2, "fit counts leading glyphs");
	/* Line metrics are plausible for a 14 px UI font. */
	{
		f32 lh = rg_text_face_line_height(&segoe, 14.0f);
		f32 asc = rg_text_face_ascent(&segoe, 14.0f);
		TEST_ASSERT(lh > 14.0f && lh < 24.0f && asc > 9.0f && asc < 16.0f, "line height and ascent in range");
		/* Segoe UI does not set USE_TYPO_METRICS: hhea 2210/-514/0 over 2048 units, as DirectWrite uses. */
		TEST_ASSERT(segoe.ascent == 2210 && segoe.descent == -514 && segoe.line_gap == 0, "hhea metrics like Windows");
	}
	TEST_PASS();
}

static void test_timing(void)
{
	/* 10k file-name-like strings, mixed scripts, 14 px: report and bound loosely. */
	static const char* samples[] = {"report_2024.pdf", "photo (3)_118.jpg", "résumé-final.docx", "日本語_meeting.txt", "تقرير.docx", "नमस्ते_notes.md", "emoji_🙂.bin", "Архив_2021.zip"};
	u64 t0 = rg_time_ticks();
	u32 i, glyphs = 0;
	f64 ms;
	for (i = 0; i < 10000; i++)
	{
		RgTextLayout l;
		const char* s = samples[i % 8];
		scratch.used = 0;
		if (!rg_text_shape_utf8(&shaper, &set, s, strlen(s), 14.0f, &scratch, &l)) break;
		glyphs += l.count;
		if ((i & 255u) == 255u) rg_text_shape_context_reset(&shaper);
	}
	ms = rg_time_ticks_to_ms(rg_time_ticks() - t0);
	printf("  10000 names shaped: %u glyphs in %.1f ms (%.2f us per name), context arena %zu KB\n", glyphs, ms, ms * 1000.0 / 10000.0,
	       (size_t)(arena.used / 1024u));
	TEST_ASSERT(i == 10000, "all names shaped");
	TEST_ASSERT(ms < 500.0, "10k names under 500 ms (target 50 ms incl. raster; shaping share reported above)");
	TEST_PASS();
}

int main(void)
{
	rg_time_init();
	if (rg_malloc(MB(256)) != 0) return 1;
	arena = rg_arena_create(MB(192));
	scratch = rg_arena_create(MB(16));
	printf("rg_text_shape tests\n");
	if (!load_face(&segoe, "C:\\Windows\\Fonts\\segoeui.ttf", 0) || !load_face(&calibri, "C:\\Windows\\Fonts\\calibri.ttf", 0) ||
	    !load_face(&nirmala, "C:\\Windows\\Fonts\\Nirmala.ttc", 0) || !load_face(&leela, "C:\\Windows\\Fonts\\LeelawUI.ttf", 0) ||
	    !load_face(&yugothic, "C:\\Windows\\Fonts\\YuGothR.ttc", 0) || !load_face(&emoji, "C:\\Windows\\Fonts\\seguiemj.ttf", 0))
	{
		printf("  system fonts not found: shaping tests skipped\n");
		return 0;
	}
	printf("  faces loaded, arena %zu KB\n", (size_t)(arena.used / 1024u));
	memset(&set, 0, sizeof set);
	rg_text_font_set_add(&set, &segoe);    /* 0 primary */
	rg_text_font_set_add(&set, &calibri);  /* 1 (unused for fallback; Latin resolves in Segoe) */
	rg_text_font_set_add(&set, &nirmala);  /* 2 Devanagari */
	rg_text_font_set_add(&set, &leela);    /* 3 Thai */
	rg_text_font_set_add(&set, &yugothic); /* 4 CJK */
	rg_text_font_set_add(&set, &emoji);    /* 5 emoji */
	if (!rg_text_shape_context_init(&shaper, &arena, 0))
	{
		printf("  FAIL: shape context\n");
		return 1;
	}
	test_latin();
	test_arabic();
	test_devanagari_and_thai();
	test_fallback_cjk_emoji();
	test_hit_caret();
	test_copy_out_and_queries();
	test_timing();
	printf("%d passed, %d failed\n", g_tests_passed, g_tests_failed);
	rg_free();
	return g_tests_failed ? 1 : 0;
}
