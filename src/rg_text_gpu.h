// rg_text_gpu - SDL_GPU renderer for rg_text bitmap fonts
//
// Part of the Reverse Gravity (rg_) libraries.
// C99-compatible optional SDL_GPU helper for uploading a bitmap font atlas and
// drawing rg_text quads.
//
// NOTES:
//   - This header depends on SDL3 GPU and rg_gpu upload rings.
//   - The caller still owns command buffers, copy passes, render passes, projection
//     uniforms, frame ordering, and scissor state.
//   - Shaders must consume vertex attributes:
//       location 0: float3 position
//       location 1: float4 color
//       location 2: float2 uv
//   - Compiled shaders are loaded through rg_gpu from the supplied shader root.
//   - Atlas inputs use straight-alpha RGBA8. Uploads premultiply RGB for filtering;
//     fragment shaders must output premultiplied color, including vertex opacity.
//   - Header functions have internal linkage and work in unity builds. The
//     optional Windows x64 assembly kernel is linked as one external object.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_TEXT_GPU_H
#define RG_TEXT_GPU_H

#include "rg_text.h"
#include "rg_gpu.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <string.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

#ifndef RG_TEXT_GPU_ASSERT
#include <assert.h>
#define RG_TEXT_GPU_ASSERT(x) assert(x)
#endif

#ifndef RG_TEXT_GPU_DEFAULT_MAX_QUADS
#define RG_TEXT_GPU_DEFAULT_MAX_QUADS 8192u
#endif

// Optional SSE2 quad packing. The portable C path remains the default.
// Define to 1 before including this header on an SSE2-capable x86 target.
#ifndef RG_TEXT_GPU_USE_SSE2
#define RG_TEXT_GPU_USE_SSE2 0
#endif

// Optional Windows x64 assembly packing. Assemble and link
// src/asm/rg_text_gpu_pack_quads_x64.asm when this is enabled.
#ifndef RG_TEXT_GPU_USE_ASM
#define RG_TEXT_GPU_USE_ASM 0
#endif

#if RG_TEXT_GPU_USE_ASM && RG_TEXT_GPU_USE_SSE2
#error Enable only one of RG_TEXT_GPU_USE_ASM and RG_TEXT_GPU_USE_SSE2
#endif

#if RG_TEXT_GPU_USE_ASM
#if !defined(_WIN32) || (!defined(_M_X64) && !defined(__x86_64__))
#error RG_TEXT_GPU_USE_ASM requires a Windows x64 target
#endif
#endif

#if RG_TEXT_GPU_USE_SSE2
#if !defined(__SSE2__) && !defined(_M_X64) && !(defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#error RG_TEXT_GPU_USE_SSE2 requires an x86 target with SSE2 enabled
#endif
#include <emmintrin.h>
#endif

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

typedef struct RgTextGpuVertex
{
	f32 x;
	f32 y;
	f32 z;
	f32 r;
	f32 g;
	f32 b;
	f32 a;
	f32 u;
	f32 v;
} RgTextGpuVertex;

#if RG_TEXT_GPU_USE_ASM
#ifdef __cplusplus
extern "C" {
#endif
extern void rg_text_gpu_pack_quads_asm(RgTextGpuVertex* vertices, u32* indices,
                                      const RgTextQuad* quads, u32 quad_count,
                                      u32 first_vertex);
#ifdef __cplusplus
}
#endif
#endif

typedef struct RgTextGpuDesc
{
	SDL_GPUDevice* device;
	SDL_GPUTextureFormat target_format;
	const char* shader_root;
	const void* atlas_pixels_rgba8; // Straight-alpha RGBA8; copied during creation.
	u32 atlas_width;
	u32 atlas_height;
	u32 max_quads;
	SDL_GPUFilter min_filter;
	SDL_GPUFilter mag_filter;
} RgTextGpuDesc;

typedef struct RgTextGpuUpload
{
	RgGpuUploadSlice vertices;
	RgGpuUploadSlice indices;
} RgTextGpuUpload;

typedef struct RgTextGpuRenderStats
{
	u32 quads;
	u32 indices;
	u32 draw_calls;
} RgTextGpuRenderStats;

typedef struct RgTextGpuUniforms
{
	f32 projection[16];
	f32 transform[16];
} RgTextGpuUniforms;

