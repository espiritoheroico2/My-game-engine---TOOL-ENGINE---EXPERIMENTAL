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

struct PointLight
{
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
    uint flags;
    uint layerMask;
    uint shadowIndex;
    float cullRadius;
};

struct SpotLight
{
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
    vec3 direction;
    float spotScale;
    float spotOffset;
    uint flags;
    uint layerMask;
    uint shadowIndex;
};

layout(binding = 0, std140) uniform ClusterGridCB
{
    ClusterGrid Grid;
} _531;

layout(binding = 0, std430) buffer OutClusterLights
{
    ClusterLight _data[];
} OutClusterLights_1;

layout(binding = 0, std430) readonly buffer InPointLights
{
    PointLight _data[];
} InPointLights_1;

layout(binding = 1, std140) uniform ViewCB
{
    mat4 ViewMatrix;
} _565;

layout(binding = 1, std430) readonly buffer InSpotLights
{
    SpotLight _data[];
} InSpotLights_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= (_531.Grid.numPointLights + _531.Grid.numSpotLights))
        {
            break;
        }
        if (gl_GlobalInvocationID.x < _531.Grid.numPointLights)
        {
            PointLight _793 = InPointLights_1._data[gl_GlobalInvocationID.x];
            vec4 _968 = vec4(_793.position, 1.0) * _565.ViewMatrix;
            vec3 _969 = _968.xyz;
            float _983 = ((_793.cullRadius > 0.0) && (_793.cullRadius < _793.radius)) ? _793.cullRadius : _793.radius;
            ivec4 _2512;
            int _2513;
            int _2514;
            do
            {
                float _1031 = (-_968.z) - _983;
                float _1036 = (-_968.z) + _983;
                if ((_1036 < _531.Grid.zNear) || (_1031 > _531.Grid.zFar))
                {
                    _2514 = 1;
                    _2513 = 0;
                    _2512 = ivec4(1, 1, 0, 0);
                    break;
                }
                int _1057 = int(uint(clamp(floor((log2(max(max(_1031, _531.Grid.zNear), _531.Grid.zNear)) * _531.Grid.sliceScale) + _531.Grid.sliceBias), 0.0, float(_531.Grid.gridZ - 1u))));
                int _1062 = int(uint(clamp(floor((log2(max(_1036, _531.Grid.zNear)) * _531.Grid.sliceScale) + _531.Grid.sliceBias), 0.0, float(_531.Grid.gridZ - 1u))));
                if (_1031 <= _531.Grid.zNear)
                {
                    _2514 = _1057;
                    _2513 = _1062;
                    _2512 = ivec4(0, 0, int(_531.Grid.gridX) - 1, int(_531.Grid.gridY) - 1);
                    break;
                }
                vec4 _1102 = vec4(_968.x - _983, _968.x - _983, _968.x + _983, _968.x + _983) / vec4(_1031, _1036, _1031, _1036);
                vec4 _1125 = vec4(_968.y - _983, _968.y - _983, _968.y + _983, _968.y + _983) / vec4(_1031, _1036, _1031, _1036);
                vec2 _1151 = vec2(min(min(_1102.x, _1102.y), min(_1102.z, _1102.w)), min(min(_1125.x, _1125.y), min(_1125.z, _1125.w))) * _531.Grid.projScale;
                vec2 _1177 = vec2(max(max(_1102.x, _1102.y), max(_1102.z, _1102.w)), max(max(_1125.x, _1125.y), max(_1125.z, _1125.w))) * _531.Grid.projScale;
                float _1185 = (((_1151.x * 0.5) + 0.5) * _531.Grid.screenSize.x) - 0.5;
                float _1193 = (((_1177.x * 0.5) + 0.5) * _531.Grid.screenSize.x) + 0.5;
                float _1201 = ((0.5 - (_1177.y * 0.5)) * _531.Grid.screenSize.y) - 0.5;
                float _1209 = ((0.5 - (_1151.y * 0.5)) * _531.Grid.screenSize.y) + 0.5;
                if ((((_1193 < 0.0) || (_1185 >= _531.Grid.screenSize.x)) || (_1209 < 0.0)) || (_1201 >= _531.Grid.screenSize.y))
                {
                    _2514 = 1;
                    _2513 = 0;
                    _2512 = ivec4(1, 1, 0, 0);
                    break;
                }
                float _1232 = float(_531.Grid.tileSizePx);
                float _1236 = float(_531.Grid.gridX - 1u);
                float _1240 = float(_531.Grid.gridY - 1u);
                _2514 = _1057;
                _2513 = _1062;
                _2512 = ivec4(int(clamp(floor(_1185 / _1232), 0.0, _1236)), int(clamp(floor(_1201 / _1232), 0.0, _1240)), int(clamp(floor(_1193 / _1232), 0.0, _1236)), int(clamp(floor(_1209 / _1232), 0.0, _1240)));
                break;
            } while(false);
            OutClusterLights_1._data[gl_GlobalInvocationID.x].boundCenter = _969;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].boundRadius = _983;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].apex = _969;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].range = _983;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].dir = vec3(0.0, 0.0, -1.0);
            OutClusterLights_1._data[gl_GlobalInvocationID.x].cosOuter = -1.0;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].sinOuter = 0.0;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].kind = 0u;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].sliceMin = _2514;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].sliceMax = _2513;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].tileRange = _2512;
        }
        else
        {
            uint _871 = gl_GlobalInvocationID.x - _531.Grid.numPointLights;
            SpotLight _873 = InSpotLights_1._data[_871];
            vec3 _1340 = (vec4(_873.position, 1.0) * _565.ViewMatrix).xyz;
            vec3 _1356 = normalize(_873.direction * mat3(_565.ViewMatrix[0].xyz, _565.ViewMatrix[1].xyz, _565.ViewMatrix[2].xyz));
            float _1364 = clamp((-_873.spotOffset) / _873.spotScale, 0.0, 1.0);
            vec3 _2490;
            float _2491;
            if (_1364 > 0.5)
            {
                float _1385 = _873.radius / (2.0 * _1364);
                _2491 = _1385;
                _2490 = _1340 + (_1356 * _1385);
            }
            else
            {
                _2491 = _873.radius;
                _2490 = _1340;
            }
            ivec4 _2501;
            int _2502;
            int _2503;
            do
            {
                float _1439 = (-_2490.z) - _2491;
                float _1444 = (-_2490.z) + _2491;
                if ((_1444 < _531.Grid.zNear) || (_1439 > _531.Grid.zFar))
                {
                    _2503 = 1;
                    _2502 = 0;
                    _2501 = ivec4(1, 1, 0, 0);
                    break;
                }
                int _1465 = int(uint(clamp(floor((log2(max(max(_1439, _531.Grid.zNear), _531.Grid.zNear)) * _531.Grid.sliceScale) + _531.Grid.sliceBias), 0.0, float(_531.Grid.gridZ - 1u))));
                int _1470 = int(uint(clamp(floor((log2(max(_1444, _531.Grid.zNear)) * _531.Grid.sliceScale) + _531.Grid.sliceBias), 0.0, float(_531.Grid.gridZ - 1u))));
                if (_1439 <= _531.Grid.zNear)
                {
                    _2503 = _1465;
                    _2502 = _1470;
                    _2501 = ivec4(0, 0, int(_531.Grid.gridX) - 1, int(_531.Grid.gridY) - 1);
                    break;
                }
                vec4 _1510 = vec4(_2490.x - _2491, _2490.x - _2491, _2490.x + _2491, _2490.x + _2491) / vec4(_1439, _1444, _1439, _1444);
                vec4 _1533 = vec4(_2490.y - _2491, _2490.y - _2491, _2490.y + _2491, _2490.y + _2491) / vec4(_1439, _1444, _1439, _1444);
                vec2 _1559 = vec2(min(min(_1510.x, _1510.y), min(_1510.z, _1510.w)), min(min(_1533.x, _1533.y), min(_1533.z, _1533.w))) * _531.Grid.projScale;
                vec2 _1585 = vec2(max(max(_1510.x, _1510.y), max(_1510.z, _1510.w)), max(max(_1533.x, _1533.y), max(_1533.z, _1533.w))) * _531.Grid.projScale;
                float _1593 = (((_1559.x * 0.5) + 0.5) * _531.Grid.screenSize.x) - 0.5;
                float _1601 = (((_1585.x * 0.5) + 0.5) * _531.Grid.screenSize.x) + 0.5;
                float _1609 = ((0.5 - (_1585.y * 0.5)) * _531.Grid.screenSize.y) - 0.5;
                float _1617 = ((0.5 - (_1559.y * 0.5)) * _531.Grid.screenSize.y) + 0.5;
                if ((((_1601 < 0.0) || (_1593 >= _531.Grid.screenSize.x)) || (_1617 < 0.0)) || (_1609 >= _531.Grid.screenSize.y))
                {
                    _2503 = 1;
                    _2502 = 0;
                    _2501 = ivec4(1, 1, 0, 0);
                    break;
                }
                float _1640 = float(_531.Grid.tileSizePx);
                float _1644 = float(_531.Grid.gridX - 1u);
                float _1648 = float(_531.Grid.gridY - 1u);
                _2503 = _1465;
                _2502 = _1470;
                _2501 = ivec4(int(clamp(floor(_1593 / _1640), 0.0, _1644)), int(clamp(floor(_1609 / _1640), 0.0, _1648)), int(clamp(floor(_1601 / _1640), 0.0, _1644)), int(clamp(floor(_1617 / _1640), 0.0, _1648)));
                break;
            } while(false);
            OutClusterLights_1._data[gl_GlobalInvocationID.x].boundCenter = _2490;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].boundRadius = _2491;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].apex = _1340;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].range = _873.radius;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].dir = _1356;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].cosOuter = _1364;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].sinOuter = sqrt(max(1.0 - (_1364 * _1364), 0.0));
            OutClusterLights_1._data[gl_GlobalInvocationID.x].kind = 1u;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].sliceMin = _2503;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].sliceMax = _2502;
            OutClusterLights_1._data[gl_GlobalInvocationID.x].tileRange = _2501;
        }
        break;
    } while(false);
}

