// rg_text - Bitmap font metrics, measurement, and quad generation
//
// Part of the Reverse Gravity (rg_) libraries.
// Single-header C99 library for renderer-neutral bitmap text layout.
//
// USAGE:
//   RgTextGlyph glyphs[256];
//   RgTextKerning kernings[64];
//   RgTextFontLoadDesc desc = {0};
//   desc.data = rgfont_data;
//   desc.data_size = rgfont_size;
//   desc.glyphs = glyphs;
//   desc.glyph_capacity = 256;
//   desc.kernings = kernings;
//   desc.kerning_capacity = 64;
//
//   RgTextFont font;
//   if (rg_text_font_load_rgfont(&font, &desc))
//   {
//       RgTextSize size = rg_text_measure(&font, "Hello", 5, 1.0f);
//   }
//
// RGFONT FORMAT:
//   rgfont 1
//   atlas <width> <height>
//   line_height <pixels>
//   ascent <pixels>
//   descent <pixels>
//   fallback <codepoint>
//   glyph <codepoint> <x> <y> <w> <h> <x_offset> <y_offset> <x_advance>
//   kerning <left_codepoint> <right_codepoint> <x_advance>
//
// NOTES:
//   - RGFONT is parsed from caller-owned memory. This header performs no file I/O.
//   - Glyph offsets are relative to the top-left text pen, not a baseline.
//   - This is a basic bitmap-font layout path, not a Unicode/OpenType shaper.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_TEXT_H
#define RG_TEXT_H

#include "rg_defs.h"
#include "rg_algo.h"

#include <stddef.h>
#include <string.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

#ifndef RG_TEXT_ASSERT
#include <assert.h>
#define RG_TEXT_ASSERT(x) assert(x)
#endif

#define RG_TEXT_REPLACEMENT_CODEPOINT 0xFFFDu

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

typedef enum RgTextAlign
{
	RG_TEXT_ALIGN_LEFT = 0,
	RG_TEXT_ALIGN_CENTER = 1,
	RG_TEXT_ALIGN_RIGHT = 2
} RgTextAlign;

typedef struct RgTextColor
{
	f32 r;
	f32 g;
	f32 b;
	f32 a;
} RgTextColor;

typedef struct RgTextMetrics
{
	u32 atlas_width;
	u32 atlas_height;
	i32 line_height;
	i32 ascent;
	i32 descent;
} RgTextMetrics;

typedef struct RgTextGlyph
{
	u32 codepoint;
	i32 x;
	i32 y;
	i32 w;
	i32 h;
	i32 x_offset;
	i32 y_offset;
	i32 x_advance;
} RgTextGlyph;

typedef struct RgTextKerning
{
	u32 left;
	u32 right;
	i32 x_advance;
} RgTextKerning;

typedef struct RgTextFont
{
	RgTextMetrics metrics;
	RgTextGlyph* glyphs;
	u32 glyph_count;
	u32 glyph_capacity;
	RgTextKerning* kernings;
	u32 kerning_count;
	u32 kerning_capacity;
	u32 fallback_codepoint;
	// Set by the loader. Keep zero for manually populated, unsorted arrays.
	// Clear before changing loaded glyph codepoints or kerning pair keys.
	u32 internal_lookup_flags;
} RgTextFont;

typedef struct RgTextFontLoadDesc
{
	const void* data;
	size_t data_size;
	RgTextGlyph* glyphs;
	u32 glyph_capacity;
	RgTextKerning* kernings;
	u32 kerning_capacity;
} RgTextFontLoadDesc;

typedef struct RgTextSize
{
	f32 width;
	f32 height;
} RgTextSize;

typedef struct RgTextQuad
{
	f32 x0;
	f32 y0;
	f32 x1;
	f32 y1;
	f32 u0;
	f32 v0;
	f32 u1;
	f32 v1;
	RgTextColor color;
} RgTextQuad;

typedef struct RgTextBuildDesc
{
	const RgTextFont* font;
	const char* text;
	size_t text_size;
	f32 x;
	f32 y;
	f32 scale;
	f32 align_width;
	RgTextAlign align;
	RgTextColor color;
	RgTextQuad* quads;
	size_t quad_capacity;
} RgTextBuildDesc;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Load RGFONT metrics from memory into caller-provided glyph arrays.
 * Arrays are sorted in place; duplicate glyph codepoints and kerning pairs
 * are rejected. On failure, the font and caller arrays may be modified.
 * @param font Font to initialize
 * @param desc Load descriptor
 * @return 1 on success, 0 on invalid data or insufficient capacity
 */
