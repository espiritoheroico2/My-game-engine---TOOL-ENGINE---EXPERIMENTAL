#version 430
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct ClusterGrid
{
    uint gridX;
    uint gridY;
    uint gridZ;
    uint tileSizePx;
    float zNear;
    float zFar;
    float sliceScale;
    float sliceBias;
    vec2 projScale;
    vec2 screenSize;
    uint numPointLights;
    uint numSpotLights;
    uint maxLightsPerCluster;
    uint maxIndices;
};

struct ClusterLight
{
    vec3 boundCenter;
    float boundRadius;
    vec3 apex;
    float range;
    vec3 dir;
    float cosOuter;
    float sinOuter;
    uint kind;
    int sliceMin;
    int sliceMax;
    ivec4 tileRange;
};

layout(binding = 0, std140) uniform ClusterGridCB
{
    ClusterGrid Grid;
} _471;

layout(binding = 0, std430) readonly buffer Lights
{
    ClusterLight _data[];
} Lights_1;

layout(binding = 0, std430) buffer ClusterCounts
{
    uint _data[];
} ClusterCounts_1;

shared ClusterLight gs_Lights[64];

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    uint lidx = gl_LocalInvocationID.x;
    uint clusterCount = (_471.Grid.gridX * _471.Grid.gridY) * _471.Grid.gridZ;
    bool activeCluster = idx < clusterCount;

    uvec3 c = uvec3(0u);
    vec3 aabbMin = vec3(0.0);
    vec3 aabbMax = vec3(0.0);
    float aabbRadius = 0.0;

    if (activeCluster)
    {
        uint xy = _471.Grid.gridX * _471.Grid.gridY;
        uint z = idx / xy;
        uint r = idx - (z * xy);
        uint y = r / _471.Grid.gridX;
        c = uvec3(r - (y * _471.Grid.gridX), y, z);

        vec2 invScreen = vec2(2.0) / _471.Grid.screenSize;
        vec2 invProj = vec2(1.0) / _471.Grid.projScale;
        vec2 p0 = vec2(c.xy * uvec2(_471.Grid.tileSizePx));
        vec2 p1 = vec2((c.xy + uvec2(1u)) * uvec2(_471.Grid.tileSizePx));

        vec2 n0 = vec2((p0.x * invScreen.x) - 1.0, 1.0 - (p0.y * invScreen.y)) * invProj;
        vec2 n1 = vec2((p1.x * invScreen.x) - 1.0, 1.0 - (p1.y * invScreen.y)) * invProj;

        float dn = exp2((float(z) - _471.Grid.sliceBias) / _471.Grid.sliceScale);
        float df = exp2((float(z + 1u) - _471.Grid.sliceBias) / _471.Grid.sliceScale);

        vec2 xy0n = n0 * dn;
        vec2 xy0f = n0 * df;
        vec2 xy1n = n1 * dn;
        vec2 xy1f = n1 * df;

        aabbMin = vec3(min(min(xy0n, xy0f), min(xy1n, xy1f)), -df);
        aabbMax = vec3(max(max(xy0n, xy0f), max(xy1n, xy1f)), -dn);
        aabbRadius = length(aabbMax - aabbMin) * 0.5;
    }

    uint total = _471.Grid.numPointLights + _471.Grid.numSpotLights;
    uint count = 0u;
    ivec3 ci = ivec3(c);

    for (uint base = 0u; base < total; base += 64u)
    {
        uint loadIdx = base + lidx;
        if (loadIdx < total)
        {
            gs_Lights[lidx] = Lights_1._data[loadIdx];
        }
        barrier();

        if (activeCluster && (count < _471.Grid.maxLightsPerCluster))
        {
            uint batchLen = min(64u, total - base);
            for (uint j = 0u; (j < batchLen) && (count < _471.Grid.maxLightsPerCluster); ++j)
            {
                ClusterLight L = gs_Lights[j];
                if ((ci.z < L.sliceMin) || (ci.z > L.sliceMax) ||
                    (ci.x < L.tileRange.x) || (ci.x > L.tileRange.z) ||
                    (ci.y < L.tileRange.y) || (ci.y > L.tileRange.w))
                {
                    continue;
                }

                vec3 d = max(aabbMin, min(L.boundCenter, aabbMax)) - L.boundCenter;
                if (dot(d, d) > (L.boundRadius * L.boundRadius))
                {
                    continue;
                }

                if (L.kind == 0u)
                {
                    ++count;
                    continue;
                }

                vec3 center = (aabbMin + aabbMax) * 0.5;
                vec3 v = center - L.apex;
                float vlenSq = dot(v, v);
                float v1 = dot(v, L.dir);
                if ((v1 > (aabbRadius + L.range)) || (v1 < (-aabbRadius)))
                {
                    continue;
                }
                float distClosest = (L.cosOuter * sqrt(max(vlenSq - (v1 * v1), 0.0))) - (v1 * L.sinOuter);
                if (distClosest <= aabbRadius)
                {
                    ++count;
                }
            }
        }
        barrier();
    }

    if (activeCluster)
    {
        ClusterCounts_1._data[idx] = count;
    }
}