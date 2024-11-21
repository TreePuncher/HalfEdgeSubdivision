#define RootSig		"SRV(t0)," \
					"SRV(t1)," \
					"RootConstants(num32BitConstants = 17, b0)"

cbuffer constants : register(b0)
{
	float4x4	PV;
	uint		patchCount;
};

struct HalfEdge
{
	 int32_t twin;
	uint32_t next;
	uint32_t prev;
	uint32_t vert;
};

StructuredBuffer<HalfEdge>	halfEdge	: register(t0);
StructuredBuffer<float3>	inputVerts	: register(t1);

struct VertexOut
{
					float4 v		: SV_Position;
	nointerpolation uint   patchID	: ID;
};

VertexOut MakeVert(float4 xyzw, uint patchID)
{
	VertexOut VOut;
	VOut.v			= xyzw;
	VOut.patchID	= patchID;
	
	return VOut;
}

[RootSignature(RootSig)]
[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void MeshMain(
	const uint					threadID : SV_DispatchThreadID,
	out indices		uint3		tris[64],
	out vertices	VertexOut	verts[128])
{
	if (threadID >= patchCount)
		return;
	
	SetMeshOutputCounts(128, 64);
	
	HalfEdge he0 = halfEdge[4 * threadID + 0];
	HalfEdge he1 = halfEdge[4 * threadID + 1];
	HalfEdge he2 = halfEdge[4 * threadID + 2];
	HalfEdge he3 = halfEdge[4 * threadID + 3];
		
	verts[4 * (threadID % 32) + 0] = MakeVert(mul(PV, float4(inputVerts[he0.vert].xzy, 1)), threadID);
	verts[4 * (threadID % 32) + 1] = MakeVert(mul(PV, float4(inputVerts[he1.vert].xzy, 1)), threadID);
	verts[4 * (threadID % 32) + 2] = MakeVert(mul(PV, float4(inputVerts[he2.vert].xzy, 1)), threadID);
	verts[4 * (threadID % 32) + 3] = MakeVert(mul(PV, float4(inputVerts[he3.vert].xzy, 1)), threadID);
		
	tris[2 * (threadID % 32) + 0] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 1, 4 * (threadID % 32) + 2);
	tris[2 * (threadID % 32) + 1] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 2, 4 * (threadID % 32) + 3);
}

float4 PMain(VertexOut vIn) : SV_Target
{
	static float3 colors[] =
	{
		float3(1, 0, 0),
		float3(0, 1, 0),
		float3(0, 0, 1),
		float3(1, 0, 1),
		float3(1, 1, 1),
		float3(0, 1, 1),
		float3(0.25f, 0.25f, 0.25f),
	};

	return float4(colors[vIn.patchID % 7], 1);
}