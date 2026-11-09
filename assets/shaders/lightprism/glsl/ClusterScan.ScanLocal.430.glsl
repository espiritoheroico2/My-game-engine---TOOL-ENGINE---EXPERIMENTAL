#version 430
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

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

layout(binding = 0, std140) uniform ClusterGridCB
{
    ClusterGrid Grid;
} _93;

layout(binding = 0, std430) buffer ClusterCounts
{
    uint _data[];
} ClusterCounts_1;

layout(binding = 1, std430) buffer GroupSums
{
    uint _data[];
} GroupSums_1;

shared uint gsScan[1024];

void main()
{
    uint _285 = (_93.Grid.gridX * _93.Grid.gridY) * _93.Grid.gridZ;
    uint _251 = ClusterCounts_1._data[gl_GlobalInvocationID.x];
    uint _252 = (gl_GlobalInvocationID.x < _285) ? _251 : 0u;
    gsScan[gl_LocalInvocationID.x] = _252;
    barrier();
    for (uint _339 = 1u; _339 < 256u; )
    {
        bool _301 = gl_LocalInvocationID.x >= _339;
        uint _304 = gl_LocalInvocationID.x - _339;
        uint _306 = gsScan[_304];
        barrier();
        gsScan[gl_LocalInvocationID.x] += (_301 ? _306 : 0u);
        barrier();
        _339 = _339 << uint(1);
        continue;
    }
    if (gl_GlobalInvocationID.x < _285)
    {
        ClusterCounts_1._data[gl_GlobalInvocationID.x] = gsScan[gl_LocalInvocationID.x] - _252;
    }
    if (gl_LocalInvocationID.x == 255u)
    {
        GroupSums_1._data[gl_WorkGroupID.x] = gsScan[gl_LocalInvocationID.x];
    }
}