RGINLINE int rg_text_font_load_rgfont(RgTextFont* font, const RgTextFontLoadDesc* desc);

/**
 * @brief Clear a font object. Caller-owned glyph arrays are not freed.
 * @param font Font to clear
 */
RGINLINE void rg_text_font_free(RgTextFont* font);

/**
 * @brief Find a glyph by Unicode codepoint, applying fallback when needed.
 * @param font Font to search
 * @param codepoint Unicode codepoint
 * @return Glyph pointer or NULL
 */
RGINLINE const RgTextGlyph* rg_text_find_glyph(const RgTextFont* font, u32 codepoint);

/**
 * @brief Find a kerning advance between two codepoints.
 * @param font Font to search
 * @param left Left codepoint
 * @param right Right codepoint
 * @return Kerning advance in pixels
 */
RGINLINE i32 rg_text_find_kerning(const RgTextFont* font, u32 left, u32 right);

/**
 * @brief Measure UTF-8 text using bitmap font advances.
 * @param font Font to use
 * @param text UTF-8 bytes
 * @param text_size Byte length; pass 0 with non-NULL text for an empty string
 * @param scale Pixel scale
 * @return Measured width and height
 */
RGINLINE RgTextSize rg_text_measure(const RgTextFont* font, const char* text, size_t text_size, f32 scale);

/**
 * @brief Measure a NUL-terminated string.
 * @param font Font to use
 * @param text NUL-terminated UTF-8 string
 * @param scale Pixel scale
 * @return Measured width and height
 */
RGINLINE RgTextSize rg_text_measure_cstr(const RgTextFont* font, const char* text, f32 scale);

/**
 * @brief Emit glyph quads for UTF-8 text.
 * @param font Font to use
 * @param text UTF-8 bytes
 * @param text_size Byte length
 * @param x Top-left x
 * @param y Top-left y
 * @param scale Pixel scale
 * @param color Vertex color
 * @param quads Output quads
 * @param quad_capacity Output quad capacity
 * @return Number of quads written
 */
RGINLINE size_t rg_text_build_quads(const RgTextFont* font,
                                    const char* text,
                                    size_t text_size,
                                    f32 x,
                                    f32 y,
                                    f32 scale,
                                    RgTextColor color,
                                    RgTextQuad* quads,
                                    size_t quad_capacity);

/**
 * @brief Emit glyph quads with optional horizontal alignment.
 * @param desc Build descriptor
 * @return Number of quads written
 */
RGINLINE size_t rg_text_build_quads_ex(const RgTextBuildDesc* desc);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

typedef struct RgTextToken
{
	const char* ptr;
	size_t len;
} RgTextToken;

#define RG_TEXT_LOOKUP_SORTED 1u

RGINLINE int rg_text_glyph_key_less(const RgTextGlyph* a, const RgTextGlyph* b)
{
	return a->codepoint < b->codepoint;
}

RGINLINE int rg_text_kerning_key_less(const RgTextKerning* a, const RgTextKerning* b)
{
	return a->left < b->left || (a->left == b->left && a->right < b->right);
}

// rg_algo also generates stable-sort helpers with assertion-only variables.
// Scope these diagnostics to the generated code for release/custom-assert builds.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100 4189)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

RG_ALGO_DEFINE(RgTextGlyph, rg_text_glyph, rg_text_glyph_key_less)
RG_ALGO_DEFINE(RgTextKerning, rg_text_kerning, rg_text_kerning_key_less)

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

RGINLINE int rg_text_token_equal(RgTextToken token, const char* literal)
{
	size_t literal_len = strlen(literal);
	return token.len == literal_len && memcmp(token.ptr, literal, literal_len) == 0;
}

RGINLINE int rg_text_is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

RGINLINE int rg_text_is_digit(char c)
{
	return c >= '0' && c <= '9';
}

RGINLINE int rg_text_hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

