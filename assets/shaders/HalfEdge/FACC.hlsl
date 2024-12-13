#define RootSig		"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
					"SRV(t0)," \
					"SRV(t1)," \
					"SRV(t2)," \
					"SRV(t3)," \
					"SRV(t4)," \
					"RootConstants(num32BitConstants = 18, b0)"


#define DEBUGVIEW 0

cbuffer Constants : register(b0)
{
	float4x4 PV;
};

struct InputVertex
{
	float3 XYZ : POSITION;
};

[RootSignature(RootSig)]
InputVertex VMain(InputVertex v)
{
	return v;
}

struct ControlPoint
{
	float3 XYZ : POINT;
};


struct HullConstants
{
	float edges[4]        : SV_TessFactor;
	float inside[2]       : SV_InsideTessFactor;
};
   

HullConstants HMainConstants( 
	InputPatch<InputVertex, 16> ip,
	uint PatchID : SV_PrimitiveID)
{   
	HullConstants constants;
#if DEBUGVIEW
	constants.edges[0] = 4;
	constants.edges[1] = 4;
	constants.edges[2] = 4;
	constants.edges[3] = 4;
			
	constants.inside[0] = 4;
	constants.inside[1] = 4;
#else
	constants.edges[0] = 64;
	constants.edges[1] = 64;
	constants.edges[2] = 64;
	constants.edges[3] = 64;
			
	constants.inside[0] = 64;
	constants.inside[1] = 64;
#endif
	
	return constants;
}
		

/*
Masks
A: Interior Points
2  --  1
|      |
|      |
4  --  2
total: 9

B: Edge Points
2  --  1
|      |
|      |
8  --  4
|      |
|      |
2  --  1
total: 18

C: Corner Points
1  --  4  --  1
|      |      |
|      |      |
4  --  16 --  4
|      |      |
|      |      |
1  --  4  --  1
total: 36

-------------------------------
Control Stencil layout:
 b00 b10 b20 b30 |  0   1  2  3
 b01 b11 b21 b31 |  4   5  6  7 
 b02 b12 b22 b32 |  8   9 10 11
 b03 b13 b23 b33 |  12 13 14 15
-------------------------------


References: 
Charles Loop, Scott Schaefer: Approximating Catmull-Clark Subdivision Surfaces with Bicubic Patches
*/

ControlPoint MaskA(uint idx, InputPatch<InputVertex, 16> ip, bool flipV, bool flipH)
{
	int h_traversalVector = flipH ? -1 : 1;
	int v_traversalVector = flipV ? -4 : 4;
	
	float3 m0 = ip[idx].XYZ / 2.0f + ip[idx + h_traversalVector].XYZ / 2.0f;
	float3 m1 = ip[idx].XYZ / 2.0f + ip[idx + v_traversalVector].XYZ / 2.0f;
	
	float3 f0 =
			ip[5].XYZ / 4.0f +
			ip[6].XYZ / 4.0f +
			ip[9].XYZ / 4.0f +
			ip[10].XYZ / 4.0f;
	
	ControlPoint cp;
	cp.XYZ	= 
			ip[idx].XYZ * 1.0f / 9.0f +
					 m0 * 2.0f / 9.0f +
					 m1 * 2.0f / 9.0f +
					 f0 * 4.0f / 9.0f;
	
	return cp;
}


ControlPoint MaskBTop(uint idx, InputPatch<InputVertex, 16> ip, bool flipH)
{
	const int h_traversal = flipH ? -1 :  1;
	
	const float3 e0 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx -4].XYZ * 1.0f / 2.0f;
	const float3 e1 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + h_traversal].XYZ * 1.0f / 2.0f;
	const float3 e2 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + 4].XYZ * 1.0f / 2.0f;
			
	const float3 f0 =
		ip[idx].XYZ * 1.0f / 4.0f +
		ip[idx + h_traversal].XYZ * 1.0f / 4.0f +
		ip[idx - 4].XYZ * 1.0f / 4.0f +
		ip[idx - 4 + h_traversal].XYZ * 1.0f / 4.0f;
			
	const float3 f1 =
		ip[idx].XYZ * 1.0f / 4.0f +
		ip[6].XYZ	* 1.0f / 4.0f +
		ip[9].XYZ	* 1.0f / 4.0f +
		ip[10].XYZ  * 1.0f / 4.0f;
			
	const float3 V0 = ip[idx].XYZ;
			
	ControlPoint cp;
	cp.XYZ = 
		e0 * 2.0f / 18.0f +
		V0 * 8.0f / 18.0f + 
		e2 * 2.0f / 18.0f +
		
		f0 * 1.0f / 18.0f +
		e1 * 4.0f / 18.0f +
		f1 * 1.0f / 18.0f;
	
	return cp;
}

