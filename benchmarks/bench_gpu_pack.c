// CPU-only quad packing comparison. Compile optimized, without whole-program optimization.
// Define RG_TEXT_BENCH_ASM=1 and link src/asm/rg_text_gpu_pack_quads_x64.asm for Windows x64.
// Usage: bench_gpu_pack.exe [--verify-only]
#define RG_TEXT_GPU_USE_SSE2 1
#include "../src/rg_text_gpu.h"
#include "quad_pack_reference.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef RG_TEXT_BENCH_ASM
#define RG_TEXT_BENCH_ASM 0
#endif

typedef size_t (*QueueFn)(RgTextGpuRenderer*, const RgTextQuad*, size_t);
static volatile u64 g_checksum;

static RG_NOINLINE size_t queue_original(RgTextGpuRenderer* r, const RgTextQuad* q, size_t n)
{
	return rg_text_bench_queue_reference(r, q, n);
}

// Keep guards, capacity clipping, and counter updates identical across the new kernels.
// Their C/SSE2 bodies can inline into these equally noinline, dynamically called wrappers.
#define DEFINE_QUEUE(name, pack) \
	static RG_NOINLINE size_t name(RgTextGpuRenderer* r, const RgTextQuad* q, size_t n) \
	{ \
		if (!r || !q || n == 0u || r->quad_count >= r->quad_capacity) return 0u; \
		size_t available = (size_t)r->quad_capacity - (size_t)r->quad_count; \
		if (n > available) n = available; \
		if (n == 0u) return 0u; \
		pack(r->vertices + r->vertex_count, r->indices + r->index_count, q, (u32)n, r->vertex_count); \
		r->quad_count += (u32)n; \
		r->vertex_count += (u32)n * 4u; \
		r->index_count += (u32)n * 6u; \
		return n; \
	}

static RG_NOINLINE size_t queue_c(RgTextGpuRenderer* r, const RgTextQuad* q, size_t n)
{
	return rg_text_gpu__queue_quads_c(r, q, n);
}
DEFINE_QUEUE(queue_sse2, rg_text_gpu__pack_quads_sse2)
#if RG_TEXT_BENCH_ASM
extern void rg_text_gpu_pack_quads_asm(RgTextGpuVertex*, u32*, const RgTextQuad*, u32, u32);
DEFINE_QUEUE(queue_asm, rg_text_gpu_pack_quads_asm)
#endif
#undef DEFINE_QUEUE

static const struct
{
	const char* name;
	QueueFn queue;
} methods[] = {
	{"original", queue_original},
	{"portable C", queue_c},
	{"SSE2", queue_sse2},
#if RG_TEXT_BENCH_ASM
	{"x64 assembly", queue_asm},
#endif
};

