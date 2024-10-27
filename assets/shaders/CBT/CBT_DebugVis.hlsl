#include "CBT.hlsl"

#define RS_DEBUGVIS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
			"UAV(u0),"\
			"RootConstants(num32BitConstants=17, b0),"\
			"DescriptorTable(SRV(t0)),"\
			"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"

cbuffer constants : register(b)
{
	float4x4 PV;
	uint32_t maxDepth;
}

RWStructuredBuffer<uint32_t>	CBTBuffer : register(u0);
Texture2D<float>				heightMap : register(t0);
sampler							bilinear  : register(s0);

struct Vertex
{
	float2	texcoord	: texcoord;
	float4	position	: SV_Position;
	uint	heapID		: ID;
};

[RootSignature(RS_DEBUGVIS)]
Vertex DrawCBT_VS(const uint vertexID : SV_VertexID)
{
	static const float3x3 M_b[] =
	{
		M0(),
		M1()
	};
	
	const uint tid = (vertexID / 3);
	const float a = 9.0f / 16.0f;
	
	const float3 IN_points[] =
	{
		float3(-a * 0.9f, 0.0f,  0.9f),
		float3(-a * 0.9f, 0.0f, -0.9f),
		float3( a * 0.9f, 0.0f, -0.9f),
		float3( a * 0.9f, 0.0f,  0.9f),
	};
	
	const float3 IN_texcoord[] =
	{
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 0.0f, 0.0f),
		float3(1.0f, 0.0f, 0.0f),
		float3(1.0f, 1.0f, 0.0f),
	};
	
	float3x3 m =
		float3x3(
			1, 0, 0, 
			0, 1, 0, 
			0, 0, 1);
	
	const uint heapID	= DecodeNode(CBTBuffer, maxDepth, tid);
	const uint d		= FindMSB(heapID);
	
	float3x3 points;
	float3x3 UVs;
	
	if (GetBitValue(heapID, d - 1) != 0)
	{
		points = 
			float3x3(
				IN_points[0],
				IN_points[1],
				IN_points[2]);
		
		UVs =
			float3x3(
				IN_texcoord[0],
				IN_texcoord[1],
				IN_texcoord[2]);
	}
	else
	{
		points = 
			float3x3(
				IN_points[2],
				IN_points[3],
				IN_points[0]);
		
		UVs =
			float3x3(
				IN_texcoord[2],
				IN_texcoord[3],
				IN_texcoord[0]);
	}
	
	for (int bitID = d - 2; bitID >= 0; --bitID)
	{
		int idx = GetBitValue(heapID, bitID);
		m = mul(M_b[idx], m);
	}

	const float3x3 tri	= mul(m, points);
	const float3x3 UV	= mul(m, UVs);
	const float2   uv	= UV[vertexID % 3].xy;
	const float    h	= 0.1f * sqrt(heightMap.SampleLevel(bilinear, uv, 0)) - 0.75f;
	const float3 pos	= float3(tri[vertexID % 3].x, h, tri[vertexID % 3].z);
	
	Vertex OUT;
	OUT.position	= mul(PV, float4(pos * 10, 1));
	//OUT.position	= float4(pos.x, pos.z, 0, 1);//mul(PV, float4(pos * 10, 1));
	OUT.texcoord	= uv;
	OUT.heapID		= heapID;
	
	return OUT;
}


float4 DrawCBT_PS(Vertex v) : SV_Target
{
	static const float3 colors[] =
	{
		float3(1, 0, 0),
		float3(0, 1, 0),
		float3(1, 0, 1),
		float3(0, 0, 1),
	};
	
	//return float4(v.texcoord, 0, 1);// float3(1, 1, 1) * heightMap.Sample(bilinear, v.texcoord), 1);
	return float4(colors[v.heapID % 4] * heightMap.Sample(bilinear, v.texcoord), 1);

}