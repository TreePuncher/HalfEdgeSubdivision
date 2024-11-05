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
	
	const float4 IN_points[] =
	{
		float4(-ar * 0.9f, 0.0f,  0.9f, 1.0f),
		float4(-ar * 0.9f, 0.0f, -0.9f, 1.0f),
		float4( ar * 0.9f, 0.0f, -0.9f, 1.0f),
		float4( ar * 0.9f, 0.0f,  0.9f, 1.0f),
	};
	
	const float3 IN_texcoord[] =
	{
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 0.0f, 0.0f),
		float3(1.0f, 0.0f, 0.0f),
		float3(1.0f, 1.0f, 0.0f),
	};
	
	const uint	heapID	= DecodeNode(CBTBuffer, maxDepth, tid);
	float3x4	points;
	float3x3	UVs;
	
	if (GetBitValue(heapID, FindMSB(heapID) - 1) != 0)
	{
		points =
			float3x4(
				float4(1, 1, 1, 1) * IN_points[0],
				float4(1, 1, 1, 1) * IN_points[1],
				float4(1, 1, 1, 1) * IN_points[2]);
		
		UVs =
			float3x3(
				IN_texcoord[0],
				IN_texcoord[1],
				IN_texcoord[2]);
	}
	else
	{
		points =
			float3x4(
				float4(10, 10, 10, 1) * IN_points[2],
				float4(10, 10, 10, 1) * IN_points[3],
				float4(10, 10, 10, 1) * IN_points[0]);
		
		UVs =
			float3x3(
				IN_texcoord[2],
				IN_texcoord[3],
				IN_texcoord[0]);
	}
	
	const float3x3 m	= GetLEBMatrix(heapID);
	const float3x3 UV	= mul(m, UVs);
	const float3x4 tri	= mul(m, points);
	
	//points[0].y = 2.0f * sqrt(heightMap.SampleLevel(bilinear, UV[0], 0));
	//points[1].y = 2.0f * sqrt(heightMap.SampleLevel(bilinear, UV[1], 0));
	//points[2].y = 2.0f * sqrt(heightMap.SampleLevel(bilinear, UV[2], 0));
	//
	//float3x4 SSTri = transpose(mul(PV, transpose(tri)));
	//
	//SSTri[0] /= SSTri[0].w;
	//SSTri[1] /= SSTri[1].w;
	//SSTri[2] /= SSTri[2].w;
	
	const float minX = min(min(tri[0].x, tri[1].x), tri[2].x);
	const float minY = min(min(tri[0].y, tri[1].y), tri[2].y);
	const float minZ = min(min(tri[0].z, tri[1].z), tri[2].z);

	const float maxX = max(max(tri[0].x, tri[1].x), tri[2].x);
	const float maxY = max(max(tri[0].y, tri[1].y), tri[2].y);
	const float maxZ = max(max(tri[0].z, tri[1].z), tri[2].z);
	
	if(tid % 32 != 0)
		return;
	
	// split
	if (FindMSB(heapID) < maxDepth)
	{
		//SetBit(CBTBuffer, HeapToBitIndex(heapID * 2, maxDepth), true, maxDepth);
		SetBit(CBTBuffer, HeapToBitIndex(heapID * 2 + 1, maxDepth), true, maxDepth);
		
		uint parentID = LEBQuadNeighbors(heapID).z;
		while (parentID > 3)
		{
			SetBit(CBTBuffer, HeapToBitIndex(parentID * 2,		maxDepth), true, maxDepth);
			SetBit(CBTBuffer, HeapToBitIndex(parentID * 2 + 1,	maxDepth), true, maxDepth);
			parentID = parentID / 2;
			
			SetBit(CBTBuffer, HeapToBitIndex(parentID * 2,		maxDepth), true, maxDepth);
			SetBit(CBTBuffer, HeapToBitIndex(parentID * 2 + 1,	maxDepth), true, maxDepth);
			parentID = LEBQuadNeighbors(parentID).z;
		}
	}
}