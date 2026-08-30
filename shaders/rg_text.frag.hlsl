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
	return input.color * atlas_texture.Sample(atlas_sampler, input.uv);
}
