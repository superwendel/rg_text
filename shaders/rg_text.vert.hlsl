cbuffer RgTextUniforms : register(b0, space1)
{
	float4x4 projection;
	float4x4 transform;
};

struct VertexInput
{
	float3 position : TEXCOORD0;
	float4 color : TEXCOORD1;
	float2 uv : TEXCOORD2;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
	VertexOutput output;
	output.position = mul(projection, mul(transform, float4(input.position, 1.0)));
	output.color = input.color;
	output.uv = input.uv;
	return output;
}
