// rg_text test suite
//
// Usage:
//   test_text.exe
//   test_text.exe test

#include "../src/rg_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// =============================================================================
// Test Framework
// =============================================================================

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

// =============================================================================
// Sample Font
// =============================================================================

static const char k_sample_rgfont[] =
    "rgfont 1\n"
    "atlas 64 32\n"
    "line_height 10\n"
    "ascent 8\n"
    "descent 2\n"
    "fallback 63\n"
    "glyph 63 15 0 4 7 0 1 4\n"
    "glyph 65 0 0 5 7 0 1 6\n"
    "glyph 66 5 0 6 7 0 1 7\n"
    "glyph U+00E9 11 0 4 7 0 1 5\n"
    "kerning 65 66 -1\n";

static int float_eq(f32 a, f32 b)
{
	return fabsf(a - b) <= 0.0001f;
}

static int load_sample_font(RgTextFont* font, RgTextGlyph* glyphs, u32 glyph_capacity,
                            RgTextKerning* kernings, u32 kerning_capacity)
{
	RgTextFontLoadDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.data = k_sample_rgfont;
	desc.data_size = sizeof(k_sample_rgfont) - 1u;
	desc.glyphs = glyphs;
	desc.glyph_capacity = glyph_capacity;
	desc.kernings = kernings;
	desc.kerning_capacity = kerning_capacity;
	return rg_text_font_load_rgfont(font, &desc);
}

static RgTextToken make_token(const char* text)
{
	RgTextToken token;
	token.ptr = text;
	token.len = strlen(text);
	return token;
}

static int load_rgfont_text(const char* data)
{
	RgTextGlyph glyphs[8];
	RgTextKerning kernings[8];
	RgTextFont font;
	RgTextFontLoadDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.data = data;
	desc.data_size = strlen(data);
	desc.glyphs = glyphs;
	desc.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	desc.kernings = kernings;
	desc.kerning_capacity = RG_ARRAY_COUNT(kernings);
	return rg_text_font_load_rgfont(&font, &desc);
}

// =============================================================================
// Tests
// =============================================================================

static void test_load_rgfont(void)
{
	RgTextGlyph glyphs[8];
	RgTextKerning kernings[4];
	RgTextFont font;

	TEST_ASSERT(load_sample_font(&font, glyphs, 8u, kernings, 4u), "load sample font");
	TEST_ASSERT(font.metrics.atlas_width == 64u, "atlas width");
	TEST_ASSERT(font.metrics.atlas_height == 32u, "atlas height");
	TEST_ASSERT(font.metrics.line_height == 10, "line height");
	TEST_ASSERT(font.metrics.ascent == 8, "ascent");
	TEST_ASSERT(font.metrics.descent == 2, "descent");
	TEST_ASSERT(font.fallback_codepoint == 63u, "fallback");
	TEST_ASSERT(font.glyph_count == 4u, "glyph count");
	TEST_ASSERT(font.kerning_count == 1u, "kerning count");
	TEST_ASSERT(rg_text_find_glyph(&font, 'A') != NULL, "find glyph");
	TEST_ASSERT(rg_text_find_kerning(&font, 'A', 'B') == -1, "find kerning");

	TEST_PASS();
}

static void test_measure_and_kerning(void)
{
	RgTextGlyph glyphs[8];
	RgTextKerning kernings[4];
	RgTextFont font;
	TEST_ASSERT(load_sample_font(&font, glyphs, 8u, kernings, 4u), "load sample font");

	RgTextSize size = rg_text_measure(&font, "AB", 2u, 1.0f);
	TEST_ASSERT(float_eq(size.width, 12.0f), "kerning width");
	TEST_ASSERT(float_eq(size.height, 10.0f), "single line height");

	size = rg_text_measure(&font, "A\nB", 3u, 1.0f);
	TEST_ASSERT(float_eq(size.width, 7.0f), "newline width");
	TEST_ASSERT(float_eq(size.height, 20.0f), "newline height");

	size = rg_text_measure(&font, "AB", 2u, 2.0f);
	TEST_ASSERT(float_eq(size.width, 24.0f), "scaled width");
	TEST_ASSERT(float_eq(size.height, 20.0f), "scaled height");

	TEST_PASS();
}

