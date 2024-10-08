#include "CBT.hlsl"

#define RS_DEBUGVIS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
			"UAV(u0),"\
			"RootConstants(num32BitConstants=1, b0)"

cbuffer constants : register(b)
{
	uint32_t maxDepth;
}

RWStructuredBuffer<uint32_t> CBTBuffer : register(u0);

struct Vertex
{
	float3	color		: COLOR;
	float4	position	: SV_Position;
	uint	heapID		: ID;
};

[RootSignature(RS_DEBUGVIS)]
Vertex DrawCBT_VS(const uint vertexID : SV_VertexID)
{
	static const float3 colors[] =
	{
		float3(1, 0, 0),
		float3(0, 1, 0),
		float3(0, 0, 1),
		float3(1, 0, 1),
	};

	static const float3x3 M_b[] =
	{
		M0(),
		M1()
	};
	
	const uint tid = (vertexID / 3);
	const float a = 9.0f / 16.0f;
	
	float3x3 t =
	{
		float3(-a * 0.9f,  0.9f, 0.1f),
		float3(-a * 0.9f, -0.9f, 0.1f),
		float3( a * 0.9f, -0.9f, 0.1f),
	};
	
	float3x3 m =
		float3x3(
			1, 0, 0, 
			0, 1, 0, 
			0, 0, 1);
	
	const uint heapID	= DecodeNode(CBTBuffer, maxDepth, tid);
	const uint d		= FindMSB(heapID);

	for (int bitID = d - 1; bitID >= 0; --bitID)
	{
		int idx = GetBitValue(heapID, bitID);
		m = mul(M_b[idx], m);
	}

	const float3x3 tri = mul(m, t);
	
	Vertex OUT;
	OUT.position = float4(tri[vertexID % 3].xyz, 1);
	OUT.color    = colors[d % 3];
	OUT.heapID	 = heapID;
	return OUT;
}


float4 DrawCBT_PS(Vertex v) : SV_Target
{
	return float4(v.color, 1);
}