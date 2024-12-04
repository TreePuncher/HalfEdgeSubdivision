#include "HE_Common.hlsl"
#include "../CBT/CBT.hlsl"
#include "../Intersection.hlsl"


/************************************************************************************************/


StructuredBuffer<HalfEdge>	inputCage	: register(t0);
StructuredBuffer<Vertex>	inputPoints : register(t1);
StructuredBuffer<HE_Face>	inputFaces	: register(t2);
StructuredBuffer<uint>		faceLookup	: register(t3);

RWStructuredBuffer<TwinEdge>	cages[]		: register(u0, space1);
RWStructuredBuffer<Vertex>		points[]	: register(u0, space2);
//RWStructuredBuffer<uint>		presence[]	: register(u0, space3);


cbuffer ViewConstants : register(b0)
{
	float4x4	view;
	uint		heCount;
	uint		patchCount;
};

cbuffer ViewConstants : register(b1)
{
	Frustum frustum;
};


struct ApplyCCArgs_HE
{
	HalfEdge	edge;
	uint32_t	edgeID;
	uint32_t	targetVertex;
};


struct LaunchParams
{
	uint3	dispatchSize : SV_DispatchGrid;
	uint	patchCount;
};


struct BuildTwinEdgeArgs
{
	uint	halfEdgeCounter;
	uint	vertexAllocation;
	uint	patchCount;
	uint	dispatchesRemaining;
	uint3	dispatchSize : SV_DispatchGrid;
};


struct TwinEdgeBuild
{
	uint	patchIdx;
	uint	i;
	uint	outputIdx;
	uint	vertexRange;
	uint	faceSize;
	uint3	dispatchSize : SV_DispatchGrid;
};


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void InitiateHalfEdgeMesh(
					DispatchNodeInputRecord<LaunchParams>	args,	
	[MaxRecords(1)]	NodeOutput<BuildTwinEdgeArgs>			BuildBaseCage)
{
	const uint dispatchX = args.Get().patchCount / 32 + (args.Get().patchCount % 32 == 0 ? 0 : 1);
	
	ThreadNodeOutputRecords<BuildTwinEdgeArgs> outputRecords = BuildBaseCage.GetThreadNodeOutputRecords(1);
	outputRecords.Get().halfEdgeCounter		= 0;
	outputRecords.Get().vertexAllocation	= 0;
	outputRecords.Get().patchCount			= args.Get().patchCount;
	outputRecords.Get().dispatchesRemaining	= dispatchX;
	outputRecords.Get().dispatchSize		= uint3(dispatchX, 1, 1);
	outputRecords.OutputComplete();
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(32, 1, 1)]
void BuildBaseCage(
	RWDispatchNodeInputRecord<BuildTwinEdgeArgs>							args,
											const uint						dispatchThreadID	: SV_DispatchThreadID,
											const uint						threadID			: SV_GroupIndex)
{
	if(dispatchThreadID >= args.Get().patchCount)
		return;

	const HE_Face face = inputFaces[dispatchThreadID];

	float3 f = float3(0, 0, 0);
#define CULL 0
#if CULL
	AABB aabb;
	aabb.mMin = float3( 100000,  100000,  100000);
	aabb.mMax = float3(-100000, -100000, -100000);
	
	uint internalEdges		= 0;
	uint internalVertices	= 0;

	for(int i = 0; i < range.y; i++)
	{	
		HalfEdge he = inputCage[range.x + i];
		
		internalEdges		+= !he.Border();
		internalVertices	+= !he.IsT();

		const float3 xyz = inputPoints[he.vert].xyz;
		f += xyz;
		aabb.Add(mul(view, float4(xyz, 1)));
	}
	
	if(Intersects(frustum, aabb))
#endif
	
	for(int i = 0; i < face.edgeCount; i++)
	{	
		const uint outputIdx = 4 * (face.begin + i);
		
		TwinEdge edges[4];
		GetTwinEdges(face, i, edges, inputCage);
		
		cages[0][outputIdx + 0] = edges[0];
		cages[0][outputIdx + 1] = edges[1];
		cages[0][outputIdx + 2] = edges[2];
		cages[0][outputIdx + 3] = edges[3];
	}
}


/************************************************************************************************/

struct LaunchSubDivParams
{
	uint patchCount;
	uint halfEdgeCount;
};