static void test_fallback_and_utf8(void)
{
	RgTextGlyph glyphs[8];
	RgTextKerning kernings[4];
	RgTextFont font;
	TEST_ASSERT(load_sample_font(&font, glyphs, 8u, kernings, 4u), "load sample font");

	RgTextSize size = rg_text_measure(&font, "AZ", 2u, 1.0f);
	TEST_ASSERT(float_eq(size.width, 10.0f), "missing glyph falls back");

	const char utf8_e[] = "\xC3\xA9";
	size = rg_text_measure(&font, utf8_e, sizeof(utf8_e) - 1u, 1.0f);
	TEST_ASSERT(float_eq(size.width, 5.0f), "utf8 glyph width");

	const char invalid_utf8[] = "\xC3";
	size = rg_text_measure(&font, invalid_utf8, sizeof(invalid_utf8) - 1u, 1.0f);
	TEST_ASSERT(float_eq(size.width, 4.0f), "invalid utf8 falls back");

	TEST_PASS();
}

static void test_build_quads(void)
{
	RgTextGlyph glyphs[8];
	RgTextKerning kernings[4];
	RgTextFont font;
	RgTextQuad quads[4];
	RgTextColor color = {1.0f, 0.5f, 0.25f, 1.0f};

	TEST_ASSERT(load_sample_font(&font, glyphs, 8u, kernings, 4u), "load sample font");

	size_t count = rg_text_build_quads(&font, "AB", 2u, 10.0f, 20.0f, 1.0f, color, quads, 4u);
	TEST_ASSERT(count == 2u, "quad count");
	TEST_ASSERT(float_eq(quads[0].x0, 10.0f), "A x0");
	TEST_ASSERT(float_eq(quads[0].y0, 21.0f), "A y0");
	TEST_ASSERT(float_eq(quads[0].x1, 15.0f), "A x1");
	TEST_ASSERT(float_eq(quads[0].y1, 28.0f), "A y1");
	TEST_ASSERT(float_eq(quads[0].u0, 0.0f), "A u0");
	TEST_ASSERT(float_eq(quads[0].u1, 5.0f / 64.0f), "A u1");
	TEST_ASSERT(float_eq(quads[1].x0, 15.0f), "B kerning x0");
	TEST_ASSERT(float_eq(quads[1].u0, 5.0f / 64.0f), "B u0");
	TEST_ASSERT(float_eq(quads[1].u1, 11.0f / 64.0f), "B u1");
	TEST_ASSERT(float_eq(quads[1].color.g, 0.5f), "color copied");

	TEST_PASS();
}

static void test_alignment(void)
{
	RgTextGlyph glyphs[8];
	RgTextKerning kernings[4];
	RgTextFont font;
	RgTextQuad quads[4];
	RgTextColor color = {1.0f, 1.0f, 1.0f, 1.0f};

	TEST_ASSERT(load_sample_font(&font, glyphs, 8u, kernings, 4u), "load sample font");

	RgTextBuildDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.font = &font;
	desc.text = "AB";
	desc.text_size = 2u;
	desc.x = 10.0f;
	desc.y = 0.0f;
	desc.scale = 1.0f;
	desc.align_width = 20.0f;
	desc.align = RG_TEXT_ALIGN_CENTER;
	desc.color = color;
	desc.quads = quads;
	desc.quad_capacity = 4u;

	size_t count = rg_text_build_quads_ex(&desc);
	TEST_ASSERT(count == 2u, "center quad count");
	TEST_ASSERT(float_eq(quads[0].x0, 14.0f), "center aligned x0");

	desc.align = RG_TEXT_ALIGN_RIGHT;
	count = rg_text_build_quads_ex(&desc);
	TEST_ASSERT(count == 2u, "right quad count");
	TEST_ASSERT(float_eq(quads[0].x0, 18.0f), "right aligned x0");

	TEST_PASS();
}