RGINLINE int rg_text_next_token(const char** cursor, const char* end, RgTextToken* out_token)
{
	const char* p = *cursor;
	while (p < end && rg_text_is_space(*p))
	{
		p++;
	}

	if (p >= end)
	{
		*cursor = p;
		out_token->ptr = NULL;
		out_token->len = 0u;
		return 0;
	}

	const char* start = p;
	while (p < end && !rg_text_is_space(*p))
	{
		p++;
	}

	out_token->ptr = start;
	out_token->len = (size_t)(p - start);
	*cursor = p;
	return 1;
}

RGINLINE int rg_text_parse_u32(RgTextToken token, u32* out_value)
{
	u32 value = 0u;
	size_t i = 0u;
	int base = 10;

	if (token.len == 0u || token.ptr == NULL)
	{
		return 0;
	}

	if (token.len >= 2u && token.ptr[0] == 'U' && token.ptr[1] == '+')
	{
		i = 2u;
		base = 16;
	}
	else if (token.len >= 2u && token.ptr[0] == '0' && (token.ptr[1] == 'x' || token.ptr[1] == 'X'))
	{
		i = 2u;
		base = 16;
	}

	if (i >= token.len)
	{
		return 0;
	}

	for (; i < token.len; i++)
	{
		int digit = (base == 16) ? rg_text_hex_value(token.ptr[i]) : (rg_text_is_digit(token.ptr[i]) ? (token.ptr[i] - '0') : -1);
		if (digit < 0 || digit >= base)
		{
			return 0;
		}

		if (value > (0xFFFFFFFFu - (u32)digit) / (u32)base)
		{
			return 0;
		}

		value = value * (u32)base + (u32)digit;
	}

	*out_value = value;
	return 1;
}

RGINLINE int rg_text_parse_i32(RgTextToken token, i32* out_value)
{
	int sign = 1;
	u32 value = 0u;
	RgTextToken unsigned_token = token;

	if (token.len == 0u || token.ptr == NULL)
	{
		return 0;
	}

	if (token.ptr[0] == '-')
	{
		sign = -1;
		unsigned_token.ptr = token.ptr + 1;
		unsigned_token.len = token.len - 1u;
	}
	else if (token.ptr[0] == '+')
	{
		unsigned_token.ptr = token.ptr + 1;
		unsigned_token.len = token.len - 1u;
	}

	if (!rg_text_parse_u32(unsigned_token, &value))
	{
		return 0;
	}

	if ((sign > 0 && value > 0x7FFFFFFFu) ||
	    (sign < 0 && value > 0x80000000u))
	{
		return 0;
	}

	if (sign < 0)
	{
		*out_value = value == 0x80000000u ? (-2147483647 - 1) : -(i32)value;
	}
	else
	{
		*out_value = (i32)value;
	}
	return 1;
}

RGINLINE int rg_text_take_u32(const char** cursor, const char* end, u32* out_value)
{
	RgTextToken token;
	return rg_text_next_token(cursor, end, &token) && rg_text_parse_u32(token, out_value);
}

RGINLINE int rg_text_take_i32(const char** cursor, const char* end, i32* out_value)
{
	RgTextToken token;
	return rg_text_next_token(cursor, end, &token) && rg_text_parse_i32(token, out_value);
}

RGINLINE int rg_text_line_has_extra_tokens(const char* cursor, const char* end)
{
	RgTextToken extra;
	return rg_text_next_token(&cursor, end, &extra);
}

