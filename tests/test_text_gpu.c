// rg_text_gpu compile and layout checks

#include "../src/rg_text_gpu.h"

#include <stdio.h>

int main(void)
{
	RgTextGpuDesc desc = {0};
	desc.shader_root = "shaders";
	desc.max_quads = RG_TEXT_GPU_DEFAULT_MAX_QUADS;

	RgTextGpuUniforms uniforms = {0};
	uniforms.projection[0] = 1.0f;
	uniforms.transform[0] = 1.0f;

	if (desc.max_quads != 8192u || sizeof(uniforms) != sizeof(f32) * 32u)
	{
		fprintf(stderr, "rg_text_gpu layout check failed\n");
		return 1;
	}

	puts("rg_text_gpu compile checks passed");
	return 0;
}
