// CPU-only contract tests for rg_text_gpu_queue_quads, shared by C, SSE2 and ASM builds.

#include "../src/rg_text_gpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define PACK_CAPACITY 65u
#define PACK_INPUT_COUNT 73u
#define PACK_GUARD_BYTE 0xA5
#define PACK_STATIC_ASSERT(name, condition) typedef char name[(condition) ? 1 : -1]
#define CHECK(condition, message) \
	do { if (!(condition)) { fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 0; } } while (0)

PACK_STATIC_ASSERT(pack_float_size, sizeof(f32) == 4u);
PACK_STATIC_ASSERT(pack_quad_size, sizeof(RgTextQuad) == 48u);
PACK_STATIC_ASSERT(pack_quad_positions, offsetof(RgTextQuad, x0) == 0u && offsetof(RgTextQuad, y0) == 4u &&
                   offsetof(RgTextQuad, x1) == 8u && offsetof(RgTextQuad, y1) == 12u);
PACK_STATIC_ASSERT(pack_quad_uvs, offsetof(RgTextQuad, u0) == 16u && offsetof(RgTextQuad, v0) == 20u &&
                   offsetof(RgTextQuad, u1) == 24u && offsetof(RgTextQuad, v1) == 28u);
PACK_STATIC_ASSERT(pack_quad_color, offsetof(RgTextQuad, color) == 32u && sizeof(RgTextColor) == 16u);
PACK_STATIC_ASSERT(pack_color_components, offsetof(RgTextColor, r) == 0u && offsetof(RgTextColor, g) == 4u &&
                   offsetof(RgTextColor, b) == 8u && offsetof(RgTextColor, a) == 12u);
PACK_STATIC_ASSERT(pack_vertex_size, sizeof(RgTextGpuVertex) == 36u);
PACK_STATIC_ASSERT(pack_vertex_positions, offsetof(RgTextGpuVertex, x) == 0u && offsetof(RgTextGpuVertex, y) == 4u &&
                   offsetof(RgTextGpuVertex, z) == 8u);
PACK_STATIC_ASSERT(pack_vertex_color, offsetof(RgTextGpuVertex, r) == 12u && offsetof(RgTextGpuVertex, g) == 16u &&
                   offsetof(RgTextGpuVertex, b) == 20u && offsetof(RgTextGpuVertex, a) == 24u);
PACK_STATIC_ASSERT(pack_vertex_uvs, offsetof(RgTextGpuVertex, u) == 28u && offsetof(RgTextGpuVertex, v) == 32u);

typedef struct PackBuffer
{
	u8* allocation;
	u8* data;
	size_t allocation_size;
	size_t data_size;
} PackBuffer;

typedef struct PackFixture
{
	PackBuffer source;
	PackBuffer vertices;
	PackBuffer indices;
	RgTextQuad source_copy[PACK_INPUT_COUNT];
	RgTextGpuVertex expected_vertices[PACK_CAPACITY * 4u];
	u32 expected_indices[PACK_CAPACITY * 6u];
	RgTextGpuRenderer actual;
	RgTextGpuRenderer expected;
} PackFixture;

static int pack_buffer_init(PackBuffer* buffer, size_t data_size)
{
	buffer->allocation_size = data_size + 80u;
	buffer->data_size = data_size;
	buffer->allocation = (u8*)malloc(buffer->allocation_size);
	if (!buffer->allocation) return 0;
	// Both guard regions are at least 29 bytes. All payloads are 4 mod 16.
	uintptr_t aligned = ((uintptr_t)(buffer->allocation + 32u) + 15u) & ~(uintptr_t)15u;
	buffer->data = (u8*)(aligned + 4u);
	memset(buffer->allocation, PACK_GUARD_BYTE, buffer->allocation_size);
	return 1;
}

static int pack_guards_intact(const PackBuffer* buffer)
{
	size_t start = (size_t)(buffer->data - buffer->allocation);
	for (size_t i = 0u; i < start; i++)
	{
		if (buffer->allocation[i] != PACK_GUARD_BYTE) return 0;
	}
	for (size_t i = start + buffer->data_size; i < buffer->allocation_size; i++)
	{
		if (buffer->allocation[i] != PACK_GUARD_BYTE) return 0;
	}
	return 1;
}

static int pack_fixture_init(PackFixture* fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	if (!pack_buffer_init(&fixture->source, sizeof(fixture->source_copy)) ||
	    !pack_buffer_init(&fixture->vertices, sizeof(fixture->expected_vertices)) ||
	    !pack_buffer_init(&fixture->indices, sizeof(fixture->expected_indices))) return 0;

	static const u32 special[] = {
		0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u,
		0x7F800000u, 0xFF800000u, 0x7FC12345u, 0xFFC98765u,
		0x7F800001u, 0xFF800001u, 0x00000001u, 0x80000001u,
		0x7F7FFFFFu, 0xFF7FFFFFu, 0x00800000u, 0x80800000u,
	};
	u32 state = 0x53A19B7Du;
	for (u32 q = 0u; q < PACK_INPUT_COUNT; q++)
	{
		for (u32 component = 0u; component < 12u; component++)
		{
			state = state * 1664525u + 1013904223u;
			u32 bits = q < RG_ARRAY_COUNT(special) ? special[(q + component) % RG_ARRAY_COUNT(special)] : state;
			memcpy(fixture->source.data + (size_t)q * sizeof(RgTextQuad) + (size_t)component * sizeof(f32),
			       &bits, sizeof(bits));
		}
	}
	memcpy(fixture->source_copy, fixture->source.data, sizeof(fixture->source_copy));
	return 1;
}