RGINLINE int rg_text_codepoint_valid(u32 codepoint)
{
	return codepoint <= 0x10FFFFu &&
	       !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

RGINLINE u32 rg_text_decode_utf8(const char* text, size_t text_size, size_t* io_offset)
{
	size_t i = *io_offset;
	const u8* bytes = (const u8*)text;
	u32 cp = 0u;

	if (i >= text_size)
	{
		return 0u;
	}

	u8 b0 = bytes[i];
	if (b0 < 0x80u)
	{
		*io_offset = i + 1u;
		return (u32)b0;
	}

	if ((b0 & 0xE0u) == 0xC0u && i + 1u < text_size)
	{
		u8 b1 = bytes[i + 1u];
		if ((b1 & 0xC0u) == 0x80u)
		{
			cp = ((u32)(b0 & 0x1Fu) << 6u) | (u32)(b1 & 0x3Fu);
			if (cp >= 0x80u)
			{
				*io_offset = i + 2u;
				return cp;
			}
		}
	}
	else if ((b0 & 0xF0u) == 0xE0u && i + 2u < text_size)
	{
		u8 b1 = bytes[i + 1u];
		u8 b2 = bytes[i + 2u];
		if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u)
		{
			cp = ((u32)(b0 & 0x0Fu) << 12u) |
			     ((u32)(b1 & 0x3Fu) << 6u) |
			     (u32)(b2 & 0x3Fu);
			if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu))
			{
				*io_offset = i + 3u;
				return cp;
			}
		}
	}
	else if ((b0 & 0xF8u) == 0xF0u && i + 3u < text_size)
	{
		u8 b1 = bytes[i + 1u];
		u8 b2 = bytes[i + 2u];
		u8 b3 = bytes[i + 3u];
		if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u)
		{
			cp = ((u32)(b0 & 0x07u) << 18u) |
			     ((u32)(b1 & 0x3Fu) << 12u) |
			     ((u32)(b2 & 0x3Fu) << 6u) |
			     (u32)(b3 & 0x3Fu);
			if (cp >= 0x10000u && cp <= 0x10FFFFu)
			{
				*io_offset = i + 4u;
				return cp;
			}
		}
	}

	*io_offset = i + 1u;
	return RG_TEXT_REPLACEMENT_CODEPOINT;
}

RGINLINE const RgTextGlyph* rg_text_find_glyph_exact(const RgTextFont* font, u32 codepoint)
{
	if (!font || !font->glyphs)
	{
		return NULL;
	}

	if (font->internal_lookup_flags & RG_TEXT_LOOKUP_SORTED)
	{
		u32 low = 0u;
		u32 high = font->glyph_count;
		while (low < high)
		{
			u32 mid = low + (high - low) / 2u;
			u32 key = font->glyphs[mid].codepoint;
			if (key < codepoint) low = mid + 1u;
			else if (key > codepoint) high = mid;
			else return &font->glyphs[mid];
		}
		return NULL;
	}

	for (u32 i = 0u; i < font->glyph_count; i++)
	{
		if (font->glyphs[i].codepoint == codepoint)
		{
			return &font->glyphs[i];
		}
	}

	return NULL;
}

RGINLINE const RgTextGlyph* rg_text_find_glyph(const RgTextFont* font, u32 codepoint)
{
	const RgTextGlyph* glyph = rg_text_find_glyph_exact(font, codepoint);
	if (glyph)
	{
		return glyph;
	}

	if (font && font->fallback_codepoint != codepoint)
	{
		return rg_text_find_glyph_exact(font, font->fallback_codepoint);
	}

	return NULL;
}

RGINLINE i32 rg_text_find_kerning(const RgTextFont* font, u32 left, u32 right)
{
	if (!font || !font->kernings)
	{
		return 0;
	}

	if (font->internal_lookup_flags & RG_TEXT_LOOKUP_SORTED)
	{
		u32 low = 0u;
		u32 high = font->kerning_count;
		while (low < high)
		{
			u32 mid = low + (high - low) / 2u;
			const RgTextKerning* pair = &font->kernings[mid];
			if (pair->left < left || (pair->left == left && pair->right < right)) low = mid + 1u;
			else if (pair->left > left || (pair->left == left && pair->right > right)) high = mid;
			else return pair->x_advance;
		}
		return 0;
	}

	for (u32 i = 0u; i < font->kerning_count; i++)
	{
		if (font->kernings[i].left == left && font->kernings[i].right == right)
		{
			return font->kernings[i].x_advance;
		}
	}

	return 0;
}

RGINLINE f32 rg_text_line_width(const RgTextFont* font, const char* text, size_t start, size_t end, f32 scale)
{
	size_t offset = start;
	f32 pen_x = 0.0f;
	const RgTextGlyph* previous = NULL;

	while (offset < end)
	{
		u32 cp = rg_text_decode_utf8(text, end, &offset);
		if (cp == '\n' || cp == '\r')
		{
			break;
		}

		const RgTextGlyph* glyph = rg_text_find_glyph(font, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}

		if (previous)
		{
			pen_x += (f32)rg_text_find_kerning(font, previous->codepoint, glyph->codepoint) * scale;
		}
		pen_x += (f32)glyph->x_advance * scale;
		previous = glyph;
	}

	return pen_x;
}