typedef struct RgTextGpuRenderer
{
	SDL_GPUDevice* device;
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUBuffer* vertex_buffer;
	SDL_GPUBuffer* index_buffer;
	SDL_GPUTexture* atlas_texture;
	SDL_GPUSampler* sampler;
	RgTextGpuVertex* vertices;
	u32* indices;
	RgTextQuad* scratch_quads;
	u32 quad_count;
	u32 quad_capacity;
	u32 vertex_count;
	u32 index_count;
	u32 atlas_width;
	u32 atlas_height;
} RgTextGpuRenderer;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Create an SDL_GPU text renderer.
 * @param renderer Renderer to initialize
 * @param desc Create descriptor
 * @return 1 on success, 0 on failure
 */
RGINLINE int rg_text_gpu_create(RgTextGpuRenderer* renderer, const RgTextGpuDesc* desc);

/**
 * @brief Destroy an SDL_GPU text renderer.
 * @param renderer Renderer to destroy
 */
RGINLINE void rg_text_gpu_destroy(RgTextGpuRenderer* renderer);

/**
 * @brief Upload straight-alpha atlas pixels, premultiplying the GPU copy.
 * @param renderer Renderer
 * @param pixels Straight-alpha RGBA8 pixels (not modified)
 * @param width Atlas width
 * @param height Atlas height
 * @return 1 on success, 0 on failure
 */
RGINLINE int rg_text_gpu_upload_atlas(RgTextGpuRenderer* renderer,
                                      const void* pixels,
                                      u32 width,
                                      u32 height);

/**
 * @brief Clear queued text for a new frame.
 * @param renderer Renderer
 */
RGINLINE void rg_text_gpu_begin(RgTextGpuRenderer* renderer);

/**
 * @brief Queue UTF-8 text.
 * @param renderer Renderer
 * @param font Font metrics
 * @param text UTF-8 bytes
 * @param text_size Byte length
 * @param x Top-left x
 * @param y Top-left y
 * @param scale Pixel scale
 * @param color Vertex color
 * @return Number of glyph quads queued
 */
RGINLINE size_t rg_text_gpu_queue(RgTextGpuRenderer* renderer,
                                  const RgTextFont* font,
                                  const char* text,
                                  size_t text_size,
                                  f32 x,
                                  f32 y,
                                  f32 scale,
                                  RgTextColor color);

/**
 * @brief Queue text using a full rg_text build descriptor.
 * @param renderer Renderer
 * @param desc Text build descriptor
 * @return Number of glyph quads queued
 */
RGINLINE size_t rg_text_gpu_queue_ex(RgTextGpuRenderer* renderer, const RgTextBuildDesc* desc);

/**
 * @brief Queue prebuilt text quads.
 * @param renderer Renderer
 * @param quads Quads; must not overlap the renderer or its output buffers
 * @param quad_count Quad count
 * @return Number of quads queued
 */
RGINLINE size_t rg_text_gpu_queue_quads(RgTextGpuRenderer* renderer,
                                        const RgTextQuad* quads,
                                        size_t quad_count);

/**
 * @brief Stage queued vertices and indices into an upload ring.
 * @param renderer Renderer
 * @param ring Mapped upload ring
 * @param out_upload Upload slices
 * @return 1 when data was staged, 0 when nothing was queued or capacity failed
 */
RGINLINE int rg_text_gpu_stage_upload(RgTextGpuRenderer* renderer,
                                      RgGpuUploadRing* ring,
                                      RgTextGpuUpload* out_upload);

/**
 * @brief Encode staged vertex/index uploads into a copy pass.
 * @param renderer Renderer
 * @param copy Copy pass
 * @param ring Upload ring
 * @param upload Upload slices
 */
RGINLINE void rg_text_gpu_encode_upload(RgTextGpuRenderer* renderer,
                                        SDL_GPUCopyPass* copy,
                                        const RgGpuUploadRing* ring,
                                        const RgTextGpuUpload* upload);

/**
 * @brief Draw all queued text. Call after rg_text_gpu_encode_upload.
 * @param renderer Renderer
 * @param command_buffer Command buffer for pushing uniforms
 * @param pass Render pass
 * @param uniforms Projection and model transform matrices
 * @return Render stats
 */
RGINLINE RgTextGpuRenderStats rg_text_gpu_flush(RgTextGpuRenderer* renderer,
                                                 SDL_GPUCommandBuffer* command_buffer,
                                                 SDL_GPURenderPass* pass,
                                                 const RgTextGpuUniforms* uniforms);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

