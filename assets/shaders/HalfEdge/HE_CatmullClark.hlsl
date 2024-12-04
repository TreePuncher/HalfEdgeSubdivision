#include "HE_Common.hlsl"

/************************************************************************************************/


StructuredBuffer<HalfEdge>	inputCage	: register(t0);
StructuredBuffer<Vertex>	inputPoints : register(t1);
StructuredBuffer<uint2>		inputFaces	: register(t2);

globallycoherent RWStructuredBuffer<TwinEdge>	cages[]		: register(u0, space1);
globallycoherent RWStructuredBuffer<Vertex>		points[]	: register(u0, space2);


/************************************************************************************************/


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
	uint32_t	newPatches;
	uint3		dispatchSize : SV_DispatchGrid;
};

struct LaunchParams
{
	uint3 dispatchSize : SV_DispatchGrid;
	uint  patchCount;
};


/************************************************************************************************/


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
	buildArgs.Get().dispatchSize	= uint3(args.Get().patchCount / 256 + (args.Get().patchCount % 256 == 0 ? 0 : 1), 1, 1);
	
	buildArgs.OutputComplete();
}


/************************************************************************************************/


struct LaunchSubdivisionParams
{
	uint3 dispatchSize : SV_DispatchGrid;
	uint patchCount;
	uint source;
	uint target;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void InitiateSubdivision(
	DispatchNodeInputRecord<LaunchParams> args,
	[MaxRecords(1)] NodeOutput<SubdivisionInit>		BuildBisectorFaces,
					const uint						dispatchThreadID : SV_DispatchThreadID)
{
	ThreadNodeOutputRecords<SubdivisionInit> buildArgs = BuildBisectorFaces.GetThreadNodeOutputRecords(1);
	buildArgs.Get().vertexCounter	= 0;
	buildArgs.Get().remaining		= args.Get().patchCount;
	buildArgs.Get().patchCount		= args.Get().patchCount;
	buildArgs.Get().newPatches		= 0;
	buildArgs.Get().dispatchSize	= uint3(args.Get().patchCount / 256 + (args.Get().patchCount % 256 == 0 ? 0 : 1), 1, 1);
	
	buildArgs.OutputComplete();
}


/************************************************************************************************/


void SubdivideTwinEdges(
	in RWStructuredBuffer<Vertex>	inputPoints, 
	in RWStructuredBuffer<TwinEdge>	inputCage, 
	in RWStructuredBuffer<Vertex>	outputPoints,
	in RWStructuredBuffer<TwinEdge> outputCage,
	in uint threadID, in uint vertexBlock)
{
	const uint vertexCount	= 9;
	
	const uint32_t	j			= threadID / 4;
	const uint32_t	i			= threadID % 4;
		
	uint32_t edgeItr	 = threadID;
	const uint outputIdx = edgeItr * 4;
		
	const TwinEdge te		= inputCage[edgeItr];
	const TwinEdge prevEdge	= inputCage[Prev(edgeItr)];
		
	TwinEdge edge0;
	edge0.twin = te.Border() ? BORDERVALUE : (Next(te.Twin()) * 4 + 3);
	edge0.vert = vertexBlock + 2 * i + 0;
	edge0.MarkCorner(te.IsCorner());
	edge0.MarkT(te.IsT());
	outputCage[outputIdx + 0] = edge0;

	TwinEdge edge1;
	edge1.twin = Next(edgeItr) * 4 + 2;
	edge1.vert = vertexBlock + 2 * i + 1;
	outputCage[outputIdx + 1] = edge1;
		
	TwinEdge edge2;
	edge2.twin = Prev(edgeItr) * 4 + 1;
	edge2.vert = vertexBlock + 8;
	outputCage[outputIdx + 2] = edge2;
		
	TwinEdge edge3;
	edge3.twin = prevEdge.Border() ? BORDERVALUE : (prevEdge.Twin() * 4);
	edge3.vert = vertexBlock + (9 - 2 + 2 * i) % 8;
	edge3.MarkT(prevEdge.Border());
	outputCage[outputIdx + 3] = edge3;

	outputPoints[vertexBlock + 2 * i + 0] = MakeVertex(inputPoints[te.vert].xyz, 6);
	outputPoints[vertexBlock + 2 * i + 1] = MakeVertex(
											(inputPoints[te.vert].xyz + 
											 inputPoints[inputCage[Next(edgeItr)].vert].xyz) / 2, 6);

	if (i == 0)
	{
		const uint32_t	edgeItr		= j;
		
		float3	facePoint		=
			inputPoints[inputCage[threadID + 0].vert].xyz +
			inputPoints[inputCage[threadID + 1].vert].xyz +
			inputPoints[inputCage[threadID + 2].vert].xyz +
			inputPoints[inputCage[threadID + 3].vert].xyz;
		
		outputPoints[vertexBlock + 8] = MakeVertex(facePoint / 4.0f, 6);
	}
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(256, 1, 1)]
void BuildBisectorFaces(
	RWDispatchNodeInputRecord<SubdivisionInit>		buildData,
	[MaxRecords(1)] NodeOutput<CatmullClarkArgs>	ApplyCCToHalfEdges,
	const uint threadID : SV_DispatchThreadID)
{
	if (threadID >= buildData.Get().patchCount)
		return;
	
	const uint2	halfEdgeRange	= inputFaces[threadID];
	const uint	vertexCount		= 1 + 2 * halfEdgeRange.y;
	
	uint idx;
	InterlockedAdd(buildData.Get().vertexCounter, vertexCount, idx);
	InterlockedAdd(buildData.Get().newPatches, halfEdgeRange.y);
	GroupMemoryBarrierWithGroupSync();
	
	SubdividePatch(halfEdgeRange, idx, vertexCount, inputCage, inputPoints, cages[0], points[0]);

	int remaining = 1;
	InterlockedAdd(buildData.Get().remaining, -1, remaining);
	
	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);

#if 0
	ThreadNodeOutputRecords<CatmullClarkArgs> subDivArgs = ApplyCCToHalfEdges.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if(remaining == 1)
	{
		const uint32_t patchCount	= buildData.Get().newPatches;
		const uint32_t dispatchX	= patchCount / 1024 + (patchCount % 1024 == 0 ? 0 : 1);
		
		subDivArgs.Get().remaining		= patchCount;
		subDivArgs.Get().patchCount		= patchCount;
		subDivArgs.Get().newPatches		= buildData.Get().newPatches;
		subDivArgs.Get().source			= 0;
		subDivArgs.Get().target			= 1;
		subDivArgs.Get().dispatchSize	= uint3(dispatchX, 1, 1);
	}


	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);

	subDivArgs.OutputComplete();