RGINLINE size_t rg_text_find_line_end(const char* text, size_t text_size, size_t start)
{
	size_t offset = start;
	while (offset < text_size)
	{
		char c = text[offset];
		if (c == '\n' || c == '\r')
		{
			break;
		}
		offset++;
	}
	return offset;
}

RGINLINE int rg_text_font_load_rgfont(RgTextFont* font, const RgTextFontLoadDesc* desc)
{
	RG_TEXT_ASSERT(font != NULL);
	RG_TEXT_ASSERT(desc != NULL);

	if (!font || !desc || !desc->data || desc->data_size == 0u ||
	    !desc->glyphs || desc->glyph_capacity == 0u)
	{
		return 0;
	}

	memset(font, 0, sizeof(*font));
	font->glyphs = desc->glyphs;
	font->glyph_capacity = desc->glyph_capacity;
	font->kernings = desc->kernings;
	font->kerning_capacity = desc->kerning_capacity;
	font->fallback_codepoint = (u32)'?';

	const char* data = (const char*)desc->data;
	const char* p = data;
	const char* end = data + desc->data_size;
	int saw_version = 0;

	if (desc->data_size >= 3u &&
	    (u8)data[0] == 0xEFu && (u8)data[1] == 0xBBu && (u8)data[2] == 0xBFu)
	{
		p += 3;
	}

	while (p < end)
	{
		const char* line_start = p;
		while (p < end && *p != '\n' && *p != '\r')
		{
			p++;
		}
		const char* line_end = p;
		if (p < end && *p == '\r') p++;
		if (p < end && *p == '\n') p++;

		const char* comment = line_start;
		while (comment < line_end)
		{
			if (*comment == '#')
			{
				line_end = comment;
				break;
			}
			if (*comment == '/' && comment + 1 < line_end && comment[1] == '/')
			{
				line_end = comment;
				break;
			}
			comment++;
		}

		const char* cursor = line_start;
		RgTextToken keyword;
		if (!rg_text_next_token(&cursor, line_end, &keyword))
		{
			continue;
		}

		if (rg_text_token_equal(keyword, "rgfont"))
		{
			u32 version = 0u;
			if (!rg_text_take_u32(&cursor, line_end, &version) || version != 1u ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}
			saw_version = 1;
		}
		else if (rg_text_token_equal(keyword, "atlas"))
		{
			if (!rg_text_take_u32(&cursor, line_end, &font->metrics.atlas_width) ||
			    !rg_text_take_u32(&cursor, line_end, &font->metrics.atlas_height) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}
		}
		else if (rg_text_token_equal(keyword, "line_height"))
		{
			if (!rg_text_take_i32(&cursor, line_end, &font->metrics.line_height) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}
		}
		else if (rg_text_token_equal(keyword, "ascent"))
		{
			if (!rg_text_take_i32(&cursor, line_end, &font->metrics.ascent) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}
		}
		else if (rg_text_token_equal(keyword, "descent"))
		{
			if (!rg_text_take_i32(&cursor, line_end, &font->metrics.descent) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}
		}
		else if (rg_text_token_equal(keyword, "fallback"))
		{
			if (!rg_text_take_u32(&cursor, line_end, &font->fallback_codepoint) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}
		}
		else if (rg_text_token_equal(keyword, "glyph"))
		{
			if (font->glyph_count >= font->glyph_capacity)
			{
				return 0;
			}

			RgTextGlyph glyph;
			memset(&glyph, 0, sizeof(glyph));
			if (!rg_text_take_u32(&cursor, line_end, &glyph.codepoint) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.x) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.y) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.w) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.h) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.x_offset) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.y_offset) ||
			    !rg_text_take_i32(&cursor, line_end, &glyph.x_advance) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}

			font->glyphs[font->glyph_count++] = glyph;
		}
		else if (rg_text_token_equal(keyword, "kerning"))
		{
			if (!font->kernings || font->kerning_count >= font->kerning_capacity)
			{
				return 0;
			}

			RgTextKerning kerning;
			memset(&kerning, 0, sizeof(kerning));
			if (!rg_text_take_u32(&cursor, line_end, &kerning.left) ||
			    !rg_text_take_u32(&cursor, line_end, &kerning.right) ||
			    !rg_text_take_i32(&cursor, line_end, &kerning.x_advance) ||
			    rg_text_line_has_extra_tokens(cursor, line_end))
			{
				return 0;
			}

			font->kernings[font->kerning_count++] = kerning;
		}
		else
		{
			return 0;
		}
	}

	// Keep the final counts explicitly bounded through validation and sorting,
	// including when GCC inlines this loader into callers with small arrays.
	const u32 glyph_count = font->glyph_count;
	const u32 kerning_count = font->kerning_count;
	if (!saw_version || font->metrics.atlas_width == 0u || font->metrics.atlas_height == 0u ||
	    font->metrics.line_height <= 0 || glyph_count == 0u ||
	    glyph_count > desc->glyph_capacity || kerning_count > desc->kerning_capacity)
	{
		return 0;
	}

	if (!rg_text_codepoint_valid(font->fallback_codepoint))
	{
		return 0;
	}

	for (u32 i = 0u; i < glyph_count; i++)
	{
		const RgTextGlyph* glyph = &font->glyphs[i];
		if (!rg_text_codepoint_valid(glyph->codepoint) ||
		    glyph->x < 0 || glyph->y < 0 || glyph->w < 0 || glyph->h < 0 ||
		    (u64)(u32)glyph->x + (u64)(u32)glyph->w > (u64)font->metrics.atlas_width ||
		    (u64)(u32)glyph->y + (u64)(u32)glyph->h > (u64)font->metrics.atlas_height)
		{
			return 0;
		}
	}

	for (u32 i = 0u; i < kerning_count; i++)
	{
		if (!rg_text_codepoint_valid(font->kernings[i].left) ||
		    !rg_text_codepoint_valid(font->kernings[i].right))
		{
			return 0;
		}
	}

	// Core introsort bounds load time to O(n log n), using a fixed local stack.
	rg_algo_sort_rg_text_glyph(font->glyphs, glyph_count);
	rg_algo_sort_rg_text_kerning(font->kernings, kerning_count);
	for (u32 i = 1u; i < glyph_count; i++)
	{
		if (font->glyphs[i - 1u].codepoint == font->glyphs[i].codepoint) return 0;
	}
	for (u32 i = 1u; i < kerning_count; i++)
	{
		if (font->kernings[i - 1u].left == font->kernings[i].left &&
		    font->kernings[i - 1u].right == font->kernings[i].right) return 0;
	}
	font->internal_lookup_flags = RG_TEXT_LOOKUP_SORTED;

	return 1;
}

