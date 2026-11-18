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

struct CsmCascade
{
    mat4 viewProj;
    float splitNear;
    float splitFar;
    float texelWorldSize;
    float depthRange;
};

struct CsmData
{
    CsmCascade cascades[4];
    uint cascadeCount;
    float shadowMapSize;
    float depthBias;
    float normalBias;
    float blendFraction;
    uint pcfTaps;
    vec2 _pad;
};

struct PointShadowLight
{
    vec4 posType;
    vec4 params;
    mat4 vp[6];
};

struct GlobalLighting
{
    uint numPointLights;
    uint numSpotLights;
    uint hasSun;
    uint qualityFlags;
    vec3 cameraPosition;
    float preExposure;
};

struct DirectionalLight
{
    vec3 direction;
    float intensity;
    vec3 color;
    uint flags;
};

struct ClusterCell
{
    uint offset;
    uint count;
};

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

struct IblData
{
    vec4 sh[9];
    float intensity;
    float specularMaxMip;
    uint flags;
    float _pad;
};

float _5957;

layout(binding = 2, std140) uniform CsmCB
{
    CsmData Csm;
} _662;

layout(binding = 7, std140) uniform PointShadowCB
{
    uint pointShadowCount;
    uint _pointShadowPad0;
    uint _pointShadowPad1;
    uint _pointShadowPad2;
    layout(row_major) PointShadowLight PointShadows[4];
} _1055;

layout(binding = 0, std140) uniform GlobalLightCB
{
    GlobalLighting Globals;
} _1353;

layout(binding = 1, std140) uniform SunLightCB
{
    DirectionalLight SunLight;
} _1363;

layout(binding = 8, std430) readonly buffer LightGrid
{
    ClusterCell _data[];
} LightGrid_1;

layout(binding = 5, std140) uniform ClusterGridCB
{
    ClusterGrid ClusterGridData;
} _1406;

layout(binding = 9, std430) readonly buffer LightIndexList
{
    uint _data[];
} LightIndexList_1;

layout(binding = 1, std430) readonly buffer PointLightsBuffer
{
    PointLight _data[];
} PointLightsBuffer_1;

layout(binding = 2, std430) readonly buffer SpotLightsBuffer
{
    SpotLight _data[];
} SpotLightsBuffer_1;

layout(binding = 3, std140) uniform IblCB
{
    IblData Ibl;
} _1606;

uniform sampler2D AlbedoMap;
uniform sampler2D AoMap;
uniform sampler2D IblBrdfLut;
uniform sampler2D CsmMap0;
uniform sampler2D CsmMap1;
uniform sampler2D CsmMap2;
uniform sampler2D CsmMap3;
uniform sampler2D PointShadowMap0;
uniform sampler2D PointShadowMap1;
uniform sampler2D PointShadowMap2;
uniform sampler2D PointShadowMap3;

layout(location = 0) in vec3 i_worldPos;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;
layout(location = 3) in vec3 i_toCam;
layout(location = 4) in float i_viewDepth;
layout(location = 5) flat in vec4 i_baseColor;
layout(location = 6) flat in vec4 i_material;
layout(location = 7) flat in vec4 i_texInfo;
layout(location = 8) flat in vec3 i_emissive;
layout(location = 9) flat in vec4 i_fogColor;
layout(location = 10) flat in vec4 i_fogParams;
layout(location = 0) out vec4 _entryPointOutput;

PointShadowLight spvWorkaroundRowMajor(PointShadowLight wrap) { return wrap; }

