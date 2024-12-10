#include "HE_Common.hlsl"
#include "../CBT/CBT.hlsl"

#define RootSig		"SRV(t0)," \
					"SRV(t1)," \
					"SRV(t2)," \
					"SRV(t3)," \
					"SRV(t4)," \
					"RootConstants(num32BitConstants = 18, b0)"

cbuffer constants : register(b0)
{
	float4x4	PV;
	uint		drawCount;
	uint		maxDepth;
};

float4 ExtractRGBA8(uint rgba)
{
	return float4(
		float((rgba >> 24) & 0xff) / float(0xff),
		float((rgba >> 16) & 0xff) / float(0xff),
		float((rgba >>  8) & 0xff) / float(0xff),
		float((rgba >>  0) & 0xff) / float(0xff));
}

StructuredBuffer<HalfEdge>	inputCage		: register(t0);
StructuredBuffer<Vertex>	inputVerts		: register(t1);
StructuredBuffer<HE_Face>	faces			: register(t2);
StructuredBuffer<uint>		faceLookup		: register(t3);
StructuredBuffer<FaceID>	faceDrawList	: register(t4);

struct VertexOut
{
					float4 v		: SV_Position;
					float4 color	: COLOR;
	nointerpolation uint   patchID	: ID;
};

struct Vertex2Out
{
					float4 v		: SV_Position;
					float4 color	: COLOR;
					float3 d		: DISTANCE;
	nointerpolation uint   patchID	: ID;
};

VertexOut MakeVert(in Vertex v, uint patchID)
{
	static float3 colors[] =
	{
		float3(1, 0, 0),  // 0
		float3(0, 1, 0),  // 1
		float3(0, 0, 1),  // 2
		float3(1, 0, 1),  // 3
		float3(1, 1, 1),  // 4
		float3(0, 1, 1),
		float3(0.75f, 0.75f, 0.75f),
	};

	
	VertexOut VOut;
	VOut.v			= mul(PV, float4(v.xyz, 1));
	VOut.patchID	= patchID;
	VOut.color		= float4(colors[patchID % 7], 1);
	
	return VOut;
}


Vertex2Out MakeWireframeVert(float4 xyzw, float3 d, uint color, uint patchID)
{
	static float3 colors[] =
	{
		float3(1, 0, 0),  // 0
		float3(0, 1, 0),  // 1
		float3(0, 0, 1),  // 2
		float3(1, 0, 1),  // 3
		float3(1, 1, 1),  // 4
		float3(0, 1, 1),
		float3(0.25f, 0.25f, 0.25f),
	};

	
	Vertex2Out VOut;
	VOut.v			= xyzw;
	VOut.patchID	= patchID;
	VOut.color		= float4(colors[color % 7] * 0.5, 1);
	VOut.d			= d;
	return VOut;
}


void CreateWireframeArgs(float4 xyz0, float4 xyz1, float4 xyz2, out float d[3])
{
	float2 WIN_SCALE = float2(1920, 1080);
	
	const float2 p0 = WIN_SCALE * xyz0.xy / xyz0.w;
	const float2 p1 = WIN_SCALE * xyz1.xy / xyz1.w;
	const float2 p2 = WIN_SCALE * xyz2.xy / xyz2.w;
	
	const float2 v0 = p2 - p1;
	const float2 v1 = p2 - p0;
	const float2 v2 = p1 - p0;
	
	const float area	= abs(v1.x * v2.y - v1.y * v2.x);
	
	d[0]		= area / length(v0);
	d[1]		= area / length(v1);
	d[2]		= area / length(v2);
}