#endif
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(65535, 1, 1)]
[NumThreads(1024, 1, 1)]
void ApplyCCToHalfEdges(
	const uint										threadID : SV_DispatchThreadID,
	RWDispatchNodeInputRecord<CatmullClarkArgs>		args,
	[MaxRecords(1)] NodeOutput<SubdivisionArgs>		SubdivideTwinEdgeTask0)
{
	if (threadID >= args.Get().patchCount)
		return;
	
	const uint2 beginCount	= inputFaces[threadID];
	uint32_t edgeItr		= threadID;
	HalfEdge he				= inputCage[edgeItr];
	HalfEdge he_prev		= inputCage[he.prev];

	const uint32_t	outEdge		= 1 + 4 * edgeItr;
	const uint32_t	outVertex	= 0 + 4 * edgeItr;
		
	if (he.IsT())
		points[0][cages[0][outVertex].vert].color	= 1;

#if 1
#if 1
	if(!he.Border() && !he_prev.Border())
	{
		float3 p0	= inputPoints[inputCage[edgeItr].vert].xyz;
		float3 Q	= points[0][cages[0][GetFaceVertexIdx(outVertex)].vert].xyz;
		float3 R	= lerp(p0, inputPoints[inputCage[Next(inputCage, edgeItr)].vert].xyz, 0.5);
		float  n	= 1.0f;
			
		uint32_t selection	= RotateSelectionCCW(inputCage, edgeItr);
		while(selection != edgeItr && selection != BORDERVALUE)
		{
			n += 1.0f;
			Q += points[0][cages[0][GetFaceVertexIdx(selection * 4)].vert].xyz;
			R += lerp(p0, inputPoints[inputCage[Next(inputCage, selection)].vert].xyz, 0.5);
			selection = RotateSelectionCCW(inputCage, selection);

			if(n > 16)
			{
				selection = BORDERVALUE;
				break;
			}
		}
			
		if(selection != BORDERVALUE)
		{
			Q /= n;
			R /= n;

			points[0][cages[0][outVertex].vert].xyz		= (Q + 2 * R + p0 * (n - 3)) / n;
			points[0][cages[0][outVertex].vert].color	= 0;
		}
		else
			points[0][cages[0][outVertex].vert].color	= 3;
}
#endif

#if 1
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
		points[0][cages[0][outEdge].vert].color	= 0;
	}
