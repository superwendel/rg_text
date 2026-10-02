// Allocation-free runtime layout benchmark; setup allocates caller-owned storage.
// Usage: bench_text [--verify-only] [optional.font]
// Compile optimized without LTO. Checks use linear lookup as a reference.

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
#define BENCH_TRIALS 7u
#define BENCH_WARMUP_CALLS 256u
#define BENCH_TARGET_MS 20.0
#define BENCH_MAX_REPETITIONS 64000000u

static volatile u64 g_checksum;
static volatile RgTextAlign g_alignment = RG_TEXT_ALIGN_LEFT;

static RG_NOINLINE size_t bench_build(RgTextBuildDesc* build)
{
	// Keep the descriptor dynamic, as it is in a UI renderer.
	build->align = g_alignment;
	return rg_text_build_quads_ex(build);
}

typedef size_t (*BenchBuildFn)(RgTextBuildDesc*);
// Load the target outside each measured batch. Calls cannot be folded or hoisted,
// even though only the final output is consumed after timing.
static BenchBuildFn volatile g_build_fn = bench_build;

typedef enum BenchTextPattern
{
	BENCH_ASCII,
	BENCH_UNICODE_HITS,
	BENCH_UNICODE_MISSES,
	BENCH_MULTILINE
} BenchTextPattern;

typedef struct BenchWorkload
{
	const char* name;
	size_t text_size;
	BenchTextPattern pattern;
	RgTextAlign align;
} BenchWorkload;

static u32 bench_glyph_codepoint(u32 index)
{
	return index < 95u ? index + 32u : 0x400u + (index - 95u) * 3u;
}

static size_t bench_encode_utf8(u32 cp, char* output)
{
	if (cp < 0x80u)
	{
		output[0] = (char)cp;
		return 1u;
	}
	if (cp < 0x800u)
	{
		output[0] = (char)(0xC0u | (cp >> 6u));
		output[1] = (char)(0x80u | (cp & 0x3Fu));
		return 2u;
	}
	output[0] = (char)(0xE0u | (cp >> 12u));
	output[1] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
	output[2] = (char)(0x80u | (cp & 0x3Fu));
	return 3u;
}

static void bench_fill_text(char* text, const BenchWorkload* workload)
{
	static const char sentence[] = "The quick brown fox jumps over the lazy dog. Score: 0123456789. ";
	static const char lines[] = "Score: 0123456789\r\nThe quick brown fox\nShort\rAnother line\n";
	if (workload->pattern == BENCH_ASCII || workload->pattern == BENCH_MULTILINE)
	{
		const char* pattern = workload->pattern == BENCH_MULTILINE ? lines : sentence;
		size_t length = strlen(pattern);
		for (size_t i = 0u; i < workload->text_size; i++) text[i] = pattern[i % length];
		return;
	}
	for (size_t used = 0u, i = 0u; used < workload->text_size; i++)
	{
		// All hit pairs lie in the table's final 256 glyphs. Misses exercise
		// existing, non-contiguous Unicode glyphs below that pair range.
		u32 first = workload->pattern == BENCH_UNICODE_HITS ? 256u : 95u;
		char encoded[3];
		size_t length = bench_encode_utf8(bench_glyph_codepoint(first + (u32)(i % 16u)), encoded);
		if (length > workload->text_size - used)
		{
			text[used++] = 'A';
			continue;
		}
		memcpy(text + used, encoded, length);
		used += length;
	}
}

static u64 bench_hash(const void* data, size_t size)
{
	const unsigned char* bytes = (const unsigned char*)data;
	u64 hash = 14695981039346656037ull;
	for (size_t i = 0u; i < size; i++) hash = (hash ^ bytes[i]) * 1099511628211ull;
	return hash;
}

static int bench_compare_double(const void* a, const void* b)
{
	double x = *(const double*)a, y = *(const double*)b;
	return (x > y) - (x < y);
}

