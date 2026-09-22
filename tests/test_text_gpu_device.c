// End-to-end hidden-window SDL_GPU rendering and pixel readback checks.
// An optional backend argument selects direct3d12, vulkan, or metal.

#include "../src/rg_text_gpu.h"

#include <stdio.h>
#include <string.h>

enum { TEST_SIZE = 64, TEST_PIXEL_BYTES = TEST_SIZE * TEST_SIZE * 4 };

static void make_identity(f32 matrix[16])
{
	memset(matrix, 0, sizeof(f32) * 16u);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

static int render_and_read(RgTextGpuRenderer* renderer,
                          RgGpuUploadRing* ring,
                          SDL_GPUTexture* target,
                          SDL_GPUTransferBuffer* download,
                          const RgTextGpuUniforms* uniforms,
                          u8 pixels[TEST_PIXEL_BYTES])
{
	RgTextGpuUpload upload = {0};
	rg_gpu_upload_ring_begin(ring, 1);
	if (!ring->mapped || !rg_text_gpu_stage_upload(renderer, ring, &upload))
	{
		rg_gpu_upload_ring_end(ring);
		return 0;
	}
	rg_gpu_upload_ring_end(ring);

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(renderer->device);
	if (!command_buffer) return 0;
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
	if (!copy)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		return 0;
	}
	rg_text_gpu_encode_upload(renderer, copy, ring, &upload);
	SDL_EndGPUCopyPass(copy);

	SDL_GPUColorTargetInfo color_target = {0};
	color_target.texture = target;
	color_target.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 0.0f};
	color_target.load_op = SDL_GPU_LOADOP_CLEAR;
	color_target.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1u, NULL);
	if (!pass)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		return 0;
	}
	RgTextGpuRenderStats stats = rg_text_gpu_flush(renderer, command_buffer, pass, uniforms);
	SDL_EndGPURenderPass(pass);
	if (stats.quads != 1u || stats.indices != 6u || stats.draw_calls != 1u)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		return 0;
	}

	copy = SDL_BeginGPUCopyPass(command_buffer);
	if (!copy)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		return 0;
	}
	SDL_GPUTextureRegion source = {0};
	source.texture = target;
	source.w = TEST_SIZE;
	source.h = TEST_SIZE;
	source.d = 1u;
	SDL_GPUTextureTransferInfo destination = {0};
	destination.transfer_buffer = download;
	destination.pixels_per_row = TEST_SIZE;
	destination.rows_per_layer = TEST_SIZE;
	SDL_DownloadFromGPUTexture(copy, &source, &destination);
	SDL_EndGPUCopyPass(copy);
	if (!SDL_SubmitGPUCommandBuffer(command_buffer) || !SDL_WaitForGPUIdle(renderer->device))
		return 0;

	const void* mapped = SDL_MapGPUTransferBuffer(renderer->device, download, false);
	if (!mapped) return 0;
	memcpy(pixels, mapped, TEST_PIXEL_BYTES);
	SDL_UnmapGPUTransferBuffer(renderer->device, download);
	return 1;
}

static int expect_pixel(const char* name, const u8* pixels, int x, int y,
                        const u8 expected[4], int tolerance)
{
	const u8* actual = pixels + (y * TEST_SIZE + x) * 4;
	for (int channel = 0; channel < 4; channel++)
	{
		int difference = (int)actual[channel] - (int)expected[channel];
		if (difference < -tolerance || difference > tolerance)
		{
			fprintf(stderr, "%s pixel (%d, %d): got (%u, %u, %u, %u), expected (%u, %u, %u, %u)\n",
			        name, x, y, (unsigned)actual[0], (unsigned)actual[1],
			        (unsigned)actual[2], (unsigned)actual[3],
			        (unsigned)expected[0], (unsigned)expected[1],
			        (unsigned)expected[2], (unsigned)expected[3]);
			return 0;
		}
	}
	return 1;
}