RGINLINE int rg_text_gpu_create_pipeline(RgTextGpuRenderer* renderer,
                                         const RgTextGpuDesc* desc,
                                         SDL_GPUShader* vertex_shader,
                                         SDL_GPUShader* fragment_shader)
{
	SDL_GPUVertexBufferDescription vb_desc;
	memset(&vb_desc, 0, sizeof(vb_desc));
	vb_desc.slot = 0;
	vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	vb_desc.instance_step_rate = 0;
	vb_desc.pitch = sizeof(RgTextGpuVertex);

	SDL_GPUVertexAttribute attrs[3];
	memset(attrs, 0, sizeof(attrs));
	attrs[0].buffer_slot = 0;
	attrs[0].location = 0;
	attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	attrs[0].offset = 0;
	attrs[1].buffer_slot = 0;
	attrs[1].location = 1;
	attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
	attrs[1].offset = sizeof(f32) * 3u;
	attrs[2].buffer_slot = 0;
	attrs[2].location = 2;
	attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attrs[2].offset = sizeof(f32) * 7u;

	SDL_GPUVertexInputState vi;
	memset(&vi, 0, sizeof(vi));
	vi.num_vertex_buffers = 1;
	vi.vertex_buffer_descriptions = &vb_desc;
	vi.num_vertex_attributes = 3;
	vi.vertex_attributes = attrs;

	SDL_GPUColorTargetBlendState blend;
	memset(&blend, 0, sizeof(blend));
	blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
	                         SDL_GPU_COLORCOMPONENT_G |
	                         SDL_GPU_COLORCOMPONENT_B |
	                         SDL_GPU_COLORCOMPONENT_A;
	blend.enable_blend = true;
	blend.enable_color_write_mask = true;

	SDL_GPUColorTargetDescription color_target;
	memset(&color_target, 0, sizeof(color_target));
	color_target.format = desc->target_format;
	color_target.blend_state = blend;

	SDL_GPUGraphicsPipelineCreateInfo pipeline_info;
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.target_info.num_color_targets = 1;
	pipeline_info.target_info.color_target_descriptions = &color_target;
	pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.vertex_shader = vertex_shader;
	pipeline_info.fragment_shader = fragment_shader;
	pipeline_info.vertex_input_state = vi;

	renderer->pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &pipeline_info);
	return renderer->pipeline != NULL ? 1 : 0;
}