static void test_numeric_boundaries(void)
{
	u32 unsigned_value = 17u;
	i32 signed_value = 17;

	TEST_ASSERT(rg_text_parse_u32(make_token("0"), &unsigned_value) && unsigned_value == 0u,
	            "parse u32 zero");
	TEST_ASSERT(rg_text_parse_u32(make_token("4294967295"), &unsigned_value) &&
	            unsigned_value == 0xFFFFFFFFu, "parse u32 max decimal");
	TEST_ASSERT(rg_text_parse_u32(make_token("0xFFFFFFFF"), &unsigned_value) &&
	            unsigned_value == 0xFFFFFFFFu, "parse u32 max hex");

	unsigned_value = 17u;
	TEST_ASSERT(!rg_text_parse_u32(make_token("4294967296"), &unsigned_value) && unsigned_value == 17u,
	            "reject u32 decimal overflow without modifying output");
	TEST_ASSERT(!rg_text_parse_u32(make_token("0x100000000"), &unsigned_value) && unsigned_value == 17u,
	            "reject u32 hex overflow");
	TEST_ASSERT(!rg_text_parse_u32(make_token("U+100000000"), &unsigned_value) && unsigned_value == 17u,
	            "reject u32 codepoint overflow");
	TEST_ASSERT(!rg_text_parse_u32(make_token("999999999999999999999999"), &unsigned_value) &&
	            unsigned_value == 17u, "reject long u32 overflow");
	TEST_ASSERT(!rg_text_parse_u32(make_token("0x"), &unsigned_value) && unsigned_value == 17u,
	            "reject empty hex token");

	TEST_ASSERT(rg_text_parse_i32(make_token("2147483647"), &signed_value) &&
	            signed_value == 2147483647, "parse i32 max");
	TEST_ASSERT(rg_text_parse_i32(make_token("-2147483648"), &signed_value) &&
	            signed_value == (-2147483647 - 1), "parse i32 min");
	TEST_ASSERT(rg_text_parse_i32(make_token("+0"), &signed_value) && signed_value == 0,
	            "parse signed zero");

	signed_value = 17;
	TEST_ASSERT(!rg_text_parse_i32(make_token("2147483648"), &signed_value) && signed_value == 17,
	            "reject positive i32 overflow without modifying output");
	TEST_ASSERT(!rg_text_parse_i32(make_token("-2147483649"), &signed_value) && signed_value == 17,
	            "reject negative i32 overflow");
	TEST_ASSERT(!rg_text_parse_i32(make_token("+"), &signed_value) && signed_value == 17,
	            "reject sign-only i32");

	TEST_PASS();
}

static void test_parse_failures(void)
{
	RgTextGlyph glyphs[1];
	RgTextKerning kernings[1];
	RgTextFont font;

	TEST_ASSERT(!load_sample_font(&font, glyphs, 1u, kernings, 1u), "glyph capacity failure");

	static const char bad_rgfont[] =
	    "rgfont 1\n"
	    "atlas 64 32\n"
	    "line_height 10\n"
	    "glyph 65 0 0 5 7 0 1\n";

	RgTextFontLoadDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.data = bad_rgfont;
	desc.data_size = sizeof(bad_rgfont) - 1u;
	desc.glyphs = glyphs;
	desc.glyph_capacity = 1u;
	desc.kernings = kernings;
	desc.kerning_capacity = 1u;
	TEST_ASSERT(!rg_text_font_load_rgfont(&font, &desc), "bad glyph line failure");

	static const char* invalid_fonts[] = {
	    "atlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 2\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 0 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 0\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\n",
	    "rgfont 1\natlas 4294967296 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 2147483648\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 1114112 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 55296 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 -1 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 7 0 2 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1 extra\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\nunknown 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nfallback 1114112\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\nkerning 65 1114112 -1\n",
	};
	for (u32 i = 0u; i < RG_ARRAY_COUNT(invalid_fonts); i++)
	{
		TEST_ASSERT(!load_rgfont_text(invalid_fonts[i]), "reject malformed rgfont");
	}

	static const char valid_controls[] =
	    "\xEF\xBB\xBFrgfont 1\r\n"
	    "# comment\r\n"
	    "atlas 8 8 // dimensions\r\n"
	    "line_height 8\r\n"
	    "fallback 63\r\n"
	    "glyph 32 8 8 0 0 0 0 4\r\n"
	    "glyph 63 0 0 1 1 0 0 1";
	TEST_ASSERT(load_rgfont_text(valid_controls), "accept BOM, CRLF, comments, and no final newline");

	static const char valid_boundaries[] =
	    "rgfont 1\n"
	    "atlas 4294967295 4294967295\n"
	    "line_height 2147483647\n"
	    "ascent -2147483648\n"
	    "descent 2147483647\n"
	    "fallback 1114111\n"
	    "glyph 1114111 2147483647 2147483647 2147483647 2147483647 "
	    "-2147483648 2147483647 -2147483648\n";
	TEST_ASSERT(load_rgfont_text(valid_boundaries), "accept exact integer and Unicode boundaries");

	static const char kerning_without_storage[] =
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n"
	    "kerning 65 65 -1\n";
	memset(&desc, 0, sizeof(desc));
	desc.data = kerning_without_storage;
	desc.data_size = sizeof(kerning_without_storage) - 1u;
	desc.glyphs = glyphs;
	desc.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	TEST_ASSERT(!rg_text_font_load_rgfont(&font, &desc), "reject kerning without caller storage");

	TEST_PASS();
}