static void pack_fixture_reset(PackFixture* fixture, u32 capacity, u32 initial_count)
{
	memset(fixture->vertices.allocation, PACK_GUARD_BYTE, fixture->vertices.allocation_size);
	memset(fixture->indices.allocation, PACK_GUARD_BYTE, fixture->indices.allocation_size);
	memset(fixture->expected_vertices, PACK_GUARD_BYTE, sizeof(fixture->expected_vertices));
	memset(fixture->expected_indices, PACK_GUARD_BYTE, sizeof(fixture->expected_indices));
	memset(&fixture->actual, 0, sizeof(fixture->actual));
	fixture->actual.vertices = (RgTextGpuVertex*)fixture->vertices.data;
	fixture->actual.indices = (u32*)fixture->indices.data;
	fixture->actual.quad_capacity = capacity;
	fixture->actual.quad_count = initial_count;
	fixture->actual.vertex_count = initial_count * 4u;
	fixture->actual.index_count = initial_count * 6u;
	fixture->expected = fixture->actual;
	fixture->expected.vertices = fixture->expected_vertices;
	fixture->expected.indices = fixture->expected_indices;
}

static void pack_copy_float(f32* destination, const f32* source)
{
	// Representation copies make the reference independent of NaN arithmetic.
	memcpy(destination, source, sizeof(*destination));
}

static size_t pack_reference_queue(RgTextGpuRenderer* renderer, const RgTextQuad* quads, size_t count)
{
	if (!quads || !count) return 0u;
	size_t available = (size_t)renderer->quad_capacity - renderer->quad_count;
	if (count > available) count = available;
	static const u32 corners[6] = {0u, 1u, 2u, 0u, 2u, 3u};
	for (size_t q = 0u; q < count; q++)
	{
		for (u32 corner = 0u; corner < 4u; corner++)
		{
			RgTextGpuVertex* vertex = &renderer->vertices[renderer->vertex_count + corner];
			int right = corner == 1u || corner == 2u;
			int bottom = corner >= 2u;
			pack_copy_float(&vertex->x, right ? &quads[q].x1 : &quads[q].x0);
			pack_copy_float(&vertex->y, bottom ? &quads[q].y1 : &quads[q].y0);
			memset(&vertex->z, 0, sizeof(vertex->z));
			pack_copy_float(&vertex->r, &quads[q].color.r);
			pack_copy_float(&vertex->g, &quads[q].color.g);
			pack_copy_float(&vertex->b, &quads[q].color.b);
			pack_copy_float(&vertex->a, &quads[q].color.a);
			pack_copy_float(&vertex->u, right ? &quads[q].u1 : &quads[q].u0);
			pack_copy_float(&vertex->v, bottom ? &quads[q].v1 : &quads[q].v0);
		}
		for (u32 i = 0u; i < 6u; i++) renderer->indices[renderer->index_count + i] = renderer->vertex_count + corners[i];
		renderer->quad_count++;
		renderer->vertex_count += 4u;
		renderer->index_count += 6u;
	}
	return count;
}

static int pack_check_queue(PackFixture* fixture, const RgTextQuad* quads, size_t requested)
{
	size_t expected_count = pack_reference_queue(&fixture->expected, quads, requested);
	size_t actual_count = rg_text_gpu_queue_quads(&fixture->actual, quads, requested);
	CHECK(actual_count == expected_count, "returned count");
	CHECK(fixture->actual.quad_count == fixture->expected.quad_count &&
	      fixture->actual.vertex_count == fixture->expected.vertex_count &&
	      fixture->actual.index_count == fixture->expected.index_count, "appended renderer counts");
	CHECK(memcmp(fixture->vertices.data, fixture->expected_vertices, sizeof(fixture->expected_vertices)) == 0,
	      "vertex bits, corner order, zero z, and untouched output prefix/tail");
	CHECK(memcmp(fixture->indices.data, fixture->expected_indices, sizeof(fixture->expected_indices)) == 0,
	      "absolute indices, winding, and untouched output prefix/tail");
	CHECK(memcmp(fixture->source.data, fixture->source_copy, sizeof(fixture->source_copy)) == 0, "input is unchanged");
	CHECK(pack_guards_intact(&fixture->source) && pack_guards_intact(&fixture->vertices) &&
	      pack_guards_intact(&fixture->indices), "source/output guards");
	return 1;
}