ControlPoint MaskBBottom(uint idx, InputPatch<InputVertex, 16> ip, bool flipH)
{
	const int h_traversal = flipH ? -1 :  1;
	
	const float3 e0 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx -4].XYZ * 1.0f / 2.0f;
	const float3 e1 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + h_traversal].XYZ * 1.0f / 2.0f;
	const float3 e2 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + 4].XYZ * 1.0f / 2.0f;
			
	const float3 f0 =
		ip[idx].XYZ * 1.0f / 4.0f +
		ip[idx + h_traversal].XYZ * 1.0f / 4.0f +
		ip[idx - 4].XYZ * 1.0f / 4.0f +
		ip[idx - 4 + h_traversal].XYZ * 1.0f / 4.0f;
			
	const float3 f1 =
		ip[idx].XYZ * 1.0f / 4.0f +
		ip[idx + h_traversal].XYZ * 1.0f / 4.0f +
		ip[idx + 4].XYZ * 1.0f / 4.0f +
		ip[idx + h_traversal + 4].XYZ * 1.0f / 4.0f;
			
	const float3 V0 = ip[idx].XYZ;
			
	ControlPoint cp;
	cp.XYZ =
		e0 * 2.0f / 18.0f +
		V0 * 8.0f / 18.0f +
		e2 * 2.0f / 18.0f +
		f0 * 1.0f / 18.0f +
		e1 * 4.0f / 18.0f +
		f1 * 1.0f / 18.0f;
	
	return cp;
}


ControlPoint MaskBLeft(uint idx, InputPatch<InputVertex, 16> ip, bool flipV)
{
	const int v_traversal = flipV ? -4 :  4;
	
	const float3 e0 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + 1].XYZ * 1.0f / 2.0f;
	const float3 e1 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + v_traversal].XYZ * 1.0f / 2.0f;
	const float3 e2 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx - 1].XYZ * 1.0f / 2.0f;
			
	const float3 f0 =
		ip[5].XYZ * 1.0f / 4.0f +
		ip[6].XYZ * 1.0f / 4.0f +
		ip[9].XYZ * 1.0f / 4.0f +
		ip[10].XYZ * 1.0f / 4.0f;
			
	const float3 f1 =
		ip[4].XYZ * 1.0f / 4.0f +
		ip[5].XYZ	* 1.0f / 4.0f +
		ip[8].XYZ	* 1.0f / 4.0f +
		ip[9].XYZ  * 1.0f / 4.0f;
			
	const float3 V0 = ip[idx].XYZ;
			
	ControlPoint cp;
	cp.XYZ =
		e0 * 2.0f / 18.0f +
		V0 * 8.0f / 18.0f +
		e2 * 2.0f / 18.0f +
		f0 * 1.0f / 18.0f +
		e1 * 4.0f / 18.0f +
		f1 * 1.0f / 18.0f;
	
	return cp;
}


ControlPoint MaskBRight(uint idx, InputPatch<InputVertex, 16> ip, bool flipV)
{
	const int v_traversal = flipV ? -4 :  4;
	
	const float3 e0 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + 1].XYZ * 1.0f / 2.0f;
	const float3 e1 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx + v_traversal].XYZ * 1.0f / 2.0f;
	const float3 e2 = ip[idx].XYZ * 1.0f / 2.0f +  ip[idx - 1].XYZ * 1.0f / 2.0f;
			
	const float3 f0 =
		ip[5].XYZ * 1.0f / 4.0f +
		ip[6].XYZ * 1.0f / 4.0f +
		ip[9].XYZ * 1.0f / 4.0f +
		ip[10].XYZ * 1.0f / 4.0f;
			
	const float3 f1 =
		ip[6].XYZ	* 1.0f / 4.0f +
		ip[7].XYZ	* 1.0f / 4.0f +
		ip[10].XYZ	* 1.0f / 4.0f +
		ip[11].XYZ  * 1.0f / 4.0f;
			
	const float3 V0 = ip[idx].XYZ;
			
	ControlPoint cp;
	cp.XYZ =
		e0 * 2.0f / 18.0f +
		V0 * 8.0f / 18.0f +
		e2 * 2.0f / 18.0f +
		f0 * 1.0f / 18.0f +
		e1 * 4.0f / 18.0f +
		f1 * 1.0f / 18.0f;
	
	return cp;
}


