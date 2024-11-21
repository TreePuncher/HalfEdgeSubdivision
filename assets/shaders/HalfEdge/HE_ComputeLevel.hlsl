#define BuildLevelRS "UAV(u0)," \
					 "UAV(u1)," \
					 "UAV(u2)," \
					 "SRV(t0)," \
					 "SRV(t1)," \
					 "RootConstants(num32BitConstants = 4, b0)"

cbuffer constants : register(b0)
{
	uint count;
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

RWStructuredBuffer<HalfEdge>	outputStructure		: register(u0);
RWStructuredBuffer<float3>		vertexPoints		: register(u1);
RWStructuredBuffer<uint>		counters			: register(u2);

[RootSignature(BuildLevelRS)]
[numthreads(64, 1, 1)]
void BuildLevel(const uint threadID : SV_DispatchThreadID)
{
	if (threadID >= count)
		return;
	
	float3 facePoint = float3(0, 0, 0);
	
	uint idx;
	uint vertexCount = 9;
	InterlockedAdd(counters[0], vertexCount, idx);
	
	uint32_t edgeItr = threadID * 16;

	for (int32_t i = 0; i < 4; i++)
	{
		const uint outputIdx = edgeItr * 4;
		HalfEdge he = halfEdge[threadID * 4 + i];
		
		HalfEdge edge0;
		edge0.twin = (he.twin != -1) ? (halfEdge[he.twin].next * 4 + 3) : -1;
		edge0.next = outputIdx + 1;
		edge0.prev = outputIdx + 3;
		edge0.vert = idx + 2 * i + 0;
		outputStructure[4 * i + threadID * 16] = edge0;

		HalfEdge edge1;
		edge1.twin = ((outputIdx * 4 + 4 + i + 1) % 4) * 4 + 2;
		edge1.next = outputIdx + 2;
		edge1.prev = outputIdx + 0;
		edge1.vert = idx + 2 * i + 1;
		outputStructure[1 + 4 * i + threadID * 16] = edge1;
		
		HalfEdge edge2;
		edge2.twin = ((outputIdx * 4 + 4 + i - 1) % 4) * 4 + 1;
		edge2.next = outputIdx + 3;
		edge2.prev = outputIdx + 1;
		edge2.vert = idx + vertexCount - 1;
		outputStructure[2 + 4 * i + threadID * 16] = edge2;
		
		HalfEdge edge3;
		edge3.twin = (he.twin != -1) ? (halfEdge[he.prev].twin * 4 + 3) : -1;
		edge3.next = outputIdx + 0;
		edge3.prev = outputIdx + 2;
		edge3.vert = idx + (vertexCount - 2 + 2 * i) % (vertexCount - 1);
		outputStructure[3 + 4 * i + threadID * 16] = edge3;

		facePoint += inputVerts[he.vert];
		edgeItr = he.next;
		
		vertexPoints[idx + 2 * i + 0] = inputVerts[he.vert];
		vertexPoints[idx + 2 * i + 1] = (inputVerts[he.vert] + inputVerts[halfEdge[he.next].vert]) / 2;
	}
	
	vertexPoints[idx + vertexCount - 1] = facePoint / 4;
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
