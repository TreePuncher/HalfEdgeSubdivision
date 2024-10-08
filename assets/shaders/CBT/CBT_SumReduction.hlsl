#include "CBT.hlsl"

#define SumReductionRS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
			"UAV(u0),"\
			"RootConstants(num32BitConstants=6, b0)"

cbuffer constants : register(b)
{
	uint32_t maxDepth;
	uint32_t i;
	uint32_t stepSize;
	uint32_t end;
	uint32_t start_IN;
	uint32_t start_OUT;
}

RWStructuredBuffer<uint32_t> CBTBuffer : register(u0);

[RootSignature(SumReductionRS)]
[NumThreads(64, 1, 1)]
void SumReduction(const uint j : SV_DispatchThreadID)
{
	if (j >= end)
		return;
	
	const uint32_t a = ReadValue(CBTBuffer, start_IN + (2 * j + 0) * stepSize, i + 1);
	const uint32_t b = ReadValue(CBTBuffer, start_IN + (2 * j + 1) * stepSize, i + 1);
	const uint32_t c = a + b;

	WriteValue(CBTBuffer, start_OUT + j * (stepSize + 1), i + 2, c);
}