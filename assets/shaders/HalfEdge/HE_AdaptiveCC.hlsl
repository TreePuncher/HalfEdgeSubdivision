#include "HE_Common.hlsl"
#include "../CBT/CBT.hlsl"
#include "../Intersection.hlsl"


/************************************************************************************************/



StructuredBuffer<HalfEdge>		inputCage	: register(t0);
StructuredBuffer<Vertex>		inputPoints : register(t1);
StructuredBuffer<HE_Face>		inputFaces	: register(t2);
StructuredBuffer<uint>			faceLookup	: register(t3);

RWStructuredBuffer<uint32_t>	cbt			: register(u0, space0);
RWStructuredBuffer<TwinEdge>	cages[]		: register(u0, space1);
RWStructuredBuffer<Vertex>		points[]	: register(u0, space2);

RWStructuredBuffer<FaceID>				faceDrawList	: register(u1);
RWStructuredBuffer<DispatchMeshArgs>	drawArgs		: register(u2);

cbuffer ViewConstants : register(b0)
{
	float4x4	view;
	uint		heCount;
	uint		patchCount;
	uint		maxDepth;
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

uint DispatchXSize(uint x, uint groupSize)
{
	return x / groupSize + (x % groupSize != 0);
}


/************************************************************************************************/


struct LaunchClassifyParams
{
	uint3	xyz : SV_DispatchGrid;
	uint	dispatchesRemaining;
	uint	faceCounter;
};


struct [NodeTrackRWInputSharing] SumReductionArgs
{
	uint32_t i;
	uint32_t stepSize;
	uint32_t end;
	uint32_t start_IN;
	uint32_t start_OUT;
	uint3	 xyz : SV_DispatchGrid;
};


/************************************************************************************************/


uint GetDispatchX(uint W, uint groupSize)
{
	return (W / groupSize) + (W % groupSize == 0 ? 0 : 1);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(32, 1, 1)]
void Classify(	RWDispatchNodeInputRecord<LaunchClassifyParams>		args, 
				[MaxRecords(1)]		NodeOutput<SumReductionArgs>	CBTSumReduction,
									const uint						dispatchThreadID	: SV_DispatchThreadID,
									const uint						threadID			: SV_GroupIndex)
{
	if(dispatchThreadID < patchCount)
	{
		const HE_Face face = inputFaces[dispatchThreadID];

		AABB aabb;
		aabb.mMin = float3( 100000,  100000,  100000);
		aabb.mMax = float3(-100000, -100000, -100000);	
	
		for(int i = 0; i < face.edgeCount; i++)
		{	
			HalfEdge he = inputCage[face.begin + i];
		
			const float3 xyz = inputPoints[he.vert].xyz;
			aabb.Add(mul(view, float4(xyz, 1)));
		}
	
		const bool intersects = Intersects(frustum, aabb);

		if(intersects)
		{
			SetBit(cbt, dispatchThreadID, true, maxDepth);
		
			for(int j = 0; j < face.edgeCount; j++)
			{
				HalfEdge he = inputCage[face.begin + j];
				
				if(!he.Border())
					SetBit(cbt, faceLookup[he.Twin()], true, maxDepth);
			}

			uint faceIdx;
			InterlockedAdd(args.Get().faceCounter, face.edgeCount, faceIdx);
			for(int i = 0; i < face.edgeCount; i++)
				faceDrawList[faceIdx + i] = MakeFaceID(face.begin + i, 0, face.vertexRange);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	int idx = 0;
	if (threadID == 0)
		InterlockedAdd(args.Get().dispatchesRemaining, -1, idx);
	
	Barrier(ALL_MEMORY, DEVICE_SCOPE);

	//ThreadNodeOutputRecords<SumReductionArgs> sumReduction = CBTSumReduction.GetThreadNodeOutputRecords(idx == 1);
	//
	//if(idx == 1)
	//{
	//	sumReduction.Get().stepSize		= 1;
	//	sumReduction.Get().i			= 0;
	//	sumReduction.Get().end			= ipow(2, maxDepth - 1);
	//	sumReduction.Get().start_IN		= GetBitOffset(ipow(2, maxDepth - 0), maxDepth);
	//	sumReduction.Get().start_OUT	= GetBitOffset(ipow(2, maxDepth - 1), maxDepth);
	//	sumReduction.Get().xyz			= uint3(GetDispatchX(ipow(2, maxDepth - 1), 32), 1, 1);

		drawArgs[0] = MakeDrawListArgs(args.Get().faceCounter);
	//}

	//sumReduction.OutputComplete();
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NodeMaxRecursionDepth(25)]
[NumThreads(32, 1, 1)]
void CBTSumReduction(	
	RWDispatchNodeInputRecord<SumReductionArgs>		args, 
	[MaxRecords(1)]	NodeOutput<SumReductionArgs>	CBTSumReduction,
	const uint										j : SV_DispatchThreadID,
	const uint										threadID : SV_GroupIndex)
{
	const uint i	= args.Get().i;
	const uint end	= args.Get().end;

	const uint start_IN		= args.Get().start_IN;
	const uint start_OUT	= args.Get().start_OUT;
	const uint stepSize		= args.Get().stepSize;

	if (j < end)
	{
		const uint32_t a = ReadValue(cbt, start_IN + (2 * j + 0) * stepSize, i + 1);
		const uint32_t b = ReadValue(cbt, start_IN + (2 * j + 1) * stepSize, i + 1);
		const uint32_t c = a + b;
 		
		WriteValue(cbt, args.Get().start_OUT + j * (args.Get().stepSize + 1), args.Get().i + 2, c);
	}	

	const bool submitNext = (args.FinishedCrossGroupSharing() && threadID == 0  && i + 1 < maxDepth);
	
	Barrier(ALL_MEMORY, DEVICE_SCOPE);

	ThreadNodeOutputRecords<SumReductionArgs> next = CBTSumReduction.GetThreadNodeOutputRecords(submitNext);
	if (submitNext)
	{
		const uint32_t ip		= i + 1;
		
		next.Get().stepSize		= stepSize + 1;
		next.Get().i			= ip;
		next.Get().end			= end / 2;
		next.Get().start_IN		= GetBitOffset(ipow(2, maxDepth - 0 - ip), maxDepth);
		next.Get().start_OUT	= GetBitOffset(ipow(2, maxDepth - 1 - ip), maxDepth);
		next.Get().xyz			= uint3(end / 2, 1, 1);
	}

	next.OutputComplete();
}


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


struct BuildFace2Args
{
	uint	patchCount;
	uint	edgeCount;
	HE_Face faces[32];
	uint3	dispatchSize : SV_DispatchGrid;
};


struct LaunchSubDivParams
{
	uint3	xyz  : SV_DispatchGrid;
	uint	dispatchesRemaining;
	uint	patchCount;
	uint	halfEdgeCount;
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


groupshared uint			localPatchCount;
groupshared uint			localEdgeCount;

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(32, 1, 1)]
void SubdivideHalfEdgeMesh(
						RWDispatchNodeInputRecord<LaunchSubDivParams>	args,
	[MaxRecords(1)]		NodeOutput<BuildFace2Args>						BuildEdges2,
					const uint											dispatchThreadID	: SV_DispatchThreadID, 
					const uint											groupDispatchID		: SV_GroupIndex)
{
	if(dispatchThreadID >= patchCount)
		return;

	if(groupDispatchID == 0)	
	{
		localPatchCount = 0;
		localEdgeCount = 0;
	}

	GroupMemoryBarrierWithGroupSync();
	
	const HE_Face face = inputFaces[dispatchThreadID];

	AABB aabb;
	aabb.mMin = float3( 100000,  100000,  100000);
	aabb.mMax = float3(-100000, -100000, -100000);
	
	float3 f = float3(0, 0, 0);
	for(int i = 0; i < face.edgeCount; i++)
	{	
		HalfEdge he = inputCage[face.begin + i];
		
		const float3 xyz = inputPoints[he.vert].xyz;
		f += xyz;
		aabb.Add(mul(view, float4(xyz, 1)));
	}
	
	const bool intersects = Intersects(frustum, aabb);
	uint patchIdx;
	uint edgeIdx;
	
	if(intersects)
	{
		InterlockedAdd(args.Get().patchCount, 1), 
		InterlockedAdd(args.Get().halfEdgeCount, face.edgeCount);
		InterlockedAdd(localPatchCount, 1, patchIdx);
		InterlockedAdd(localEdgeCount,	face.edgeCount, edgeIdx);

		const uint	vertexCount	= face.GetVertexCount();
		points[0][face.vertexRange + vertexCount - 1].xyz	= f / face.edgeCount;
		points[0][face.vertexRange + vertexCount - 1].color	= 6;
	}

	GroupMemoryBarrierWithGroupSync();
	GroupNodeOutputRecords<BuildFace2Args> dispatchEdges = BuildEdges2.GetGroupNodeOutputRecords(intersects);
	
	if(intersects)
		dispatchEdges[0].faces[patchIdx] = face;
	
	if(groupDispatchID == 0)
	{
		dispatchEdges[0].edgeCount		= localEdgeCount;
		dispatchEdges[0].patchCount		= localPatchCount;
		dispatchEdges[0].dispatchSize	= uint3(localEdgeCount / 32 + (localEdgeCount % 32 == 0 ? 0 : 1), 1, 1); 
	}

	dispatchEdges.OutputComplete();
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


/************************************************************************************************/


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


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(1024, 1, 1)]
void BuildFaces(RWDispatchNodeInputRecord<BuildFacesArgs>	args,
				const uint dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID >= patchCount)
		return;
	
	const HE_Face	face		= inputFaces[dispatchThreadID];
	const uint		vertexCount	= face.GetVertexCount();

	points[0][face.vertexRange + vertexCount - 1].xyz	= GetFacePoint(face.begin);
	points[0][face.vertexRange + vertexCount - 1].color	= 6;
}


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NodeShareInputOf("BuildEdges2")]
[NumThreads(32, 1, 1)]
void BuildVertices2(DispatchNodeInputRecord<BuildFace2Args>	args,
					const uint								dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID >= args.Get().edgeCount)
		return;
	
	HE_Face face	= args.Get().faces[0];
	uint edgeCount	= 0;
	
	for (int i = 0; i < args.Get().patchCount; i++)
	{
		edgeCount += face.edgeCount;
		if (dispatchThreadID < edgeCount)
			break;
		else
			face = args.Get().faces[i + 1];
	}
	
	const uint faceEdgeID	= (dispatchThreadID - edgeCount) % face.edgeCount;
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


/************************************************************************************************/


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16000, 1, 1)]
[NumThreads(32, 1, 1)]
void BuildEdges2(	DispatchNodeInputRecord<BuildFace2Args>	args,
					const uint								dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID >= args.Get().edgeCount)
		return;
	
	HE_Face face	= args.Get().faces[0];
	uint edgeCount	= 0;
	
	for (int i = 0; i < args.Get().patchCount; i++)
	{
		edgeCount += face.edgeCount;
		if (dispatchThreadID < edgeCount)
			break;
		else
			face = args.Get().faces[i + 1];
	}
	
	const uint		faceEdgeID	= (dispatchThreadID - edgeCount) % face.edgeCount;
	const uint32_t	vertexID	= face.vertexRange + ((faceEdgeID - face.vertexRange) % face.edgeCount) * 2 + 1;
	const uint32_t	edgeID		= face.begin + dispatchThreadID % face.edgeCount;
	
		
	points[0][vertexID].color	= edgeCount;
	points[0][vertexID].UV		= (dispatchThreadID - edgeCount) % face.edgeCount;
	
	HalfEdge he		= inputCage[face.begin + faceEdgeID];
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