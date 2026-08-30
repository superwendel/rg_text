// End-to-end validation for an Inter ASCII bake.

#include "../src/rg_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static unsigned char* read_file(const char* path, size_t* out_size)
{
	FILE* file = fopen(path, "rb");
	if (!file || fseek(file, 0, SEEK_END) != 0)
	{
		if (file) fclose(file);
		return NULL;
	}
	long end = ftell(file);
	if (end < 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return NULL;
	}

	size_t size = (size_t)end;
	unsigned char* data = (unsigned char*)malloc(size ? size : 1u);
	if (!data || fread(data, 1, size, file) != size)
	{
		free(data);
		fclose(file);
		return NULL;
	}

	fclose(file);
	*out_size = size;
	return data;
}

static int make_asset_path(char* path, size_t path_size, const char* base, const char* extension)
{
	int path_length = snprintf(path, path_size, "%s%s", base, extension);
	return path_length >= 0 && (size_t)path_length < path_size;
}

static int load_font_from_base(const char* base,
                               RgTextFont* font,
                               RgTextGlyph* glyphs,
                               u32 glyph_capacity,
                               RgTextKerning* kernings,
                               u32 kerning_capacity)
{
	char path[1024];
	if (!make_asset_path(path, sizeof(path), base, ".font"))
	{
		return 0;
	}

	size_t data_size = 0u;
	unsigned char* data = read_file(path, &data_size);
	if (!data)
	{
		return 0;
	}

	RgTextFontLoadDesc desc = {0};
	desc.data = data;
	desc.data_size = data_size;
	desc.glyphs = glyphs;
	desc.glyph_capacity = glyph_capacity;
	desc.kernings = kernings;
	desc.kerning_capacity = kerning_capacity;
	int loaded = rg_text_font_load_rgfont(font, &desc);
	free(data);
	return loaded;
}

static int metrics_equal(const RgTextMetrics* a, const RgTextMetrics* b)
{
	return a->atlas_width == b->atlas_width && a->atlas_height == b->atlas_height &&
	       a->line_height == b->line_height && a->ascent == b->ascent &&
	       a->descent == b->descent;
}

static int glyph_equal(const RgTextGlyph* a, const RgTextGlyph* b)
{
	return a->codepoint == b->codepoint && a->x == b->x && a->y == b->y &&
	       a->w == b->w && a->h == b->h && a->x_offset == b->x_offset &&
	       a->y_offset == b->y_offset && a->x_advance == b->x_advance;
}

