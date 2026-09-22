// Allocation-free runtime layout benchmark; setup allocates caller-owned storage.
// Usage: bench_text.exe [optional.font]
// Compile with optimization. Each run checks indexed results against linear lookup.

#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "../src/rg_text.h"
#include "rg_time.h"

#include <stdio.h>
#include <stdlib.h>

#define BENCH_TEXT_SIZE 2000u
#define BENCH_GLYPH_CAPACITY 65536u
#define BENCH_PAIR_CAPACITY 65536u

static volatile double g_checksum;
static volatile RgTextAlign g_alignment = RG_TEXT_ALIGN_LEFT;

static RG_NOINLINE size_t bench_build(RgTextBuildDesc* build)
{
	// Keep the descriptor dynamic, as it is in a UI renderer.
	build->align = g_alignment;
	return rg_text_build_quads_ex(build);
}

static int bench_font(const char* label, const char* data, size_t data_size)
{
	RgTextGlyph* glyphs = (RgTextGlyph*)malloc(sizeof(*glyphs) * BENCH_GLYPH_CAPACITY);
	RgTextKerning* pairs = (RgTextKerning*)malloc(sizeof(*pairs) * BENCH_PAIR_CAPACITY);
	RgTextQuad quads[BENCH_TEXT_SIZE];
	RgTextQuad reference[BENCH_TEXT_SIZE];
	char text[BENCH_TEXT_SIZE];
	int ok = 0;
	if (!glyphs || !pairs) goto done;

	RgTextFont font;
	RgTextFontLoadDesc load = {0};
	load.data = data;
	load.data_size = data_size;
	load.glyphs = glyphs;
	load.glyph_capacity = BENCH_GLYPH_CAPACITY;
	load.kernings = pairs;
	load.kerning_capacity = BENCH_PAIR_CAPACITY;
	if (!rg_text_font_load_rgfont(&font, &load))
	{
		fprintf(stderr, "%s: invalid font or benchmark storage capacity exceeded\n", label);
		goto done;
	}

	static const char sentence[] = "The quick brown fox jumps over the lazy dog. Score: 0123456789. ";
	for (size_t i = 0u; i < sizeof(text); i++) text[i] = sentence[i % (sizeof(sentence) - 1u)];
	memset(quads, 0, sizeof(quads));
	memset(reference, 0, sizeof(reference));
	RgTextBuildDesc build = {0};
	build.font = &font;
	build.text = text;
	build.text_size = sizeof(text);
	build.scale = 1.0f;
	build.color.r = build.color.g = build.color.b = build.color.a = 1.0f;
	build.quads = quads;
	build.quad_capacity = RG_ARRAY_COUNT(quads);
	size_t full_count = bench_build(&build);

	RgTextFont linear = font;
	linear.internal_lookup_flags = 0u;
	build.font = &linear;
	build.quads = reference;
	size_t reference_count = bench_build(&build);
	RgTextSize indexed_size = rg_text_measure(&font, text, sizeof(text), 1.0f);
	RgTextSize linear_size = rg_text_measure(&linear, text, sizeof(text), 1.0f);
	if (full_count != reference_count ||
	    memcmp(quads, reference, full_count * sizeof(*quads)) != 0 ||
	    indexed_size.width != linear_size.width || indexed_size.height != linear_size.height)
	{
		fprintf(stderr, "%s: indexed lookup differs from linear reference\n", label);
		goto done;
	}

	build.font = &font;
	build.quads = quads;
	printf("%s: %u glyphs, %u pairs, %u text bytes, %zu visible quads\n",
	       label, font.glyph_count, font.kerning_count, BENCH_TEXT_SIZE, full_count);
	for (u32 test = 0u; test < 2u; test++)
	{
		build.quad_capacity = test ? 1u : RG_ARRAY_COUNT(quads);
		u32 repetitions = test ? 1000000u : 1000u;
		size_t expected = test && full_count > 1u ? 1u : full_count;
		size_t count = bench_build(&build);
		if (count != expected || (count && memcmp(quads, reference, sizeof(*quads)) != 0))
		{
			fprintf(stderr, "%s: capacity-limited layout differs from full layout\n", label);
			goto done;
		}
		u64 start = rg_time_ticks();
		for (u32 i = 0u; i < repetitions; i++)
		{
			count = bench_build(&build);
			g_checksum += (double)count;
			if (count) g_checksum += (double)quads[count - 1u].x1;
		}
		double elapsed_ms = rg_time_ticks_to_ms(rg_time_ticks() - start);
		printf("  left capacity=%zu: %.6f ms/call (%u calls)\n",
		       build.quad_capacity, elapsed_ms / (double)repetitions, repetitions);
	}
	ok = 1;
done:
	free(pairs);
	free(glyphs);
	return ok;
}