static u32 random_bits(u32* state)
{
	u32 x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static void fill_quads(RgTextQuad* quads, size_t count)
{
	static const u32 special[] = {
		0u, 0x80000000u, 0x7FC12345u, 0xFFC56789u,
		0x7F812345u, 0x7F800000u, 0xFF800000u, 1u,
	};
	u32 state = 193847u;
	unsigned char* bytes = (unsigned char*)quads;
	for (size_t i = 0u; i < count * sizeof(*quads) / sizeof(u32); i++)
	{
		u32 bits = i % 4u ? random_bits(&state) : special[(i / 4u) % RG_ARRAY_COUNT(special)];
		memcpy(bytes + i * sizeof(bits), &bits, sizeof(bits));
	}
}

static RgTextGpuRenderer make_renderer(RgTextGpuVertex* vertices, u32* indices, u32 capacity, u32 used)
{
	RgTextGpuRenderer r = {0};
	r.vertices = vertices;
	r.indices = indices;
	r.quad_capacity = capacity;
	r.quad_count = used;
	r.vertex_count = used * 4u;
	r.index_count = used * 6u;
	return r;
}

static int counters_equal(const RgTextGpuRenderer* a, const RgTextGpuRenderer* b)
{
	return a->quad_count == b->quad_count && a->vertex_count == b->vertex_count &&
	       a->index_count == b->index_count;
}

static int validate_contract(void)
{
	static const struct { u32 used, capacity, requested; } cases[] = {
		{0u, 0u, 0u}, {0u, 19u, 0u}, {0u, 19u, 1u}, {0u, 19u, 2u},
		{0u, 19u, 3u}, {0u, 19u, 4u}, {0u, 19u, 7u}, {0u, 19u, 16u},
		{3u, 22u, 19u}, {3u, 22u, 25u}, {19u, 19u, 4u},
	};
	const size_t vertex_bytes = 24u * 4u * sizeof(RgTextGpuVertex) + 32u;
	const size_t index_bytes = 24u * 6u * sizeof(u32) + 32u;
	unsigned char* source = (unsigned char*)malloc(24u * sizeof(RgTextQuad) + 32u);
	unsigned char* expected_v = (unsigned char*)malloc(vertex_bytes);
	unsigned char* expected_i = (unsigned char*)malloc(index_bytes);
	unsigned char* output_v = (unsigned char*)malloc(vertex_bytes);
	unsigned char* output_i = (unsigned char*)malloc(index_bytes);
	int ok = 0;
	if (!source || !expected_v || !expected_i || !output_v || !output_i) goto done;
	for (u32 alignment = 0u; alignment < 16u; alignment += 4u)
	{
		RgTextQuad* quads = (RgTextQuad*)(source + ((alignment + 4u) & 12u));
		fill_quads(quads, 24u);
		for (u32 c = 0u; c < RG_ARRAY_COUNT(cases); c++)
		{
			memset(expected_v, 0xA5, vertex_bytes);
			memset(expected_i, 0xA5, index_bytes);
			RgTextGpuRenderer expected = make_renderer((RgTextGpuVertex*)(expected_v + alignment),
			    (u32*)(expected_i + (alignment ^ 12u)), cases[c].capacity, cases[c].used);
			size_t expected_count = queue_original(&expected, quads, cases[c].requested);
			for (u32 m = 0u; m < RG_ARRAY_COUNT(methods); m++)
			{
				memset(output_v, 0xA5, vertex_bytes);
				memset(output_i, 0xA5, index_bytes);
				RgTextGpuRenderer actual = make_renderer((RgTextGpuVertex*)(output_v + alignment),
				    (u32*)(output_i + (alignment ^ 12u)), cases[c].capacity, cases[c].used);
				size_t actual_count = methods[m].queue(&actual, quads, cases[c].requested);
				if (actual_count != expected_count || !counters_equal(&actual, &expected) ||
				    memcmp(output_v, expected_v, vertex_bytes) || memcmp(output_i, expected_i, index_bytes) ||
				    methods[m].queue(NULL, quads, 1u) != 0u || methods[m].queue(&actual, NULL, 1u) != 0u ||
				    !counters_equal(&actual, &expected))
				{
					fprintf(stderr, "Contract mismatch: %s, case %u, alignment %u\n", methods[m].name, c, alignment);
					goto done;
				}
			}
		}
	}
	ok = 1;
done:
	free(output_i); free(output_v); free(expected_i); free(expected_v); free(source);
	if (!ok) fprintf(stderr, "Packing contract validation failed.\n");
	return ok;
}

static u64 hash_bytes(const void* data, size_t size)
{
	const unsigned char* bytes = (const unsigned char*)data;
	u64 hash = 14695981039346656037ull;
	for (size_t i = 0u; i < size; i++) hash = (hash ^ bytes[i]) * 1099511628211ull;
	return hash;
}

static int compare_double(const void* a, const void* b)
{
	double x = *(const double*)a, y = *(const double*)b;
	return (x > y) - (x < y);
}

static int run_case(u32 count, u32 slots, int validate_only)
{
	size_t vertex_count = (size_t)count * 4u, index_count = (size_t)count * 6u;
	size_t vertex_bytes = vertex_count * sizeof(RgTextGpuVertex);
	size_t index_bytes = index_count * sizeof(u32);
	RgTextGpuVertex* vertices = (RgTextGpuVertex*)malloc(vertex_bytes * slots);
	u32* indices = (u32*)malloc(index_bytes * slots);
	RgTextQuad* quads = (RgTextQuad*)malloc((size_t)count * slots * sizeof(*quads));
	RgTextGpuVertex* expected_v = (RgTextGpuVertex*)malloc(vertex_bytes);
	u32* expected_i = (u32*)malloc(index_bytes);
	int ok = 0;
	if (!vertices || !indices || !quads || !expected_v || !expected_i) goto done;
	fill_quads(quads, (size_t)count * slots);
	u64 expected_hash = 0u;
	for (u32 slot = 0u; slot < slots; slot++)
	{
		RgTextGpuRenderer expected = make_renderer(expected_v, expected_i, count, 0u);
		queue_original(&expected, quads + (size_t)count * slot, count);
		expected_hash += hash_bytes(expected_v, vertex_bytes) + hash_bytes(expected_i, index_bytes);
		for (u32 m = 0u; m < RG_ARRAY_COUNT(methods); m++)
		{
			RgTextGpuRenderer r = make_renderer(vertices + vertex_count * slot, indices + index_count * slot, count, 0u);
			memset(r.vertices, 0xA5, vertex_bytes);
			memset(r.indices, 0xA5, index_bytes);
			if (methods[m].queue(&r, quads + (size_t)count * slot, count) != count ||
			    !counters_equal(&r, &expected) || memcmp(r.vertices, expected_v, vertex_bytes) ||
			    memcmp(r.indices, expected_i, index_bytes))
			{
				fprintf(stderr, "Byte mismatch: %s, count %u, slot %u\n", methods[m].name, count, slot);
				goto done;
			}
		}
	}
	if (!validate_only)
	{
		double samples[RG_ARRAY_COUNT(methods)][7];
		u32 iterations = 12000000u / count;
		if (iterations > 2000000u) iterations = 2000000u;
		Uint64 frequency = SDL_GetPerformanceFrequency();
		if (frequency == 0u) goto done;
		for (u32 trial = 0u; trial < 7u; trial++)
			for (u32 order = 0u; order < RG_ARRAY_COUNT(methods); order++)
			{
				u32 m = trial & 1u ? RG_ARRAY_COUNT(methods) - 1u - order : order;
				QueueFn queue = methods[m].queue;
				RgTextGpuRenderer r = make_renderer(vertices, indices, count, 0u);
				for (u32 warm = 0u; warm < 256u; warm++)
				{
					u32 slot = warm % slots;
					r.vertices = vertices + vertex_count * slot;
					r.indices = indices + index_count * slot;
					rg_text_gpu_begin(&r);
					queue(&r, quads + (size_t)count * slot, count);
				}
				Uint64 start = SDL_GetPerformanceCounter();
				for (u32 i = 0u; i < iterations; i++)
				{
					u32 slot = i % slots;
					r.vertices = vertices + vertex_count * slot;
					r.indices = indices + index_count * slot;
					rg_text_gpu_begin(&r);
					queue(&r, quads + (size_t)count * slot, count);
				}
				Uint64 elapsed = SDL_GetPerformanceCounter() - start;
				samples[m][trial] = (double)elapsed * 1e9 / (double)frequency / (double)iterations;
				u64 actual_hash = 0u;
				for (u32 slot = 0u; slot < slots; slot++)
					actual_hash += hash_bytes(vertices + vertex_count * slot, vertex_bytes) +
					               hash_bytes(indices + index_count * slot, index_bytes);
				g_checksum += actual_hash;
				if (actual_hash != expected_hash || r.quad_count != count || r.vertex_count != count * 4u ||
				    r.index_count != count * 6u)
				{
					fprintf(stderr, "Post-timing checksum/counter mismatch: %s\n", methods[m].name);
					goto done;
				}
			}
		printf("\nquads=%u buffers=%u working_set=%.3f MiB iterations=%u\n", count, slots,
		       (double)((vertex_bytes + index_bytes + (size_t)count * sizeof(*quads)) * slots) / 1048576.0,
		       iterations);
		for (u32 m = 0u; m < RG_ARRAY_COUNT(methods); m++)
		{
			qsort(samples[m], 7u, sizeof(double), compare_double);
			printf("  %-12s median=%10.3f ns min=%10.3f max=%10.3f original/median=%.3fx\n",
			       methods[m].name, samples[m][3], samples[m][0], samples[m][6], samples[0][3] / samples[m][3]);
		}
	}
	ok = 1;
done:
	free(expected_i); free(expected_v); free(quads); free(indices); free(vertices);
	if (!ok) fprintf(stderr, "Packing case failed: quads=%u buffers=%u\n", count, slots);
	return ok;
}

int main(int argc, char** argv)
{
	int validate_only = argc == 2 && strcmp(argv[1], "--verify-only") == 0;
	if (argc > 2 || (argc == 2 && !validate_only))
	{
		fprintf(stderr, "Usage: %s [--verify-only]\n", argv[0]);
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
	printf("Platform: %s; logical CPUs: %d; SSE2: %s; ASM comparison: %s\n",
	       SDL_GetPlatform(), SDL_GetNumLogicalCPUCores(), SDL_HasSSE2() ? "yes" : "no",
	       RG_TEXT_BENCH_ASM ? "enabled" : "disabled");
	puts("CPU packing only; seven alternating trials; benchmark does not set affinity; compile optimized without LTO.");
	if (!SDL_HasSSE2() || !validate_contract()) return 1;
	static const u32 counts[] = {1u, 16u, 1656u, 8192u, 8192u};
	static const u32 slots[] = {1u, 1u, 1u, 1u, 64u};
	for (u32 c = 0u; c < RG_ARRAY_COUNT(counts); c++)
		if (!run_case(counts[c], slots[c], validate_only)) return 1;
	printf("\nBitwise, boundary, and counter checks passed. Checksum: %llu\n", (unsigned long long)g_checksum);
	return 0;
}