RGINLINE int rg_text_gpu_create(RgTextGpuRenderer* renderer, const RgTextGpuDesc* desc)
{
	RG_TEXT_GPU_ASSERT(renderer != NULL);
	RG_TEXT_GPU_ASSERT(desc != NULL);

	if (!renderer || !desc || !desc->device || !desc->shader_root ||
	    desc->target_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
	    desc->atlas_width == 0u || desc->atlas_height == 0u)
	{
		return 0;
	}

	memset(renderer, 0, sizeof(*renderer));
	renderer->device = desc->device;
	renderer->quad_capacity = desc->max_quads ? desc->max_quads : RG_TEXT_GPU_DEFAULT_MAX_QUADS;
	renderer->atlas_width = desc->atlas_width;
	renderer->atlas_height = desc->atlas_height;
	u64 vertex_bytes64 = (u64)sizeof(RgTextGpuVertex) * (u64)renderer->quad_capacity * 4ull;
	u64 index_bytes64 = (u64)sizeof(u32) * (u64)renderer->quad_capacity * 6ull;
	u64 quad_bytes64 = (u64)sizeof(RgTextQuad) * (u64)renderer->quad_capacity;
	u64 atlas_bytes64 = (u64)desc->atlas_width * (u64)desc->atlas_height * 4ull;
	if (renderer->quad_capacity == 0u || vertex_bytes64 > 0xFFFFFFFFull ||
	    index_bytes64 > 0xFFFFFFFFull || quad_bytes64 > (u64)SIZE_MAX ||
	    atlas_bytes64 > 0xFFFFFFFFull)
	{
		memset(renderer, 0, sizeof(*renderer));
		return 0;
	}

	renderer->vertices = (RgTextGpuVertex*)SDL_malloc((size_t)vertex_bytes64);
	renderer->indices = (u32*)SDL_malloc((size_t)index_bytes64);
	renderer->scratch_quads = (RgTextQuad*)SDL_malloc((size_t)quad_bytes64);
	if (!renderer->vertices || !renderer->indices || !renderer->scratch_quads)
	{
		rg_text_gpu_destroy(renderer);
		return 0;
	}

	RgGpuShaderDesc vertex_desc = {0};
	vertex_desc.name = "rg_text.vert";
	vertex_desc.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	vertex_desc.uniform_buffer_count = 1u;
	SDL_GPUShader* vertex_shader = rg_gpu_shader_load(desc->device, desc->shader_root, &vertex_desc);

	RgGpuShaderDesc fragment_desc = {0};
	fragment_desc.name = "rg_text.frag";
	fragment_desc.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	fragment_desc.sampler_count = 1u;
	SDL_GPUShader* fragment_shader = rg_gpu_shader_load(desc->device, desc->shader_root, &fragment_desc);

	if (!vertex_shader || !fragment_shader ||
	    !rg_text_gpu_create_pipeline(renderer, desc, vertex_shader, fragment_shader))
	{
		if (vertex_shader) SDL_ReleaseGPUShader(desc->device, vertex_shader);
		if (fragment_shader) SDL_ReleaseGPUShader(desc->device, fragment_shader);
		rg_text_gpu_destroy(renderer);
		return 0;
	}

	SDL_ReleaseGPUShader(desc->device, vertex_shader);
	SDL_ReleaseGPUShader(desc->device, fragment_shader);

	SDL_GPUBufferCreateInfo vb_info;
	memset(&vb_info, 0, sizeof(vb_info));
	vb_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	vb_info.size = (u32)vertex_bytes64;
	renderer->vertex_buffer = SDL_CreateGPUBuffer(desc->device, &vb_info);

	SDL_GPUBufferCreateInfo ib_info;
	memset(&ib_info, 0, sizeof(ib_info));
	ib_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
	ib_info.size = (u32)index_bytes64;
	renderer->index_buffer = SDL_CreateGPUBuffer(desc->device, &ib_info);

	SDL_GPUTextureCreateInfo tex_info;
	memset(&tex_info, 0, sizeof(tex_info));
	tex_info.type = SDL_GPU_TEXTURETYPE_2D;
	tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	tex_info.width = desc->atlas_width;
	tex_info.height = desc->atlas_height;
	tex_info.layer_count_or_depth = 1;
	tex_info.num_levels = 1;
	renderer->atlas_texture = SDL_CreateGPUTexture(desc->device, &tex_info);

	SDL_GPUSamplerCreateInfo sampler_info;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.min_filter = desc->min_filter;
	sampler_info.mag_filter = desc->mag_filter;
	sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	renderer->sampler = SDL_CreateGPUSampler(desc->device, &sampler_info);

	if (!renderer->vertex_buffer || !renderer->index_buffer || !renderer->atlas_texture || !renderer->sampler)
	{
		rg_text_gpu_destroy(renderer);
		return 0;
	}

	if (desc->atlas_pixels_rgba8 &&
	    !rg_text_gpu_upload_atlas(renderer, desc->atlas_pixels_rgba8, desc->atlas_width, desc->atlas_height))
	{
		rg_text_gpu_destroy(renderer);
		return 0;
	}

	return 1;
}

RGINLINE void rg_text_gpu_destroy(RgTextGpuRenderer* renderer)
{
	if (!renderer)
	{
		return;
	}

	if (renderer->sampler)
	{
		SDL_ReleaseGPUSampler(renderer->device, renderer->sampler);
	}
	if (renderer->atlas_texture)
	{
		SDL_ReleaseGPUTexture(renderer->device, renderer->atlas_texture);
	}
	if (renderer->index_buffer)
	{
		SDL_ReleaseGPUBuffer(renderer->device, renderer->index_buffer);
	}
	if (renderer->vertex_buffer)
	{
		SDL_ReleaseGPUBuffer(renderer->device, renderer->vertex_buffer);
	}
	if (renderer->pipeline)
	{
		SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
	}
	if (renderer->scratch_quads)
	{
		SDL_free(renderer->scratch_quads);
	}
	if (renderer->indices)
	{
		SDL_free(renderer->indices);
	}
	if (renderer->vertices)
	{
		SDL_free(renderer->vertices);
	}

	memset(renderer, 0, sizeof(*renderer));
}