RGINLINE void rg_text_font_free(RgTextFont* font)
{
	if (!font)
	{
		return;
	}

	memset(font, 0, sizeof(*font));
}

RGINLINE RgTextSize rg_text_measure(const RgTextFont* font, const char* text, size_t text_size, f32 scale)
{
	RgTextSize size;
	size.width = 0.0f;
	size.height = 0.0f;

	if (!font || !text || text_size == 0u || scale == 0.0f)
	{
		return size;
	}

	f32 max_width = 0.0f;
	f32 line_width = 0.0f;
	size_t line_count = 1u;
	size_t offset = 0u;
	const RgTextGlyph* previous = NULL;

	while (offset < text_size)
	{
		u32 cp = rg_text_decode_utf8(text, text_size, &offset);
		if (cp == '\r')
		{
			if (offset < text_size && text[offset] == '\n')
			{
				offset++;
			}
			if (line_width > max_width) max_width = line_width;
			line_width = 0.0f;
			line_count++;
			previous = NULL;
			continue;
		}
		if (cp == '\n')
		{
			if (line_width > max_width) max_width = line_width;
			line_width = 0.0f;
			line_count++;
			previous = NULL;
			continue;
		}

		const RgTextGlyph* glyph = rg_text_find_glyph(font, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}

		if (previous)
		{
			line_width += (f32)rg_text_find_kerning(font, previous->codepoint, glyph->codepoint) * scale;
		}
		line_width += (f32)glyph->x_advance * scale;
		previous = glyph;
	}

	if (line_width > max_width)
	{
		max_width = line_width;
	}

	size.width = max_width;
	size.height = (f32)line_count * (f32)font->metrics.line_height * scale;
	return size;
}

