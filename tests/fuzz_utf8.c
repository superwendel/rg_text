// libFuzzer target for UTF-8 decoding, measurement, and quad generation.

#include "../src/rg_text.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	RgTextGlyph glyphs[2] = {0};
	glyphs[0].codepoint = '?';
	glyphs[0].w = 1;
	glyphs[0].h = 1;
	glyphs[0].x_advance = 1;
	glyphs[1] = glyphs[0];
	glyphs[1].codepoint = RG_TEXT_REPLACEMENT_CODEPOINT;

	RgTextFont font = {0};
	font.metrics.atlas_width = 1u;
	font.metrics.atlas_height = 1u;
	font.metrics.line_height = 1;
	font.glyphs = glyphs;
	font.glyph_count = RG_ARRAY_COUNT(glyphs);
	font.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	font.fallback_codepoint = '?';

	RgTextQuad quads[64];
	RgTextColor color = {1.0f, 1.0f, 1.0f, 1.0f};
	(void)rg_text_measure(&font, (const char*)data, size, 1.0f);
	(void)rg_text_build_quads(&font, (const char*)data, size,
	                         0.0f, 0.0f, 1.0f, color,
	                         quads, RG_ARRAY_COUNT(quads));
	return 0;
}