static int bench_check_output(const char* label, RgTextBuildDesc* build,
                             const RgTextQuad* reference, size_t expected, size_t actual)
{
	if (actual != expected || memcmp(build->quads, reference, expected * sizeof(*reference)) != 0)
	{
		fprintf(stderr, "%s: output differs from linear reference (capacity=%zu)\n",
		        label, build->quad_capacity);
		return 0;
	}
	const unsigned char* bytes = (const unsigned char*)build->quads;
	for (size_t i = expected * sizeof(*reference); i < BENCH_TEXT_SIZE * sizeof(*reference); i++)
	{
		if (bytes[i] != 0xA5u)
		{
			fprintf(stderr, "%s: layout wrote beyond its returned prefix (capacity=%zu)\n",
			        label, build->quad_capacity);
			return 0;
		}
	}
	return 1;
}

static double bench_batch(RgTextBuildDesc* build, u32 repetitions, size_t* out_count)
{
	BenchBuildFn build_fn = g_build_fn;
	size_t count = 0u;
	u64 start = rg_time_ticks();
	for (u32 i = 0u; i < repetitions; i++) count = build_fn(build);
	u64 elapsed = rg_time_ticks() - start;
	*out_count = count;
	return rg_time_ticks_to_ms(elapsed);
}

static int bench_workload(const char* font_label, const RgTextFont* font,
                          const BenchWorkload* workload, int verify_only)
{
	RgTextQuad quads[BENCH_TEXT_SIZE];
	RgTextQuad reference[BENCH_TEXT_SIZE];
	char text[BENCH_TEXT_SIZE];
	bench_fill_text(text, workload);
	memset(quads, 0, sizeof(quads));
	memset(reference, 0, sizeof(reference));
	g_alignment = workload->align;
	RgTextBuildDesc build = {0};
	build.font = font;
	build.text = text;
	build.text_size = workload->text_size;
	build.scale = 1.0f;
	build.align = workload->align;
	build.color.r = build.color.g = build.color.b = build.color.a = 1.0f;
	build.quads = quads;
	build.quad_capacity = RG_ARRAY_COUNT(quads);
	size_t full_count = bench_build(&build);

	RgTextFont linear = *font;
	linear.internal_lookup_flags = 0u;
	build.font = &linear;
	build.quads = reference;
	size_t reference_count = bench_build(&build);
	RgTextSize indexed_size = rg_text_measure(font, text, workload->text_size, 1.0f);
	RgTextSize linear_size = rg_text_measure(&linear, text, workload->text_size, 1.0f);
	if (full_count != reference_count ||
	    memcmp(quads, reference, full_count * sizeof(*quads)) != 0 ||
	    indexed_size.width != linear_size.width || indexed_size.height != linear_size.height)
	{
		fprintf(stderr, "%s / %s: indexed layout/measurement differs from linear reference\n",
		        font_label, workload->name);
		return 0;
	}

	build.font = font;
	build.quads = quads;
	printf("%s / %s: %zu text bytes, %zu visible quads\n",
	       font_label, workload->name, workload->text_size, full_count);
	for (u32 limited = 0u; limited < 2u; limited++)
	{
		build.quad_capacity = limited ? 1u : RG_ARRAY_COUNT(quads);
		size_t expected = limited && full_count > 1u ? 1u : full_count;
		memset(quads, 0xA5, sizeof(quads));
		size_t count = bench_build(&build);
		if (!bench_check_output(workload->name, &build, reference, expected, count)) return 0;
		g_checksum += bench_hash(quads, count * sizeof(*quads)) + (u64)count;
		if (verify_only) continue;

		// Calibrate only the indexed implementation; reference checks are never timed.
		u32 repetitions = 64u;
		for (;;)
		{
			double elapsed_ms = bench_batch(&build, repetitions, &count);
			if (!bench_check_output(workload->name, &build, reference, expected, count)) return 0;
			if (elapsed_ms >= BENCH_TARGET_MS || repetitions >= BENCH_MAX_REPETITIONS) break;
			repetitions *= 2u;
			if (repetitions > BENCH_MAX_REPETITIONS) repetitions = BENCH_MAX_REPETITIONS;
		}
		double samples[BENCH_TRIALS];
		for (u32 trial = 0u; trial < BENCH_TRIALS; trial++)
		{
			BenchBuildFn build_fn = g_build_fn;
			for (u32 warm = 0u; warm < BENCH_WARMUP_CALLS; warm++) count = build_fn(&build);
			double elapsed_ms = bench_batch(&build, repetitions, &count);
			if (elapsed_ms <= 0.0)
			{
				fprintf(stderr, "%s: monotonic timer did not advance\n", workload->name);
				return 0;
			}
			samples[trial] = elapsed_ms * 1000000.0 / (double)repetitions;
			if (!bench_check_output(workload->name, &build, reference, expected, count)) return 0;
		}
		qsort(samples, BENCH_TRIALS, sizeof(samples[0]), bench_compare_double);
		printf("  capacity=%zu median=%.3f ns/call min=%.3f max=%.3f (%u calls/trial)\n",
		       build.quad_capacity, samples[BENCH_TRIALS / 2u], samples[0],
		       samples[BENCH_TRIALS - 1u], repetitions);
	}
	return 1;
}