[RootSignature(RootSig)]
[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void MeshMain(
	const uint					threadID : SV_DispatchThreadID,
	out indices		uint3		tris[64],
	out vertices	VertexOut	verts[128])
{
	SetMeshOutputCounts(0, 0);
	
	if (threadID >= drawCount)
		return;
	
	const uint patchID			= faceLookup[threadID];
	const HE_Face face			= faces[patchID];
	
	TwinEdge edges[4];
	GetTwinEdges(face, 0, edges, inputCage);
	
	verts[4 * (threadID % 32) + 0] = MakeVert(inputVerts[edges[0].vert], threadID);
	verts[4 * (threadID % 32) + 1] = MakeVert(inputVerts[edges[1].vert], threadID);
	verts[4 * (threadID % 32) + 2] = MakeVert(inputVerts[edges[2].vert], threadID);
	verts[4 * (threadID % 32) + 3] = MakeVert(inputVerts[edges[3].vert], threadID);
		
	tris[2 * (threadID % 32) + 0] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 1, 4 * (threadID % 32) + 2);
	tris[2 * (threadID % 32) + 1] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 2, 4 * (threadID % 32) + 3);	
}


[RootSignature(RootSig)]
[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void WireMain(
	const uint					threadID		: SV_DispatchThreadID,
	const uint					groupThreadID	: SV_GroupIndex,
	const uint					groupID			: SV_GroupID,
	out indices		uint3		tris[64],
	out vertices	Vertex2Out	verts[192])
{
	const uint primitiveCount = groupID * 32 < drawCount ? 64 : max(2 * (groupID * 32 - drawCount), 0);
	
	SetMeshOutputCounts(primitiveCount * 6, primitiveCount);
	
	if (groupThreadID * 2 >= primitiveCount)
		return;
	
	const FaceID faceID = faceDrawList[threadID];
	HE_Face face = faces[faceLookup[faceID.faceID]];
	
	TwinEdge edges[4];
	GetTwinEdges(face, groupThreadID % face.edgeCount, edges, inputCage);
	
	const float4 v0 = mul(PV, float4(inputVerts[edges[0].vert].xyz, 1));
	const float4 v1 = mul(PV, float4(inputVerts[edges[1].vert].xyz, 1));
	const float4 v2 = mul(PV, float4(inputVerts[edges[2].vert].xyz, 1));
	const float4 v3 = mul(PV, float4(inputVerts[edges[3].vert].xyz, 1));
	
	float d0[3];
	float d1[3];
	CreateWireframeArgs(v0, v1, v2, d0);
	CreateWireframeArgs(v0, v2, v3, d1);
	
	verts[6 * groupThreadID + 0] = MakeWireframeVert(v0, float3(d0[0], 0, 0), inputVerts[edges[0].vert].color, groupThreadID);
	verts[6 * groupThreadID + 1] = MakeWireframeVert(v1, float3(0, d0[1], 0), inputVerts[edges[1].vert].color, groupThreadID);
	verts[6 * groupThreadID + 2] = MakeWireframeVert(v2, float3(0, 0, d0[2]), inputVerts[edges[2].vert].color, groupThreadID);
	
	verts[6 * groupThreadID + 3] = MakeWireframeVert(v0, float3(d1[0], 0, 0), inputVerts[edges[0].vert].color, groupThreadID);
	verts[6 * groupThreadID + 4] = MakeWireframeVert(v2, float3(0, d1[1], 0), inputVerts[edges[2].vert].color, groupThreadID);
	verts[6 * groupThreadID + 5] = MakeWireframeVert(v3, float3(0, 0, d1[2]), inputVerts[edges[3].vert].color, groupThreadID);
		
	tris[2 * groupThreadID + 0] = uint3(6 * groupThreadID + 0, 6 * groupThreadID + 1, 6 * groupThreadID + 2);
	tris[2 * groupThreadID + 1] = uint3(6 * groupThreadID + 3, 6 * groupThreadID + 4, 6 * groupThreadID + 5);
}


float4 PMain(VertexOut vIn) : SV_Target
{
	return vIn.color;
}


float4 WhiteWireframe(Vertex2Out IN) : SV_Target
{
	const float d = min(IN.d[0], min(IN.d[1], IN.d[2])) / 2.0f;
	const float I = exp2(-2 * d * d);
	
	return	float4(0.01f, 0.01f, 0.01f, 1) * I + 
			IN.color * (1.0 - I);
}


/**********************************************************************

Copyright (c) 2024 Robert May

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**********************************************************************/