#version 430
#if defined(GL_EXT_control_flow_attributes)
#extension GL_EXT_control_flow_attributes : require
#define SPIRV_CROSS_FLATTEN [[flatten]]
#define SPIRV_CROSS_BRANCH [[dont_flatten]]
#define SPIRV_CROSS_UNROLL [[unroll]]
#define SPIRV_CROSS_LOOP [[dont_unroll]]
#else
#define SPIRV_CROSS_FLATTEN
#define SPIRV_CROSS_BRANCH
#define SPIRV_CROSS_UNROLL
#define SPIRV_CROSS_LOOP
#endif
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

struct ClusterCell
{
    uint offset;
    uint count;
};

layout(binding = 0, std140) uniform ClusterGridCB
{
    ClusterGrid Grid;
} _471;

layout(binding = 2, std430) buffer ClusterCounts
{
    uint _data[];
} ClusterCounts_1;

layout(binding = 3, std430) buffer GroupSums
{
    uint _data[];
} GroupSums_1;

layout(binding = 0, std430) readonly buffer Lights
{
    ClusterLight _data[];
} Lights_1;

layout(binding = 0, std430) buffer LightIndexList
{
    uint _data[];
} LightIndexList_1;

layout(binding = 1, std430) buffer LightGrid
{
    ClusterCell _data[];
} LightGrid_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= ((_471.Grid.gridX * _471.Grid.gridY) * _471.Grid.gridZ))
        {
            break;
        }
        uint _792 = ClusterCounts_1._data[gl_GlobalInvocationID.x];
        uint _796 = GroupSums_1._data[gl_GlobalInvocationID.x / 256u];
        uint _797 = _792 + _796;
        uint _974 = _471.Grid.gridX * _471.Grid.gridY;
        uint _977 = gl_GlobalInvocationID.x / _974;
        uint _982 = gl_GlobalInvocationID.x - (_977 * _974);
        uint _986 = _982 / _471.Grid.gridX;
        uvec3 _995 = uvec3(_982 - (_986 * _471.Grid.gridX), _986, _977);
        vec2 _1021 = vec2(_995.xy * uvec2(_471.Grid.tileSizePx));
        vec2 _1030 = vec2((_995.xy + uvec2(1u)) * uvec2(_471.Grid.tileSizePx));
        float _1037 = ((_1021.x / _471.Grid.screenSize.x) * 2.0) - 1.0;
        float _1044 = 1.0 - ((_1021.y / _471.Grid.screenSize.y) * 2.0);
        float _1052 = ((_1030.x / _471.Grid.screenSize.x) * 2.0) - 1.0;
        float _1059 = 1.0 - ((_1030.y / _471.Grid.screenSize.y) * 2.0);
        float _1148 = exp2((float(_977) - _471.Grid.sliceBias) / _471.Grid.sliceScale);
        float _1159 = exp2((float(_977 + 1u) - _471.Grid.sliceBias) / _471.Grid.sliceScale);
        vec3 _1561;
        vec3 _1562;
        _1562 = vec3(-1000000015047466219876688855040.0);
        _1561 = vec3(1000000015047466219876688855040.0);
        SPIRV_CROSS_UNROLL
        for (int _1560 = 0; _1560 < 8; )
        {
            float _1097 = ((_1560 & 4) != int(0u)) ? _1159 : _1148;
            vec3 _1114 = vec3(((((_1560 & 1) != int(0u)) ? _1052 : _1037) * _1097) / _471.Grid.projScale.x, ((((_1560 & 2) != int(0u)) ? _1059 : _1044) * _1097) / _471.Grid.projScale.y, -_1097);
            _1562 = max(_1562, _1114);
            _1561 = min(_1561, _1114);
            _1560++;
            continue;
        }
        vec4 _1129 = vec4(_1561, 0.0);
        vec4 _1135 = vec4(_1562, 0.0);
        uint _866 = _471.Grid.numPointLights + _471.Grid.numSpotLights;
        uint _1564;
        _1564 = 0u;
        uint _1572;
        for (uint _1563 = 0u; (_1563 < _866) && (_1564 < _471.Grid.maxLightsPerCluster); _1564 = _1572, _1563++)
        {
            ivec3 _1164 = ivec3(_995);
            if (!((((((_1164.z >= Lights_1._data[_1563].sliceMin) && (_1164.z <= Lights_1._data[_1563].sliceMax)) && (_1164.x >= Lights_1._data[_1563].tileRange.x)) && (_1164.x <= Lights_1._data[_1563].tileRange.z)) && (_1164.y >= Lights_1._data[_1563].tileRange.y)) && (_1164.y <= Lights_1._data[_1563].tileRange.w)))
            {
                _1572 = _1564;
                continue;
            }
            bool _1565;
            do
            {
                vec3 _1267 = max(_1129.xyz, min(Lights_1._data[_1563].boundCenter, _1135.xyz)) - Lights_1._data[_1563].boundCenter;
                if (!(dot(_1267, _1267) <= (Lights_1._data[_1563].boundRadius * Lights_1._data[_1563].boundRadius)))
                {
                    _1565 = false;
                    break;
                }
                if (Lights_1._data[_1563].kind == 0u)
                {
                    _1565 = true;
                    break;
                }
                float _1248 = length(_1135.xyz - _1129.xyz);
                float _1249 = _1248 * 0.5;
                vec3 _1287 = ((_1129.xyz + _1135.xyz) * 0.5) - Lights_1._data[_1563].apex;
                float _1294 = dot(_1287, Lights_1._data[_1563].dir);
                _1565 = !(((((Lights_1._data[_1563].cosOuter * sqrt(max(dot(_1287, _1287) - (_1294 * _1294), 0.0))) - (_1294 * Lights_1._data[_1563].sinOuter)) > _1249) || (_1294 > (_1249 + Lights_1._data[_1563].range))) || (_1294 < (_1248 * (-0.5))));
                break;
            } while(false);
            uint _1573;
            if (_1565)
            {
                uint _915 = _797 + _1564;
                if (_915 < _471.Grid.maxIndices)
                {
                    LightIndexList_1._data[_915] = _1563;
                }
                _1573 = _1564 + uint(1);
            }
            else
            {
                _1573 = _1564;
            }
            _1572 = _1573;
        }
        LightGrid_1._data[gl_GlobalInvocationID.x].offset = _797;
        LightGrid_1._data[gl_GlobalInvocationID.x].count = (_797 >= _471.Grid.maxIndices) ? 0u : min(_1564, (_471.Grid.maxIndices - _797));
        break;
    } while(false);
}