static int bench_font(const char* label, const char* data, size_t data_size,
                      const BenchWorkload* workloads, size_t workload_count, int verify_only)
{
	RgTextGlyph* glyphs = (RgTextGlyph*)malloc(sizeof(*glyphs) * BENCH_GLYPH_CAPACITY);
	RgTextKerning* pairs = (RgTextKerning*)malloc(sizeof(*pairs) * BENCH_PAIR_CAPACITY);
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
	printf("\n%s: %u glyphs, %u pairs\n", label, font.glyph_count, font.kerning_count);
	for (size_t i = 0u; i < workload_count; i++)
		if (!bench_workload(label, &font, &workloads[i], verify_only)) goto done;
	ok = 1;
done:
	free(pairs);
	free(glyphs);
	return ok;
}

static char* bench_synthetic(u32 glyph_count, u32 pair_span, u32 pair_first, size_t* out_size)
{
	u32 pair_count = pair_span * pair_span;
	size_t capacity = 128u + (size_t)glyph_count * 64u + (size_t)pair_count * 64u;
	char* data = (char*)malloc(capacity);
	if (!data) return NULL;
	int written = snprintf(data, capacity, "rgfont 1\natlas 16 16\nline_height 16\nfallback 63\n");
	if (written < 0 || (size_t)written >= capacity) { free(data); return NULL; }
	size_t used = (size_t)written;
	for (u32 i = 0u; i < glyph_count; i++)
	{
		u32 cp = bench_glyph_codepoint(glyph_count - 1u - i);
		written = snprintf(data + used, capacity - used, "glyph %u 0 0 %d %d 0 0 %d\n",
		                   cp, cp == 32u ? 0 : 6, cp == 32u ? 0 : 12, cp == 32u ? 4 : 8);
		if (written < 0 || (size_t)written >= capacity - used) { free(data); return NULL; }
		used += (size_t)written;
	}
	for (u32 i = 0u; i < pair_count; i++)
	{
		u32 key = pair_count - 1u - i;
		written = snprintf(data + used, capacity - used, "kerning %u %u -1\n",
		                   bench_glyph_codepoint(pair_first + key / pair_span),
		                   bench_glyph_codepoint(pair_first + key % pair_span));
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
	int verify_only = 0;
	const char* font_path = NULL;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--verify-only") == 0 && !verify_only) verify_only = 1;
		else if (argv[i][0] != '-' && !font_path) font_path = argv[i];
		else
		{
			fprintf(stderr, "Usage: %s [--verify-only] [optional.font]\n", argv[0]);
			return 1;
		}
	}
	rg_time_init();
	if (rg_time_ticks_per_second() == 0u)
	{
		fprintf(stderr, "Monotonic timer is unavailable\n");
		return 1;
	}
#if defined(__clang__)
	printf("Compiler: Clang %s\n", __clang_version__);
#elif defined(_MSC_VER)
	printf("Compiler: MSVC %d\n", _MSC_FULL_VER);
#elif defined(__GNUC__)
	printf("Compiler: GCC %s\n", __VERSION__);
#else
	puts("Compiler: unknown");
#endif
	puts("rg_text layout benchmark; indexed output checked against linear lookup");
	puts("Seven warmed trials; calibrated batches; checks outside timing; compile optimized without LTO.");
	static const BenchWorkload dense[] = {
		{"ASCII hits short", 16u, BENCH_ASCII, RG_TEXT_ALIGN_LEFT},
		{"ASCII hits full", BENCH_TEXT_SIZE, BENCH_ASCII, RG_TEXT_ALIGN_LEFT},
		{"multiline center", BENCH_TEXT_SIZE, BENCH_MULTILINE, RG_TEXT_ALIGN_CENTER},
		{"multiline right", BENCH_TEXT_SIZE, BENCH_MULTILINE, RG_TEXT_ALIGN_RIGHT},
	};
	static const BenchWorkload unkerned[] = {
		{"ASCII no kerning", BENCH_TEXT_SIZE, BENCH_ASCII, RG_TEXT_ALIGN_LEFT},
	};
	static const BenchWorkload sparse[] = {
		{"ASCII pair misses", BENCH_TEXT_SIZE, BENCH_ASCII, RG_TEXT_ALIGN_LEFT},
		{"Unicode pair hits", BENCH_TEXT_SIZE, BENCH_UNICODE_HITS, RG_TEXT_ALIGN_LEFT},
		{"Unicode pair misses", BENCH_TEXT_SIZE, BENCH_UNICODE_MISSES, RG_TEXT_ALIGN_LEFT},
	};
	static const struct
	{
		const char* name;
		u32 glyph_count, pair_span, pair_first;
		const BenchWorkload* workloads;
		size_t workload_count;
	} fonts[] = {
		{"dense ASCII", 95u, 95u, 0u, dense, RG_ARRAY_COUNT(dense)},
		{"dense ASCII unkerned", 95u, 0u, 0u, unkerned, RG_ARRAY_COUNT(unkerned)},
		{"sparse Unicode", 512u, 256u, 256u, sparse, RG_ARRAY_COUNT(sparse)},
	};
	for (size_t i = 0u; i < RG_ARRAY_COUNT(fonts); i++)
	{
		size_t size = 0u;
		char* data = bench_synthetic(fonts[i].glyph_count, fonts[i].pair_span, fonts[i].pair_first, &size);
		if (!data) return 1;
		int ok = bench_font(fonts[i].name, data, size, fonts[i].workloads, fonts[i].workload_count, verify_only);
		free(data);
		if (!ok) return 1;
	}
	if (font_path)
	{
		static const BenchWorkload asset[] = {
			{"asset ASCII short", 16u, BENCH_ASCII, RG_TEXT_ALIGN_LEFT},
			{"asset ASCII full", BENCH_TEXT_SIZE, BENCH_ASCII, RG_TEXT_ALIGN_LEFT},
		};
		size_t size = 0u;
		char* data = bench_read_file(font_path, &size);
		if (!data) { fprintf(stderr, "Could not read %s\n", font_path); return 1; }
		int ok = bench_font(font_path, data, size, asset, RG_ARRAY_COUNT(asset), verify_only);
		free(data);
		if (!ok) return 1;
	}
	printf("\nLayout and measurement checks passed. Checksum: %llu\n", (unsigned long long)g_checksum);
	return 0;
}