static int compare_no_kerning_outputs(const char* kerned_base, const char* no_kerning_base)
{
	static RgTextGlyph kerned_glyphs[2048];
	static RgTextGlyph no_kerning_glyphs[2048];
	static RgTextKerning kernings[65536];
	static RgTextKerning unused_kerning[1];
	RgTextFont kerned_font;
	RgTextFont no_kerning_font;
	if (!load_font_from_base(kerned_base, &kerned_font, kerned_glyphs,
	                         RG_ARRAY_COUNT(kerned_glyphs), kernings,
	                         RG_ARRAY_COUNT(kernings)) ||
	    !load_font_from_base(no_kerning_base, &no_kerning_font, no_kerning_glyphs,
	                         RG_ARRAY_COUNT(no_kerning_glyphs), unused_kerning,
	                         RG_ARRAY_COUNT(unused_kerning)))
	{
		fprintf(stderr, "Failed to load outputs for no-kerning comparison.\n");
		return 0;
	}

	if (kerned_font.kerning_count == 0u || no_kerning_font.kerning_count != 0u ||
	    kerned_font.glyph_count != no_kerning_font.glyph_count ||
	    kerned_font.fallback_codepoint != no_kerning_font.fallback_codepoint ||
	    !metrics_equal(&kerned_font.metrics, &no_kerning_font.metrics))
	{
		fprintf(stderr, "No-kerning output changed the font contract.\n");
		return 0;
	}
	for (u32 i = 0u; i < kerned_font.glyph_count; i++)
	{
		if (!glyph_equal(&kerned_font.glyphs[i], &no_kerning_font.glyphs[i]))
		{
			fprintf(stderr, "No-kerning output changed glyph %u.\n", i);
			return 0;
		}
	}

	char kerned_rgba_path[1024];
	char no_kerning_rgba_path[1024];
	if (!make_asset_path(kerned_rgba_path, sizeof(kerned_rgba_path), kerned_base,
	                     ".rgba") ||
	    !make_asset_path(no_kerning_rgba_path, sizeof(no_kerning_rgba_path),
	                     no_kerning_base, ".rgba"))
	{
		fprintf(stderr, "Output base path is too long.\n");
		return 0;
	}
	size_t kerned_rgba_size = 0u;
	size_t no_kerning_rgba_size = 0u;
	unsigned char* kerned_rgba = read_file(kerned_rgba_path, &kerned_rgba_size);
	unsigned char* no_kerning_rgba =
	    read_file(no_kerning_rgba_path, &no_kerning_rgba_size);
	int equal = kerned_rgba && no_kerning_rgba &&
	            kerned_rgba_size == no_kerning_rgba_size &&
	            memcmp(kerned_rgba, no_kerning_rgba, kerned_rgba_size) == 0;
	free(no_kerning_rgba);
	free(kerned_rgba);
	if (!equal)
	{
		fprintf(stderr, "No-kerning output changed atlas bytes.\n");
		return 0;
	}

	puts("No-kerning output differs only by its omitted kerning records.");
	return 1;
}

static int validate_large_no_kerning_output(const char* base)
{
	static RgTextGlyph glyphs[2048];
	static RgTextKerning unused_kerning[1];
	RgTextFont font;
	if (!load_font_from_base(base, &font, glyphs, RG_ARRAY_COUNT(glyphs),
	                         unused_kerning, RG_ARRAY_COUNT(unused_kerning)))
	{
		fprintf(stderr, "Failed to load large no-kerning output.\n");
		return 0;
	}

	char rgba_path[1024];
	if (!make_asset_path(rgba_path, sizeof(rgba_path), base, ".rgba"))
	{
		return 0;
	}
	size_t rgba_size = 0u;
	unsigned char* rgba = read_file(rgba_path, &rgba_size);
	u64 width = (u64)font.metrics.atlas_width;
	u64 height = (u64)font.metrics.atlas_height;
	int size_valid = width == 512u && height != 0u && height <= 16384u &&
	                 (height & (height - 1u)) == 0u &&
	                 width <= UINT64_MAX / height && width * height <= UINT64_MAX / 4u &&
	                 width * height * 4u <= (u64)SIZE_MAX &&
	                 (u64)rgba_size == width * height * 4u;
	int glyphs_valid = 1;
	for (u32 i = 0u; i < font.glyph_count; i++)
	{
		const RgTextGlyph* glyph = &font.glyphs[i];
		if ((i != 0u && font.glyphs[i - 1u].codepoint >= glyph->codepoint) ||
		    glyph->w < 0 || glyph->h < 0 || (glyph->w == 0) != (glyph->h == 0) ||
		    (glyph->w > 0 &&
		     (glyph->x < 0 || glyph->y < 0 ||
		      (i64)glyph->x + (i64)glyph->w > (i64)font.metrics.atlas_width ||
		      (i64)glyph->y + (i64)glyph->h > (i64)font.metrics.atlas_height)))
		{
			glyphs_valid = 0;
			break;
		}
	}
	int has_late_coverage = 0;
	if (rgba && size_valid && glyphs_valid)
	{
		for (u32 i = 512u; i < font.glyph_count && !has_late_coverage; i++)
		{
			const RgTextGlyph* glyph = &font.glyphs[i];
			for (i32 y = glyph->y; y < glyph->y + glyph->h && !has_late_coverage; y++)
			{
				for (i32 x = glyph->x; x < glyph->x + glyph->w; x++)
				{
					size_t offset =
					    ((size_t)y * font.metrics.atlas_width + (size_t)x) * 4u;
					if (rgba[offset + 3u] != 0u)
					{
						has_late_coverage = 1;
						break;
					}
				}
			}
		}
	}
	int valid = rgba && size_valid && font.glyph_count > 512u &&
	            font.glyph_count <= 2048u && font.kerning_count == 0u &&
	            glyphs_valid && has_late_coverage && font.metrics.line_height == 16 &&
	            font.metrics.ascent >= 0 && font.metrics.descent >= 0 &&
	            (i64)font.metrics.ascent + (i64)font.metrics.descent == 16 &&
	            rg_text_find_glyph_exact(&font, font.fallback_codepoint) != NULL;
	free(rgba);
	if (!valid)
	{
		fprintf(stderr,
		        "Large no-kerning output failed validation (glyphs=%u, kernings=%u).\n",
		        font.glyph_count, font.kerning_count);
		return 0;
	}

	printf("Large no-kerning output passed (%u glyphs).\n", font.glyph_count);
	return 1;
}