RGINLINE int rg_text_gpu_upload_atlas(RgTextGpuRenderer* renderer,
                                      const void* pixels,
                                      u32 width,
                                      u32 height)
{
	if (!renderer || !renderer->device || !renderer->atlas_texture || !pixels ||
	    width == 0u || height == 0u || width != renderer->atlas_width || height != renderer->atlas_height)
	{
		return 0;
	}

	u64 size64 = (u64)width * (u64)height * 4ull;
	if (size64 == 0u || size64 > 0xFFFFFFFFull)
	{
		return 0;
	}
	u32 size = (u32)size64;
	RgGpuUploadRing staging = {0};
	if (!rg_gpu_upload_ring_init(&staging, renderer->device, size))
	{
		return 0;
	}

	rg_gpu_upload_ring_begin(&staging, 0);
	if (!staging.mapped)
	{
		rg_gpu_upload_ring_destroy(&staging);
		return 0;
	}

	const u8* source_pixels = (const u8*)pixels;
	u8* upload_pixels = (u8*)staging.mapped;
	for (u32 offset = 0u; offset < size; offset += 4u)
	{
		u32 alpha = source_pixels[offset + 3u];
		upload_pixels[offset + 0u] = (u8)(((u32)source_pixels[offset + 0u] * alpha + 127u) / 255u);
		upload_pixels[offset + 1u] = (u8)(((u32)source_pixels[offset + 1u] * alpha + 127u) / 255u);
		upload_pixels[offset + 2u] = (u8)(((u32)source_pixels[offset + 2u] * alpha + 127u) / 255u);
		upload_pixels[offset + 3u] = (u8)alpha;
	}
	rg_gpu_upload_ring_end(&staging);

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(renderer->device);
	if (!command_buffer)
	{
		rg_gpu_upload_ring_destroy(&staging);
		return 0;
	}

	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
	SDL_GPUTextureTransferInfo src;
	memset(&src, 0, sizeof(src));
	src.transfer_buffer = staging.buffer;
	src.offset = 0;
	src.pixels_per_row = width;
	src.rows_per_layer = height;

	SDL_GPUTextureRegion dst;
	memset(&dst, 0, sizeof(dst));
	dst.texture = renderer->atlas_texture;
	dst.mip_level = 0;
	dst.layer = 0;
	dst.x = 0;
	dst.y = 0;
	dst.z = 0;
	dst.w = width;
	dst.h = height;
	dst.d = 1;

	SDL_UploadToGPUTexture(copy, &src, &dst, false);
	SDL_EndGPUCopyPass(copy);

	int ok = SDL_SubmitGPUCommandBuffer(command_buffer) ? 1 : 0;
	rg_gpu_upload_ring_destroy(&staging);
	return ok;
}

RGINLINE void rg_text_gpu_begin(RgTextGpuRenderer* renderer)
{
	if (!renderer)
	{
		return;
	}

	renderer->quad_count = 0u;
	renderer->vertex_count = 0u;
	renderer->index_count = 0u;
}

// Cache the output arrays and counts, then publish counts once per batch.
RGINLINE size_t rg_text_gpu__queue_quads_c(RgTextGpuRenderer* renderer,
                                        const RgTextQuad* quads,
                                        size_t quad_count)
{
	if (!renderer || !quads || quad_count == 0u ||
	    renderer->quad_count >= renderer->quad_capacity)
	{
		return 0u;
	}

	size_t available = (size_t)renderer->quad_capacity - (size_t)renderer->quad_count;
	if (quad_count > available)
	{
		quad_count = available;
	}

	u32 vertex_count = renderer->vertex_count;
	u32 index_count = renderer->index_count;
	RgTextGpuVertex* vertices = renderer->vertices;
	u32* indices = renderer->indices;
	for (size_t i = 0u; i < quad_count; i++)
	{
		const RgTextQuad value = quads[i];
		const RgTextQuad* q = &value;
		u32 vertex_base = vertex_count;
		u32 index_base = index_count;
		RgTextGpuVertex* v = &vertices[vertex_base];
		u32* idx = &indices[index_base];

		v[0].x = q->x0;
		v[0].y = q->y0;
		v[0].z = 0.0f;
		v[0].r = q->color.r;
		v[0].g = q->color.g;
		v[0].b = q->color.b;
		v[0].a = q->color.a;
		v[0].u = q->u0;
		v[0].v = q->v0;
		v[1].x = q->x1;
		v[1].y = q->y0;
		v[1].z = 0.0f;
		v[1].r = q->color.r;
		v[1].g = q->color.g;
		v[1].b = q->color.b;
		v[1].a = q->color.a;
		v[1].u = q->u1;
		v[1].v = q->v0;
		v[2].x = q->x1;
		v[2].y = q->y1;
		v[2].z = 0.0f;
		v[2].r = q->color.r;
		v[2].g = q->color.g;
		v[2].b = q->color.b;
		v[2].a = q->color.a;
		v[2].u = q->u1;
		v[2].v = q->v1;
		v[3].x = q->x0;
		v[3].y = q->y1;
		v[3].z = 0.0f;
		v[3].r = q->color.r;
		v[3].g = q->color.g;
		v[3].b = q->color.b;
		v[3].a = q->color.a;
		v[3].u = q->u0;
		v[3].v = q->v1;

		idx[0] = vertex_base + 0u;
		idx[1] = vertex_base + 1u;
		idx[2] = vertex_base + 2u;
		idx[3] = vertex_base + 0u;
		idx[4] = vertex_base + 2u;
		idx[5] = vertex_base + 3u;

		vertex_count += 4u;
		index_count += 6u;
	}

	renderer->quad_count += (u32)quad_count;
	renderer->vertex_count = vertex_count;
	renderer->index_count = index_count;
	return quad_count;
}