static void test_load_capacity_boundaries(void)
{
	static const char one_glyph[] =
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 4\n";
	static const char one_pair[] =
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 4\n"
	    "kerning 65 65 -2\n";
	static const char two_glyphs[] =
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 4\n"
	    "glyph 66 1 0 1 1 0 0 5\n";
	static const char two_pairs[] =
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 4\n"
	    "kerning 65 65 -2\nkerning 65 66 -1\n";
	static const struct
	{
		const char* data;
		int succeeds;
		int kerning_storage;
		int allow_kernings;
		u32 success_pair_count;
	} cases[] = {
		{one_glyph, 1, 1, 1, 0u},
		{one_glyph, 1, 0, 0, 0u},
		{one_pair, 1, 1, 1, 1u},
		{two_glyphs, 0, 1, 1, 0u},
		{two_pairs, 0, 1, 1, 0u},
		{one_pair, 0, 1, 0, 0u},
		{one_pair, 0, 0, 0, 0u},
	};

	for (u32 i = 0u; i < RG_ARRAY_COUNT(cases); i++)
	{
		struct { u32 before; RgTextGlyph values[1]; u32 after; } glyphs;
		struct { u32 before; RgTextKerning values[1]; u32 after; } kernings;
		memset(&glyphs, 0xA5, sizeof(glyphs));
		memset(&kernings, 0x5A, sizeof(kernings));
		glyphs.before = 0x12345678u;
		glyphs.after = 0x87654321u;
		kernings.before = 0x13579BDFu;
		kernings.after = 0xFDB97531u;

		RgTextFont font = {0};
		RgTextFontLoadDesc load = {0};
		load.data = cases[i].data;
		load.data_size = strlen(cases[i].data);
		load.glyphs = glyphs.values;
		load.glyph_capacity = RG_ARRAY_COUNT(glyphs.values);
		load.kernings = cases[i].kerning_storage ? kernings.values : NULL;
		load.kerning_capacity = cases[i].allow_kernings ? RG_ARRAY_COUNT(kernings.values) : 0u;
		int loaded = rg_text_font_load_rgfont(&font, &load);
		TEST_ASSERT(loaded == cases[i].succeeds, "zero/one-capacity load result");
		TEST_ASSERT(glyphs.before == 0x12345678u && glyphs.after == 0x87654321u &&
		            kernings.before == 0x13579BDFu && kernings.after == 0xFDB97531u,
		            "one-element storage guards remain intact");
		TEST_ASSERT(font.glyph_count <= load.glyph_capacity && font.kerning_count <= load.kerning_capacity,
		            "loaded counts stay within provided capacity, including failures");
		if (loaded)
		{
			TEST_ASSERT(font.glyph_count == 1u && font.kerning_count == cases[i].success_pair_count,
			            "successful one-element table counts");
			const RgTextGlyph* glyph = rg_text_find_glyph(&font, 'A');
			TEST_ASSERT(glyph && glyph->codepoint == 'A' && glyph->x_advance == 4,
			            "lookup in successfully loaded one-element glyph table");
			TEST_ASSERT(rg_text_find_glyph(&font, 'B') == NULL, "missing lookup in one-element glyph table");
			TEST_ASSERT(rg_text_find_kerning(&font, 'A', 'A') == (cases[i].success_pair_count ? -2 : 0),
			            "lookup in empty or one-element kerning table");
			TEST_ASSERT(rg_text_find_kerning(&font, 'A', 'B') == 0, "missing lookup in one-element kerning table");
		}
	}
	TEST_PASS();
}

