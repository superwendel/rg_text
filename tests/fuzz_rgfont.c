// libFuzzer target for the bounded RGFONT parser and layout path.

#include "../src/rg_text.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	RgTextGlyph glyphs[128];
	RgTextKerning kernings[256];
	RgTextFont font;
	RgTextFontLoadDesc load = {0};
	load.data = data;
	load.data_size = size;
	load.glyphs = glyphs;
	load.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	load.kernings = kernings;
	load.kerning_capacity = RG_ARRAY_COUNT(kernings);

	if (rg_text_font_load_rgfont(&font, &load))
	{
		RgTextQuad quads[64];
		RgTextColor color = {1.0f, 1.0f, 1.0f, 1.0f};
		(void)rg_text_find_glyph(&font, font.fallback_codepoint);
		(void)rg_text_measure(&font, (const char*)data, size, 1.0f);
		(void)rg_text_build_quads(&font, (const char*)data, size,
		                         0.0f, 0.0f, 1.0f, color,
		                         quads, RG_ARRAY_COUNT(quads));
	}

	return 0;
}
