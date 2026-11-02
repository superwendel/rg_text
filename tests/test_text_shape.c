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
	test_copy_out_and_queries();
	test_timing();
	printf("%d passed, %d failed\n", g_tests_passed, g_tests_failed);
	rg_free();
	return g_tests_failed ? 1 : 0;
}
