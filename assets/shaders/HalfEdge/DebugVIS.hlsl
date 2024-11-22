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

struct Vertex
{
	float3	xyz;
	uint	color;
	float2  UV;
};

Vertex MakeVertex(float3 xyz, uint color = 0, float2 UV = float2(0, 0))
{
	Vertex v;
	v.xyz	= xyz;
	v.color	= color;
	v.UV	= UV;
	
	return v;
}

float4 ExtractRGBA8(uint rgba)
{
	return float4(
		float((rgba >> 24) & 0xff) / float(0xff),
		float((rgba >> 16) & 0xff) / float(0xff),
		float((rgba >>  8) & 0xff) / float(0xff),
		float((rgba >>  0) & 0xff) / float(0xff));
}

StructuredBuffer<HalfEdge>	halfEdge	: register(t0);
StructuredBuffer<Vertex>	inputVerts	: register(t1);

struct VertexOut
{
					float4 v		: SV_Position;
					float4 color	: COLOR;
	nointerpolation uint   patchID	: ID;
};

VertexOut MakeVert(in Vertex v, uint patchID)
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

	
	VertexOut VOut;
	VOut.v			= mul(PV, float4(v.xyz.xzy, 1));
	VOut.patchID	= patchID;
	VOut.color		= float4(colors[v.color % 7], 1);
	
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
		
	verts[4 * (threadID % 32) + 0] = MakeVert(inputVerts[he0.vert], threadID);
	verts[4 * (threadID % 32) + 1] = MakeVert(inputVerts[he1.vert], threadID);
	verts[4 * (threadID % 32) + 2] = MakeVert(inputVerts[he2.vert], threadID);
	verts[4 * (threadID % 32) + 3] = MakeVert(inputVerts[he3.vert], threadID);
		
	tris[2 * (threadID % 32) + 0] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 1, 4 * (threadID % 32) + 2);
	tris[2 * (threadID % 32) + 1] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 2, 4 * (threadID % 32) + 3);
}

float4 PMain(VertexOut vIn) : SV_Target
{
	return vIn.color;
}