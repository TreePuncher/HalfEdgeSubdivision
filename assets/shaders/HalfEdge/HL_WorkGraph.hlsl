#define BORDERVALUE (0xffffffff >> 1)
#define TWINMASK (0xffffffff >> 1)

struct HalfEdge
{
	 int32_t twin;
	uint32_t next;
	uint32_t prev;
	uint32_t vert;
	
	int32_t Twin()
	{ 
		return twin & TWINMASK;
	}
	
	bool Border()
	{
		return Twin() == BORDERVALUE;
	}
	
	bool IsCorner()
	{
		return (twin & (1 << 31)) != 0;
	}
	
	void MarkCorner(bool f)
	{
		twin = Twin() | (f << 31);
	}
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


uint32_t GetFaceVertexIdx(uint32_t halfEdge)
{
	return (halfEdge & (~0x3)) | 2;
}


template<typename TY_cage, typename TY_points>
uint32_t GetTwinFaceVertexIdx(in TY_cage cage, uint32_t halfEdge)
{
	const uint32_t twin			= cage[halfEdge].Twin();
	const uint32_t faceEdge		= (twin & (~0x3)) | 2;
	return faceEdge;
}


template<typename TY_cage, typename TY_points>
Vertex GetTwinFaceVertex(in TY_cage cage, in TY_points points, uint32_t halfEdge)
{
	const uint32_t twin			= cage[halfEdge].Twin();
	const uint32_t faceEdge		= (twin & (~0x3)) + (4 + (twin % 4) - 1) % 4;
	return points[cage[faceEdge].vert];
}

template<typename TY_Cage>
uint32_t Next(in TY_Cage cage, uint32_t idx)
{
	return cage[idx].next;
}

template<typename TY_Cage>
uint32_t Prev(in TY_Cage cage, uint32_t idx)
{
	return cage[idx].prev;
}
#if 1
uint32_t Next(uint32_t idx)
{
	return inputCage[idx].next;
}

uint32_t Prev(uint32_t idx)
{
	return inputCage[idx].prev;
}

#else
uint32_t Next(uint32_t idx)
{
	return (idx & 0xffffffff << 2) | (idx + 1) & 0x3;
}

uint32_t Prev(uint32_t idx)
{
	return (idx & 0xffffffff << 2) | (idx - 1) & 0x3;
}
#endif


template<typename TY_cage>
uint32_t RotateSelectionCW(in TY_cage cage, uint32_t halfEdge)
{
	return (halfEdge != BORDERVALUE) ? cage[Prev(cage, halfEdge)].Twin() : BORDERVALUE;
}


template<typename TY_cage>
uint32_t RotateSelectionCCW(in TY_cage cage, uint32_t halfEdge)
{
	const uint32_t twin = cage[halfEdge].Twin();
	return (twin == BORDERVALUE) ? BORDERVALUE : Next(cage, twin);
}

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

struct CatmullClarkArgs
{
	int32_t		remaining;
	int32_t		patchCount;
	uint32_t    source;
	uint32_t	target;
	uint3		dispatchSize : SV_DispatchGrid;
};

struct LaunchParams
{
	uint3 dispatchSize : SV_DispatchGrid;
	uint  patchCount;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void InitiateBuild(
	DispatchNodeInputRecord<LaunchParams> args,
	[MaxRecords(1)] NodeOutput<SubdivisionInit>		BuildBisectorFaces,
					const uint						dispatchThreadID : SV_DispatchThreadID)
{
	ThreadNodeOutputRecords<SubdivisionInit> buildArgs = BuildBisectorFaces.GetThreadNodeOutputRecords(1);
	buildArgs.Get().vertexCounter	= 0;
	buildArgs.Get().remaining		= args.Get().patchCount;
	buildArgs.Get().patchCount		= args.Get().patchCount;
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
		
		HalfEdge he			= inputCage[edgeItr];
		HalfEdge prevEdge	= inputCage[he.prev];
		
		HalfEdge edge0;
		edge0.twin = he.Border() ? BORDERVALUE : (Next(inputCage, he.Twin()) * 4 + 3);
		edge0.next = outputIdx + 1;
		edge0.prev = outputIdx + 3;
		edge0.vert = idx + 2 * i + 0;
		edge0.MarkCorner(he.IsCorner());
		cages[outputDest][outputIdx + 0] = edge0;

		HalfEdge edge1;
		edge1.twin = (beginCount.x + (beginCount.y + i + 1) % beginCount.y) * 4 + 2;
		edge1.next = outputIdx + 2;
		edge1.prev = outputIdx + 0;
		edge1.vert = idx + 2 * i + 1;
		cages[outputDest][outputIdx + 1] = edge1;
		
		HalfEdge edge2;
		edge2.twin = (beginCount.x + (beginCount.y + i - 1) % beginCount.y) * 4 + 1;
		edge2.next = outputIdx + 3;
		edge2.prev = outputIdx + 1;
		edge2.vert = idx + vertexCount - 1;
		cages[outputDest][outputIdx + 2] = edge2;
		
		HalfEdge edge3;
		edge3.twin = prevEdge.Border() ? BORDERVALUE : (prevEdge.Twin() * 4);
		edge3.next = outputIdx + 0;
		edge3.prev = outputIdx + 2;
		edge3.vert = idx + (vertexCount - 2 + 2 * i) % (vertexCount - 1);
		edge3.MarkCorner(prevEdge.IsCorner());
		cages[outputDest][outputIdx + 3] = edge3;

		facePoint += inputPoints[he.vert].xyz;
		edgeItr	   = he.next;
		
		points[outputDest][idx + 2 * i + 0] = MakeVertex(inputPoints[he.vert].xyz, 6);
		points[outputDest][idx + 2 * i + 1] = MakeVertex(
												(inputPoints[he.vert].xyz + 
												 inputPoints[inputCage[he.next].vert].xyz) / 2, 6);
		
		i++;
		
		if(i > 32)
			return;
	}