static int pack_test_contract(PackFixture* fixture)
{
	const RgTextQuad* quads = (const RgTextQuad*)fixture->source.data;
	CHECK(((uintptr_t)quads & 15u) == 4u && ((uintptr_t)fixture->vertices.data & 15u) == 4u &&
	      ((uintptr_t)fixture->indices.data & 15u) == 4u, "exercise four-byte but not sixteen-byte alignment");
	for (u32 count = 0u; count <= PACK_CAPACITY; count++)
	{
		pack_fixture_reset(fixture, PACK_CAPACITY, 0u);
		CHECK(pack_check_queue(fixture, quads, count), "all batch sizes zero through 65");
	}
	pack_fixture_reset(fixture, PACK_CAPACITY, 7u);
	CHECK(pack_check_queue(fixture, quads, 3u), "append to existing vertices and indices");
	CHECK(pack_check_queue(fixture, NULL, 3u), "null input does not change queued data");
	CHECK(pack_check_queue(fixture, quads, 0u), "zero input does not change queued data");
	CHECK(pack_check_queue(fixture, NULL, 0u), "null zero input");
	CHECK(rg_text_gpu_queue_quads(NULL, quads, 1u) == 0u, "null renderer");
	pack_fixture_reset(fixture, 0u, 0u);
	CHECK(pack_check_queue(fixture, quads, 1u), "zero output capacity");
	pack_fixture_reset(fixture, PACK_CAPACITY, PACK_CAPACITY - 3u);
	CHECK(pack_check_queue(fixture, quads, PACK_INPUT_COUNT), "clamp oversized request to remaining capacity");
	CHECK(pack_check_queue(fixture, quads, 1u), "full output is a no-op");
	pack_fixture_reset(fixture, 19u, 2u);
	CHECK(pack_check_queue(fixture, quads, 3u), "first append");
	CHECK(pack_check_queue(fixture, quads + 3u, 7u), "second append");
	CHECK(pack_check_queue(fixture, quads + 10u, 50u), "final append clamps to capacity");
	CHECK(pack_check_queue(fixture, quads + 60u, 1u), "append after filling capacity");
	return 1;
}

#if defined(_WIN32)
static int pack_test_page_ends(PackFixture* fixture)
{
	// A protected page catches overreads too, including in uninstrumented assembly.
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	size_t page_size = (size_t)info.dwPageSize;
	u8* pages[3] = {NULL, NULL, NULL};
	int ok = 0;
	for (u32 i = 0u; i < 3u; i++)
	{
		pages[i] = (u8*)VirtualAlloc(NULL, page_size * 2u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!pages[i]) goto done;
		DWORD old_protection;
		if (!VirtualProtect(pages[i] + page_size, page_size, PAGE_NOACCESS, &old_protection)) goto done;
	}
	RgTextQuad* source = (RgTextQuad*)(pages[0] + page_size - sizeof(RgTextQuad));
	RgTextGpuVertex* vertices = (RgTextGpuVertex*)(pages[1] + page_size - sizeof(RgTextGpuVertex) * 4u);
	u32* indices = (u32*)(pages[2] + page_size - sizeof(u32) * 6u);
	memcpy(source, fixture->source_copy, sizeof(*source));
	pack_fixture_reset(fixture, 1u, 0u);
	pack_reference_queue(&fixture->expected, source, 1u);
	RgTextGpuRenderer renderer = {0};
	renderer.vertices = vertices;
	renderer.indices = indices;
	renderer.quad_capacity = 1u;
	if (rg_text_gpu_queue_quads(&renderer, source, 1u) != 1u ||
	    renderer.quad_count != 1u || renderer.vertex_count != 4u || renderer.index_count != 6u ||
	    memcmp(vertices, fixture->expected_vertices, sizeof(*vertices) * 4u) != 0 ||
	    memcmp(indices, fixture->expected_indices, sizeof(*indices) * 6u) != 0 ||
	    memcmp(source, fixture->source_copy, sizeof(*source)) != 0) goto done;
	// Full capacity and zero input must not touch even an inaccessible source.
	const RgTextQuad* inaccessible = (const RgTextQuad*)(pages[0] + page_size);
	if (rg_text_gpu_queue_quads(&renderer, inaccessible, 1u) != 0u ||
	    rg_text_gpu_queue_quads(&renderer, inaccessible, 0u) != 0u) goto done;
	ok = 1;
done:
	for (u32 i = 0u; i < 3u; i++)
	{
		if (pages[i]) VirtualFree(pages[i], 0u, MEM_RELEASE);
	}
	if (!ok) fprintf(stderr, "FAIL: protected page boundary packing test\n");
	return ok;
}
#endif

int main(void)
{
	PackFixture fixture;
	int ok = pack_fixture_init(&fixture);
	if (ok) ok = pack_test_contract(&fixture);
#if defined(_WIN32)
	if (ok) ok = pack_test_page_ends(&fixture);
#endif
	free(fixture.source.allocation);
	free(fixture.vertices.allocation);
	free(fixture.indices.allocation);
	if (!ok) return 1;
	puts("rg_text_gpu packing contract tests passed (CPU only)");
	return 0;
}