static void test_unordered_lookup(void)
{
	static const char data[] =
	    "rgfont 1\natlas 8 8\nline_height 8\n"
	    "glyph 66 0 0 1 1 0 0 7\n"
	    "glyph 1114111 0 0 1 1 0 0 9\n"
	    "glyph 63 0 0 1 1 0 0 4\n"
	    "glyph 65 0 0 1 1 0 0 6\n"
	    "kerning 66 65 -3\nkerning 65 66 -2\n"
	    "kerning 1114111 1114111 -5\nkerning 65 63 -1\n"
	    "kerning 63 65 -4\n";
	RgTextGlyph glyphs[4];
	RgTextKerning kernings[5];
	RgTextFont font;
	RgTextFontLoadDesc load = {0};
	load.data = data;
	load.data_size = sizeof(data) - 1u;
	load.glyphs = glyphs;
	load.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	load.kernings = kernings;
	load.kerning_capacity = RG_ARRAY_COUNT(kernings);
	TEST_ASSERT(rg_text_font_load_rgfont(&font, &load), "load unordered records");
	TEST_ASSERT(rg_text_find_glyph(&font, 1114111u)->x_advance == 9, "find final Unicode key");
	TEST_ASSERT(rg_text_find_glyph(&font, 'A')->x_advance == 6, "find middle glyph key");
	TEST_ASSERT(rg_text_find_glyph(&font, 'B')->x_advance == 7, "find reordered glyph");
	TEST_ASSERT(rg_text_find_glyph(&font, 0u)->codepoint == '?', "fallback below first key");
	TEST_ASSERT(rg_text_find_kerning(&font, 'A', '?') == -1, "find first pair with same left key");
	TEST_ASSERT(rg_text_find_kerning(&font, 'A', 'B') == -2, "find second pair with same left key");
	TEST_ASSERT(rg_text_find_kerning(&font, 'B', 'A') == -3, "find reordered pair");
	TEST_ASSERT(rg_text_find_kerning(&font, '?', 'A') == -4, "find lowest pair key");
	TEST_ASSERT(rg_text_find_kerning(&font, 1114111u, 1114111u) == -5, "find highest pair key");
	TEST_ASSERT(rg_text_find_kerning(&font, 'A', 'A') == 0, "missing pair between keys");
	TEST_ASSERT(rg_text_find_kerning(&font, 0u, 0u) == 0, "missing pair below keys");
	TEST_ASSERT(rg_text_find_kerning(&font, 0xFFFFFFFFu, 0u) == 0, "missing pair above keys");

	// Zero-initialized, manually populated fonts remain valid without sorting.
	RgTextGlyph manual_glyphs[3] = {{0}};
	manual_glyphs[0].codepoint = 'B'; manual_glyphs[0].x_advance = 7;
	manual_glyphs[1].codepoint = '?'; manual_glyphs[1].x_advance = 4;
	manual_glyphs[2].codepoint = 'A'; manual_glyphs[2].x_advance = 6;
	RgTextKerning manual_kernings[3] = {{'B', 'A', -3}, {'A', 'B', -2}, {'?', 'A', -4}};
	RgTextFont manual = {0};
	manual.metrics.line_height = 8;
	manual.glyphs = manual_glyphs;
	manual.glyph_count = RG_ARRAY_COUNT(manual_glyphs);
	manual.kernings = manual_kernings;
	manual.kerning_count = RG_ARRAY_COUNT(manual_kernings);
	manual.fallback_codepoint = '?';
	TEST_ASSERT(rg_text_find_glyph(&manual, 'A') == &manual_glyphs[2], "find unsorted manual glyph");
	TEST_ASSERT(rg_text_find_kerning(&manual, 'A', 'B') == -2, "find unsorted manual pair");
	TEST_ASSERT(float_eq(rg_text_measure_cstr(&manual, "AB", 1.0f).width, 11.0f), "measure manual font");
	TEST_PASS();
}

static void test_duplicate_records(void)
{
	static const char* duplicates[] = {
	    "rgfont 1\natlas 8 8\nline_height 8\n"
	    "glyph 65 0 0 1 1 0 0 1\nglyph 66 0 0 1 1 0 0 1\nglyph 65 0 0 1 1 0 0 1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\n"
	    "glyph 65 0 0 1 1 0 0 1\nglyph 65 1 0 2 1 0 0 3\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n"
	    "kerning 65 66 -1\nkerning 66 65 -2\nkerning 65 66 -1\n",
	    "rgfont 1\natlas 8 8\nline_height 8\nglyph 65 0 0 1 1 0 0 1\n"
	    "kerning 65 66 -1\nkerning 65 66 -3\n",
	};
	for (u32 i = 0u; i < RG_ARRAY_COUNT(duplicates); i++)
	{
		TEST_ASSERT(!load_rgfont_text(duplicates[i]), "reject identical and conflicting duplicate keys");
	}
	TEST_PASS();
}