void main()
{
    vec3 _5788;
    if (i_texInfo.z > 0.5)
    {
        _5788 = i_baseColor.xyz * texelFetch(AlbedoMap, ivec3(clamp(int(fract(i_uv.x) * i_texInfo.x), 0, int(i_texInfo.x) - 1), clamp(int(fract(i_uv.y) * i_texInfo.y), 0, int(i_texInfo.y) - 1), 0).xy, 0).xyz;
    }
    else
    {
        _5788 = i_baseColor.xyz;
    }
    vec3 _2269 = normalize(i_normal);
    vec3 _2274 = normalize(i_toCam);
    float _2281 = max(i_material.y, 0.04500000178813934326171875);
    float _2433 = clamp((clamp(max(dot(_2269, _2274), 9.9999997473787516355514526367188e-05), 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    float _2438 = clamp((clamp(_2281, 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    int _2440 = int(_2433);
    int _2442 = int(_2438);
    int _2445 = min((_2440 + 1), 31);
    int _2448 = min((_2442 + 1), 31);
    float _2452 = _2433 - float(_2440);
    vec2 _2521 = mix(mix(vec2(texelFetch(IblBrdfLut, ivec3(_2440, _2442, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2445, _2442, 0).xy, 0).xy), vec2(_2452)), mix(vec2(texelFetch(IblBrdfLut, ivec3(_2440, _2448, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2445, _2448, 0).xy, 0).xy), vec2(_2452)), vec2(_2438 - float(_2442)));
    vec3 _2322 = (mix(vec3(0.039999999105930328369140625), _5788, vec3(i_material.x)) * _2521.x) + vec3(_2521.y);
    float _2528 = _2281 * _2281;
    float _2531 = clamp(1.0 - _2528, 0.0, 1.0);
    vec3 _2346 = normalize(mix(_2269, (_2269 * (2.0 * dot(_2269, _2274))) - _2274, vec3(_2531 * (sqrt(_2531) + _2528))));
    vec3 _2624 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2269.y)) + (_1606.Ibl.sh[2].xyz * _2269.z)) + (_1606.Ibl.sh[3].xyz * _2269.x)) + (_1606.Ibl.sh[4].xyz * (_2269.x * _2269.y))) + (_1606.Ibl.sh[5].xyz * (_2269.y * _2269.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2269.z) * _2269.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2269.x * _2269.z))) + (_1606.Ibl.sh[8].xyz * ((_2269.x * _2269.x) - (_2269.y * _2269.y))), vec3(0.0));
    vec3 _2711 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2346.y)) + (_1606.Ibl.sh[2].xyz * _2346.z)) + (_1606.Ibl.sh[3].xyz * _2346.x)) + (_1606.Ibl.sh[4].xyz * (_2346.x * _2346.y))) + (_1606.Ibl.sh[5].xyz * (_2346.y * _2346.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2346.z) * _2346.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2346.x * _2346.z))) + (_1606.Ibl.sh[8].xyz * ((_2346.x * _2346.x) - (_2346.y * _2346.y))), vec3(0.0));
    vec3 _5906;
    if (_1353.Globals.hasSun != 0u)
    {
        vec3 _2926 = -_1363.SunLight.direction;
        vec3 _5789;
        do
        {
            float _2960 = clamp(dot(_2269, _2926), 0.0, 1.0);
            if (_2960 <= 0.0)
            {
                _5789 = vec3(0.0);
                break;
            }
            vec3 _2969 = normalize(_2274 + _2926);
            float _2975 = max(dot(_2269, _2274), 9.9999997473787516355514526367188e-05);
            float _2980 = clamp(dot(_2269, _2969), 0.0, 1.0);
            vec3 _2991 = mix(vec3(0.039999999105930328369140625), _5788, vec3(i_material.x));
            float _3035 = clamp(1.0 - clamp(dot(_2274, _2969), 0.0, 1.0), 0.0, 1.0);
            float _3038 = _3035 * _3035;
            vec3 _3049 = _2991 + ((vec3(1.0) - _2991) * ((_3038 * _3038) * _3035));
            float _3057 = max(_2281, 0.04500000178813934326171875);
            float _3060 = _3057 * _3057;
            float _3063 = _3060 * _3060;
            float _3071 = max(((_2980 * _2980) * (_3063 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
            float _3085 = max(_2281, 0.04500000178813934326171875);
            float _3092 = ((_3085 * _3085) * _3085) * _3085;
            _5789 = ((((((vec3(1.0) - _3049) * (1.0 - i_material.x)) * _5788) * vec3(0.3183098733425140380859375)) + (_3049 * ((_3063 / ((3.1415927410125732421875 * _3071) * _3071)) * (0.5 / max((_2960 * sqrt(((_2975 * _2975) * (1.0 - _3092)) + _3092)) + (_2975 * sqrt(((_2960 * _2960) * (1.0 - _3092)) + _3092)), 9.9999997473787516355514526367188e-06))))) * (_1363.SunLight.color * _1363.SunLight.intensity)) * _2960;
            break;
        } while(false);
        float _5856;
        do
        {
            uint _5790;
            _5790 = 0u;
            for (; (_5790 < _662.Csm.cascadeCount) && (i_viewDepth > _662.Csm.cascades[_5790].splitFar); )
            {
                _5790++;
                continue;
            }
            if (_5790 >= _662.Csm.cascadeCount)
            {
                _5856 = 1.0;
                break;
            }
            float _5817;
            do
            {
                vec4 _3277 = vec4(i_worldPos + (_2269 * (_662.Csm.normalBias * _662.Csm.cascades[_5790].texelWorldSize)), 1.0) * _662.Csm.cascades[_5790].viewProj;
                if (any(greaterThan(abs(_3277.xy), vec2(1.0))) || (_3277.z > 1.0))
                {
                    _5817 = 1.0;
                    break;
                }
                float _3297 = _3277.z - (_662.Csm.depthBias / _662.Csm.cascades[_5790].depthRange);
                vec2 _3310 = vec2((_3277.x * 0.5) + 0.5, 0.5 - (_3277.y * 0.5)) * _662.Csm.shadowMapSize;
                int _3313 = int(_662.Csm.shadowMapSize) - 1;
                float _5801;
                float _5804;
                if (_662.Csm.pcfTaps >= 9u)
                {
                    float _5802;
                    _5802 = 0.0;
                    float _5815;
                    SPIRV_CROSS_UNROLL
                    for (int _5800 = -1; _5800 <= 1; _5802 = _5815, _5800++)
                    {
                        _5815 = _5802;
                        float _3347;
                        SPIRV_CROSS_UNROLL
                        for (int _5811 = -1; _5811 <= 1; _5815 = _3347, _5811++)
                        {
                            ivec2 _3339 = clamp(ivec2(floor(_3310 + vec2(float(_5811), float(_5800)))), ivec2(0), ivec2(_3313));
                            float _5813;
                            do
                            {
                                if (_5790 == 0u)
                                {
                                    _5813 = texelFetch(CsmMap0, ivec3(_3339, 0).xy, 0).x;
                                    break;
                                }
                                if (_5790 == 1u)
                                {
                                    _5813 = texelFetch(CsmMap1, ivec3(_3339, 0).xy, 0).x;
                                    break;
                                }
                                if (_5790 == 2u)
                                {
                                    _5813 = texelFetch(CsmMap2, ivec3(_3339, 0).xy, 0).x;
                                    break;
                                }
                                _5813 = texelFetch(CsmMap3, ivec3(_3339, 0).xy, 0).x;
                                break;
                            } while(false);
                            _3347 = _5815 + float(_3297 <= _5813);
                        }
                    }
                    _5804 = 9.0;
                    _5801 = _5802;
                }
                else
                {
                    bool _3359 = _662.Csm.pcfTaps >= 4u;
                    float _5803;
                    if (_3359)
                    {
                        float _5798;
                        _5798 = 0.0;
                        float _5797;
                        SPIRV_CROSS_UNROLL
                        for (int _5792 = 0; _5792 < 2; _5792++, _5798 = _5797)
                        {
                            _5797 = _5798;
                            float _3392;
                            SPIRV_CROSS_UNROLL
                            for (int _5793 = 0; _5793 < 2; _5797 = _3392, _5793++)
                            {
                                ivec2 _3384 = clamp(ivec2(floor(_3310 + (vec2(float(_5793), float(_5792)) - vec2(0.5)))), ivec2(0), ivec2(_3313));
                                float _5795;
                                do
                                {
                                    if (_5790 == 0u)
                                    {
                                        _5795 = texelFetch(CsmMap0, ivec3(_3384, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5790 == 1u)
                                    {
                                        _5795 = texelFetch(CsmMap1, ivec3(_3384, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5790 == 2u)
                                    {
                                        _5795 = texelFetch(CsmMap2, ivec3(_3384, 0).xy, 0).x;
                                        break;
                                    }
                                    _5795 = texelFetch(CsmMap3, ivec3(_3384, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3392 = _5797 + float(_3297 <= _5795);
                            }
                        }
                        _5803 = _5798;
                    }
                    else
                    {
                        ivec2 _3407 = clamp(ivec2(floor(_3310)), ivec2(0), ivec2(_3313));
                        float _5791;
                        do
                        {
                            if (_5790 == 0u)
                            {
                                _5791 = texelFetch(CsmMap0, ivec3(_3407, 0).xy, 0).x;
                                break;
                            }
                            if (_5790 == 1u)
                            {
                                _5791 = texelFetch(CsmMap1, ivec3(_3407, 0).xy, 0).x;
                                break;
                            }
                            if (_5790 == 2u)
                            {
                                _5791 = texelFetch(CsmMap2, ivec3(_3407, 0).xy, 0).x;
                                break;
                            }
                            _5791 = texelFetch(CsmMap3, ivec3(_3407, 0).xy, 0).x;
                            break;
                        } while(false);
                        _5803 = float(_3297 <= _5791);
                    }
                    _5804 = _3359 ? 4.0 : 1.0;
                    _5801 = _5803;
                }
                _5817 = _5801 / _5804;
                break;
            } while(false);
            float _3183 = _662.Csm.blendFraction * (_662.Csm.cascades[_5790].splitFar - _662.Csm.cascades[_5790].splitNear);
            float _5855;
            if ((((_5790 + 1u) < _662.Csm.cascadeCount) && (_3183 > 0.0)) && (i_viewDepth > (_662.Csm.cascades[_5790].splitFar - _3183)))
            {
                uint _3211 = _5790 + 1u;
                float _5854;
                do
                {
                    vec4 _3690 = vec4(i_worldPos + (_2269 * (_662.Csm.normalBias * _662.Csm.cascades[_3211].texelWorldSize)), 1.0) * _662.Csm.cascades[_3211].viewProj;
                    if (any(greaterThan(abs(_3690.xy), vec2(1.0))) || (_3690.z > 1.0))
                    {
                        _5854 = 1.0;
                        break;
                    }
                    float _3710 = _3690.z - (_662.Csm.depthBias / _662.Csm.cascades[_3211].depthRange);
                    vec2 _3723 = vec2((_3690.x * 0.5) + 0.5, 0.5 - (_3690.y * 0.5)) * _662.Csm.shadowMapSize;
                    int _3726 = int(_662.Csm.shadowMapSize) - 1;
                    float _5838;
                    float _5841;
                    if (_662.Csm.pcfTaps >= 9u)
                    {
                        float _5839;
                        _5839 = 0.0;
                        float _5852;
                        SPIRV_CROSS_UNROLL
                        for (int _5837 = -1; _5837 <= 1; _5839 = _5852, _5837++)
                        {
                            _5852 = _5839;
                            float _3760;
                            SPIRV_CROSS_UNROLL
                            for (int _5848 = -1; _5848 <= 1; _5852 = _3760, _5848++)
                            {
                                ivec2 _3752 = clamp(ivec2(floor(_3723 + vec2(float(_5848), float(_5837)))), ivec2(0), ivec2(_3726));
                                float _5850;
                                do
                                {
                                    if (_3211 == 0u)
                                    {
                                        _5850 = texelFetch(CsmMap0, ivec3(_3752, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3211 == 1u)
                                    {
                                        _5850 = texelFetch(CsmMap1, ivec3(_3752, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3211 == 2u)
                                    {
                                        _5850 = texelFetch(CsmMap2, ivec3(_3752, 0).xy, 0).x;
                                        break;
                                    }
                                    _5850 = texelFetch(CsmMap3, ivec3(_3752, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3760 = _5852 + float(_3710 <= _5850);
                            }
                        }
                        _5841 = 9.0;
                        _5838 = _5839;
                    }
                    else
                    {
                        bool _3772 = _662.Csm.pcfTaps >= 4u;
                        float _5840;
                        if (_3772)
                        {
                            float _5835;
                            _5835 = 0.0;
                            float _5834;
                            SPIRV_CROSS_UNROLL
                            for (int _5829 = 0; _5829 < 2; _5829++, _5835 = _5834)
                            {
                                _5834 = _5835;
                                float _3805;
                                SPIRV_CROSS_UNROLL
                                for (int _5830 = 0; _5830 < 2; _5834 = _3805, _5830++)
                                {
                                    ivec2 _3797 = clamp(ivec2(floor(_3723 + (vec2(float(_5830), float(_5829)) - vec2(0.5)))), ivec2(0), ivec2(_3726));
                                    float _5832;
                                    do
                                    {
                                        if (_3211 == 0u)
                                        {
                                            _5832 = texelFetch(CsmMap0, ivec3(_3797, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3211 == 1u)
                                        {
                                            _5832 = texelFetch(CsmMap1, ivec3(_3797, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3211 == 2u)
                                        {
                                            _5832 = texelFetch(CsmMap2, ivec3(_3797, 0).xy, 0).x;
                                            break;
                                        }
                                        _5832 = texelFetch(CsmMap3, ivec3(_3797, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _3805 = _5834 + float(_3710 <= _5832);
                                }
                            }
                            _5840 = _5835;
                        }
                        else
                        {
                            ivec2 _3820 = clamp(ivec2(floor(_3723)), ivec2(0), ivec2(_3726));
                            float _5828;
                            do
                            {
                                if (_3211 == 0u)
                                {
                                    _5828 = texelFetch(CsmMap0, ivec3(_3820, 0).xy, 0).x;
                                    break;
                                }
                                if (_3211 == 1u)
                                {
                                    _5828 = texelFetch(CsmMap1, ivec3(_3820, 0).xy, 0).x;
                                    break;
                                }
                                if (_3211 == 2u)
                                {
                                    _5828 = texelFetch(CsmMap2, ivec3(_3820, 0).xy, 0).x;
                                    break;
                                }
                                _5828 = texelFetch(CsmMap3, ivec3(_3820, 0).xy, 0).x;
                                break;
                            } while(false);
                            _5840 = float(_3710 <= _5828);
                        }
                        _5841 = _3772 ? 4.0 : 1.0;
                        _5838 = _5840;
                    }
                    _5854 = _5838 / _5841;
                    break;
                } while(false);
                _5855 = mix(_5817, _5854, clamp((i_viewDepth - (_662.Csm.cascades[_5790].splitFar - _3183)) / _3183, 0.0, 1.0));
            }
            else
            {
                _5855 = _5817;
            }
            _5856 = _5855;
            break;
        } while(false);
        _5906 = _5789 * ((i_material.z > 0.5) ? _5856 : 1.0);
    }
    else
    {
        _5906 = vec3(0.0);
    }
    uvec2 _4067 = min((uvec2(gl_FragCoord.xy) / uvec2(_1406.ClusterGridData.tileSizePx)), uvec2(_1406.ClusterGridData.gridX - 1u, _1406.ClusterGridData.gridY - 1u));
    uint _4117 = (_4067.x + (_4067.y * _1406.ClusterGridData.gridX)) + ((uint(clamp(floor((log2(max(i_viewDepth, _1406.ClusterGridData.zNear)) * _1406.ClusterGridData.sliceScale) + _1406.ClusterGridData.sliceBias), 0.0, float(_1406.ClusterGridData.gridZ - 1u))) * _1406.ClusterGridData.gridX) * _1406.ClusterGridData.gridY);
    vec3 _5905;
    _5905 = _5906;
    mat4 _5388[6];
    mat4 _5406[6];
    vec3 _6105;
    float _6133;
    float _6151;
    float _5954;
    float _6024;
    for (uint _5904 = 0u; _5904 < LightGrid_1._data[_4117].count; _6024 = _6151, _5954 = _6133, _5905 = _6105, _5904++)
    {
        uint _2827 = LightGrid_1._data[_4117].offset + _5904;
        if (LightIndexList_1._data[_2827] < _1406.ClusterGridData.numPointLights)
        {
            vec3 _4132 = PointLightsBuffer_1._data[LightIndexList_1._data[_2827]].position - i_worldPos;
            float _4134 = length(_4132);
            vec3 _4139 = _4132 / vec3(max(_4134, 9.9999997473787516355514526367188e-05));
            float _4161 = _4134 / PointLightsBuffer_1._data[LightIndexList_1._data[_2827]].radius;
            float _4164 = _4161 * _4161;
            float _4169 = clamp(1.0 - (_4164 * _4164), 0.0, 1.0);
            vec3 _6004;
            do
            {
                float _4204 = clamp(dot(_2269, _4139), 0.0, 1.0);
                if (_4204 <= 0.0)
                {
                    _6004 = vec3(0.0);
                    break;
                }
                vec3 _4213 = normalize(_2274 + _4139);
                float _4219 = max(dot(_2269, _2274), 9.9999997473787516355514526367188e-05);
                float _4224 = clamp(dot(_2269, _4213), 0.0, 1.0);
                vec3 _4235 = mix(vec3(0.039999999105930328369140625), _5788, vec3(i_material.x));
                float _4279 = clamp(1.0 - clamp(dot(_2274, _4213), 0.0, 1.0), 0.0, 1.0);
                float _4282 = _4279 * _4279;
                vec3 _4293 = _4235 + ((vec3(1.0) - _4235) * ((_4282 * _4282) * _4279));
                float _4301 = max(_2281, 0.04500000178813934326171875);
                float _4304 = _4301 * _4301;
                float _4307 = _4304 * _4304;
                float _4315 = max(((_4224 * _4224) * (_4307 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4329 = max(_2281, 0.04500000178813934326171875);
                float _4336 = ((_4329 * _4329) * _4329) * _4329;
                _6004 = ((((((vec3(1.0) - _4293) * (1.0 - i_material.x)) * _5788) * vec3(0.3183098733425140380859375)) + (_4293 * ((_4307 / ((3.1415927410125732421875 * _4315) * _4315)) * (0.5 / max((_4204 * sqrt(((_4219 * _4219) * (1.0 - _4336)) + _4336)) + (_4219 * sqrt(((_4204 * _4204) * (1.0 - _4336)) + _4336)), 9.9999997473787516355514526367188e-06))))) * ((PointLightsBuffer_1._data[LightIndexList_1._data[_2827]].color * PointLightsBuffer_1._data[LightIndexList_1._data[_2827]].intensity) * ((_4169 * _4169) / max(_4134 * _4134, 9.9999997473787516355514526367188e-05)))) * _4204;
                break;
            } while(false);
            float _6020;
            do
            {
                bool _6018;
                float _6021;
                uint _6005 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_6005 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_6005].posType.w) != 0u)
                        {
                            uint _4409 = _6005 + uint(1);
                            _6005 = _4409;
                            continue;
                        }
                        vec3 _4395 = _1055.PointShadows[_6005].posType.xyz - PointLightsBuffer_1._data[LightIndexList_1._data[_2827]].position;
                        if (dot(_4395, _4395) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _4409 = _6005 + uint(1);
                            _6005 = _4409;
                            continue;
                        }
                        float _6017;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _6017 = 1.0;
                                break;
                            }
                            _5406[0] = spvWorkaroundRowMajor(_1055.PointShadows[_6005]).vp[0];
                            _5406[1] = spvWorkaroundRowMajor(_1055.PointShadows[_6005]).vp[1];
                            _5406[2] = spvWorkaroundRowMajor(_1055.PointShadows[_6005]).vp[2];
                            _5406[3] = spvWorkaroundRowMajor(_1055.PointShadows[_6005]).vp[3];
                            _5406[4] = spvWorkaroundRowMajor(_1055.PointShadows[_6005]).vp[4];
                            _5406[5] = spvWorkaroundRowMajor(_1055.PointShadows[_6005]).vp[5];
                            vec3 _4472 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_6005]).posType.xyz;
                            uint _6006;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_6005]).posType.w < 0.5)
                            {
                                vec3 _4478 = abs(_4472);
                                uint _6007;
                                if ((_4478.x >= _4478.y) && (_4478.x >= _4478.z))
                                {
                                    _6007 = (_4472.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _6008;
                                    if (_4478.y >= _4478.z)
                                    {
                                        _6008 = (_4472.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _6008 = (_4472.z > 0.0) ? 4u : 5u;
                                    }
                                    _6007 = _6008;
                                }
                                _6006 = _6007;
                            }
                            else
                            {
                                _6006 = 0u;
                            }
                            if (_6006 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_6005]).params.y))
                            {
                                _6017 = 1.0;
                                break;
                            }
                            vec4 _4538 = vec4(i_worldPos + (_2269 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_4472)))), 1.0) * _5406[_6006];
                            if (_4538.w <= spvWorkaroundRowMajor(_1055.PointShadows[_6005]).params.x)
                            {
                                _6017 = 1.0;
                                break;
                            }
                            float _4550 = _4538.x / _4538.w;
                            float _4555 = _4538.y / _4538.w;
                            if ((abs(_4550) > 1.0) || (abs(_4555) > 1.0))
                            {
                                _6017 = 1.0;
                                break;
                            }
                            int _4567 = int(spvWorkaroundRowMajor(_1055.PointShadows[_6005]).params.z);
                            int _4579 = clamp(int(roundEven((((_4550 * 0.5) + 0.5) * float(_4567)) - 0.5)), 0, _4567 - 1);
                            int _4591 = clamp(int(roundEven(((0.5 - (_4555 * 0.5)) * float(_4567)) - 0.5)), 0, _4567 - 1);
                            int _4595 = int(_6006) * _4567;
                            int _4599 = (_4595 + _4567) - 1;
                            float _4601 = _4538.w;
                            float _4604 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _4601);
                            float _6010;
                            _6010 = 0.0;
                            float _6015;
                            SPIRV_CROSS_UNROLL
                            for (int _6009 = -1; _6009 <= 1; _6010 = _6015, _6009++)
                            {
                                _6015 = _6010;
                                float _4644;
                                SPIRV_CROSS_UNROLL
                                for (int _6011 = -1; _6011 <= 1; _6015 = _4644, _6011++)
                                {
                                    int _4620 = clamp(_4579 + _6011, 0, _4567 - 1);
                                    int _4632 = clamp(clamp(_4591 + _6009, 0, _4567 - 1) + _4595, _4595, _4599);
                                    float _6013;
                                    do
                                    {
                                        if (_6005 == 0u)
                                        {
                                            _6013 = texelFetch(PointShadowMap0, ivec3(_4620, _4632, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_6005 == 1u)
                                        {
                                            _6013 = texelFetch(PointShadowMap1, ivec3(_4620, _4632, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_6005 == 2u)
                                        {
                                            _6013 = texelFetch(PointShadowMap2, ivec3(_4620, _4632, 0).xy, 0).x;
                                            break;
                                        }
                                        _6013 = texelFetch(PointShadowMap3, ivec3(_4620, _4632, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _4644 = _6015 + float(_6013 >= (_4601 - _4604));
                                }
                            }
                            _6017 = _6010 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _6021 = _6017;
                        _6018 = true;
                        break;
                    }
                    else
                    {
                        _6021 = _6024;
                        _6018 = false;
                        break;
                    }
                }
                if (_6018)
                {
                    _6020 = _6021;
                    break;
                }
                _6020 = 1.0;
                break;
            } while(false);
            _6151 = _6020;
            _6133 = _5954;
            _6105 = _5905 + (_6004 * _6020);
        }
        else
        {
            uint _2872 = LightIndexList_1._data[_2827] - _1406.ClusterGridData.numPointLights;
            vec3 _4746 = SpotLightsBuffer_1._data[_2872].position - i_worldPos;
            float _4748 = length(_4746);
            vec3 _4753 = _4746 / vec3(max(_4748, 9.9999997473787516355514526367188e-05));
            float _4787 = _4748 / SpotLightsBuffer_1._data[_2872].radius;
            float _4790 = _4787 * _4787;
            float _4795 = clamp(1.0 - (_4790 * _4790), 0.0, 1.0);
            float _4812 = clamp((dot(-_4753, SpotLightsBuffer_1._data[_2872].direction) * SpotLightsBuffer_1._data[_2872].spotScale) + SpotLightsBuffer_1._data[_2872].spotOffset, 0.0, 1.0);
            vec3 _5934;
            do
            {
                float _4842 = clamp(dot(_2269, _4753), 0.0, 1.0);
                if (_4842 <= 0.0)
                {
                    _5934 = vec3(0.0);
                    break;
                }
                vec3 _4851 = normalize(_2274 + _4753);
                float _4857 = max(dot(_2269, _2274), 9.9999997473787516355514526367188e-05);
                float _4862 = clamp(dot(_2269, _4851), 0.0, 1.0);
                vec3 _4873 = mix(vec3(0.039999999105930328369140625), _5788, vec3(i_material.x));
                float _4917 = clamp(1.0 - clamp(dot(_2274, _4851), 0.0, 1.0), 0.0, 1.0);
                float _4920 = _4917 * _4917;
                vec3 _4931 = _4873 + ((vec3(1.0) - _4873) * ((_4920 * _4920) * _4917));
                float _4939 = max(_2281, 0.04500000178813934326171875);
                float _4942 = _4939 * _4939;
                float _4945 = _4942 * _4942;
                float _4953 = max(((_4862 * _4862) * (_4945 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4967 = max(_2281, 0.04500000178813934326171875);
                float _4974 = ((_4967 * _4967) * _4967) * _4967;
                _5934 = ((((((vec3(1.0) - _4931) * (1.0 - i_material.x)) * _5788) * vec3(0.3183098733425140380859375)) + (_4931 * ((_4945 / ((3.1415927410125732421875 * _4953) * _4953)) * (0.5 / max((_4842 * sqrt(((_4857 * _4857) * (1.0 - _4974)) + _4974)) + (_4857 * sqrt(((_4842 * _4842) * (1.0 - _4974)) + _4974)), 9.9999997473787516355514526367188e-06))))) * ((SpotLightsBuffer_1._data[_2872].color * SpotLightsBuffer_1._data[_2872].intensity) * (((_4795 * _4795) / max(_4748 * _4748, 9.9999997473787516355514526367188e-05)) * (_4812 * _4812)))) * _4842;
                break;
            } while(false);
            uint _2900 = LightIndexList_1._data[_2827] - _1406.ClusterGridData.numPointLights;
            float _5950;
            do
            {
                bool _5948;
                float _5951;
                uint _5935 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_5935 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_5935].posType.w) != 1u)
                        {
                            uint _5047 = _5935 + uint(1);
                            _5935 = _5047;
                            continue;
                        }
                        vec3 _5033 = _1055.PointShadows[_5935].posType.xyz - SpotLightsBuffer_1._data[_2900].position;
                        if (dot(_5033, _5033) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _5047 = _5935 + uint(1);
                            _5935 = _5047;
                            continue;
                        }
                        float _5947;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _5947 = 1.0;
                                break;
                            }
                            _5388[0] = spvWorkaroundRowMajor(_1055.PointShadows[_5935]).vp[0];
                            _5388[1] = spvWorkaroundRowMajor(_1055.PointShadows[_5935]).vp[1];
                            _5388[2] = spvWorkaroundRowMajor(_1055.PointShadows[_5935]).vp[2];
                            _5388[3] = spvWorkaroundRowMajor(_1055.PointShadows[_5935]).vp[3];
                            _5388[4] = spvWorkaroundRowMajor(_1055.PointShadows[_5935]).vp[4];
                            _5388[5] = spvWorkaroundRowMajor(_1055.PointShadows[_5935]).vp[5];
                            vec3 _5110 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_5935]).posType.xyz;
                            uint _5936;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_5935]).posType.w < 0.5)
                            {
                                vec3 _5116 = abs(_5110);
                                uint _5937;
                                if ((_5116.x >= _5116.y) && (_5116.x >= _5116.z))
                                {
                                    _5937 = (_5110.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _5938;
                                    if (_5116.y >= _5116.z)
                                    {
                                        _5938 = (_5110.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _5938 = (_5110.z > 0.0) ? 4u : 5u;
                                    }
                                    _5937 = _5938;
                                }
                                _5936 = _5937;
                            }
                            else
                            {
                                _5936 = 0u;
                            }
                            if (_5936 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_5935]).params.y))
                            {
                                _5947 = 1.0;
                                break;
                            }
                            vec4 _5176 = vec4(i_worldPos + (_2269 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_5110)))), 1.0) * _5388[_5936];
                            if (_5176.w <= spvWorkaroundRowMajor(_1055.PointShadows[_5935]).params.x)
                            {
                                _5947 = 1.0;
                                break;
                            }
                            float _5188 = _5176.x / _5176.w;
                            float _5193 = _5176.y / _5176.w;
                            if ((abs(_5188) > 1.0) || (abs(_5193) > 1.0))
                            {
                                _5947 = 1.0;
                                break;
                            }
                            int _5205 = int(spvWorkaroundRowMajor(_1055.PointShadows[_5935]).params.z);
                            int _5217 = clamp(int(roundEven((((_5188 * 0.5) + 0.5) * float(_5205)) - 0.5)), 0, _5205 - 1);
                            int _5229 = clamp(int(roundEven(((0.5 - (_5193 * 0.5)) * float(_5205)) - 0.5)), 0, _5205 - 1);
                            int _5233 = int(_5936) * _5205;
                            int _5237 = (_5233 + _5205) - 1;
                            float _5239 = _5176.w;
                            float _5242 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _5239);
                            float _5940;
                            _5940 = 0.0;
                            float _5945;
                            SPIRV_CROSS_UNROLL
                            for (int _5939 = -1; _5939 <= 1; _5940 = _5945, _5939++)
                            {
                                _5945 = _5940;
                                float _5282;
                                SPIRV_CROSS_UNROLL
                                for (int _5941 = -1; _5941 <= 1; _5945 = _5282, _5941++)
                                {
                                    int _5258 = clamp(_5217 + _5941, 0, _5205 - 1);
                                    int _5270 = clamp(clamp(_5229 + _5939, 0, _5205 - 1) + _5233, _5233, _5237);
                                    float _5943;
                                    do
                                    {
                                        if (_5935 == 0u)
                                        {
                                            _5943 = texelFetch(PointShadowMap0, ivec3(_5258, _5270, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5935 == 1u)
                                        {
                                            _5943 = texelFetch(PointShadowMap1, ivec3(_5258, _5270, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5935 == 2u)
                                        {
                                            _5943 = texelFetch(PointShadowMap2, ivec3(_5258, _5270, 0).xy, 0).x;
                                            break;
                                        }
                                        _5943 = texelFetch(PointShadowMap3, ivec3(_5258, _5270, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _5282 = _5945 + float(_5943 >= (_5239 - _5242));
                                }
                            }
                            _5947 = _5940 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _5951 = _5947;
                        _5948 = true;
                        break;
                    }
                    else
                    {
                        _5951 = _5954;
                        _5948 = false;
                        break;
                    }
                }
                if (_5948)
                {
                    _5950 = _5951;
                    break;
                }
                _5950 = 1.0;
                break;
            } while(false);
            _6151 = _6024;
            _6133 = _5950;
            _6105 = _5905 + (_5934 * _5950);
        }
    }
    vec3 _2376 = (((((((vec3(1.0) - min(_2322, vec3(1.0))) * (1.0 - i_material.x)) * _5788) * _2624) + (_2322 * _2711)) * texelFetch(AoMap, ivec3(ivec2(gl_FragCoord.xy), 0).xy, 0).x) + _5905) + i_emissive;
    vec3 _5933;
    if (i_fogParams.z > 0.0)
    {
        _5933 = mix(_2376, i_fogColor.xyz, vec3(clamp((length(i_toCam) - i_fogParams.x) / max(i_fogParams.y - i_fogParams.x, 0.001000000047497451305389404296875), 0.0, 1.0) * i_fogParams.z));
    }
    else
    {
        _5933 = _2376;
    }
    _entryPointOutput = vec4(clamp((_5933 * ((_5933 * 2.5099999904632568359375) + vec3(0.02999999932944774627685546875))) / ((_5933 * ((_5933 * 2.4300000667572021484375) + vec3(0.589999973773956298828125))) + vec3(0.14000000059604644775390625)), vec3(0.0), vec3(1.0)), i_baseColor.w);
}

