struct Plane
{
	float4 n;
	float4 o;
};

struct Frustum
{
	Plane planes[6];
};

struct AABB
{
	float3 mMin;
	float3 mMax;
	
	void Add(in float3 xyz)
	{
		mMin.x = min(mMin.x, xyz.x);
		mMin.y = min(mMin.y, xyz.y);
		mMin.z = min(mMin.z, xyz.z);
		mMax.x = max(mMax.x, xyz.x);
		mMax.y = max(mMax.y, xyz.y);
		mMax.z = max(mMax.z, xyz.z);
	}
};

bool Intersects(const in Frustum frustum, const in AABB aabb)
{
	int Result = 1;

	for (int I = 0; I < 6; ++I)
	{
		float px = (frustum.planes[I].n.x >= 0.0f) ? aabb.mMin.x : aabb.mMax.x;
		float py = (frustum.planes[I].n.y >= 0.0f) ? aabb.mMin.y : aabb.mMax.y;
		float pz = (frustum.planes[I].n.z >= 0.0f) ? aabb.mMin.z : aabb.mMax.z;

		float3 pV = float3(px, py, pz) - frustum.planes[I].o;
		float dP = dot(frustum.planes[I].n, pV);

		if (dP >= 0)
			return false;
	}

	return true;
}