static char* bench_synthetic(u32 glyph_count, u32 pair_count, int ascii_misses, size_t* out_size)
{
	size_t capacity = 128u + (size_t)glyph_count * 64u + (size_t)pair_count * 64u;
	char* data = (char*)malloc(capacity);
	if (!data) return NULL;
	int written = snprintf(data, capacity, "rgfont 1\natlas 16 16\nline_height 16\nfallback 63\n");
	if (written < 0 || (size_t)written >= capacity) { free(data); return NULL; }
	size_t used = (size_t)written;
	for (u32 i = 0u; i < glyph_count; i++)
	{
		u32 cp = glyph_count - i + 31u;
		written = snprintf(data + used, capacity - used, "glyph %u 0 0 %d %d 0 0 %d\n",
		                   cp, cp == 32u ? 0 : 6, cp == 32u ? 0 : 12, cp == 32u ? 4 : 8);
		if (written < 0 || (size_t)written >= capacity - used) { free(data); return NULL; }
		used += (size_t)written;
	}
	u32 span = ascii_misses ? 256u : glyph_count;
	u32 base = ascii_misses ? 256u : 32u;
	for (u32 i = 0u; i < pair_count; i++)
	{
		u32 key = (i * 40503u + 17u) % (span * span);
		written = snprintf(data + used, capacity - used, "kerning %u %u -1\n",
		                   base + key / span, base + key % span);
		if (written < 0 || (size_t)written >= capacity - used) { free(data); return NULL; }
		used += (size_t)written;
	}
	*out_size = used;
	return data;
}

static char* bench_read_file(const char* path, size_t* out_size)
{
	FILE* file = fopen(path, "rb");
	if (!file) return NULL;
	if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
	long length = ftell(file);
	if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
	char* data = (char*)malloc((size_t)length);
	if (!data) { fclose(file); return NULL; }
	size_t read_count = fread(data, 1u, (size_t)length, file);
	int close_result = fclose(file);
	if (read_count != (size_t)length || close_result != 0) { free(data); return NULL; }
	*out_size = (size_t)length;
	return data;
}

int main(int argc, char** argv)
{
	if (argc > 2)
	{
		fprintf(stderr, "Usage: %s [optional.font]\n", argv[0]);
		return 1;
	}
	rg_time_init();
	printf("rg_text optimized layout benchmark; checks use linear lookup as a reference\n");
	for (u32 test = 0u; test < 2u; test++)
	{
		size_t size = 0u;
		char* data = bench_synthetic(test ? 512u : 95u, test ? 65536u : 616u, test != 0u, &size);
		if (!data) return 1;
		int ok = bench_font(test ? "synthetic large table (ASCII pair misses)" : "synthetic ASCII", data, size);
		free(data);
		if (!ok) return 1;
	}
	if (argc == 2)
	{
		size_t size = 0u;
		char* data = bench_read_file(argv[1], &size);
		if (!data) { fprintf(stderr, "Could not read %s\n", argv[1]); return 1; }
		int ok = bench_font(argv[1], data, size);
		free(data);
		if (!ok) return 1;
	}
	printf("checksum: %.0f\n", g_checksum);
	return 0;
}
