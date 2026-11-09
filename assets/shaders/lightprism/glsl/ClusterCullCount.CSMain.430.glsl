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

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= ((_471.Grid.gridX * _471.Grid.gridY) * _471.Grid.gridZ))
        {
            break;
        }
        uint _881 = _471.Grid.gridX * _471.Grid.gridY;
        uint _884 = gl_GlobalInvocationID.x / _881;
        uint _889 = gl_GlobalInvocationID.x - (_884 * _881);
        uint _893 = _889 / _471.Grid.gridX;
        uvec3 _902 = uvec3(_889 - (_893 * _471.Grid.gridX), _893, _884);
        vec2 _928 = vec2(_902.xy * uvec2(_471.Grid.tileSizePx));
        vec2 _937 = vec2((_902.xy + uvec2(1u)) * uvec2(_471.Grid.tileSizePx));
        float _944 = ((_928.x / _471.Grid.screenSize.x) * 2.0) - 1.0;
        float _951 = 1.0 - ((_928.y / _471.Grid.screenSize.y) * 2.0);
        float _959 = ((_937.x / _471.Grid.screenSize.x) * 2.0) - 1.0;
        float _966 = 1.0 - ((_937.y / _471.Grid.screenSize.y) * 2.0);
        float _1055 = exp2((float(_884) - _471.Grid.sliceBias) / _471.Grid.sliceScale);
        float _1066 = exp2((float(_884 + 1u) - _471.Grid.sliceBias) / _471.Grid.sliceScale);
        vec3 _1463;
        vec3 _1464;
        _1464 = vec3(-1000000015047466219876688855040.0);
        _1463 = vec3(1000000015047466219876688855040.0);
        SPIRV_CROSS_UNROLL
        for (int _1462 = 0; _1462 < 8; )
        {
            float _1004 = ((_1462 & 4) != int(0u)) ? _1066 : _1055;
            vec3 _1021 = vec3(((((_1462 & 1) != int(0u)) ? _959 : _944) * _1004) / _471.Grid.projScale.x, ((((_1462 & 2) != int(0u)) ? _966 : _951) * _1004) / _471.Grid.projScale.y, -_1004);
            _1464 = max(_1464, _1021);
            _1463 = min(_1463, _1021);
            _1462++;
            continue;
        }
        vec4 _1036 = vec4(_1463, 0.0);
        vec4 _1042 = vec4(_1464, 0.0);
        uint _803 = _471.Grid.numPointLights + _471.Grid.numSpotLights;
        uint _1466;
        _1466 = 0u;
        uint _1472;
        for (uint _1465 = 0u; (_1465 < _803) && (_1466 < _471.Grid.maxLightsPerCluster); _1466 = _1472, _1465++)
        {
            ivec3 _1071 = ivec3(_902);
            if (!((((((_1071.z >= Lights_1._data[_1465].sliceMin) && (_1071.z <= Lights_1._data[_1465].sliceMax)) && (_1071.x >= Lights_1._data[_1465].tileRange.x)) && (_1071.x <= Lights_1._data[_1465].tileRange.z)) && (_1071.y >= Lights_1._data[_1465].tileRange.y)) && (_1071.y <= Lights_1._data[_1465].tileRange.w)))
            {
                _1472 = _1466;
                continue;
            }
            bool _1467;
            do
            {
                vec3 _1174 = max(_1036.xyz, min(Lights_1._data[_1465].boundCenter, _1042.xyz)) - Lights_1._data[_1465].boundCenter;
                if (!(dot(_1174, _1174) <= (Lights_1._data[_1465].boundRadius * Lights_1._data[_1465].boundRadius)))
                {
                    _1467 = false;
                    break;
                }
                if (Lights_1._data[_1465].kind == 0u)
                {
                    _1467 = true;
                    break;
                }
                float _1155 = length(_1042.xyz - _1036.xyz);
                float _1156 = _1155 * 0.5;
                vec3 _1194 = ((_1036.xyz + _1042.xyz) * 0.5) - Lights_1._data[_1465].apex;
                float _1201 = dot(_1194, Lights_1._data[_1465].dir);
                _1467 = !(((((Lights_1._data[_1465].cosOuter * sqrt(max(dot(_1194, _1194) - (_1201 * _1201), 0.0))) - (_1201 * Lights_1._data[_1465].sinOuter)) > _1156) || (_1201 > (_1156 + Lights_1._data[_1465].range))) || (_1201 < (_1155 * (-0.5))));
                break;
            } while(false);
            uint _1473;
            if (_1467)
            {
                _1473 = _1466 + uint(1);
            }
            else
            {
                _1473 = _1466;
            }
            _1472 = _1473;
        }
        ClusterCounts_1._data[gl_GlobalInvocationID.x] = _1466;
        break;
    } while(false);
}