struct BuildFacesArgs
{
	uint	remaining;
	uint	patchCount;
	uint3	dispatchSize : SV_DispatchGrid;
};

struct BuildEdgesArgs
{
	uint	halfEdgeCount;
	uint3	dispatchSize : SV_DispatchGrid;
};


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NumThreads(1, 1, 1)]
void SubdivideHalfEdgeMesh(
					DispatchNodeInputRecord<LaunchSubDivParams>	args,
	[MaxRecords(1)]	NodeOutput<BuildFacesArgs>					BuildFaces,
	[MaxRecords(1)]	NodeOutput<BuildEdgesArgs>					BuildEdges,
	[MaxRecords(1)]	NodeOutput<BuildEdgesArgs>					BuildVertices,
					const uint									dispatchThreadID : SV_DispatchThreadID)
{
	const uint patchCount		= args.Get().patchCount;
	const uint halfEdgeCount	= args.Get().halfEdgeCount;

	ThreadNodeOutputRecords<BuildFacesArgs> launchFaces = BuildFaces.GetThreadNodeOutputRecords(1);
	launchFaces.Get().patchCount		= patchCount;
	launchFaces.Get().remaining			= patchCount;
	launchFaces.Get().dispatchSize		= uint3(patchCount / 1024 + (patchCount % 1024 == 0 ? 0 : 1), 1, 1);
	launchFaces.OutputComplete();

	ThreadNodeOutputRecords<BuildEdgesArgs> launchEdges = BuildEdges.GetThreadNodeOutputRecords(1);
	launchEdges.Get().halfEdgeCount		= heCount;
	launchEdges.Get().dispatchSize		= uint3(heCount / 1024 + (heCount % 1024 == 0 ? 0 : 1), 1, 1);
	launchEdges.OutputComplete();

	ThreadNodeOutputRecords<BuildEdgesArgs> launchVertex = BuildVertices.GetThreadNodeOutputRecords(1);
	launchVertex.Get().halfEdgeCount	= halfEdgeCount;
	launchVertex.Get().dispatchSize		= uint3(halfEdgeCount / 1024 + (halfEdgeCount % 1024 == 0 ? 0 : 1), 1, 1);
	launchVertex.OutputComplete();
}


/************************************************************************************************/


