Texture2D<float4> atlas_texture : register(t0, space2);
SamplerState atlas_sampler : register(s0, space2);

struct FragmentInput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
	float4 sample_color = atlas_texture.Sample(atlas_sampler, input.uv);
	return sample_color * float4(input.color.rgb * input.color.a, input.color.a);
}
