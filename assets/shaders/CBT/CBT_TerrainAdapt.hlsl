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
void UpdateAdaptiveTerrain()
{
	
}