#define RS1 "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
			"UAV(u0),"\
			"RootConstants(num32BitConstants=1, b0)"

cbuffer constants : register(b)
{
	uint maxDepth;
};


RWStructuredBuffer<uint32_t> CBTBuffer : register(u0);


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

uint32_t GetBitOffset(uint32_t idx)
{
	const uint32_t d_k		= FindMSB(idx);
	const uint32_t N_d_k	= maxDepth - d_k + 1;
	const uint32_t X_k		= ipow(2, d_k + 1) + idx * N_d_k;

	return X_k;
}

uint32_t GetHeapValue(uint32_t heapIdx)
{
	const uint32_t bitIdx		= GetBitOffset(heapIdx);
	const uint32_t bitWidth		= maxDepth - FindMSB(heapIdx) + 1;
	const uint32_t wordIdx		= bitIdx / (32);
	const uint32_t bitOffset	= bitIdx % (32);
	const uint32_t mask			= ((uint32_t(1) << (bitWidth)) - 1) << bitOffset;
	const uint32_t qw			= bitField[wordIdx];
	const uint32_t value		= qw & mask;

	return value >> bitOffset;
}

uint32_t DecodeNode(RWStructuredBuffer<uint> CBTBuffer, uint32_t leafID)
{
	uint32_t heapID = 1;
	uint32_t value	= GetHeapValue(CBTBuffer, heapID);

	for (int i = 0; i < maxDepth; i++)
	{
		if (value > 1)
		{
			uint32_t temp = GetHeapValue(CBTBuffer, 2 * heapID);
			if (leafID < temp)
			{
				heapID *= 2;
				value = temp;
			}
			else
			{
				leafID	= leafID - temp;
				heapID	= 2 * heapID + 1;
				value	= GetHeapValue(CBTBuffer, heapID);
			}
		}
		else
			break;
	}

	return heapID;
}

[RootSignature(RS1)]
[NumThreads(1, 1, 1)]
void UpdateCBT(const uint threadID : SV_DispatchThreadID)
{
	uint x = CBTBuffer[0];
	CBTBuffer[0] = x;
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