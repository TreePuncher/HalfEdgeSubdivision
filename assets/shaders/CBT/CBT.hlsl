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


uint32_t GetHeapValue(RWStructuredBuffer<uint> CBTBuffer, uint32_t heapIdx)
{
	const uint32_t	bitIdx		= GetBitOffset(heapIdx);
	const uint32_t	bitWidth	= maxDepth - FindMSB(heapIdx) + 1;
	const uint32_t	wordIdx		= bitIdx / (32);
	const uint32_t	bitOffset	= bitIdx % (32);
	const uint32_t	mask		= ((uint32_t(1) << (bitWidth)) - 1);
	const uint32_t	qw			= CBTBuffer[wordIdx];
	uint32_t		value		= (qw & (mask << bitOffset)) >> bitOffset;
	
	if(bitOffset + bitWidth > 32)
	{
		uint32_t bitsRemaining	= (bitOffset + bitWidth)  % 32;
		uint32_t secondHalf		= CBTBuffer[wordIdx];             
        uint32_t newMask        = (mask >> (bitWidth - bitsRemaining));
		uint32_t bits           = ((secondHalf & newMask) << (bitWidth - bitsRemaining));      
        value = value | bits;
	}

	return value;
}


uint32_t DecodeNode(RWStructuredBuffer<uint> CBTBuffer, int32_t leafID)
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


bool GetBitValue(uint heapID, uint bitID)
{
	return (heapID >> bitID) & 0x01;
}


[RootSignature(RS1)]
[NumThreads(1, 1, 1)]
void UpdateCBT(const uint tid : SV_DispatchThreadID)
{
	static const float3 colors[] =
	{
		float3(1, 0, 0),
		float3(0, 1, 0),
		float3(0, 0, 1),
		float3(1, 0, 1),
	};

	static const float3x3 M_b[] =
	{
		M0(),
		M1()
	};
	
	const float a = 9.0f / 16.0f;
	
	float3x3 t =
	{
		float3(-a * 0.9f,  0.9f, 0.1f),
		float3(-a * 0.9f, -0.9f, 0.1f),
		float3( a * 0.9f, -0.9f, 0.1f),
	};
	
	float3x3 m =
		float3x3(
			1, 0, 0, 
			0, 1, 0, 
			0, 0, 1);
	
	const uint heapID	= DecodeNode(CBTBuffer, tid);
	const uint d		= FindMSB(heapID);

	for (int bitID = d - 1; bitID >= 0; --bitID)
	{
		int idx = GetBitValue(heapID, bitID);
		m = mul(M_b[idx], m);
	}

	const float3x3 tri = mul(m, t);
}


struct Vertex
{
	float3	color : COLOR;
	float4	position : SV_Position;
	uint	heapID : ID;
};


[RootSignature(RS1)]
Vertex DrawCBT_VS(const uint vertexID : SV_VertexID)
{
	static const float3 colors[] =
	{
		float3(1, 0, 0),
		float3(0, 1, 0),
		float3(0, 0, 1),
		float3(1, 0, 1),
	};

	static const float3x3 M_b[] =
	{
		M0(),
		M1()
	};
	
	const uint tid = (vertexID / 3);
	const float a = 9.0f / 16.0f;
	
	float3x3 t =
	{
		float3(-a * 0.9f,  0.9f, 0.1f),
		float3(-a * 0.9f, -0.9f, 0.1f),
		float3( a * 0.9f, -0.9f, 0.1f),
	};
	
	float3x3 m =
		float3x3(
			1, 0, 0, 
			0, 1, 0, 
			0, 0, 1);
	
	const uint heapID	= DecodeNode(CBTBuffer, tid);
	const uint d		= FindMSB(heapID);

	for (int bitID = d - 1; bitID >= 0; --bitID)
	{
		int idx = GetBitValue(heapID, bitID);
		m = mul(M_b[idx], m);
	}

	const float3x3 tri = mul(m, t);
	
	Vertex OUT;
	OUT.position	= float4(tri[vertexID % 3].xyz, 1);
	OUT.color		= colors[tid % 4];
	OUT.heapID		= heapID;
	return OUT;
}


float4 DrawCBT_PS(Vertex v) : SV_Target
{
	return float4(v.color, 1);
}