ControlPoint MaskC(uint idx, InputPatch<InputVertex, 16> ip)
{
	const float3 f0 = 
		  ip[idx - 5].XYZ / 4.0f
		+ ip[idx - 4].XYZ / 4.0f
		+ ip[idx - 1].XYZ / 4.0f
		+ ip[idx + 0].XYZ / 4.0f;
	
	const float3 f1 =
		  ip[idx - 4].XYZ / 4.0f
		+ ip[idx - 3].XYZ / 4.0f
		+ ip[idx + 0].XYZ / 4.0f
		+ ip[idx + 1].XYZ / 4.0f;
	
	const float3 f2 =
		  ip[idx + 0].XYZ / 4.0f
		+ ip[idx + 1].XYZ / 4.0f
		+ ip[idx + 4].XYZ / 4.0f
		+ ip[idx + 5].XYZ / 4.0f;
	
	const float3 f3 =
		  ip[idx + 0].XYZ / 4.0f
		+ ip[idx - 1].XYZ / 4.0f
		+ ip[idx + 3].XYZ / 4.0f
		+ ip[idx + 4].XYZ / 4.0f;
	
	const float3 m0 =
		  ip[idx].XYZ		/ 2.0f
		+ ip[idx - 4].XYZ	/ 2.0f;
	
	const float3 m1 =
		  ip[idx].XYZ		/ 2.0f
		+ ip[idx + 1].XYZ	/ 2.0f;
	
	const float3 m2 =
		  ip[idx].XYZ		/ 2.0f
		+ ip[idx + 4].XYZ	/ 2.0f;
	
	const float3 m3 =
		  ip[idx].XYZ		/ 2.0f
		+ ip[idx - 1].XYZ	/ 2.0f;
	
	ControlPoint cp;
	cp.XYZ = 
		ip[idx].XYZ * 1.0f / 9.0f
		+ f0		* 1.0f / 9.0f
		+ f1		* 1.0f / 9.0f
		+ f2		* 1.0f / 9.0f
		+ f3		* 1.0f / 9.0f
		+ m0		* 1.0f / 9.0f
		+ m1		* 1.0f / 9.0f
		+ m2		* 1.0f / 9.0f
		+ m3		* 1.0f / 9.0f;
	
	return cp;
}


[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(16)]
[patchconstantfunc("HMainConstants")]
ControlPoint HMain(
	InputPatch<InputVertex, 16> ip,
	uint                        i       : SV_OutputControlPointID,
	uint                        PatchID : SV_PrimitiveID)
{
	ControlPoint cp;
	cp.XYZ = float3(0, 0, 0);
#if 0
	cp.XYZ = ip[i].XYZ;
	return cp;
#endif
	
	switch (i)
	{
		// Edge Points
		case 1:		cp = MaskBTop(5, ip, false);	break;
		case 2:		cp = MaskBTop(6, ip, true);		break;
		case 4:		cp = MaskBLeft(5, ip, false);	break;
		case 8:		cp = MaskBLeft(9, ip, true);	break;
		case 7:		cp = MaskBRight(6, ip, false);	break;
		case 11:	cp = MaskBRight(10, ip, true);	break;
		case 13:	cp = MaskBBottom(9, ip, false);	break;
		case 14:	cp = MaskBBottom(10, ip, true);	break;
		// Corner Points
		case 0:
		case 3:
		case 12:
		case 15:
		cp = MaskC(5 + i / 3, ip);
		break;
		// Interior points
		case 5:		cp = MaskA(i, ip, false, false);	break;
		case 6:		cp = MaskA(i, ip, false, true);		break;
		case 9:		cp = MaskA(i, ip, true, false);		break;
		case 10:	cp = MaskA(i, ip, true, true);		break;
	}
	
	return cp;
}

struct DS_OUTPUT
{
	float2 UV		 : TEXCOORD;
	float4 position  : SV_POSITION;
};
		
[domain("quad")]
DS_OUTPUT DMain(HullConstants input,
						float2 UV : SV_DomainLocation,
						const OutputPatch<ControlPoint, 16> bezpatch)
{
#if DEBUGVIEW
	uint X = UV.x * 3;
	uint Y = UV.y * 3;
	float3 v4 = bezpatch[X + Y * 4].XYZ;
#else	
	float3 v0 = lerp(
		lerp(bezpatch[0].XYZ, bezpatch[1].XYZ, UV.x),
		lerp(bezpatch[2].XYZ, bezpatch[3].XYZ, UV.x), UV.x);
	
	float3 v1 = lerp(
		lerp(bezpatch[4].XYZ, bezpatch[5].XYZ, UV.x),
		lerp(bezpatch[6].XYZ, bezpatch[7].XYZ, UV.x), UV.x);
	
	float3 v2 = lerp(
		lerp(bezpatch[8].XYZ, bezpatch[9].XYZ, UV.x),
		lerp(bezpatch[10].XYZ, bezpatch[11].XYZ, UV.x), UV.x);
	
	float3 v3 = lerp(
		lerp(bezpatch[12].XYZ, bezpatch[13].XYZ, UV.x),
		lerp(bezpatch[14].XYZ, bezpatch[15].XYZ, UV.x), UV.x);
	
	
	float3 v4 = 
		lerp(
			lerp(v0, v2, UV.y),
			lerp(v1, v3, UV.y), UV.y);
#endif
	
	DS_OUTPUT Output;
	float4 position = float4(v4, 1.0f);

	Output.position = position;
	Output.position = mul(PV, position);
	Output.UV		= UV;
	return Output;    
}


float4 PMain(float2 UV : TEXCOORD) : SV_Target
{
	return float4(UV, 1, 1);
}