RGINLINE RgTextSize rg_text_measure_cstr(const RgTextFont* font, const char* text, f32 scale)
{
	return rg_text_measure(font, text, text ? strlen(text) : 0u, scale);
}

RGINLINE size_t rg_text_build_quads(const RgTextFont* font,
                                    const char* text,
                                    size_t text_size,
                                    f32 x,
                                    f32 y,
                                    f32 scale,
                                    RgTextColor color,
                                    RgTextQuad* quads,
                                    size_t quad_capacity)
{
	RgTextBuildDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.font = font;
	desc.text = text;
	desc.text_size = text_size;
	desc.x = x;
	desc.y = y;
	desc.scale = scale;
	desc.align = RG_TEXT_ALIGN_LEFT;
	desc.color = color;
	desc.quads = quads;
	desc.quad_capacity = quad_capacity;
	return rg_text_build_quads_ex(&desc);
}

RGINLINE size_t rg_text_build_quads_ex(const RgTextBuildDesc* desc)
{
	if (!desc || !desc->font || !desc->text || !desc->quads ||
	    desc->text_size == 0u || desc->quad_capacity == 0u || desc->scale == 0.0f)
	{
		return 0u;
	}

	const RgTextFont* font = desc->font;
	int aligned = desc->align == RG_TEXT_ALIGN_CENTER || desc->align == RG_TEXT_ALIGN_RIGHT;
	f32 align_width = desc->align_width;
	if (align_width <= 0.0f && aligned)
	{
		align_width = rg_text_measure(font, desc->text, desc->text_size, desc->scale).width;
	}

	size_t quad_count = 0u;
	size_t offset = 0u;
	f32 line_x = desc->x;
	f32 line_y = desc->y;
	f32 pen_x = 0.0f;
	const RgTextGlyph* previous = NULL;

	if (aligned)
	{
		size_t line_end = rg_text_find_line_end(desc->text, desc->text_size, 0u);
		f32 line_width = rg_text_line_width(font, desc->text, 0u, line_end, desc->scale);
		line_x += (align_width - line_width) * (desc->align == RG_TEXT_ALIGN_CENTER ? 0.5f : 1.0f);
	}

	while (offset < desc->text_size && quad_count < desc->quad_capacity)
	{
		u32 cp = rg_text_decode_utf8(desc->text, desc->text_size, &offset);
		if (cp == '\r' || cp == '\n')
		{
			if (cp == '\r' && offset < desc->text_size && desc->text[offset] == '\n')
			{
				offset++;
			}

			line_y += (f32)font->metrics.line_height * desc->scale;
			line_x = desc->x;
			if (aligned)
			{
				size_t line_end = rg_text_find_line_end(desc->text, desc->text_size, offset);
				f32 line_width = rg_text_line_width(font, desc->text, offset, line_end, desc->scale);
				line_x += (align_width - line_width) * (desc->align == RG_TEXT_ALIGN_CENTER ? 0.5f : 1.0f);
			}
			pen_x = 0.0f;
			previous = NULL;
			continue;
		}

		const RgTextGlyph* glyph = rg_text_find_glyph(font, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}

		if (previous)
		{
			pen_x += (f32)rg_text_find_kerning(font, previous->codepoint, glyph->codepoint) * desc->scale;
		}

		if (glyph->w > 0 && glyph->h > 0)
		{
			RgTextQuad* quad = &desc->quads[quad_count++];
			f32 gx = line_x + pen_x + (f32)glyph->x_offset * desc->scale;
			f32 gy = line_y + (f32)glyph->y_offset * desc->scale;
			quad->x0 = gx;
			quad->y0 = gy;
			quad->x1 = gx + (f32)glyph->w * desc->scale;
			quad->y1 = gy + (f32)glyph->h * desc->scale;
			quad->u0 = (f32)glyph->x / (f32)font->metrics.atlas_width;
			quad->v0 = (f32)glyph->y / (f32)font->metrics.atlas_height;
			quad->u1 = ((f32)glyph->x + (f32)glyph->w) / (f32)font->metrics.atlas_width;
			quad->v1 = ((f32)glyph->y + (f32)glyph->h) / (f32)font->metrics.atlas_height;
			quad->color = desc->color;
		}

		pen_x += (f32)glyph->x_advance * desc->scale;
		previous = glyph;
	}

	return quad_count;
}

#endif // RG_TEXT_H