static void test_large_unordered_font(void)
{
	// Exercise the baker's maximum pair count, with unique keys in a shuffled order.
	static char data[2u * 1024u * 1024u];
	static RgTextGlyph glyphs[257];
	static RgTextKerning kernings[65536];
	size_t used = 0u;
	int written = snprintf(data, sizeof(data), "rgfont 1\natlas 8 8\nline_height 8\n");
	TEST_ASSERT(written > 0, "write large font header");
	used = (size_t)written;
	for (u32 i = 0u; i < RG_ARRAY_COUNT(glyphs); i++)
	{
		u32 key = (i * 73u) % 257u;
		written = snprintf(data + used, sizeof(data) - used,
		                   "glyph %u 0 0 1 1 0 0 %u\n", key + 32u, key % 13u + 1u);
		TEST_ASSERT(written > 0 && (size_t)written < sizeof(data) - used, "write large glyph record");
		used += (size_t)written;
	}
	for (u32 i = 0u; i < RG_ARRAY_COUNT(kernings); i++)
	{
		u32 key = (i * 40503u + 17u) & 65535u;
		written = snprintf(data + used, sizeof(data) - used, "kerning %u %u %d\n",
		                   key / 256u + 32u, key % 256u + 32u, (int)(key % 7u) - 3);
		TEST_ASSERT(written > 0 && (size_t)written < sizeof(data) - used, "write large pair record");
		used += (size_t)written;
	}
	RgTextFont font;
	RgTextFontLoadDesc load = {0};
	load.data = data;
	load.data_size = used;
	load.glyphs = glyphs;
	load.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	load.kernings = kernings;
	load.kerning_capacity = RG_ARRAY_COUNT(kernings);
	TEST_ASSERT(rg_text_font_load_rgfont(&font, &load), "load maximum unordered pair table");
	for (u32 key = 0u; key < RG_ARRAY_COUNT(glyphs); key++)
	{
		const RgTextGlyph* glyph = rg_text_find_glyph(&font, key + 32u);
		TEST_ASSERT(glyph && glyph->codepoint == key + 32u && glyph->x_advance == (i32)(key % 13u + 1u),
		            "preserve every shuffled glyph record");
	}
	for (u32 key = 0u; key < RG_ARRAY_COUNT(kernings); key++)
	{
		TEST_ASSERT(rg_text_find_kerning(&font, key / 256u + 32u, key % 256u + 32u) == (i32)(key % 7u) - 3,
		            "preserve every shuffled pair record");
	}
	TEST_ASSERT(rg_text_find_kerning(&font, 288u, 32u) == 0, "miss beyond maximum pair table");
	TEST_PASS();
}

