// Minimal SDL3 GPU example. Run from the repository root after building shaders.
// The bundled pixel font is original sample art, included under this project's MIT license.
#include "../src/rg_text_gpu.h"
#include "assets/demo_font.h"

#include <stdio.h>

static size_t queue_centered(RgTextGpuRenderer* renderer, const RgTextFont* font,
                             const char* text, f32 y, f32 scale, f32 width, RgTextColor color)
{
	RgTextBuildDesc desc = {0};
	desc.font = font;
	desc.text = text;
	desc.text_size = strlen(text);
	desc.y = y;
	desc.scale = scale;
	desc.align = RG_TEXT_ALIGN_CENTER;
	desc.align_width = width;
	desc.color = color;
	return rg_text_gpu_queue_ex(renderer, &desc);
}

int main(int argc, char** argv)
{
	int smoke_test = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
	if (argc > 2 || (argc == 2 && !smoke_test))
	{
		fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
		return 1;
	}
	int result = 1, claimed = 0;
	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* smoke_target = NULL;
	RgTextGpuRenderer renderer = {0};
	RgGpuUploadRing ring = {0};

	// Parse metrics into caller-owned storage, then expand the embedded 1-bit atlas.
	RgTextGlyph glyphs[DEMO_GLYPH_COUNT];
	RgTextFont font;
	RgTextFontLoadDesc load = {0};
	load.data = demo_font_metrics;
	load.data_size = sizeof(demo_font_metrics) - 1u;
	load.glyphs = glyphs;
	load.glyph_capacity = DEMO_GLYPH_COUNT;
	if (!rg_text_font_load_rgfont(&font, &load))
	{
		fprintf(stderr, "Invalid embedded demo font.\n");
		return 1;
	}
	u8 atlas[DEMO_ATLAS_WIDTH * DEMO_ATLAS_HEIGHT * 4] = {0};
	for (u32 i = 0; i < font.glyph_count; i++)
	{
		const RgTextGlyph* glyph = &glyphs[i];
		for (i32 row = 0; row < glyph->h; row++)
			for (i32 col = 0; col < glyph->w; col++)
				if (demo_font_rows[glyph->codepoint][row] & (1u << (4 - col)))
					memset(atlas + ((glyph->y + row) * DEMO_ATLAS_WIDTH + glyph->x + col) * 4, 255, 4);
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) goto cleanup;
	window = SDL_CreateWindow("rg_text - bitmap text", 640, 360,
	                          SDL_WINDOW_RESIZABLE | (smoke_test ? SDL_WINDOW_HIDDEN : 0));
	if (!window) goto cleanup;
	RgGpuDeviceDesc device_desc = {0};
	device_desc.enable_debug = 1;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, window)) goto cleanup;
	claimed = 1;
	SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(device, window);
	if (smoke_test)
	{
		// Hidden windows can have no swapchain texture; smoke mode renders offscreen.
		SDL_GPUTextureCreateInfo target = {0};
		target.type = SDL_GPU_TEXTURETYPE_2D;
		target.format = format;
		target.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
		target.width = 640;
		target.height = 360;
		target.layer_count_or_depth = 1;
		target.num_levels = 1;
		smoke_target = SDL_CreateGPUTexture(device, &target);
		if (!smoke_target) goto cleanup;
	}
	RgTextGpuDesc desc = {0};
	desc.device = device;
	desc.target_format = format;
	desc.shader_root = "shaders";
	desc.atlas_pixels_rgba8 = atlas;
	desc.atlas_width = DEMO_ATLAS_WIDTH;
	desc.atlas_height = DEMO_ATLAS_HEIGHT;
	desc.max_quads = 256;
	desc.min_filter = SDL_GPU_FILTER_NEAREST;
	desc.mag_filter = SDL_GPU_FILTER_NEAREST;
	if (!rg_text_gpu_create(&renderer, &desc) ||
	    !rg_gpu_upload_ring_init(&ring, device, 64u * 1024u)) goto cleanup;

	int running = 1;
	while (running)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
			if (!smoke_test && (event.type == SDL_EVENT_QUIT ||
			    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))) running = 0;
		if (!running) break;

		SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
		if (!command) goto cleanup;
		u32 width = 640, height = 360;
		SDL_GPUTexture* target = smoke_target;
		if (!smoke_test && !SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &target, &width, &height))
		{
			SDL_CancelGPUCommandBuffer(command);
			goto cleanup;
		}
		if (!target)
		{
			if (!SDL_SubmitGPUCommandBuffer(command)) goto cleanup;
			SDL_Delay(16);
			continue;
		}

		f32 scale_x = (f32)width / 640.0f, scale_y = (f32)height / 360.0f;
		f32 scale = scale_x < scale_y ? scale_x : scale_y;
		rg_text_gpu_begin(&renderer);
		queue_centered(&renderer, &font, "RG TEXT", 40.0f * scale, 6.0f * scale,
		               (f32)width, (RgTextColor){0.35f, 0.85f, 1.0f, 1.0f});
		queue_centered(&renderer, &font, "SCORE: 0123456789", 125.0f * scale, 3.0f * scale,
		               (f32)width, (RgTextColor){1.0f, 0.75f, 0.3f, 1.0f});
		queue_centered(&renderer, &font, "SIMPLE BITMAP TEXT\nRESIZE THE WINDOW", 200.0f * scale, 2.0f * scale,
		               (f32)width, (RgTextColor){0.9f, 0.95f, 1.0f, 1.0f});
		const char* footer = "ESC TO QUIT";
		rg_text_gpu_queue(&renderer, &font, footer, strlen(footer), 20.0f * scale,
		                  (f32)height - 28.0f * scale, 2.0f * scale, (RgTextColor){0.5f, 0.65f, 0.8f, 1.0f});

		RgTextGpuUpload upload = {0};
		rg_gpu_upload_ring_begin(&ring, 1);
		int staged = rg_text_gpu_stage_upload(&renderer, &ring, &upload);
		rg_gpu_upload_ring_end(&ring); // Always unmap before encoding GPU copies.
		if (!staged)
		{
			// A command buffer with an acquired swapchain texture must be submitted.
			SDL_SubmitGPUCommandBuffer(command);
			goto cleanup;
		}
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
		if (!copy)
		{
			SDL_SubmitGPUCommandBuffer(command);
			goto cleanup;
		}
		rg_text_gpu_encode_upload(&renderer, copy, &ring, &upload);
		SDL_EndGPUCopyPass(copy);

		// Column-major, top-left pixel coordinates; rebuild from actual drawable size.
		RgTextGpuUniforms uniforms = {0};
		uniforms.projection[0] = 2.0f / (f32)width;
		uniforms.projection[5] = -2.0f / (f32)height;
		uniforms.projection[10] = uniforms.projection[15] = 1.0f;
		uniforms.projection[12] = -1.0f;
		uniforms.projection[13] = 1.0f;
		uniforms.transform[0] = uniforms.transform[5] = 1.0f;
		uniforms.transform[10] = uniforms.transform[15] = 1.0f;
		SDL_GPUColorTargetInfo color_target = {0};
		color_target.texture = target;
		color_target.clear_color = (SDL_FColor){0.035f, 0.06f, 0.095f, 1.0f};
		color_target.load_op = SDL_GPU_LOADOP_CLEAR;
		color_target.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &color_target, 1, NULL);
		if (!pass)
		{
			SDL_SubmitGPUCommandBuffer(command);
			goto cleanup;
		}
		rg_text_gpu_flush(&renderer, command, pass, &uniforms);
		SDL_EndGPURenderPass(pass);
		if (!SDL_SubmitGPUCommandBuffer(command)) goto cleanup;
		if (smoke_test) running = 0;
	}
	if (!SDL_WaitForGPUIdle(device)) goto cleanup;
	if (smoke_test) puts("hello_text smoke test passed.");
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "hello_text failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	rg_gpu_upload_ring_destroy(&ring);
	rg_text_gpu_destroy(&renderer);
	if (smoke_target) SDL_ReleaseGPUTexture(device, smoke_target);
	if (claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device) rg_gpu_device_destroy(device);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
