/************************************************************************************************/

#define BORDERVALUE (0xffffffff >> 2)
#define TWINMASK (0xffffffff >> 2)

/************************************************************************************************/


struct HalfEdge
{
	int32_t twin;
	uint32_t next;
	uint32_t prev;
	uint32_t vert;
	
	uint32_t Twin()
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
	
	bool IsT()
	{
		return (twin & (1 << 30)) != 0;
	}
	
	void MarkCorner(bool f)
	{
		twin = Twin() | (f << 31);
	}
	
	void MarkT(bool f)
	{
		twin = Twin() | (f << 30);
	}
};


/************************************************************************************************/


struct TwinEdge
{
	int32_t twin;
	uint32_t vert;
	
	uint32_t Twin()
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
	
	bool IsT()
	{
		return (twin & (1 << 30)) != 0;
	}
	
	void MarkT(bool f)
	{
		twin = Twin() | (f << 30);
	}
};


/************************************************************************************************/


uint32_t GetFaceVertexIdx(uint32_t halfEdge)
{
	return (halfEdge & (~0x3)) | 2;
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


uint32_t Next(uint32_t idx)
{
	return (idx & (0xffffffff << 2)) | ((idx + 1) & 3);
}


uint32_t Prev(uint32_t idx)
{
	return (idx & (0xffffffff << 2)) | ((idx - 1) & 3);
}


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


template<typename TY_cage>
uint32_t RotateSelectionCW_TE(in TY_cage cage, uint32_t halfEdge)
{
	return (halfEdge != BORDERVALUE) ? cage[Prev(halfEdge)].Twin() : BORDERVALUE;
}


template<typename TY_cage>
uint32_t RotateSelectionCCW_TE(in TY_cage cage, uint32_t halfEdge)
{
	const uint32_t twin = cage[halfEdge].Twin();
	return (twin == BORDERVALUE) ? BORDERVALUE : Next(twin);
}


/************************************************************************************************/


struct Vertex
{
	float3 xyz;
	uint color;
	float2 UV;
};


Vertex MakeVertex(float3 xyz, uint color = 0, float2 UV = float2(0, 0))
{
	Vertex v;
	v.xyz = xyz;
	v.color = color;
	v.UV = UV;
	
	return v;
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


/************************************************************************************************/


void SubdividePatch(
	in uint2 beginCount, in uint idx, in uint vertexCount, 
	in StructuredBuffer<HalfEdge>	inputCage,
	in StructuredBuffer<Vertex>		inputPoints,
	in RWStructuredBuffer<TwinEdge>	outputCage,
	in RWStructuredBuffer<Vertex>	outputPoints)
{
	float3	facePoint	= float3(0, 0, 0);
	
	int32_t i			= 0;
	uint32_t edgeItr	= beginCount.x;
	
	while (edgeItr != beginCount.x || i == 0)
	{
		const uint outputIdx = edgeItr * 4;
		
		HalfEdge he			= inputCage[edgeItr];
		HalfEdge prevEdge	= inputCage[he.prev];
		
		TwinEdge edge0;
		edge0.twin = he.Border() ? BORDERVALUE : (Next(inputCage, he.Twin()) * 4 + 3);
		edge0.vert = idx + 2 * i + 0;
		edge0.MarkCorner(he.IsCorner());
		edge0.MarkT(he.IsT());
		outputCage[outputIdx + 0] = edge0;

		TwinEdge edge1;
		edge1.twin = (beginCount.x + (beginCount.y + i + 1) % beginCount.y) * 4 + 2;
		edge1.vert = idx + 2 * i + 1;
		outputCage[outputIdx + 1] = edge1;
		
		TwinEdge edge2;
		edge2.twin = (beginCount.x + (beginCount.y + i - 1) % beginCount.y) * 4 + 1;
		edge2.vert = idx + vertexCount - 1;
		outputCage[outputIdx + 2] = edge2;
		
		TwinEdge edge3;
		edge3.twin = prevEdge.Border() ? BORDERVALUE : (prevEdge.Twin() * 4);
		edge3.vert = idx + (vertexCount - 2 + 2 * i) % (vertexCount - 1);
		outputCage[outputIdx + 3] = edge3;

		facePoint += inputPoints[he.vert].xyz;
		edgeItr	   = he.next;
		
		outputPoints[idx + 2 * i + 0] = MakeVertex(inputPoints[he.vert].xyz, 6);
		outputPoints[idx + 2 * i + 1] = MakeVertex(
												(inputPoints[he.vert].xyz + 
												 inputPoints[inputCage[he.next].vert].xyz) / 2, 6);
		
		i++;
		
		if(i > 32)
			break;
	}

	outputPoints[idx + vertexCount - 1] = MakeVertex(facePoint / beginCount.y, 6);
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