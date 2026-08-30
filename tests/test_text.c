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

	printf("\nResults: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
	return g_tests_failed ? 1 : 0;
}
