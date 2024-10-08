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


uint Depth(in uint maxDepth, uint Bit)
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


uint32_t GetBitOffset(in uint32_t maxDepth, uint32_t idx)
{
	const uint32_t d_k		= FindMSB(idx);
	const uint32_t N_d_k	= maxDepth - d_k + 1;
	const uint32_t X_k		= ipow(2, d_k + 1) + idx * N_d_k;

	return X_k;
}


uint32_t ReadValue(RWStructuredBuffer<uint> CBTBuffer, uint32_t start, uint32_t bitWidth)
{
	const uint32_t	mask	= (uint32_t(1) << (bitWidth)) - 1;
	const uint32_t	idx		= start / 32;
	const uint32_t	offset	= start % 32;

	uint32_t value = (CBTBuffer[idx] >> offset) & mask;
	if (((start % 32) + bitWidth) > 32)
	{
		uint32_t bitsRemaining = ((start % 32) + bitWidth) % 32;
		uint32_t remainingBits = CBTBuffer[idx + 1] << (bitWidth - bitsRemaining);

		value |= (remainingBits & mask);
	}

	return value;
}


void WriteValue(RWStructuredBuffer<uint> CBTBuffer, uint32_t start, uint32_t bitWidth, uint32_t value)
{
	const uint32_t	idx		= start / 32;
	const uint32_t	offset	= start % 32;
	const uint32_t	mask	= ~(((uint32_t(0x01) << (bitWidth)) - 1) << offset);

	InterlockedAnd(CBTBuffer[idx], mask);
	InterlockedOr(CBTBuffer[idx], (~mask & (value << offset)));
	
	if (((start % 32) + bitWidth) > 32)
	{
		const uint32_t bitsRemaining	= ((start % 32) + bitWidth) % 32;
		const uint32_t remainingBits	= value >> (bitWidth - bitsRemaining);
		const uint32_t mask				= ~(((uint32_t(1) << (bitWidth)) - 1) >> (bitWidth - bitsRemaining));

		InterlockedAnd(CBTBuffer[idx + 1], mask);
		InterlockedOr(CBTBuffer[idx + 1], ~mask & remainingBits);
	}
}


uint32_t GetHeapValue(RWStructuredBuffer<uint> CBTBuffer, in uint32_t maxDepth, uint32_t heapIdx)
{
	const uint32_t bitIdx	= GetBitOffset(maxDepth, heapIdx);
	const uint32_t bitWidth = maxDepth - FindMSB(heapIdx) + 1;
	return ReadValue(CBTBuffer, bitIdx, bitWidth);
}


uint32_t DecodeNode(RWStructuredBuffer<uint> CBTBuffer, in uint32_t maxDepth, int32_t leafID)
{
	uint32_t heapID = 1;
	uint32_t value	= GetHeapValue(CBTBuffer, maxDepth, heapID);

	for (int i = 0; i < maxDepth; i++)
	{
		if (value > 1)
		{
			uint32_t temp = GetHeapValue(CBTBuffer, maxDepth, 2 * heapID);
			if (leafID < temp)
			{
				heapID *= 2;
				value = temp;
			}
			else
			{
				leafID	= leafID - temp;
				heapID	= 2 * heapID + 1;
				value	= GetHeapValue(CBTBuffer, maxDepth, heapID);
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