	points[outputDest][idx + vertexCount - 1] = MakeVertex(facePoint / beginCount.y, 6);
}


[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(32, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void BuildBisectorFaces(
	RWDispatchNodeInputRecord<SubdivisionInit>		buildData,
	[MaxRecords(1)] NodeOutput<CatmullClarkArgs>	CreateInitialEdgePoints,
	const uint threadID : SV_DispatchThreadID)
{
	if (threadID >= buildData.Get().patchCount)
		return;
	
	const uint2	beginCount	= inputFaces[threadID];
	const uint	vertexCount = 1 + 2 * beginCount.y;
	
	uint idx;
	InterlockedAdd(buildData.Get().vertexCounter, vertexCount, idx);
	InterlockedAdd(buildData.Get().newPatches, beginCount.y);
	GroupMemoryBarrierWithGroupSync();
	
	SubdividePatch(inputPoints, inputCage, 0, beginCount, idx, vertexCount);

	int remaining = 1;
	InterlockedAdd(buildData.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	ThreadNodeOutputRecords<CatmullClarkArgs> subDivArgs = CreateInitialEdgePoints.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if(remaining == 1)
	{
		subDivArgs.Get().remaining		= buildData.Get().patchCount;
		subDivArgs.Get().patchCount		= buildData.Get().patchCount;
		subDivArgs.Get().source			= 0;
		subDivArgs.Get().target			= 1;
		subDivArgs.Get().dispatchSize	= uint3(1, 1, 1);
	}
	subDivArgs.OutputComplete();

	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(32, 1, 1)]
void CreateInitialVertexPoints(
	RWDispatchNodeInputRecord<CatmullClarkArgs>	args)
{
}


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(32, 1, 1)]
void CreateInitialEdgePoints(
	const uint										threadID : SV_DispatchThreadID,
	RWDispatchNodeInputRecord<CatmullClarkArgs>		args,
	[MaxRecords(1)] NodeOutput<CatmullClarkArgs>	CreateInitialVertexPoints)
{
	if (threadID >= args.Get().patchCount)
		return;
	
	const uint2 beginCount	= inputFaces[threadID];
	uint32_t edgeItr		= beginCount.x;
	HalfEdge he				= inputCage[edgeItr];

	for (uint32_t i = 0; i < beginCount.y; edgeItr = he.next, i++, he = inputCage[edgeItr])
	{
		const uint32_t	outEdge		= 1 + 4 * edgeItr;
		const uint32_t	outVertex	= 0 + 4 * edgeItr;

		if(!he.Border() && !he.IsCorner())
		{
			float3 p0	= inputPoints[inputCage[edgeItr].vert].xyz;
			float3 Q	= points[0][cages[0][GetFaceVertexIdx(outVertex)].vert].xyz;
			float3 R	= lerp(p0, inputPoints[inputCage[Next(inputCage, edgeItr)].vert].xyz, 0.5);
			float  n	= 1.0f;

			uint32_t selection	= RotateSelectionCCW(inputCage, edgeItr);
			while(selection != edgeItr)
			{
				n += 1.0f;
				Q += points[0][cages[0][GetFaceVertexIdx(selection * 4)].vert].xyz;
				R += lerp(p0, inputPoints[inputCage[Next(inputCage, selection)].vert].xyz, 0.5);
				selection = RotateSelectionCCW(inputCage, selection);
			}

			Q /= n;
			R /= n;

			//points[0][cages[0][outVertex].vert].xyz = Q;
			//points[0][cages[0][outVertex].vert].xyz = R;
			points[0][cages[0][outVertex].vert].xyz = (Q + 2 * R + p0 * (n - 3)) / n;
			points[0][cages[0][outVertex].vert].color =
n;
		}

		// Calculate Edge point
		if(!he.Border())
		{
			uint32_t e0	= min(he.Twin(), edgeItr);
			uint32_t e1	= max(he.Twin(), edgeItr);
			
			uint32_t f0 = GetFaceVertexIdx(4 * e0);
			uint32_t f1 = GetFaceVertexIdx(4 * e1);
			
			const float3 fv0 = points[0][cages[0][f0].vert].xyz;
			const float3 fv1 = points[0][cages[0][f1].vert].xyz;
			const float3 ev0 = points[0][cages[0][outEdge].vert].xyz;

			points[0][cages[0][outEdge].vert].xyz	= (fv0 + fv1) / 4.0f + ev0 / 2.0f;
		}
		
		if(he.IsCorner())
		{	// Boundry Vertex
			uint32_t n				= 2; 
			uint32_t prevSelection	= edgeItr;
			uint32_t selection		= RotateSelectionCCW(inputCage, edgeItr);

			while(selection != BORDERVALUE)
			{
				n++;
				prevSelection = selection;
				selection = RotateSelectionCCW(inputCage, selection);
				break;
			}

			const uint32_t selection0 = prevSelection;

			prevSelection	= edgeItr;
			selection		= RotateSelectionCW(inputCage, edgeItr);
			while(selection != BORDERVALUE)
			{
				n++;
				prevSelection	= selection;
				selection		= RotateSelectionCW(inputCage, selection);
			}
			
			const uint32_t selection1 = prevSelection;

			if(n > 2)
			{
				const float3 p0 = inputPoints[inputCage[Next(inputCage, selection0)].vert].xyz;
				const float3 p1 = inputPoints[he.vert].xyz;
				const float3 p2 = inputPoints[inputCage[Prev(inputCage, selection1)].vert].xyz;	
				points[0][cages[0][outVertex].vert].xyz	= (p0 + 6 * p1 + p2) / 8.0f;
			}
		}
	}

	ThreadNodeOutputRecords <CatmullClarkArgs>catmullClarkPoints = CreateInitialVertexPoints.GetThreadNodeOutputRecords(0);
	catmullClarkPoints.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(32, 1, 1)]
[NodeMaxRecursionDepth(4)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void Subdivide(
	RWDispatchNodeInputRecord<SubdivisionArgs>		args,
	[MaxRecords(1)] NodeOutput<SubdivisionArgs>		Subdivide,
	const uint										threadID : SV_DispatchThreadID)
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