#endif

#if 0
	{
		TwinEdge te1 = cages[0][outVertex];
		TwinEdge te2 = cages[0][outVertex + 3];
			
		if((te1.Border() && !te1.IsCorner()) || 
		   (te2.Border() && !te2.IsCorner()))
		{	// Boundry Vertex
			uint32_t n				= 2; 
			uint32_t prevSelection	= edgeItr;
			uint32_t selection		= RotateSelectionCCW(inputCage, edgeItr);

			while(selection != BORDERVALUE)
			{
				n++;
				prevSelection	= selection;
				selection		= RotateSelectionCCW(inputCage, selection);
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

			if(prevSelection == BORDERVALUE)
			{
				const float3 p0 = inputPoints[inputCage[Next(inputCage, selection0)].vert].xyz;
				const float3 p1 = inputPoints[he.vert].xyz;
				const float3 p2 = inputPoints[inputCage[Prev(inputCage, selection1)].vert].xyz;	
				points[0][cages[0][outVertex].vert].xyz		= (p0 + 6 * p1 + p2) / 8.0f;
				points[0][cages[0][outVertex].vert].color	= 0;
			}
		}
	}
#endif
#endif


#if 0
	int remaining = 1;
	InterlockedAdd(args.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);
	
	ThreadNodeOutputRecords<SubdivisionArgs> subdivArgs = SubdivideTwinEdgeTask0.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if(remaining == 1)
	{
		const uint32_t patchCount	= args.Get().newPatches * 4;
		const uint32_t dispatchX	= patchCount / 256 + (patchCount % 256 == 0 ? 0 : 1);
	
		subdivArgs.Get().remaining		= patchCount;
		subdivArgs.Get().patchCount		= patchCount;
		subdivArgs.Get().input			= 1;
		subdivArgs.Get().output			= 2;
		subdivArgs.Get().dispatchSize	= uint3(dispatchX, 1, 1);
	}

	subdivArgs.OutputComplete();
#endif
}


/************************************************************************************************/