static void test_fallback_kerning_and_alignment(void)
{
	static const char data[] =
	    "rgfont 1\natlas 8 8\nline_height 8\nfallback 63\n"
	    "glyph 65 0 0 1 1 0 0 6\nglyph 63 1 0 1 1 0 0 4\n"
	    "glyph 66 2 0 1 1 0 0 7\nglyph 32 0 0 0 0 0 0 3\n"
	    "kerning 63 65 -2\nkerning 65 63 -1\nkerning 65 66 -3\n"
	    "kerning 65 32 -1\nkerning 32 66 -2\n";
	RgTextGlyph glyphs[4];
	RgTextKerning kernings[5];
	RgTextFont font;
	RgTextFontLoadDesc load = {0};
	load.data = data;
	load.data_size = sizeof(data) - 1u;
	load.glyphs = glyphs;
	load.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	load.kernings = kernings;
	load.kerning_capacity = RG_ARRAY_COUNT(kernings);
	TEST_ASSERT(rg_text_font_load_rgfont(&font, &load), "load fallback kerning font");
	TEST_ASSERT(float_eq(rg_text_measure_cstr(&font, "ZA", 1.0f).width, 8.0f), "kern resolved left fallback");
	TEST_ASSERT(float_eq(rg_text_measure_cstr(&font, "AZ", 1.0f).width, 9.0f), "kern resolved right fallback");
	TEST_ASSERT(float_eq(rg_text_measure_cstr(&font, "\xFF" "A", 1.0f).width, 8.0f), "kern invalid UTF-8 fallback");

	RgTextQuad quads[4];
	RgTextColor white = {1.0f, 1.0f, 1.0f, 1.0f};
	RgTextBuildDesc build = {0};
	build.font = &font;
	build.text = "ZA\r\nAZ";
	build.text_size = 6u;
	build.x = 10.0f;
	build.y = 3.0f;
	build.scale = 1.0f;
	build.align = RG_TEXT_ALIGN_CENTER;
	build.align_width = 20.0f;
	build.color = white;
	build.quads = quads;
	build.quad_capacity = RG_ARRAY_COUNT(quads);
	TEST_ASSERT(rg_text_build_quads_ex(&build) == 4u, "emit multiline fallback quads");
	TEST_ASSERT(float_eq(quads[0].x0, 16.0f) && float_eq(quads[1].x0, 18.0f), "center first fallback line");
	TEST_ASSERT(float_eq(quads[2].x0, 15.5f) && float_eq(quads[3].x0, 20.5f), "center second fallback line");
	TEST_ASSERT(float_eq(quads[2].y0, 11.0f), "CRLF advances exactly one line");
	build.align_width = 0.0f;
	TEST_ASSERT(rg_text_build_quads_ex(&build) == 4u, "automatic multiline alignment width");
	TEST_ASSERT(float_eq(quads[0].x0, 10.5f) && float_eq(quads[2].x0, 10.0f), "auto width uses resolved kerning");
	build.align = RG_TEXT_ALIGN_RIGHT;
	build.align_width = 20.0f;
	TEST_ASSERT(rg_text_build_quads_ex(&build) == 4u, "right multiline alignment");
	TEST_ASSERT(float_eq(quads[0].x0, 22.0f) && float_eq(quads[2].x0, 21.0f), "right aligns each line");
	build.align = RG_TEXT_ALIGN_LEFT;
	TEST_ASSERT(rg_text_build_quads_ex(&build) == 4u, "left multiline alignment");
	TEST_ASSERT(float_eq(quads[0].x0, 10.0f) && float_eq(quads[1].x0, 12.0f) &&
	            float_eq(quads[2].x0, 10.0f), "left alignment ignores alignment width");
	build.quad_capacity = 1u;
	quads[1].x0 = 12345.0f;
	TEST_ASSERT(rg_text_build_quads_ex(&build) == 1u, "truncate to caller capacity");
	TEST_ASSERT(float_eq(quads[0].x0, 10.0f) && float_eq(quads[1].x0, 12345.0f), "truncation preserves prefix and bounds");
	TEST_ASSERT(float_eq(rg_text_measure_cstr(&font, "ZAZ", 2.0f).width, 22.0f), "scale fallback kerning on both sides");

	// An absent glyph without fallback breaks adjacency; spaces retain theirs.
	font.fallback_codepoint = 0u;
	TEST_ASSERT(float_eq(rg_text_measure_cstr(&font, "AZB", 1.0f).width, 13.0f), "do not kern across missing glyph");
	TEST_ASSERT(rg_text_build_quads(&font, "AZB", 3u, 0, 0, 1, white, quads, 4u) == 2u &&
	            float_eq(quads[1].x0, 6.0f), "missing glyph emits nothing and breaks kerning");
	TEST_ASSERT(rg_text_build_quads(&font, "A B", 3u, 0, 0, 1, white, quads, 4u) == 2u &&
	            float_eq(quads[1].x0, 6.0f), "zero-area space advances and participates in kerning");
	TEST_ASSERT(rg_text_build_quads(&font, "A\nB", 3u, 0, 0, 1, white, quads, 4u) == 2u &&
	            float_eq(quads[1].x0, 0.0f), "newline breaks kerning adjacency");
	TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	printf("Running rg_text tests...\n\n");

	test_load_rgfont();
	test_measure_and_kerning();
	test_fallback_and_utf8();
	test_build_quads();
	test_alignment();
	test_numeric_boundaries();
	test_parse_failures();
	test_load_capacity_boundaries();
	test_unordered_lookup();
	test_duplicate_records();
	test_large_unordered_font();
	test_fallback_kerning_and_alignment();

	printf("\nResults: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
	return g_tests_failed ? 1 : 0;
}
