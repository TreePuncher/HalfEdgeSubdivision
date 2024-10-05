#define RS1 "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
			"UAV(u0),"\
			"RootConstants(num32BitConstants=1, b0)"

cbuffer constants : register(b)
{
	uint maxDepth;
};


RWBuffer<uint> CBTBuffer : register(t0);


float3x3 M0()
{
	return float3x3(
		1.0f, 0.0f, 0.0f,
		0.5f, 0.0f, 0.5f,
		0.0f, 1.0f, 0.0f);
}

float3x3 M1()
{
	return float3x3(
		0.0f, 1.0f, 0.0f,
		0.5f, 0.0f, 0.5f,
		0.0f, 0.0f, 1.0f);
}

uint FindMSB(uint heapID)
{
	return firstbithigh(heapID);
}

uint FindLSB(uint x)
{
	return firstbitlow(x);
}


uint Depth(uint Bit)
{
	return maxDepth - log2(Bit + 1);
}

uint ipow(uint base, uint exp)
{
	uint result = 1;
	for (;;)
	{
		if (exp & 1)
			result *= base;
		exp >>= 1;
		if (!exp)
			break;
		base *= base;
	}

	return result;
}


uint BitToHeapIndex()
{
	
}

uint HeapIndexToBitIndex(const uint k) { return k * ipow(2, maxDepth - FindMSB(k)) - ipow(2, maxDepth); }

[RootSignature(RS1)]
[NumThreads(1, 1, 1)]
void UpdateCBT(const uint threadID : SV_DispatchThreadID)
{
	CBTBuffer[0] = 0x01;
}

[RootSignature(RS1)]
float4 DrawCBT_VS(const uint vertexID : SV_VertexID) : SV_Position
{
	return float4(0, 0, 0, 1);
}

float4 DrawCBT_PS() : SV_Target
{
	return float4(1, 1, 1, 1);
}