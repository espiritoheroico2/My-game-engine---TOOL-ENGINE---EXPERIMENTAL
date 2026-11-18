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

float _5963;

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
    vec3 _5794;
    if (i_texInfo.z > 0.5)
    {
        _5794 = i_baseColor.xyz * texelFetch(AlbedoMap, ivec3(clamp(int(fract(i_uv.x) * i_texInfo.x), 0, int(i_texInfo.x) - 1), clamp(int(fract(i_uv.y) * i_texInfo.y), 0, int(i_texInfo.y) - 1), 0).xy, 0).xyz;
    }
    else
    {
        _5794 = i_baseColor.xyz;
    }
    vec3 _2275 = normalize(i_normal);
    vec3 _2280 = normalize(i_toCam);
    float _2287 = max(i_material.y, 0.04500000178813934326171875);
    float _2439 = clamp((clamp(max(dot(_2275, _2280), 9.9999997473787516355514526367188e-05), 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    float _2444 = clamp((clamp(_2287, 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    int _2446 = int(_2439);
    int _2448 = int(_2444);
    int _2451 = min((_2446 + 1), 31);
    int _2454 = min((_2448 + 1), 31);
    float _2458 = _2439 - float(_2446);
    vec2 _2527 = mix(mix(vec2(texelFetch(IblBrdfLut, ivec3(_2446, _2448, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2451, _2448, 0).xy, 0).xy), vec2(_2458)), mix(vec2(texelFetch(IblBrdfLut, ivec3(_2446, _2454, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2451, _2454, 0).xy, 0).xy), vec2(_2458)), vec2(_2444 - float(_2448)));
    vec3 _2328 = (mix(vec3(0.039999999105930328369140625), _5794, vec3(i_material.x)) * _2527.x) + vec3(_2527.y);
    float _2534 = _2287 * _2287;
    float _2537 = clamp(1.0 - _2534, 0.0, 1.0);
    vec3 _2352 = normalize(mix(_2275, (_2275 * (2.0 * dot(_2275, _2280))) - _2280, vec3(_2537 * (sqrt(_2537) + _2534))));
    vec3 _2630 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2275.y)) + (_1606.Ibl.sh[2].xyz * _2275.z)) + (_1606.Ibl.sh[3].xyz * _2275.x)) + (_1606.Ibl.sh[4].xyz * (_2275.x * _2275.y))) + (_1606.Ibl.sh[5].xyz * (_2275.y * _2275.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2275.z) * _2275.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2275.x * _2275.z))) + (_1606.Ibl.sh[8].xyz * ((_2275.x * _2275.x) - (_2275.y * _2275.y))), vec3(0.0));
    vec3 _2717 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2352.y)) + (_1606.Ibl.sh[2].xyz * _2352.z)) + (_1606.Ibl.sh[3].xyz * _2352.x)) + (_1606.Ibl.sh[4].xyz * (_2352.x * _2352.y))) + (_1606.Ibl.sh[5].xyz * (_2352.y * _2352.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2352.z) * _2352.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2352.x * _2352.z))) + (_1606.Ibl.sh[8].xyz * ((_2352.x * _2352.x) - (_2352.y * _2352.y))), vec3(0.0));
    vec3 _5912;
    if (_1353.Globals.hasSun != 0u)
    {
        vec3 _2932 = -_1363.SunLight.direction;
        vec3 _5795;
        do
        {
            float _2966 = clamp(dot(_2275, _2932), 0.0, 1.0);
            if (_2966 <= 0.0)
            {
                _5795 = vec3(0.0);
                break;
            }
            vec3 _2975 = normalize(_2280 + _2932);
            float _2981 = max(dot(_2275, _2280), 9.9999997473787516355514526367188e-05);
            float _2986 = clamp(dot(_2275, _2975), 0.0, 1.0);
            vec3 _2997 = mix(vec3(0.039999999105930328369140625), _5794, vec3(i_material.x));
            float _3041 = clamp(1.0 - clamp(dot(_2280, _2975), 0.0, 1.0), 0.0, 1.0);
            float _3044 = _3041 * _3041;
            vec3 _3055 = _2997 + ((vec3(1.0) - _2997) * ((_3044 * _3044) * _3041));
            float _3063 = max(_2287, 0.04500000178813934326171875);
            float _3066 = _3063 * _3063;
            float _3069 = _3066 * _3066;
            float _3077 = max(((_2986 * _2986) * (_3069 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
            float _3091 = max(_2287, 0.04500000178813934326171875);
            float _3098 = ((_3091 * _3091) * _3091) * _3091;
            _5795 = ((((((vec3(1.0) - _3055) * (1.0 - i_material.x)) * _5794) * vec3(0.3183098733425140380859375)) + (_3055 * ((_3069 / ((3.1415927410125732421875 * _3077) * _3077)) * (0.5 / max((_2966 * sqrt(((_2981 * _2981) * (1.0 - _3098)) + _3098)) + (_2981 * sqrt(((_2966 * _2966) * (1.0 - _3098)) + _3098)), 9.9999997473787516355514526367188e-06))))) * (_1363.SunLight.color * _1363.SunLight.intensity)) * _2966;
            break;
        } while(false);
        float _5862;
        do
        {
            uint _5796;
            _5796 = 0u;
            for (; (_5796 < _662.Csm.cascadeCount) && (i_viewDepth > _662.Csm.cascades[_5796].splitFar); )
            {
                _5796++;
                continue;
            }
            if (_5796 >= _662.Csm.cascadeCount)
            {
                _5862 = 1.0;
                break;
            }
            float _5823;
            do
            {
                vec4 _3283 = vec4(i_worldPos + (_2275 * (_662.Csm.normalBias * _662.Csm.cascades[_5796].texelWorldSize)), 1.0) * _662.Csm.cascades[_5796].viewProj;
                if (any(greaterThan(abs(_3283.xy), vec2(1.0))) || (_3283.z > 1.0))
                {
                    _5823 = 1.0;
                    break;
                }
                float _3303 = _3283.z - (_662.Csm.depthBias / _662.Csm.cascades[_5796].depthRange);
                vec2 _3316 = vec2((_3283.x * 0.5) + 0.5, 0.5 - (_3283.y * 0.5)) * _662.Csm.shadowMapSize;
                int _3319 = int(_662.Csm.shadowMapSize) - 1;
                float _5807;
                float _5810;
                if (_662.Csm.pcfTaps >= 9u)
                {
                    float _5808;
                    _5808 = 0.0;
                    float _5821;
                    SPIRV_CROSS_UNROLL
                    for (int _5806 = -1; _5806 <= 1; _5808 = _5821, _5806++)
                    {
                        _5821 = _5808;
                        float _3353;
                        SPIRV_CROSS_UNROLL
                        for (int _5817 = -1; _5817 <= 1; _5821 = _3353, _5817++)
                        {
                            ivec2 _3345 = clamp(ivec2(floor(_3316 + vec2(float(_5817), float(_5806)))), ivec2(0), ivec2(_3319));
                            float _5819;
                            do
                            {
                                if (_5796 == 0u)
                                {
                                    _5819 = texelFetch(CsmMap0, ivec3(_3345, 0).xy, 0).x;
                                    break;
                                }
                                if (_5796 == 1u)
                                {
                                    _5819 = texelFetch(CsmMap1, ivec3(_3345, 0).xy, 0).x;
                                    break;
                                }
                                if (_5796 == 2u)
                                {
                                    _5819 = texelFetch(CsmMap2, ivec3(_3345, 0).xy, 0).x;
                                    break;
                                }
                                _5819 = texelFetch(CsmMap3, ivec3(_3345, 0).xy, 0).x;
                                break;
                            } while(false);
                            _3353 = _5821 + float(_3303 <= _5819);
                        }
                    }
                    _5810 = 9.0;
                    _5807 = _5808;
                }
                else
                {
                    bool _3365 = _662.Csm.pcfTaps >= 4u;
                    float _5809;
                    if (_3365)
                    {
                        float _5804;
                        _5804 = 0.0;
                        float _5803;
                        SPIRV_CROSS_UNROLL
                        for (int _5798 = 0; _5798 < 2; _5798++, _5804 = _5803)
                        {
                            _5803 = _5804;
                            float _3398;
                            SPIRV_CROSS_UNROLL
                            for (int _5799 = 0; _5799 < 2; _5803 = _3398, _5799++)
                            {
                                ivec2 _3390 = clamp(ivec2(floor(_3316 + (vec2(float(_5799), float(_5798)) - vec2(0.5)))), ivec2(0), ivec2(_3319));
                                float _5801;
                                do
                                {
                                    if (_5796 == 0u)
                                    {
                                        _5801 = texelFetch(CsmMap0, ivec3(_3390, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5796 == 1u)
                                    {
                                        _5801 = texelFetch(CsmMap1, ivec3(_3390, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5796 == 2u)
                                    {
                                        _5801 = texelFetch(CsmMap2, ivec3(_3390, 0).xy, 0).x;
                                        break;
                                    }
                                    _5801 = texelFetch(CsmMap3, ivec3(_3390, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3398 = _5803 + float(_3303 <= _5801);
                            }
                        }
                        _5809 = _5804;
                    }
                    else
                    {
                        ivec2 _3413 = clamp(ivec2(floor(_3316)), ivec2(0), ivec2(_3319));
                        float _5797;
                        do
                        {
                            if (_5796 == 0u)
                            {
                                _5797 = texelFetch(CsmMap0, ivec3(_3413, 0).xy, 0).x;
                                break;
                            }
                            if (_5796 == 1u)
                            {
                                _5797 = texelFetch(CsmMap1, ivec3(_3413, 0).xy, 0).x;
                                break;
                            }
                            if (_5796 == 2u)
                            {
                                _5797 = texelFetch(CsmMap2, ivec3(_3413, 0).xy, 0).x;
                                break;
                            }
                            _5797 = texelFetch(CsmMap3, ivec3(_3413, 0).xy, 0).x;
                            break;
                        } while(false);
                        _5809 = float(_3303 <= _5797);
                    }
                    _5810 = _3365 ? 4.0 : 1.0;
                    _5807 = _5809;
                }
                _5823 = _5807 / _5810;
                break;
            } while(false);
            float _3189 = _662.Csm.blendFraction * (_662.Csm.cascades[_5796].splitFar - _662.Csm.cascades[_5796].splitNear);
            float _5861;
            if ((((_5796 + 1u) < _662.Csm.cascadeCount) && (_3189 > 0.0)) && (i_viewDepth > (_662.Csm.cascades[_5796].splitFar - _3189)))
            {
                uint _3217 = _5796 + 1u;
                float _5860;
                do
                {
                    vec4 _3696 = vec4(i_worldPos + (_2275 * (_662.Csm.normalBias * _662.Csm.cascades[_3217].texelWorldSize)), 1.0) * _662.Csm.cascades[_3217].viewProj;
                    if (any(greaterThan(abs(_3696.xy), vec2(1.0))) || (_3696.z > 1.0))
                    {
                        _5860 = 1.0;
                        break;
                    }
                    float _3716 = _3696.z - (_662.Csm.depthBias / _662.Csm.cascades[_3217].depthRange);
                    vec2 _3729 = vec2((_3696.x * 0.5) + 0.5, 0.5 - (_3696.y * 0.5)) * _662.Csm.shadowMapSize;
                    int _3732 = int(_662.Csm.shadowMapSize) - 1;
                    float _5844;
                    float _5847;
                    if (_662.Csm.pcfTaps >= 9u)
                    {
                        float _5845;
                        _5845 = 0.0;
                        float _5858;
                        SPIRV_CROSS_UNROLL
                        for (int _5843 = -1; _5843 <= 1; _5845 = _5858, _5843++)
                        {
                            _5858 = _5845;
                            float _3766;
                            SPIRV_CROSS_UNROLL
                            for (int _5854 = -1; _5854 <= 1; _5858 = _3766, _5854++)
                            {
                                ivec2 _3758 = clamp(ivec2(floor(_3729 + vec2(float(_5854), float(_5843)))), ivec2(0), ivec2(_3732));
                                float _5856;
                                do
                                {
                                    if (_3217 == 0u)
                                    {
                                        _5856 = texelFetch(CsmMap0, ivec3(_3758, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3217 == 1u)
                                    {
                                        _5856 = texelFetch(CsmMap1, ivec3(_3758, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3217 == 2u)
                                    {
                                        _5856 = texelFetch(CsmMap2, ivec3(_3758, 0).xy, 0).x;
                                        break;
                                    }
                                    _5856 = texelFetch(CsmMap3, ivec3(_3758, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3766 = _5858 + float(_3716 <= _5856);
                            }
                        }
                        _5847 = 9.0;
                        _5844 = _5845;
                    }
                    else
                    {
                        bool _3778 = _662.Csm.pcfTaps >= 4u;
                        float _5846;
                        if (_3778)
                        {
                            float _5841;
                            _5841 = 0.0;
                            float _5840;
                            SPIRV_CROSS_UNROLL
                            for (int _5835 = 0; _5835 < 2; _5835++, _5841 = _5840)
                            {
                                _5840 = _5841;
                                float _3811;
                                SPIRV_CROSS_UNROLL
                                for (int _5836 = 0; _5836 < 2; _5840 = _3811, _5836++)
                                {
                                    ivec2 _3803 = clamp(ivec2(floor(_3729 + (vec2(float(_5836), float(_5835)) - vec2(0.5)))), ivec2(0), ivec2(_3732));
                                    float _5838;
                                    do
                                    {
                                        if (_3217 == 0u)
                                        {
                                            _5838 = texelFetch(CsmMap0, ivec3(_3803, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3217 == 1u)
                                        {
                                            _5838 = texelFetch(CsmMap1, ivec3(_3803, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3217 == 2u)
                                        {
                                            _5838 = texelFetch(CsmMap2, ivec3(_3803, 0).xy, 0).x;
                                            break;
                                        }
                                        _5838 = texelFetch(CsmMap3, ivec3(_3803, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _3811 = _5840 + float(_3716 <= _5838);
                                }
                            }
                            _5846 = _5841;
                        }
                        else
                        {
                            ivec2 _3826 = clamp(ivec2(floor(_3729)), ivec2(0), ivec2(_3732));
                            float _5834;
                            do
                            {
                                if (_3217 == 0u)
                                {
                                    _5834 = texelFetch(CsmMap0, ivec3(_3826, 0).xy, 0).x;
                                    break;
                                }
                                if (_3217 == 1u)
                                {
                                    _5834 = texelFetch(CsmMap1, ivec3(_3826, 0).xy, 0).x;
                                    break;
                                }
                                if (_3217 == 2u)
                                {
                                    _5834 = texelFetch(CsmMap2, ivec3(_3826, 0).xy, 0).x;
                                    break;
                                }
                                _5834 = texelFetch(CsmMap3, ivec3(_3826, 0).xy, 0).x;
                                break;
                            } while(false);
                            _5846 = float(_3716 <= _5834);
                        }
                        _5847 = _3778 ? 4.0 : 1.0;
                        _5844 = _5846;
                    }
                    _5860 = _5844 / _5847;
                    break;
                } while(false);
                _5861 = mix(_5823, _5860, clamp((i_viewDepth - (_662.Csm.cascades[_5796].splitFar - _3189)) / _3189, 0.0, 1.0));
            }
            else
            {
                _5861 = _5823;
            }
            _5862 = _5861;
            break;
        } while(false);
        _5912 = _5795 * ((i_material.z > 0.5) ? _5862 : 1.0);
    }
    else
    {
        _5912 = vec3(0.0);
    }
    uvec2 _4073 = min((uvec2(gl_FragCoord.xy) / uvec2(_1406.ClusterGridData.tileSizePx)), uvec2(_1406.ClusterGridData.gridX - 1u, _1406.ClusterGridData.gridY - 1u));
    uint _4123 = (_4073.x + (_4073.y * _1406.ClusterGridData.gridX)) + ((uint(clamp(floor((log2(max(i_viewDepth, _1406.ClusterGridData.zNear)) * _1406.ClusterGridData.sliceScale) + _1406.ClusterGridData.sliceBias), 0.0, float(_1406.ClusterGridData.gridZ - 1u))) * _1406.ClusterGridData.gridX) * _1406.ClusterGridData.gridY);
    vec3 _5911;
    _5911 = _5912;
    mat4 _5394[6];
    mat4 _5412[6];
    vec3 _6111;
    float _6139;
    float _6157;
    float _5960;
    float _6030;
    for (uint _5910 = 0u; _5910 < LightGrid_1._data[_4123].count; _6030 = _6157, _5960 = _6139, _5911 = _6111, _5910++)
    {
        uint _2833 = LightGrid_1._data[_4123].offset + _5910;
        if (LightIndexList_1._data[_2833] < _1406.ClusterGridData.numPointLights)
        {
            vec3 _4138 = PointLightsBuffer_1._data[LightIndexList_1._data[_2833]].position - i_worldPos;
            float _4140 = length(_4138);
            vec3 _4145 = _4138 / vec3(max(_4140, 9.9999997473787516355514526367188e-05));
            float _4167 = _4140 / PointLightsBuffer_1._data[LightIndexList_1._data[_2833]].radius;
            float _4170 = _4167 * _4167;
            float _4175 = clamp(1.0 - (_4170 * _4170), 0.0, 1.0);
            vec3 _6010;
            do
            {
                float _4210 = clamp(dot(_2275, _4145), 0.0, 1.0);
                if (_4210 <= 0.0)
                {
                    _6010 = vec3(0.0);
                    break;
                }
                vec3 _4219 = normalize(_2280 + _4145);
                float _4225 = max(dot(_2275, _2280), 9.9999997473787516355514526367188e-05);
                float _4230 = clamp(dot(_2275, _4219), 0.0, 1.0);
                vec3 _4241 = mix(vec3(0.039999999105930328369140625), _5794, vec3(i_material.x));
                float _4285 = clamp(1.0 - clamp(dot(_2280, _4219), 0.0, 1.0), 0.0, 1.0);
                float _4288 = _4285 * _4285;
                vec3 _4299 = _4241 + ((vec3(1.0) - _4241) * ((_4288 * _4288) * _4285));
                float _4307 = max(_2287, 0.04500000178813934326171875);
                float _4310 = _4307 * _4307;
                float _4313 = _4310 * _4310;
                float _4321 = max(((_4230 * _4230) * (_4313 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4335 = max(_2287, 0.04500000178813934326171875);
                float _4342 = ((_4335 * _4335) * _4335) * _4335;
                _6010 = ((((((vec3(1.0) - _4299) * (1.0 - i_material.x)) * _5794) * vec3(0.3183098733425140380859375)) + (_4299 * ((_4313 / ((3.1415927410125732421875 * _4321) * _4321)) * (0.5 / max((_4210 * sqrt(((_4225 * _4225) * (1.0 - _4342)) + _4342)) + (_4225 * sqrt(((_4210 * _4210) * (1.0 - _4342)) + _4342)), 9.9999997473787516355514526367188e-06))))) * ((PointLightsBuffer_1._data[LightIndexList_1._data[_2833]].color * PointLightsBuffer_1._data[LightIndexList_1._data[_2833]].intensity) * ((_4175 * _4175) / max(_4140 * _4140, 9.9999997473787516355514526367188e-05)))) * _4210;
                break;
            } while(false);
            float _6026;
            do
            {
                bool _6024;
                float _6027;
                uint _6011 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_6011 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_6011].posType.w) != 0u)
                        {
                            uint _4415 = _6011 + uint(1);
                            _6011 = _4415;
                            continue;
                        }
                        vec3 _4401 = _1055.PointShadows[_6011].posType.xyz - PointLightsBuffer_1._data[LightIndexList_1._data[_2833]].position;
                        if (dot(_4401, _4401) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _4415 = _6011 + uint(1);
                            _6011 = _4415;
                            continue;
                        }
                        float _6023;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _6023 = 1.0;
                                break;
                            }
                            _5412[0] = spvWorkaroundRowMajor(_1055.PointShadows[_6011]).vp[0];
                            _5412[1] = spvWorkaroundRowMajor(_1055.PointShadows[_6011]).vp[1];
                            _5412[2] = spvWorkaroundRowMajor(_1055.PointShadows[_6011]).vp[2];
                            _5412[3] = spvWorkaroundRowMajor(_1055.PointShadows[_6011]).vp[3];
                            _5412[4] = spvWorkaroundRowMajor(_1055.PointShadows[_6011]).vp[4];
                            _5412[5] = spvWorkaroundRowMajor(_1055.PointShadows[_6011]).vp[5];
                            vec3 _4478 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_6011]).posType.xyz;
                            uint _6012;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_6011]).posType.w < 0.5)
                            {
                                vec3 _4484 = abs(_4478);
                                uint _6013;
                                if ((_4484.x >= _4484.y) && (_4484.x >= _4484.z))
                                {
                                    _6013 = (_4478.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _6014;
                                    if (_4484.y >= _4484.z)
                                    {
                                        _6014 = (_4478.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _6014 = (_4478.z > 0.0) ? 4u : 5u;
                                    }
                                    _6013 = _6014;
                                }
                                _6012 = _6013;
                            }
                            else
                            {
                                _6012 = 0u;
                            }
                            if (_6012 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_6011]).params.y))
                            {
                                _6023 = 1.0;
                                break;
                            }
                            vec4 _4544 = vec4(i_worldPos + (_2275 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_4478)))), 1.0) * _5412[_6012];
                            if (_4544.w <= spvWorkaroundRowMajor(_1055.PointShadows[_6011]).params.x)
                            {
                                _6023 = 1.0;
                                break;
                            }
                            float _4556 = _4544.x / _4544.w;
                            float _4561 = _4544.y / _4544.w;
                            if ((abs(_4556) > 1.0) || (abs(_4561) > 1.0))
                            {
                                _6023 = 1.0;
                                break;
                            }
                            int _4573 = int(spvWorkaroundRowMajor(_1055.PointShadows[_6011]).params.z);
                            int _4585 = clamp(int(roundEven((((_4556 * 0.5) + 0.5) * float(_4573)) - 0.5)), 0, _4573 - 1);
                            int _4597 = clamp(int(roundEven(((0.5 - (_4561 * 0.5)) * float(_4573)) - 0.5)), 0, _4573 - 1);
                            int _4601 = int(_6012) * _4573;
                            int _4605 = (_4601 + _4573) - 1;
                            float _4607 = _4544.w;
                            float _4610 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _4607);
                            float _6016;
                            _6016 = 0.0;
                            float _6021;
                            SPIRV_CROSS_UNROLL
                            for (int _6015 = -1; _6015 <= 1; _6016 = _6021, _6015++)
                            {
                                _6021 = _6016;
                                float _4650;
                                SPIRV_CROSS_UNROLL
                                for (int _6017 = -1; _6017 <= 1; _6021 = _4650, _6017++)
                                {
                                    int _4626 = clamp(_4585 + _6017, 0, _4573 - 1);
                                    int _4638 = clamp(clamp(_4597 + _6015, 0, _4573 - 1) + _4601, _4601, _4605);
                                    float _6019;
                                    do
                                    {
                                        if (_6011 == 0u)
                                        {
                                            _6019 = texelFetch(PointShadowMap0, ivec3(_4626, _4638, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_6011 == 1u)
                                        {
                                            _6019 = texelFetch(PointShadowMap1, ivec3(_4626, _4638, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_6011 == 2u)
                                        {
                                            _6019 = texelFetch(PointShadowMap2, ivec3(_4626, _4638, 0).xy, 0).x;
                                            break;
                                        }
                                        _6019 = texelFetch(PointShadowMap3, ivec3(_4626, _4638, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _4650 = _6021 + float(_6019 >= (_4607 - _4610));
                                }
                            }
                            _6023 = _6016 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _6027 = _6023;
                        _6024 = true;
                        break;
                    }
                    else
                    {
                        _6027 = _6030;
                        _6024 = false;
                        break;
                    }
                }
                if (_6024)
                {
                    _6026 = _6027;
                    break;
                }
                _6026 = 1.0;
                break;
            } while(false);
            _6157 = _6026;
            _6139 = _5960;
            _6111 = _5911 + (_6010 * _6026);
        }
        else
        {
            uint _2878 = LightIndexList_1._data[_2833] - _1406.ClusterGridData.numPointLights;
            vec3 _4752 = SpotLightsBuffer_1._data[_2878].position - i_worldPos;
            float _4754 = length(_4752);
            vec3 _4759 = _4752 / vec3(max(_4754, 9.9999997473787516355514526367188e-05));
            float _4793 = _4754 / SpotLightsBuffer_1._data[_2878].radius;
            float _4796 = _4793 * _4793;
            float _4801 = clamp(1.0 - (_4796 * _4796), 0.0, 1.0);
            float _4818 = clamp((dot(-_4759, SpotLightsBuffer_1._data[_2878].direction) * SpotLightsBuffer_1._data[_2878].spotScale) + SpotLightsBuffer_1._data[_2878].spotOffset, 0.0, 1.0);
            vec3 _5940;
            do
            {
                float _4848 = clamp(dot(_2275, _4759), 0.0, 1.0);
                if (_4848 <= 0.0)
                {
                    _5940 = vec3(0.0);
                    break;
                }
                vec3 _4857 = normalize(_2280 + _4759);
                float _4863 = max(dot(_2275, _2280), 9.9999997473787516355514526367188e-05);
                float _4868 = clamp(dot(_2275, _4857), 0.0, 1.0);
                vec3 _4879 = mix(vec3(0.039999999105930328369140625), _5794, vec3(i_material.x));
                float _4923 = clamp(1.0 - clamp(dot(_2280, _4857), 0.0, 1.0), 0.0, 1.0);
                float _4926 = _4923 * _4923;
                vec3 _4937 = _4879 + ((vec3(1.0) - _4879) * ((_4926 * _4926) * _4923));
                float _4945 = max(_2287, 0.04500000178813934326171875);
                float _4948 = _4945 * _4945;
                float _4951 = _4948 * _4948;
                float _4959 = max(((_4868 * _4868) * (_4951 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4973 = max(_2287, 0.04500000178813934326171875);
                float _4980 = ((_4973 * _4973) * _4973) * _4973;
                _5940 = ((((((vec3(1.0) - _4937) * (1.0 - i_material.x)) * _5794) * vec3(0.3183098733425140380859375)) + (_4937 * ((_4951 / ((3.1415927410125732421875 * _4959) * _4959)) * (0.5 / max((_4848 * sqrt(((_4863 * _4863) * (1.0 - _4980)) + _4980)) + (_4863 * sqrt(((_4848 * _4848) * (1.0 - _4980)) + _4980)), 9.9999997473787516355514526367188e-06))))) * ((SpotLightsBuffer_1._data[_2878].color * SpotLightsBuffer_1._data[_2878].intensity) * (((_4801 * _4801) / max(_4754 * _4754, 9.9999997473787516355514526367188e-05)) * (_4818 * _4818)))) * _4848;
                break;
            } while(false);
            uint _2906 = LightIndexList_1._data[_2833] - _1406.ClusterGridData.numPointLights;
            float _5956;
            do
            {
                bool _5954;
                float _5957;
                uint _5941 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_5941 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_5941].posType.w) != 1u)
                        {
                            uint _5053 = _5941 + uint(1);
                            _5941 = _5053;
                            continue;
                        }
                        vec3 _5039 = _1055.PointShadows[_5941].posType.xyz - SpotLightsBuffer_1._data[_2906].position;
                        if (dot(_5039, _5039) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _5053 = _5941 + uint(1);
                            _5941 = _5053;
                            continue;
                        }
                        float _5953;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _5953 = 1.0;
                                break;
                            }
                            _5394[0] = spvWorkaroundRowMajor(_1055.PointShadows[_5941]).vp[0];
                            _5394[1] = spvWorkaroundRowMajor(_1055.PointShadows[_5941]).vp[1];
                            _5394[2] = spvWorkaroundRowMajor(_1055.PointShadows[_5941]).vp[2];
                            _5394[3] = spvWorkaroundRowMajor(_1055.PointShadows[_5941]).vp[3];
                            _5394[4] = spvWorkaroundRowMajor(_1055.PointShadows[_5941]).vp[4];
                            _5394[5] = spvWorkaroundRowMajor(_1055.PointShadows[_5941]).vp[5];
                            vec3 _5116 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_5941]).posType.xyz;
                            uint _5942;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_5941]).posType.w < 0.5)
                            {
                                vec3 _5122 = abs(_5116);
                                uint _5943;
                                if ((_5122.x >= _5122.y) && (_5122.x >= _5122.z))
                                {
                                    _5943 = (_5116.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _5944;
                                    if (_5122.y >= _5122.z)
                                    {
                                        _5944 = (_5116.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _5944 = (_5116.z > 0.0) ? 4u : 5u;
                                    }
                                    _5943 = _5944;
                                }
                                _5942 = _5943;
                            }
                            else
                            {
                                _5942 = 0u;
                            }
                            if (_5942 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_5941]).params.y))
                            {
                                _5953 = 1.0;
                                break;
                            }
                            vec4 _5182 = vec4(i_worldPos + (_2275 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_5116)))), 1.0) * _5394[_5942];
                            if (_5182.w <= spvWorkaroundRowMajor(_1055.PointShadows[_5941]).params.x)
                            {
                                _5953 = 1.0;
                                break;
                            }
                            float _5194 = _5182.x / _5182.w;
                            float _5199 = _5182.y / _5182.w;
                            if ((abs(_5194) > 1.0) || (abs(_5199) > 1.0))
                            {
                                _5953 = 1.0;
                                break;
                            }
                            int _5211 = int(spvWorkaroundRowMajor(_1055.PointShadows[_5941]).params.z);
                            int _5223 = clamp(int(roundEven((((_5194 * 0.5) + 0.5) * float(_5211)) - 0.5)), 0, _5211 - 1);
                            int _5235 = clamp(int(roundEven(((0.5 - (_5199 * 0.5)) * float(_5211)) - 0.5)), 0, _5211 - 1);
                            int _5239 = int(_5942) * _5211;
                            int _5243 = (_5239 + _5211) - 1;
                            float _5245 = _5182.w;
                            float _5248 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _5245);
                            float _5946;
                            _5946 = 0.0;
                            float _5951;
                            SPIRV_CROSS_UNROLL
                            for (int _5945 = -1; _5945 <= 1; _5946 = _5951, _5945++)
                            {
                                _5951 = _5946;
                                float _5288;
                                SPIRV_CROSS_UNROLL
                                for (int _5947 = -1; _5947 <= 1; _5951 = _5288, _5947++)
                                {
                                    int _5264 = clamp(_5223 + _5947, 0, _5211 - 1);
                                    int _5276 = clamp(clamp(_5235 + _5945, 0, _5211 - 1) + _5239, _5239, _5243);
                                    float _5949;
                                    do
                                    {
                                        if (_5941 == 0u)
                                        {
                                            _5949 = texelFetch(PointShadowMap0, ivec3(_5264, _5276, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5941 == 1u)
                                        {
                                            _5949 = texelFetch(PointShadowMap1, ivec3(_5264, _5276, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5941 == 2u)
                                        {
                                            _5949 = texelFetch(PointShadowMap2, ivec3(_5264, _5276, 0).xy, 0).x;
                                            break;
                                        }
                                        _5949 = texelFetch(PointShadowMap3, ivec3(_5264, _5276, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _5288 = _5951 + float(_5949 >= (_5245 - _5248));
                                }
                            }
                            _5953 = _5946 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _5957 = _5953;
                        _5954 = true;
                        break;
                    }
                    else
                    {
                        _5957 = _5960;
                        _5954 = false;
                        break;
                    }
                }
                if (_5954)
                {
                    _5956 = _5957;
                    break;
                }
                _5956 = 1.0;
                break;
            } while(false);
            _6157 = _6030;
            _6139 = _5956;
            _6111 = _5911 + (_5940 * _5956);
        }
    }
    vec3 _2382 = (((((((vec3(1.0) - min(_2328, vec3(1.0))) * (1.0 - i_material.x)) * _5794) * _2630) + (_2328 * _2717)) * texelFetch(AoMap, ivec3(ivec2(gl_FragCoord.xy), 0).xy, 0).x) + _5911) + i_emissive;
    vec3 _5939;
    if (i_fogParams.z > 0.0)
    {
        _5939 = mix(_2382, i_fogColor.xyz, vec3(clamp((length(i_toCam) - i_fogParams.x) / max(i_fogParams.y - i_fogParams.x, 0.001000000047497451305389404296875), 0.0, 1.0) * i_fogParams.z));
    }
    else
    {
        _5939 = _2382;
    }
    _entryPointOutput = vec4(clamp((_5939 * ((_5939 * 2.5099999904632568359375) + vec3(0.02999999932944774627685546875))) / ((_5939 * ((_5939 * 2.4300000667572021484375) + vec3(0.589999973773956298828125))) + vec3(0.14000000059604644775390625)), vec3(0.0), vec3(1.0)), i_baseColor.w);
}

