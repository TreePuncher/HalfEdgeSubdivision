struct HalfEdge
{
	int32_t		twin;
	uint32_t	next;
	uint32_t	prev;
	uint32_t	vert;
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

StructuredBuffer<HalfEdge>	inputCage	: register(t0);
StructuredBuffer<Vertex>	inputPoints : register(t1);
StructuredBuffer<uint2>		inputFaces	: register(t2);

globallycoherent RWStructuredBuffer<HalfEdge>	cages[]		: register(u0, space1);
globallycoherent RWStructuredBuffer<Vertex>		points[]	: register(u0, space2);

struct SubdivisionInit
{
	int32_t vertexCounter;
	int32_t remaining;
	int32_t	patchCount;
	int32_t newPatches;
	uint3	dispatchSize : SV_DispatchGrid;
};

struct SubdivisionArgs
{
	int32_t  vertexCounter;
	int32_t  remaining;
	int32_t  patchCount;
	int32_t	 newPatches;
	uint32_t input;
	uint32_t output;
	uint3	dispatchSize : SV_DispatchGrid;
};

struct LaunchParams
{
	uint3 dispatchSize : SV_DispatchGrid;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void InitiateBuild(
	DispatchNodeInputRecord<LaunchParams> args,
	[MaxRecords(1)] NodeOutput<SubdivisionInit>		BuildBisectors,
					const uint						dispatchThreadID : SV_DispatchThreadID)
{
	ThreadNodeOutputRecords<SubdivisionInit> buildArgs = BuildBisectors.GetThreadNodeOutputRecords(1);
	buildArgs.Get().vertexCounter	= 0;
	buildArgs.Get().remaining		= 2;
	buildArgs.Get().patchCount		= 2;
	buildArgs.Get().newPatches		= 0;
	buildArgs.Get().dispatchSize	= uint3(1, 1, 1);
	
	buildArgs.OutputComplete();
}


template<typename TY_points, typename TY_cage>
void SubdividePatch(TY_points inputPoints, TY_cage inputCage, uint outputDest, in uint2 beginCount, in uint idx, in uint vertexCount)
{
	float3	facePoint	= float3(0, 0, 0);
	
	int32_t i			= 0;
	uint32_t edgeItr	= beginCount.x;
	
	while (edgeItr != beginCount.x || i == 0)
	{
		const uint outputIdx = edgeItr * 4;
		
		HalfEdge he = inputCage[edgeItr];
		
		HalfEdge edge0;
		edge0.twin = (he.twin != -1) ? (inputCage[he.twin].next * 4 + 3) : -1;
		edge0.next = outputIdx + 1;
		edge0.prev = outputIdx + 3;
		edge0.vert = idx + 2 * i + 0;
		cages[outputDest][outputIdx + 0] = edge0;

		HalfEdge edge1;
		edge1.twin = ((beginCount.x + beginCount.y + i + 1) % beginCount.y) * 4 + 2;
		edge1.next = outputIdx + 2;
		edge1.prev = outputIdx + 0;
		edge1.vert = idx + 2 * i + 1;
		cages[outputDest][outputIdx + 1] = edge1;
		
		HalfEdge edge2;
		edge2.twin = ((beginCount.x + beginCount.y + i - 1) % beginCount.y) * 4 + 1;
		edge2.next = outputIdx + 3;
		edge2.prev = outputIdx + 1;
		edge2.vert = idx + vertexCount - 1;
		cages[outputDest][outputIdx + 2] = edge2;
		
		HalfEdge edge3;
		edge3.twin = (he.twin != -1) ? (inputCage[he.prev].twin * 4 + 3) : -1;
		edge3.next = outputIdx + 0;
		edge3.prev = outputIdx + 2;
		edge3.vert = idx + (vertexCount - 2 + 2 * i) % (vertexCount - 1);
		cages[outputDest][outputIdx + 3] = edge3;

		facePoint += inputPoints[he.vert].xyz;
		edgeItr		= he.next;
		
		points[outputDest][idx + 2 * i + 0] = MakeVertex(inputPoints[he.vert].xyz, 0);
		points[outputDest][idx + 2 * i + 1] = MakeVertex(
												(inputPoints[he.vert].xyz + 
												 inputPoints[inputCage[he.next].vert].xyz) / 2, 1);
		
		i++;
		
		if(i > 32)
			return;
	}

	points[outputDest][idx + vertexCount - 1] = MakeVertex(facePoint / beginCount.y, 3);
}


[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(32, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void BuildBisectors(
	RWDispatchNodeInputRecord<SubdivisionInit>	buildData,
	[MaxRecords(1)] NodeOutput<SubdivisionArgs>	Subdivide,
	const uint threadID : SV_DispatchThreadID)
{
	if (threadID >= buildData.Get().patchCount)
		return;
	
	const uint2	beginCount	= inputFaces[threadID];
	const uint	vertexCount = 1 + 2 * beginCount.y;
	
	uint idx;
	InterlockedAdd(buildData.Get().vertexCounter, vertexCount, idx);
	InterlockedAdd(buildData.Get().newPatches, beginCount.y);

	SubdividePatch(inputPoints, inputCage, 0, beginCount, idx, vertexCount);

	int remaining = 1;
	InterlockedAdd(buildData.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	ThreadNodeOutputRecords<SubdivisionArgs> subDivArgs = Subdivide.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if(remaining == 1)
	{
		subDivArgs.Get().vertexCounter	= 0;
		subDivArgs.Get().remaining		= buildData.Get().newPatches;
		subDivArgs.Get().patchCount		= buildData.Get().newPatches;
		subDivArgs.Get().newPatches		= 0;
		subDivArgs.Get().input			= 0;
		subDivArgs.Get().output			= 1;
		subDivArgs.Get().dispatchSize	= uint3(1, 1, 1);
	}
	subDivArgs.OutputComplete();

	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(32, 1, 1)]
[NodeMaxRecursionDepth(4)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void Subdivide(
	RWDispatchNodeInputRecord<SubdivisionArgs>	args,
	[MaxRecords(1)] NodeOutput<SubdivisionArgs>	Subdivide,
	const uint									threadID : SV_DispatchThreadID)
{
	if (threadID >= args.Get().patchCount)
		return;
	
	const uint2 beginCount = uint2(threadID * 4, 4);
	const uint vertexCount = 1 + 2 * beginCount.y;
	
	uint idx;
	InterlockedAdd(args.Get().vertexCounter, vertexCount, idx);
	InterlockedAdd(args.Get().newPatches, beginCount.y);
	
	int remaining = 1;
	InterlockedAdd(args.Get().remaining, -1, remaining);
	
	SubdividePatch(
		points[args.Get().input],
		cages[args.Get().input],
		args.Get().output,
		beginCount, 
		idx, vertexCount);
	
	GroupMemoryBarrierWithGroupSync();
	
	if(args.Get().output == 2)
		return;
	
	ThreadNodeOutputRecords < SubdivisionArgs > subDivArgs = Subdivide.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if (remaining == 1)
	{
		subDivArgs.Get().vertexCounter	= 0;
		subDivArgs.Get().remaining		= args.Get().newPatches;
		subDivArgs.Get().remaining		= args.Get().newPatches;
		subDivArgs.Get().patchCount		= args.Get().patchCount * 4;
		subDivArgs.Get().newPatches		= 0;
		subDivArgs.Get().input			= args.Get().output;
		subDivArgs.Get().output			= args.Get().output + 1;
		subDivArgs.Get().dispatchSize	= uint3(args.Get().patchCount * 4 / 32 + (args.Get().patchCount * 4 % 32 == 0 ? 0 : 1), 1, 1);
	}
	subDivArgs.OutputComplete();
	
	Barrier(points[1], DEVICE_SCOPE);
	Barrier(cages[1], DEVICE_SCOPE);
}