#if RG_TEXT_GPU_USE_SSE2 || RG_TEXT_GPU_USE_ASM
// Fail at compile time if a structure edit invalidates the kernel's offsets.
typedef char RgTextGpuPackLayoutCheck[
	sizeof(f32) == 4 && sizeof(u32) == 4 && sizeof(int) == 4 && sizeof(RgTextQuad) == 48 &&
	offsetof(RgTextQuad, x0) == 0 && offsetof(RgTextQuad, y0) == 4 &&
	offsetof(RgTextQuad, x1) == 8 && offsetof(RgTextQuad, y1) == 12 &&
	offsetof(RgTextQuad, u0) == 16 && offsetof(RgTextQuad, v0) == 20 &&
	offsetof(RgTextQuad, u1) == 24 && offsetof(RgTextQuad, v1) == 28 &&
	offsetof(RgTextQuad, color) == 32 && sizeof(RgTextColor) == 16 &&
	offsetof(RgTextColor, r) == 0 && offsetof(RgTextColor, g) == 4 &&
	offsetof(RgTextColor, b) == 8 && offsetof(RgTextColor, a) == 12 &&
	sizeof(RgTextGpuVertex) == 36 && offsetof(RgTextGpuVertex, x) == 0 &&
	offsetof(RgTextGpuVertex, y) == 4 && offsetof(RgTextGpuVertex, z) == 8 &&
	offsetof(RgTextGpuVertex, r) == 12 && offsetof(RgTextGpuVertex, g) == 16 &&
	offsetof(RgTextGpuVertex, b) == 20 && offsetof(RgTextGpuVertex, a) == 24 &&
	offsetof(RgTextGpuVertex, u) == 28 && offsetof(RgTextGpuVertex, v) == 32 ? 1 : -1];
#endif

