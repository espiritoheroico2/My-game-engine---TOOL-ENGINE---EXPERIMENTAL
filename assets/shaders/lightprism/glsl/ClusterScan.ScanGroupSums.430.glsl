#version 430
layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

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
} _94;

layout(binding = 1, std430) buffer GroupSums
{
    uint _data[];
} GroupSums_1;

layout(binding = 2, std430) buffer Stats
{
    uint _data[];
} Stats_1;

shared uint gsScan[1024];

void main()
{
    uint _283 = (((_94.Grid.gridX * _94.Grid.gridY) * _94.Grid.gridZ) + 255u) / 256u;
    uint _287 = (_283 + 1023u) / 1024u;
    uint _290 = gl_LocalInvocationID.x * _287;
    uint _295 = min((_290 + _287), _283);
    uint _406;
    _406 = 0u;
    for (uint _405 = _290; _405 < _295; )
    {
        _406 += GroupSums_1._data[_405];
        _405++;
        continue;
    }
    gsScan[gl_LocalInvocationID.x] = _406;
    barrier();
    for (uint _407 = 1u; _407 < 1024u; )
    {
        bool _369 = gl_LocalInvocationID.x >= _407;
        uint _372 = gl_LocalInvocationID.x - _407;
        uint _374 = gsScan[_372];
        barrier();
        gsScan[gl_LocalInvocationID.x] += (_369 ? _374 : 0u);
        barrier();
        _407 = _407 << uint(1);
        continue;
    }
    uint _317 = gsScan[gl_LocalInvocationID.x] - _406;
    for (uint _409 = _290, _410 = _317; _409 < _295; )
    {
        uint _327 = GroupSums_1._data[_409];
        GroupSums_1._data[_409] = _410;
        _410 += _327;
        _409++;
        continue;
    }
    if (gl_LocalInvocationID.x == 1023u)
    {
        Stats_1._data[0] = gsScan[gl_LocalInvocationID.x];
    }
}