int main(int argc, char** argv)
{
	if (argc == 4 && strcmp(argv[2], "--compare-no-kerning") == 0)
	{
		return compare_no_kerning_outputs(argv[1], argv[3]) ? 0 : 1;
	}
	if (argc == 3 && strcmp(argv[2], "--no-kerning-large") == 0)
	{
		return validate_large_no_kerning_output(argv[1]) ? 0 : 1;
	}
	if (argc < 2 || argc > 3)
	{
		fprintf(stderr,
		        "Usage: test_bake_output.exe <output_base> [--golden|--no-kerning|--no-kerning-large|--expect-invalid-atlas|--simulate-truncated-atlas]\n"
		        "       test_bake_output.exe <kerned_base> --compare-no-kerning <no_kerning_base>\n");
		return 1;
	}
	int golden = 0;
	int expect_kerning = 1;
	int expect_invalid_atlas = 0;
	int simulate_truncated_atlas = 0;
	if (argc == 3)
	{
		if (strcmp(argv[2], "--golden") == 0)
		{
			golden = 1;
		}
		else if (strcmp(argv[2], "--no-kerning") == 0)
		{
			expect_kerning = 0;
		}
		else if (strcmp(argv[2], "--expect-invalid-atlas") == 0)
		{
			expect_invalid_atlas = 1;
		}
		else if (strcmp(argv[2], "--simulate-truncated-atlas") == 0)
		{
			expect_invalid_atlas = 1;
			simulate_truncated_atlas = 1;
		}
		else
		{
			fprintf(stderr, "Unknown verifier option: %s\n", argv[2]);
			return 1;
		}
	}

	char font_path[1024];
	char rgba_path[1024];
	int font_path_length = snprintf(font_path, sizeof(font_path), "%s.font", argv[1]);
	int rgba_path_length = snprintf(rgba_path, sizeof(rgba_path), "%s.rgba", argv[1]);
	if (font_path_length < 0 || (size_t)font_path_length >= sizeof(font_path) ||
	    rgba_path_length < 0 || (size_t)rgba_path_length >= sizeof(rgba_path))
	{
		fprintf(stderr, "Output base path is too long.\n");
		return 1;
	}

	size_t font_size = 0u;
	size_t rgba_size = 0u;
	unsigned char* font_data = read_file(font_path, &font_size);
	unsigned char* rgba = read_file(rgba_path, &rgba_size);
	if (!font_data || !rgba)
	{
		fprintf(stderr, "Failed to read baked output.\n");
		free(rgba);
		free(font_data);
		return 1;
	}
	if (simulate_truncated_atlas)
	{
		if (rgba_size == 0u)
		{
			fprintf(stderr, "Cannot simulate truncation of an empty atlas.\n");
			free(rgba);
			free(font_data);
			return 1;
		}
		rgba_size--;
	}

	static RgTextGlyph glyphs[128];
	static RgTextKerning kernings[16384];
	RgTextFontLoadDesc desc = {0};
	desc.data = font_data;
	desc.data_size = font_size;
	desc.glyphs = glyphs;
	desc.glyph_capacity = RG_ARRAY_COUNT(glyphs);
	desc.kernings = kernings;
	desc.kerning_capacity = RG_ARRAY_COUNT(kernings);

	RgTextFont font;
	if (!rg_text_font_load_rgfont(&font, &desc))
	{
		fprintf(stderr, "Baked RGFONT metrics did not load.\n");
		free(rgba);
		free(font_data);
		return 1;
	}

	int atlas_shape_valid = font.metrics.atlas_width == 256u &&
	                        font.metrics.atlas_height != 0u &&
	                        font.metrics.atlas_height <= 16384u &&
	                        (font.metrics.atlas_height & (font.metrics.atlas_height - 1u)) == 0u;
	u64 atlas_width64 = (u64)font.metrics.atlas_width;
	u64 atlas_height64 = (u64)font.metrics.atlas_height;
	if (!atlas_shape_valid || atlas_width64 > UINT64_MAX / atlas_height64)
	{
		fprintf(stderr, "Baked output declared invalid atlas dimensions.\n");
		free(rgba);
		free(font_data);
		return expect_invalid_atlas ? 0 : 1;
	}
	u64 pixel_count = atlas_width64 * atlas_height64;
	if (pixel_count > UINT64_MAX / 4u || pixel_count * 4u > (u64)SIZE_MAX)
	{
		fprintf(stderr, "Baked output atlas byte size overflowed.\n");
		free(rgba);
		free(font_data);
		return expect_invalid_atlas ? 0 : 1;
	}
	u64 expected_size = pixel_count * 4u;
	if ((u64)rgba_size != expected_size)
	{
		fprintf(stderr, "Baked atlas has %zu bytes; expected %llu.\n",
		        rgba_size, (unsigned long long)expected_size);
		free(rgba);
		free(font_data);
		return expect_invalid_atlas ? 0 : 1;
	}

	int has_coverage = 0;
	for (size_t i = 3u; i < rgba_size; i += 4u)
	{
		if (rgba[i] != 0u)
		{
			has_coverage = 1;
			break;
		}
	}

	int glyphs_sorted = 1;
	for (u32 i = 1u; i < font.glyph_count; i++)
	{
		if (font.glyphs[i - 1u].codepoint >= font.glyphs[i].codepoint)
		{
			glyphs_sorted = 0;
			break;
		}
	}

	int rectangles_valid = 1;
	int every_glyph_has_coverage = 1;
	int has_foreground = 0;
	int has_shadow = 0;
	for (u32 i = 0u; i < font.glyph_count; i++)
	{
		const RgTextGlyph* glyph = &font.glyphs[i];
		if (glyph->w < 0 || glyph->h < 0 ||
		    (glyph->w == 0) != (glyph->h == 0) ||
		    (glyph->w > 0 &&
		     ((u64)(u32)glyph->x + (u64)(u32)glyph->w > font.metrics.atlas_width ||
		      (u64)(u32)glyph->y + (u64)(u32)glyph->h > font.metrics.atlas_height)))
		{
			rectangles_valid = 0;
			break;
		}

		for (u32 j = 0u; j < i && glyph->w > 0 && glyph->h > 0; j++)
		{
			const RgTextGlyph* other = &font.glyphs[j];
			if (other->w > 0 && other->h > 0 &&
			    glyph->x < other->x + other->w && other->x < glyph->x + glyph->w &&
			    glyph->y < other->y + other->h && other->y < glyph->y + glyph->h)
			{
				rectangles_valid = 0;
				break;
			}
		}

		if (glyph->w > 0 && glyph->h > 0)
		{
			int glyph_has_coverage = 0;
			for (i32 y = glyph->y; y < glyph->y + glyph->h; y++)
			{
				for (i32 x = glyph->x; x < glyph->x + glyph->w; x++)
				{
					size_t offset = ((size_t)y * font.metrics.atlas_width + (size_t)x) * 4u;
					if (rgba[offset + 3u] != 0u)
					{
						glyph_has_coverage = 1;
						if (rgba[offset] == 255u && rgba[offset + 1u] == 255u &&
						    rgba[offset + 2u] == 255u)
						{
							has_foreground = 1;
						}
						if (rgba[offset] == 5u && rgba[offset + 1u] == 5u &&
						    rgba[offset + 2u] == 8u)
						{
							has_shadow = 1;
						}
					}
				}
			}
			if (!glyph_has_coverage)
			{
				every_glyph_has_coverage = 0;
			}
		}
	}

	int pixels_stay_in_rectangles = 1;
	for (u32 y = 0u; y < font.metrics.atlas_height && pixels_stay_in_rectangles; y++)
	{
		for (u32 x = 0u; x < font.metrics.atlas_width; x++)
		{
			size_t offset = ((size_t)y * font.metrics.atlas_width + x) * 4u;
			if (rgba[offset + 3u] == 0u)
			{
				continue;
			}

			int inside = 0;
			for (u32 i = 0u; i < font.glyph_count; i++)
			{
				const RgTextGlyph* glyph = &font.glyphs[i];
				if ((i32)x >= glyph->x && (i32)x < glyph->x + glyph->w &&
				    (i32)y >= glyph->y && (i32)y < glyph->y + glyph->h)
				{
					inside = 1;
					break;
				}
			}
			if (!inside)
			{
				pixels_stay_in_rectangles = 0;
				break;
			}
		}
	}

	int kerning_valid = expect_kerning ? font.kerning_count != 0u : font.kerning_count == 0u;
	int has_negative_kerning = 0;
	u64 previous_pair = 0u;
	for (u32 i = 0u; i < font.kerning_count; i++)
	{
		const RgTextKerning* kerning = &font.kernings[i];
		u64 pair = ((u64)kerning->left << 32u) | (u64)kerning->right;
		if (kerning->x_advance == 0 ||
		    !rg_text_find_glyph_exact(&font, kerning->left) ||
		    !rg_text_find_glyph_exact(&font, kerning->right) ||
		    (i != 0u && pair <= previous_pair))
		{
			kerning_valid = 0;
			break;
		}
		if (kerning->x_advance < 0)
		{
			has_negative_kerning = 1;
		}
		previous_pair = pair;
	}

	const RgTextGlyph* fallback = rg_text_find_glyph_exact(&font, font.fallback_codepoint);
	const RgTextGlyph* space = rg_text_find_glyph_exact(&font, ' ');
	const RgTextGlyph* left_paren = rg_text_find_glyph_exact(&font, '(');
	const RgTextGlyph* uppercase_a = rg_text_find_glyph_exact(&font, 'A');
	const RgTextGlyph* underscore = rg_text_find_glyph_exact(&font, '_');
	const RgTextGlyph* lowercase_g = rg_text_find_glyph_exact(&font, 'g');
	const RgTextGlyph* lowercase_o = rg_text_find_glyph_exact(&font, 'o');
	const RgTextGlyph* lowercase_t = rg_text_find_glyph_exact(&font, 't');
	int av_kerning = rg_text_find_kerning(&font, 'A', 'V');
	int to_kerning = rg_text_find_kerning(&font, 'T', 'o');
	int underscore_j_kerning = rg_text_find_kerning(&font, '_', 'j');
	int golden_valid = 1;
	if (golden)
	{
		golden_valid =
		    space && space->w == 0 && space->h == 0 && space->x_offset == 0 &&
		    space->y_offset == 13 && space->x_advance == 4 &&
		    left_paren && left_paren->w == 6 && left_paren->h == 16 &&
		    left_paren->x_offset == 1 && left_paren->y_offset == 0 &&
		    left_paren->x_advance == 6 &&
		    uppercase_a && uppercase_a->w == 12 && uppercase_a->h == 13 &&
		    uppercase_a->x_offset == 0 && uppercase_a->y_offset == 1 &&
		    uppercase_a->x_advance == 11 &&
		    underscore && underscore->w == 9 && underscore->h == 4 &&
		    underscore->x_offset == 0 && underscore->y_offset == 12 &&
		    underscore->x_advance == 7 &&
		    lowercase_g && lowercase_g->w == 10 && lowercase_g->h == 13 &&
		    lowercase_g->x_offset == 0 && lowercase_g->y_offset == 4 &&
		    lowercase_g->x_advance == 10 &&
		    lowercase_o && lowercase_o->w == 10 && lowercase_o->h == 10 &&
		    lowercase_o->x_offset == 0 && lowercase_o->y_offset == 4 &&
		    lowercase_o->x_advance == 10 &&
		    lowercase_t && lowercase_t->w == 7 && lowercase_t->h == 12 &&
		    lowercase_t->x_offset == 0 && lowercase_t->y_offset == 2 &&
		    lowercase_t->x_advance == 5 &&
		    font.kerning_count == 616u && av_kerning == -1 && to_kerning == -1 &&
		    underscore_j_kerning == 2 && font.metrics.ascent == 13 &&
		    font.metrics.descent == 3;
	}
	if (expect_invalid_atlas || !has_coverage ||
	    !rg_text_find_glyph_exact(&font, 'A') || !rg_text_find_glyph_exact(&font, 'V') ||
	    !fallback || !space || !left_paren || !uppercase_a || !underscore ||
	    !lowercase_g || !lowercase_o || !lowercase_t ||
	    space->w != 0 || space->h != 0 || space->x_advance <= 0 ||
	    font.glyph_count != 95u || font.fallback_codepoint != '?' ||
	    !glyphs_sorted || !atlas_shape_valid || !rectangles_valid ||
	    !every_glyph_has_coverage || !pixels_stay_in_rectangles ||
	    !has_foreground || !has_shadow || !kerning_valid ||
	    (expect_kerning && (!has_negative_kerning || av_kerning >= 0 ||
	                        to_kerning >= 0 || underscore_j_kerning <= 0)) ||
	    (!expect_kerning && (av_kerning != 0 || to_kerning != 0 ||
	                         underscore_j_kerning != 0)) ||
	    !golden_valid ||
	    font.metrics.line_height != 16 ||
	    font.metrics.ascent <= 0 || font.metrics.descent < 0 ||
	    (i64)font.metrics.ascent + (i64)font.metrics.descent !=
	        (i64)font.metrics.line_height)
	{
		fprintf(stderr,
		        "Baked output validation failed (bytes=%zu expected=%llu glyphs=%u kernings=%u fallback=%u AV=%d To=%d _j=%d).\n",
		        rgba_size, (unsigned long long)expected_size, font.glyph_count,
		        font.kerning_count, font.fallback_codepoint, av_kerning, to_kerning,
		        underscore_j_kerning);
		free(rgba);
		free(font_data);
		return 1;
	}

	printf("Baked output passed%s (%u glyphs, %u kernings, AV=%d, To=%d, _j=%d, fallback=U+%04X).\n",
	       golden ? " pinned golden" : "",
	       font.glyph_count, font.kerning_count, av_kerning, to_kerning,
	       underscore_j_kerning,
	       font.fallback_codepoint);
	free(rgba);
	free(font_data);
	return 0;
}