static int expect_rectangle(const char* name, const u8* pixels,
                            int x0, int y0, int x1, int y1, const u8 color[4])
{
	const u8 clear[4] = {0u, 0u, 0u, 0u};
	for (int y = 0; y < TEST_SIZE; y++)
	{
		for (int x = 0; x < TEST_SIZE; x++)
		{
			const u8* expected = x >= x0 && x < x1 && y >= y0 && y < y1 ? color : clear;
			if (!expect_pixel(name, pixels, x, y, expected, 1)) return 0;
		}
	}
	return 1;
}

int main(int argc, char** argv)
{
	int result = 1;
	int window_claimed = 0;
	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* target = NULL;
	SDL_GPUTransferBuffer* download = NULL;
	RgGpuUploadRing ring = {0};
	RgTextGpuRenderer renderer = {0};
	u8 pixels[TEST_PIXEL_BYTES];

	if (argc > 2)
	{
		fprintf(stderr, "Usage: %s [GPU backend]\n", argv[0]);
		return 1;
	}
	if (!SDL_Init(SDL_INIT_VIDEO)) goto cleanup;
	window = SDL_CreateWindow("rg_text GPU validation", TEST_SIZE, TEST_SIZE, SDL_WINDOW_HIDDEN);
	if (!window) goto cleanup;

	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device_desc.enable_debug = 1;
	device_desc.name = argc == 2 ? argv[1] : NULL;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, window)) goto cleanup;
	window_claimed = 1;

	SDL_GPUTextureCreateInfo target_info = {0};
	target_info.type = SDL_GPU_TEXTURETYPE_2D;
	target_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	target_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	target_info.width = TEST_SIZE;
	target_info.height = TEST_SIZE;
	target_info.layer_count_or_depth = 1u;
	target_info.num_levels = 1u;
	target_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	target = SDL_CreateGPUTexture(device, &target_info);
	if (!target) goto cleanup;

	SDL_GPUTransferBufferCreateInfo download_info = {0};
	download_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	download_info.size = TEST_PIXEL_BYTES;
	download = SDL_CreateGPUTransferBuffer(device, &download_info);
	if (!download) goto cleanup;

	const u8 white_pixel[4] = {255u, 255u, 255u, 255u};
	RgTextGpuDesc desc = {0};
	desc.device = device;
	desc.target_format = target_info.format;
	desc.shader_root = "shaders";
	desc.atlas_pixels_rgba8 = white_pixel;
	desc.atlas_width = 1u;
	desc.atlas_height = 1u;
	desc.max_quads = 4u;
	desc.min_filter = SDL_GPU_FILTER_NEAREST;
	desc.mag_filter = SDL_GPU_FILTER_NEAREST;
	RgTextGpuRenderer overflow_renderer = {0};
	RgTextGpuDesc overflow_desc = desc;
	overflow_desc.max_quads = 0xFFFFFFFFu;
	if (rg_text_gpu_create(&overflow_renderer, &overflow_desc))
	{
		rg_text_gpu_destroy(&overflow_renderer);
		goto cleanup;
	}
	overflow_desc = desc;
	overflow_desc.atlas_width = 0xFFFFFFFFu;
	overflow_desc.atlas_height = 0xFFFFFFFFu;
	if (rg_text_gpu_create(&overflow_renderer, &overflow_desc))
	{
		rg_text_gpu_destroy(&overflow_renderer);
		goto cleanup;
	}
	if (!rg_text_gpu_create(&renderer, &desc)) goto cleanup;
	if (!rg_gpu_upload_ring_init(&ring, device, MB(1))) goto cleanup;

	RgTextGlyph glyph = {0};
	glyph.codepoint = 'A';
	glyph.w = 1;
	glyph.h = 1;
	glyph.x_advance = 1;
	RgTextFont font = {0};
	font.metrics.atlas_width = 1u;
	font.metrics.atlas_height = 1u;
	font.metrics.line_height = 1;
	font.glyphs = &glyph;
	font.glyph_count = 1u;
	font.glyph_capacity = 1u;
	font.fallback_codepoint = 'A';

	RgTextGpuUniforms uniforms;
	make_identity(uniforms.projection);
	make_identity(uniforms.transform);
	rg_text_gpu_begin(&renderer);
	RgTextColor white = {1.0f, 1.0f, 1.0f, 1.0f};
	if (rg_text_gpu_queue(&renderer, &font, "A", 1u, 0.0f, 0.0f, 1.0f, white) != 1u)
		goto cleanup;
	if (!render_and_read(&renderer, &ring, target, download, &uniforms, pixels) ||
	    !expect_rectangle("nearest glyph", pixels, 32, 0, 64, 32, white_pixel))
		goto cleanup;

	// Reupload straight-alpha color, then combine texture alpha with tint opacity.
	const u8 partial_pixel[4] = {128u, 64u, 32u, 128u};
	if (!rg_text_gpu_upload_atlas(&renderer, partial_pixel, 1u, 1u)) goto cleanup;
	RgTextQuad quad = {0};
	quad.x0 = -1.0f;
	quad.y0 = -1.0f;
	quad.x1 = 1.0f;
	quad.y1 = 1.0f;
	quad.u1 = 1.0f;
	quad.v1 = 1.0f;
	quad.color = (RgTextColor){0.5f, 1.0f, 0.25f, 0.5f};
	uniforms.transform[0] = 0.5f;
	uniforms.transform[5] = 0.25f;
	uniforms.transform[12] = 0.5f;
	uniforms.transform[13] = 0.25f;
	rg_text_gpu_begin(&renderer);
	if (rg_text_gpu_queue_quads(&renderer, &quad, 1u) != 1u) goto cleanup;
	const u8 tinted_pixel[4] = {16u, 16u, 2u, 64u};
	if (!render_and_read(&renderer, &ring, target, download, &uniforms, pixels) ||
	    !expect_rectangle("alpha, tint, and transform", pixels, 32, 16, 64, 32, tinted_pixel))
		goto cleanup;

	// Black transparent padding must interpolate coverage without darkening white.
	rg_text_gpu_destroy(&renderer);
	const u8 edge_pixels[8] = {255u, 255u, 255u, 255u, 0u, 0u, 0u, 0u};
	desc.atlas_pixels_rgba8 = edge_pixels;
	desc.atlas_width = 2u;
	desc.min_filter = SDL_GPU_FILTER_LINEAR;
	desc.mag_filter = SDL_GPU_FILTER_LINEAR;
	if (!rg_text_gpu_create(&renderer, &desc)) goto cleanup;
	quad.color = white;
	make_identity(uniforms.transform);
	rg_text_gpu_begin(&renderer);
	if (rg_text_gpu_queue_quads(&renderer, &quad, 1u) != 1u ||
	    !render_and_read(&renderer, &ring, target, download, &uniforms, pixels))
		goto cleanup;
	for (int y = 0; y < TEST_SIZE; y++)
	{
		for (int x = 0; x < TEST_SIZE; x++)
		{
			// Sample the two texels at each screen-pixel center with clamp-to-edge.
			int coverage = 95 - 2 * x;
			if (coverage < 0) coverage = 0;
			if (coverage > 64) coverage = 64;
			u8 value = (u8)((coverage * 255 + 32) / 64);
			u8 expected[4] = {value, value, value, value};
			if (!expect_pixel("linear edge", pixels, x, y, expected, 1)) goto cleanup;
		}
	}

	printf("rg_text SDL_GPU pixel tests passed with %s.\n", SDL_GetGPUDeviceDriver(device));
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "rg_text SDL_GPU device test failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	rg_gpu_upload_ring_destroy(&ring);
	rg_text_gpu_destroy(&renderer);
	if (download) SDL_ReleaseGPUTransferBuffer(device, download);
	if (target) SDL_ReleaseGPUTexture(device, target);
	if (window_claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device) rg_gpu_device_destroy(device);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