#if RG_TEXT_GPU_USE_SSE2
RGINLINE void rg_text_gpu__pack_quads_sse2(RgTextGpuVertex* vertices, u32* indices,
                                          const RgTextQuad* quads, u32 quad_count,
                                          u32 first_vertex)
{
	int signed_base;
	memcpy(&signed_base, &first_vertex, sizeof(signed_base));
	__m128i base = _mm_set1_epi32(signed_base);
	__m128i indices0120 = _mm_add_epi32(base, _mm_setr_epi32(0, 1, 2, 0));
	__m128i indices2300 = _mm_add_epi32(base, _mm_setr_epi32(2, 3, 0, 0));
	const __m128i step = _mm_set1_epi32(4);
	for (u32 i = 0u; i < quad_count; i++)
	{
		// Fixed-size memcpy permits unaligned, alias-safe access across fields.
		// Only moves and shuffles touch float values, preserving their bits.
		unsigned char* output = (unsigned char*)vertices;
		const unsigned char* input = (const unsigned char*)&quads[i];
		__m128 p, uv, c, t;
		memcpy(&p, input, 16);
		memcpy(&uv, input + 16, 16);
		memcpy(&c, input + 32, 16);
		t = _mm_move_ss(c, uv);
		t = _mm_shuffle_ps(t, t, 0x39);
		memcpy(output + 16, &t, 16); // g b a u0
		t = _mm_shuffle_ps(uv, p, 0x65);
		t = _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(t), 4));
		memcpy(output + 32, &t, 16); // v0 x1 y0 0
		memcpy(output + 48, &c, 16); // r g b a
		t = _mm_shuffle_ps(uv, p, 0xE6);
		memcpy(output + 64, &t, 16); // u1 v0 x1 y1
		t = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(c), 4));
		memcpy(output + 80, &t, 16); // 0 r g b
		t = _mm_shuffle_ps(c, uv, 0xEF);
		t = _mm_move_ss(t, p);
		t = _mm_shuffle_ps(t, t, 0x39);
		memcpy(output + 96, &t, 16); // a u1 v1 x0
		t = _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(p), 12));
		t = _mm_movelh_ps(t, c);
		memcpy(output + 112, &t, 16); // y1 0 r g
		t = _mm_shuffle_ps(c, uv, 0xCE);
		memcpy(output + 128, &t, 16); // b a u0 v1
		t = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(c), 4));
		t = _mm_shuffle_ps(p, t, 0x44);
		memcpy(output, &t, 16); // x0 y0 0 r
		memcpy(indices, &indices0120, 16);
		// This intrinsic permits unaligned stores. MSVC spills the live vector
		// for an equivalent 8-byte memcpy; store its low half directly instead.
		_mm_storel_epi64((__m128i*)(void*)(indices + 4), indices2300);
		indices0120 = _mm_add_epi32(indices0120, step);
		indices2300 = _mm_add_epi32(indices2300, step);
		vertices += 4;
		indices += 6;
	}
}
#endif

RGINLINE size_t rg_text_gpu_queue_quads(RgTextGpuRenderer* renderer,
                                        const RgTextQuad* quads,
                                        size_t quad_count)
{
#if !RG_TEXT_GPU_USE_ASM && !RG_TEXT_GPU_USE_SSE2
	return rg_text_gpu__queue_quads_c(renderer, quads, quad_count);
#else
	if (!renderer || !quads || quad_count == 0u ||
	    renderer->quad_count >= renderer->quad_capacity)
	{
		return 0u;
	}

	size_t available = (size_t)renderer->quad_capacity - (size_t)renderer->quad_count;
	if (quad_count > available)
	{
		quad_count = available;
	}

	RgTextGpuVertex* vertices = renderer->vertices + renderer->vertex_count;
	u32* indices = renderer->indices + renderer->index_count;
#if RG_TEXT_GPU_USE_ASM
	rg_text_gpu_pack_quads_asm(vertices, indices, quads, (u32)quad_count, renderer->vertex_count);
#elif RG_TEXT_GPU_USE_SSE2
	rg_text_gpu__pack_quads_sse2(vertices, indices, quads, (u32)quad_count, renderer->vertex_count);
#endif
	renderer->quad_count += (u32)quad_count;
	renderer->vertex_count += (u32)quad_count * 4u;
	renderer->index_count += (u32)quad_count * 6u;
	return quad_count;
#endif
}

RGINLINE size_t rg_text_gpu_queue(RgTextGpuRenderer* renderer,
                                  const RgTextFont* font,
                                  const char* text,
                                  size_t text_size,
                                  f32 x,
                                  f32 y,
                                  f32 scale,
                                  RgTextColor color)
{
	if (!renderer || !font || !text || text_size == 0u)
	{
		return 0u;
	}

	size_t available = (size_t)renderer->quad_capacity - (size_t)renderer->quad_count;
	if (available == 0u)
	{
		return 0u;
	}

	size_t built = rg_text_build_quads(font, text, text_size, x, y, scale, color,
	                                   renderer->scratch_quads, available);
	return rg_text_gpu_queue_quads(renderer, renderer->scratch_quads, built);
}

RGINLINE size_t rg_text_gpu_queue_ex(RgTextGpuRenderer* renderer, const RgTextBuildDesc* desc)
{
	if (!renderer || !desc)
	{
		return 0u;
	}

	size_t available = (size_t)renderer->quad_capacity - (size_t)renderer->quad_count;
	if (available == 0u)
	{
		return 0u;
	}

	RgTextBuildDesc local = *desc;
	local.quads = renderer->scratch_quads;
	local.quad_capacity = available;
	size_t built = rg_text_build_quads_ex(&local);
	return rg_text_gpu_queue_quads(renderer, renderer->scratch_quads, built);
}

