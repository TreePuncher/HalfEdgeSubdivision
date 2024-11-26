#define RootSig		"SRV(t0)," \
					"SRV(t1)," \
					"RootConstants(num32BitConstants = 17, b0)"

cbuffer constants : register(b0)
{
	float4x4	PV;
	uint		patchCount;
};

struct TwinEdge
{
	 int32_t twin;
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

StructuredBuffer<TwinEdge>	twinEdges	: register(t0);
StructuredBuffer<Vertex>	inputVerts	: register(t1);

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
	VOut.color		= float4(colors[patchID% 7], 1);
	
	return VOut;
}


Vertex2Out MakeWireframeVert(float4 xyzw, float3 d, uint patchID)
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

	
	Vertex2Out VOut;
	VOut.v			= xyzw;
	VOut.patchID	= patchID;
	VOut.color		= float4(colors[patchID % 7], 1);
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
	if (threadID >= patchCount)
		return;
	
	SetMeshOutputCounts(128, 64);
	
	TwinEdge he0 = twinEdges[4 * threadID + 0];
	TwinEdge he1 = twinEdges[4 * threadID + 1];
	TwinEdge he2 = twinEdges[4 * threadID + 2];
	TwinEdge he3 = twinEdges[4 * threadID + 3];
		
	verts[4 * (threadID % 32) + 0] = MakeVert(inputVerts[he0.vert], threadID);
	verts[4 * (threadID % 32) + 1] = MakeVert(inputVerts[he1.vert], threadID);
	verts[4 * (threadID % 32) + 2] = MakeVert(inputVerts[he2.vert], threadID);
	verts[4 * (threadID % 32) + 3] = MakeVert(inputVerts[he3.vert], threadID);
		
	tris[2 * (threadID % 32) + 0] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 1, 4 * (threadID % 32) + 2);
	tris[2 * (threadID % 32) + 1] = uint3(4 * (threadID % 32) + 0, 4 * (threadID % 32) + 2, 4 * (threadID % 32) + 3);	
}


[RootSignature(RootSig)]
[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void WireMain(
	const uint					threadID : SV_DispatchThreadID,
	out indices		uint3		tris[64],
	out vertices	Vertex2Out	verts[192])
{
	if (threadID >= patchCount)
		return;
	
	SetMeshOutputCounts(128, 64);
	
	TwinEdge he0 = twinEdges[4 * threadID + 0];
	TwinEdge he1 = twinEdges[4 * threadID + 1];
	TwinEdge he2 = twinEdges[4 * threadID + 2];
	TwinEdge he3 = twinEdges[4 * threadID + 3];
		
	const float4 v0 = mul(PV, float4(inputVerts[he0.vert].xyz, 1));
	const float4 v1 = mul(PV, float4(inputVerts[he1.vert].xyz, 1));
	const float4 v2 = mul(PV, float4(inputVerts[he2.vert].xyz, 1));
	const float4 v3 = mul(PV, float4(inputVerts[he3.vert].xyz, 1));
	
	float d0[3];
	float d1[3];
	CreateWireframeArgs(v0, v1, v2, d0);
	CreateWireframeArgs(v0, v2, v3, d1);
	
	verts[6 * (threadID % 32) + 0] = MakeWireframeVert(v0, float3(d0[0], 0, 0), threadID);
	verts[6 * (threadID % 32) + 1] = MakeWireframeVert(v1, float3(0, d0[1], 0), threadID);
	verts[6 * (threadID % 32) + 2] = MakeWireframeVert(v2, float3(0, 0, d0[2]), threadID);
	
	verts[6 * (threadID % 32) + 3] = MakeWireframeVert(v0, float3(d1[0], 0, 0), threadID);
	verts[6 * (threadID % 32) + 4] = MakeWireframeVert(v2, float3(0, d1[1], 0), threadID);
	verts[6 * (threadID % 32) + 5] = MakeWireframeVert(v3, float3(0, 0, d1[2]), threadID);
		
	tris[2 * (threadID % 32) + 0] = uint3(6 * (threadID % 32) + 0, 6 * (threadID % 32) + 1, 6 * (threadID % 32) + 2);
	tris[2 * (threadID % 32) + 1] = uint3(6 * (threadID % 32) + 3, 6 * (threadID % 32) + 4, 6 * (threadID % 32) + 5);	
}

float4 PMain(VertexOut vIn) : SV_Target
{
	return vIn.color;
}

float4 WhiteWireframe(Vertex2Out IN) : SV_Target
{
	const float d = min(IN.d[0], min(IN.d[1], IN.d[2])) / 2.0f;
	const float I = exp2(-2 * d * d);
	
	return	float4(0, 0, 0, 1) * I + 
			IN.color * (1.0 - I);
}