void ApplyCCToTwinEdges(const uint threadID, in CatmullClarkArgs args)
{
	if (threadID >= args.patchCount)
		return;

	const uint source = args.source;	
	const uint target = args.target;	

	uint32_t edgeItr			= threadID;
	const TwinEdge he			= cages[source][edgeItr];
	const TwinEdge he_prev		= cages[source][Prev(edgeItr)];
		
	const uint32_t outEdge		= 1 + 4 * edgeItr;
	const uint32_t outVertex	= 0 + 4 * edgeItr;
		
	if (he.IsT())
		points[target][cages[target][outVertex].vert].color = 1;
		
#if 1
	if (!he.Border() && !he.IsCorner() && !he.IsT() && !he_prev.Border())
	{
		float3 p0	= points[source][cages[source][edgeItr].vert].xyz;
		float3 Q	= points[target][cages[target][GetFaceVertexIdx(outVertex)].vert].xyz;
		float3 R	= lerp(p0, points[source][cages[source][Next(edgeItr)].vert].xyz, 0.5);
		float  n	= 1.0f;

		uint32_t selection	= RotateSelectionCCW_TE(cages[source], edgeItr);
		while(selection != edgeItr && selection != BORDERVALUE)
		{
			n += 1.0f;
			Q += points[target][cages[target][GetFaceVertexIdx(selection * 4)].vert].xyz;
			R += lerp(p0, points[source][cages[source][Next(selection)].vert].xyz, 0.5);
			selection = RotateSelectionCCW_TE(cages[source], selection);
				
			if(n > 16)
			{
				selection = BORDERVALUE;
				break;
			}
		}
		if(selection != BORDERVALUE)
		{
			Q /= n;
			R /= n;
			
			points[target][cages[target][outVertex].vert].xyz = Q;
			points[target][cages[target][outVertex].vert].xyz = (Q + 2 * R + p0 * (n - 3)) / n;
		}
		else
			points[target][cages[target][outVertex].vert].color = 0;
	}
#endif

	// Calculate Edge point
	if(!he.Border() && !he.IsCorner())
	{
		const uint32_t e0 = min(he.Twin(), edgeItr);
		const uint32_t e1 = max(he.Twin(), edgeItr);
			
		const uint32_t f0 = GetFaceVertexIdx(4 * e0);
		const uint32_t f1 = GetFaceVertexIdx(4 * e1);
			
		const float3 fv0 = points[target][cages[target][f0].vert].xyz;
		const float3 fv1 = points[target][cages[target][f1].vert].xyz;
		const float3 ev0 = points[target][cages[target][outEdge].vert].xyz;
			
		points[target][cages[target][outEdge].vert].xyz	= (fv0 + fv1) / 4.0f + ev0 / 2.0f;
	}

	#if 0

	TwinEdge te1 = cages[target][outVertex];
	TwinEdge te2 = cages[target][outVertex + 3];
	if (!he.IsCorner() &&
		((te1.Border() && !te1.IsCorner()) ||
		(te2.Border() && !te2.IsCorner())))
	{
		uint32_t n				= 2; 
		uint32_t prevSelection	= edgeItr;
		uint32_t selection		= RotateSelectionCCW_TE(cages[source], edgeItr);
				
		while(selection != BORDERVALUE)
		{
			n++;
			prevSelection	= selection;
			selection		= RotateSelectionCCW_TE(cages[source], selection);
		}

		const uint32_t selection0 = prevSelection;

		prevSelection	= edgeItr;
		selection		= RotateSelectionCW_TE(cages[source], edgeItr);
		
		while(selection != BORDERVALUE)
		{
			n++;
			prevSelection	= selection;
			selection		= RotateSelectionCW_TE(cages[source], selection);
		}
					
		const uint32_t selection1 = prevSelection;
					
		if(prevSelection != BORDERVALUE)
		{
			const float3 p0 = points[source][cages[source][Next(selection0)].vert].xyz;
			const float3 p1 = points[source][he.vert].xyz;
			const float3 p2 = points[source][cages[source][Prev(selection1)].vert].xyz;	
					
			const float3 newPoint = (p0 + 6 * p1 + p2) / 8.0f;
			points[target][cages[target][outVertex].vert].xyz	= newPoint;
			points[target][cages[target][outVertex].vert].color = 0;
		}
		else 
			points[target][cages[target][outVertex].vert].color = 4;
	}

#endif
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(256, 1, 1)]
[NodeMaxDispatchGrid(4096, 1, 1)]
void SubdivideTwinEdgeTask0(
	RWDispatchNodeInputRecord<SubdivisionArgs>		args,
	[MaxRecords(1)] NodeOutput<CatmullClarkArgs>	ApplyCCToTwinEdges0,
	const uint										threadID : SV_DispatchThreadID)
{
	if (threadID >= args.Get().patchCount)
		return;
	
	const uint vertexBlock = (threadID / 4) * 9;
	SubdivideTwinEdges(points[0], cages[0], points[1], cages[1], threadID, vertexBlock);
	
	int remaining = 1;
	InterlockedAdd(args.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	Barrier(points[1], DEVICE_SCOPE);
	Barrier(cages[1], DEVICE_SCOPE);

#if 1
	ThreadNodeOutputRecords<CatmullClarkArgs> subDivArgs = ApplyCCToTwinEdges0.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if (remaining == 1)
	{
		const uint32_t patchCount	= args.Get().patchCount / 1;
		const uint32_t dispatchX	= patchCount / 256 + (patchCount % 256 == 0 ? 0 : 1);
	
		subDivArgs.Get().source			= 0;
		subDivArgs.Get().target			= 1;
		subDivArgs.Get().remaining		= patchCount;
		subDivArgs.Get().patchCount		= patchCount;
		subDivArgs.Get().dispatchSize	= uint3(dispatchX, 1, 1);
	}
	
	subDivArgs.OutputComplete();
#endif
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(256, 1, 1)]
[NodeMaxDispatchGrid(4096, 1, 1)]
void ApplyCCToTwinEdges0(
	const uint										threadID : SV_DispatchThreadID,
	RWDispatchNodeInputRecord<CatmullClarkArgs>		args, 
	[MaxRecords(1)] NodeOutput<SubdivisionArgs>		SubdivideTwinEdgeTask1)
{
	ApplyCCToTwinEdges(threadID, args.Get());

	int remaining = 1;
	InterlockedAdd(args.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);

#if 1
	ThreadNodeOutputRecords<SubdivisionArgs> subdivArgs = SubdivideTwinEdgeTask1.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if(remaining == 1 && args.Get().patchCount > 0)
	{
		const uint32_t patchCount	= args.Get().patchCount * 4;
		const uint32_t dispatchX	= patchCount / 1024 + (patchCount % 1024 == 0 ? 0 : 1);
	
		subdivArgs.Get().remaining		= patchCount;
		subdivArgs.Get().patchCount		= patchCount;
		subdivArgs.Get().input			= 1;
		subdivArgs.Get().output			= 2;
		subdivArgs.Get().dispatchSize	= uint3(dispatchX, 1, 1);
	}
	
	subdivArgs.OutputComplete();
#endif
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(1024, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void SubdivideTwinEdgeTask1(
	RWDispatchNodeInputRecord<SubdivisionArgs>		args,
	[MaxRecords(1)] NodeOutput<CatmullClarkArgs>	ApplyCCToTwinEdges1,
	const uint										threadID : SV_DispatchThreadID)
{
	if (threadID >= args.Get().patchCount)
		return;
	
	uint vertexBuffer;
	SubdivideTwinEdges(points[1], cages[1], points[2], cages[2], threadID, threadID * 9);
	
	int remaining = 1;
	InterlockedAdd(args.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	Barrier(points[1], DEVICE_SCOPE);
	Barrier(cages[1], DEVICE_SCOPE);
	
#if 1
	ThreadNodeOutputRecords<CatmullClarkArgs> subDivArgs = ApplyCCToTwinEdges1.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if (remaining == 1)
	{
		const uint32_t patchCount	= args.Get().patchCount;
		const uint32_t dispatchX	= patchCount / 256 + (patchCount % 256 == 0 ? 0 : 1);
		
		subDivArgs.Get().source			= 1;
		subDivArgs.Get().target			= 2;
		subDivArgs.Get().remaining		= patchCount;
		subDivArgs.Get().patchCount		= patchCount;
		subDivArgs.Get().dispatchSize	= uint3(dispatchX, 1, 1);
	}
	
	subDivArgs.OutputComplete();
#endif
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(256, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void ApplyCCToTwinEdges1(
	const uint									threadID : SV_DispatchThreadID,
	RWDispatchNodeInputRecord<CatmullClarkArgs>	args)
{
	ApplyCCToTwinEdges(threadID, args.Get());


#if 0
	int remaining = 1;
	InterlockedAdd(args.Get().remaining, -1, remaining);
	
	GroupMemoryBarrierWithGroupSync();
	
	ThreadNodeOutputRecords<SubdivisionArgs> subdivArgs = SubdivideTwinEdgeTask1.GetThreadNodeOutputRecords(remaining == 1 ? 1 : 0);
	if(remaining == 1)
	{
		subdivArgs.Get().remaining		= args.Get().patchCount;
		subdivArgs.Get().patchCount		= args.Get().patchCount;
		subdivArgs.Get().input			= 1;
		subdivArgs.Get().output			= 2;
		subdivArgs.Get().dispatchSize	= uint3(7, 1, 1);
	}
	
	Barrier(points[0], DEVICE_SCOPE);
	Barrier(cages[0], DEVICE_SCOPE);
	
	subdivArgs.OutputComplete();
#endif
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