RGINLINE int rg_text_gpu_stage_upload(RgTextGpuRenderer* renderer,
                                      RgGpuUploadRing* ring,
                                      RgTextGpuUpload* out_upload)
{
	if (!renderer || !ring || !out_upload || renderer->vertex_count == 0u || renderer->index_count == 0u)
	{
		return 0;
	}

	memset(out_upload, 0, sizeof(*out_upload));
	u64 vertex_size64 = (u64)sizeof(RgTextGpuVertex) * (u64)renderer->vertex_count;
	u64 index_size64 = (u64)sizeof(u32) * (u64)renderer->index_count;
	if (vertex_size64 > 0xFFFFFFFFull || index_size64 > 0xFFFFFFFFull)
	{
		return 0;
	}
	u32 vertex_size = (u32)vertex_size64;
	u32 index_size = (u32)index_size64;
	u32 base_offset = ring->offset;

	if (!rg_gpu_upload_ring_alloc(ring, vertex_size, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &out_upload->vertices))
	{
		return 0;
	}

	if (!rg_gpu_upload_ring_alloc(ring, index_size, sizeof(u32), &out_upload->indices))
	{
		ring->offset = base_offset;
		memset(out_upload, 0, sizeof(*out_upload));
		return 0;
	}

	SDL_memcpy(rg_gpu_upload_ring_ptr(ring, &out_upload->vertices), renderer->vertices, vertex_size);
	SDL_memcpy(rg_gpu_upload_ring_ptr(ring, &out_upload->indices), renderer->indices, index_size);
	return 1;
}

RGINLINE void rg_text_gpu_encode_upload(RgTextGpuRenderer* renderer,
                                        SDL_GPUCopyPass* copy,
                                        const RgGpuUploadRing* ring,
                                        const RgTextGpuUpload* upload)
{
	if (!renderer || !copy || !ring || !upload ||
	    upload->vertices.size == 0u || upload->indices.size == 0u)
	{
		return;
	}

	SDL_GPUTransferBufferLocation v_src;
	v_src.transfer_buffer = ring->buffer;
	v_src.offset = upload->vertices.offset;
	SDL_GPUBufferRegion v_dst;
	v_dst.buffer = renderer->vertex_buffer;
	v_dst.offset = 0;
	v_dst.size = upload->vertices.size;
	SDL_UploadToGPUBuffer(copy, &v_src, &v_dst, true);

	SDL_GPUTransferBufferLocation i_src;
	i_src.transfer_buffer = ring->buffer;
	i_src.offset = upload->indices.offset;
	SDL_GPUBufferRegion i_dst;
	i_dst.buffer = renderer->index_buffer;
	i_dst.offset = 0;
	i_dst.size = upload->indices.size;
	SDL_UploadToGPUBuffer(copy, &i_src, &i_dst, true);
}

RGINLINE RgTextGpuRenderStats rg_text_gpu_flush(RgTextGpuRenderer* renderer,
                                                 SDL_GPUCommandBuffer* command_buffer,
                                                 SDL_GPURenderPass* pass,
                                                 const RgTextGpuUniforms* uniforms)
{
	RgTextGpuRenderStats stats;
	memset(&stats, 0, sizeof(stats));

	if (!renderer || !command_buffer || !pass || renderer->index_count == 0u)
	{
		return stats;
	}

	SDL_BindGPUGraphicsPipeline(pass, renderer->pipeline);

	SDL_GPUBufferBinding vb;
	vb.buffer = renderer->vertex_buffer;
	vb.offset = 0;
	SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

	SDL_GPUBufferBinding ib;
	ib.buffer = renderer->index_buffer;
	ib.offset = 0;
	SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

	if (uniforms)
	{
		SDL_PushGPUVertexUniformData(command_buffer, 0, uniforms, sizeof(*uniforms));
	}

	SDL_GPUTextureSamplerBinding binding;
	binding.texture = renderer->atlas_texture;
	binding.sampler = renderer->sampler;
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

	SDL_DrawGPUIndexedPrimitives(pass, renderer->index_count, 1, 0, 0, 0);
	stats.quads = renderer->quad_count;
	stats.indices = renderer->index_count;
	stats.draw_calls = 1u;
	return stats;
}

#endif // RG_TEXT_GPU_H
