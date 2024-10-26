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
	float4 v : SV_Position;
};

[RootSignature(RootSig)]
[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void MeshMain(
	const uint					threadID : SV_DispatchThreadID,
	out indices		uint3		tris[16],
	out vertices	VertexOut	verts[64])
{
	const int vertexCount	= 2 * patchCount + patchCount;
	int primitiveCount		= 0;
	int vertCount			= 0;

	SetMeshOutputCounts(3, 2 * patchCount);
	
	for (int i = 0; i < patchCount; i++)
	{
		HalfEdge he0 = halfEdge[4 * (threadID + i) + 0];
		HalfEdge he1 = halfEdge[4 * (threadID + i) + 1];
		HalfEdge he2 = halfEdge[4 * (threadID + i) + 2];
		HalfEdge he3 = halfEdge[4 * (threadID + i) + 3];
		
		tris[primitiveCount++] = uint3(he0.vert, he1.vert, he2.vert);
		tris[primitiveCount++] = uint3(he0.vert, he2.vert, he3.vert);
	}
	
	for (int i = 0; i < vertexCount; i++)
		verts[i].v = mul(PV, float4(inputVerts[i], 1));
}

float4 PMain() : SV_Target
{
	return float4(1, 1, 1, 1);
}