float3 GetFacePoint(uint halfEdgeID)
{
	HalfEdge he = inputCage[halfEdgeID];
	float3	f = inputPoints[he.vert].xyz;
	float	n = 1.0f;
	
	while (he.next != halfEdgeID)
	{
		he					= inputCage[he.next];
		const float3 xyz	= inputPoints[he.vert].xyz;
		f += xyz;
		n += 1.0f;
		
		if(n > 16)
			return float3(100000000, 100000000, 100000000);
	}
	
	return f / n;
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(1024, 1, 1)]
void BuildEdges(
	DispatchNodeInputRecord<BuildEdgesArgs>	args,
	const uint dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID >= args.Get().halfEdgeCount)
		return;
	
	const uint faceIdx	= faceLookup[dispatchThreadID];
	const HE_Face face	= inputFaces[faceIdx];
	
	const uint32_t vertexID	= face.vertexRange + ((dispatchThreadID - face.vertexRange) % face.edgeCount) * 2 + 1;
	const uint32_t edgeID	= face.begin + dispatchThreadID % face.edgeCount;
	
	HalfEdge he		= inputCage[edgeID];
	HalfEdge heNext = inputCage[he.next];

	if (!he.Border())
	{
		const uint32_t e0 = min(he.Twin(), edgeID);
		const uint32_t e1 = max(he.Twin(), edgeID);
		
		const float3 fv0 = GetFacePoint(e0);
		const float3 fv1 = GetFacePoint(e1);
		const float3 ev0 = lerp(inputPoints[he.vert].xyz, inputPoints[heNext.vert].xyz, 0.5f);
		
		points[0][vertexID].color	= 6;
		points[0][vertexID].xyz		= (fv0 + fv1) / 4.0f + ev0 / 2.0f;
	}
	else
	{
		const float3 p0 = inputPoints[he.vert].xyz;
		const float3 p1 = inputPoints[heNext.vert].xyz;
	
		points[0][vertexID].xyz		= lerp(p0, p1, 0.5f);
		points[0][vertexID].color	= 6;
	}
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(1024, 1, 1)]
void BuildVertices(
	DispatchNodeInputRecord<BuildEdgesArgs>	args,
	const uint dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID >= heCount)
		return;
	
	const uint faceIdx	= faceLookup[dispatchThreadID];
	const HE_Face face	= inputFaces[faceIdx];
		
	const uint32_t vertexID = face.vertexRange + ((dispatchThreadID - face.vertexRange) % face.edgeCount) * 2 + 0;
	const uint32_t edgeID	= face.begin + dispatchThreadID % face.edgeCount;

	HalfEdge he	= inputCage[edgeID];
	if (he.IsT())
	{		
		uint32_t n				= 2; 
		uint32_t prevSelection	= edgeID;
		uint32_t selection		= RotateSelectionCCW(inputCage, edgeID);

		while(selection != BORDERVALUE)
		{
			n++;
			prevSelection	= selection;
			selection		= RotateSelectionCCW(inputCage, selection);
			
			if (n > 16)
			{
				const float3 p0				= inputPoints[he.vert].xyz;
				points[0][vertexID].xyz		= p0;
				points[0][vertexID].color	= 1;
				return;
			}
		}

		const uint32_t selection0 = prevSelection;

		prevSelection	= edgeID;
		selection		= RotateSelectionCW(inputCage, edgeID);
		while(selection != BORDERVALUE)
		{
			n++;
			prevSelection	= selection;
			selection		= RotateSelectionCW(inputCage, selection);
		
			if (n > 16)
			{
				const float3 p0				= inputPoints[he.vert].xyz;
				points[0][vertexID].xyz		= p0;
				points[0][vertexID].color	= 1;
				return;
			}
		}
			
		const uint32_t selection1 = prevSelection;

		if (selection0 != BORDERVALUE && selection0 != BORDERVALUE && n > 2)
		{
			const float3 p0 = inputPoints[inputCage[Next(inputCage, selection0)].vert].xyz;
			const float3 p1 = inputPoints[he.vert].xyz;
			const float3 p2 = inputPoints[inputCage[Prev(inputCage, selection1)].vert].xyz;	
			points[0][vertexID].xyz		= (p0 + 6 * p1 + p2) / 8.0f;
			points[0][vertexID].color	= 6;
		}
		else
		{
			const float3 p0 = inputPoints[he.vert].xyz;
			points[0][vertexID].xyz		= p0;
			points[0][vertexID].color	= 6;
		}
	}
	else
	{
		float3	p0	= inputPoints[he.vert].xyz;
		float3	Q	= GetFacePoint(edgeID);
		float3	R	= lerp(p0, inputPoints[inputCage[Next(inputCage, edgeID)].vert].xyz, 0.5f);
		float	n	= 1.0f;
		
		uint32_t selection = RotateSelectionCCW(inputCage, edgeID);
		while(selection != edgeID)
		{
			n += 1.0f;
			Q += GetFacePoint(selection);
			R += lerp(p0, inputPoints[inputCage[Next(inputCage, selection)].vert].xyz, 0.5);
			selection = RotateSelectionCCW(inputCage, selection);
					
			if(n >= 16)
			{
				points[0][vertexID].xyz		= p0;
				points[0][vertexID].color	= 2;
				return;
			}
		}
		
		if(selection != BORDERVALUE)
		{
			Q /= n;
			R /= n;

			points[0][vertexID].xyz		= (Q + 2 * R + p0 * (n - 3)) / n;
			points[0][vertexID].color	= 6;
		}
	}
}


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(1024, 1, 1)]
void BuildFaces(RWDispatchNodeInputRecord<BuildFacesArgs>	args,
				[MaxRecords(1)]	NodeOutput<BuildEdgesArgs>	BuildEdges,
				const uint dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID >= patchCount)
		return;
	
	const HE_Face	face		= inputFaces[dispatchThreadID];
	const uint		vertexCount	= face.GetVertexCount();

	points[0][face.vertexRange + vertexCount - 1].xyz	= GetFacePoint(face.begin);
	points[0][face.vertexRange + vertexCount - 1].color	= 6;

	uint idx = 0;
	InterlockedAdd(args.Get().remaining, -1, idx);	
	Barrier(points[0], DEVICE_SCOPE);
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