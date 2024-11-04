#include "CBT.hlsl"

#define RS_DEBUGVIS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
			"UAV(u0),"\
			"RootConstants(num32BitConstants=18, b1),"\
			"DescriptorTable(SRV(t0)),"\
			"RootConstants(num32BitConstants=1, b0),"\
			"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"


cbuffer indirectArguments : register(b0)
{
	uint32_t	nodeCount;
}

cbuffer constants : register(b1)
{
	float4x4	PV;
	uint32_t	maxDepth;
	float		scale;
}

RWStructuredBuffer<uint32_t>	CBTBuffer	: register(u0);
Texture2D<float>				heightMap	: register(t0);
sampler							bilinear	: register(s0);


[RootSignature(RS_DEBUGVIS)]
[NumThreads(64, 1, 1)]
void UpdateAdaptiveTerrain(const uint tid : SV_DispatchThreadID)
{
	if (tid >= nodeCount)
		return;
	
	const float ar = 9.0f / 16.0f;
	
	const float3 IN_points[] =
	{
		float3(-ar * 0.9f, 0.0f,  0.9f),
		float3(-ar * 0.9f, 0.0f, -0.9f),
		float3( ar * 0.9f, 0.0f, -0.9f),
		float3( ar * 0.9f, 0.0f,  0.9f),
	};
	
	const uint	heapID	= DecodeNode(CBTBuffer, maxDepth, tid);
	float3x3	points;
	
	
	if (GetBitValue(heapID, FindMSB(heapID) - 1) != 0)
	{
		points =
			float3x3(
				scale * IN_points[0],
				scale * IN_points[1],
				scale * IN_points[2]);
	}
	else
	{
		points =
			float3x3(
				scale * IN_points[2],
				scale * IN_points[3],
				scale * IN_points[0]);
	}
	
	const float3x3 m	= GetLEBMatrix(heapID);
	const float3x3 tri	= mul(m, points); 
	
	const float3 a = tri[0] - tri[1]; 
	const float3 b = tri[0] - tri[2];
	
	const float l = length(cross(a, b)) / 2.0f;
	if (l > 0.0125f)
	{
		SetBit(CBTBuffer, HeapToBitIndex(heapID * 2 + 1, maxDepth), true, maxDepth);
	}
	//else if (l < 0.01)
	//{ // merge
	//	
	//}
}