// End-to-end hidden-window SDL_GPU submission checks for rg_text_gpu.

#include "../src/rg_text_gpu.h"

#include <stdio.h>
#include <string.h>

static void make_identity(f32 matrix[16])
{
	memset(matrix, 0, sizeof(f32) * 16u);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

int main(void)
{
	int result = 1;
	int window_claimed = 0;
	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* target = NULL;
	RgGpuUploadRing ring = {0};
	RgTextGpuRenderer renderer = {0};

	if (!SDL_Init(SDL_INIT_VIDEO)) goto cleanup;
	window = SDL_CreateWindow("rg_text GPU validation", 64, 64, SDL_WINDOW_HIDDEN);
	if (!window) goto cleanup;

	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, window)) goto cleanup;
	window_claimed = 1;

	SDL_GPUTextureCreateInfo target_info = {0};
	target_info.type = SDL_GPU_TEXTURETYPE_2D;
	target_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	target_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	target_info.width = 64u;
	target_info.height = 64u;
	target_info.layer_count_or_depth = 1u;
	target_info.num_levels = 1u;
	target_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	target = SDL_CreateGPUTexture(device, &target_info);
	if (!target) goto cleanup;

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

	rg_text_gpu_begin(&renderer);
	RgTextColor white = {1.0f, 1.0f, 1.0f, 1.0f};
	if (rg_text_gpu_queue(&renderer, &font, "A", 1u, 0.0f, 0.0f, 1.0f, white) != 1u)
		goto cleanup;

	RgTextGpuUpload upload = {0};
	rg_gpu_upload_ring_begin(&ring, 1);
	if (!ring.mapped || !rg_text_gpu_stage_upload(&renderer, &ring, &upload))
	{
		rg_gpu_upload_ring_end(&ring);
		goto cleanup;
	}
	rg_gpu_upload_ring_end(&ring);

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device);
	if (!command_buffer) goto cleanup;
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
	if (!copy)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto cleanup;
	}
	rg_text_gpu_encode_upload(&renderer, copy, &ring, &upload);
	SDL_EndGPUCopyPass(copy);

	SDL_GPUColorTargetInfo color_target = {0};
	color_target.texture = target;
	color_target.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};
	color_target.load_op = SDL_GPU_LOADOP_CLEAR;
	color_target.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1u, NULL);
	if (!pass)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto cleanup;
	}

	RgTextGpuUniforms uniforms;
	make_identity(uniforms.projection);
	make_identity(uniforms.transform);
	RgTextGpuRenderStats stats = rg_text_gpu_flush(&renderer, command_buffer, pass, &uniforms);
	SDL_EndGPURenderPass(pass);
	if (stats.quads != 1u || stats.indices != 6u || stats.draw_calls != 1u ||
	    !SDL_SubmitGPUCommandBuffer(command_buffer))
		goto cleanup;

	rg_gpu_wait_idle(device);
	printf("rg_text SDL_GPU device test passed with %s.\n", SDL_GetGPUDeviceDriver(device));
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "rg_text SDL_GPU device test failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	rg_gpu_upload_ring_destroy(&ring);
	rg_text_gpu_destroy(&renderer);
	if (target) SDL_ReleaseGPUTexture(device, target);
	if (window_claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device) rg_gpu_device_destroy(device);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
