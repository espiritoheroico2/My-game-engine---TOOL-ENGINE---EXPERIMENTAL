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

float _5867;

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
layout(location = 0) out vec4 _entryPointOutput;

PointShadowLight spvWorkaroundRowMajor(PointShadowLight wrap) { return wrap; }

void main()
{
    vec3 _5699;
    if (i_texInfo.z > 0.5)
    {
        _5699 = i_baseColor.xyz * texelFetch(AlbedoMap, ivec3(clamp(int(fract(i_uv.x) * i_texInfo.x), 0, int(i_texInfo.x) - 1), clamp(int(fract(i_uv.y) * i_texInfo.y), 0, int(i_texInfo.y) - 1), 0).xy, 0).xyz;
    }
    else
    {
        _5699 = i_baseColor.xyz;
    }
    vec3 _2228 = normalize(i_normal);
    vec3 _2233 = normalize(i_toCam);
    float _2240 = max(i_material.y, 0.04500000178813934326171875);
    float _2362 = clamp((clamp(max(dot(_2228, _2233), 9.9999997473787516355514526367188e-05), 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    float _2367 = clamp((clamp(_2240, 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    int _2369 = int(_2362);
    int _2371 = int(_2367);
    int _2374 = min((_2369 + 1), 31);
    int _2377 = min((_2371 + 1), 31);
    float _2381 = _2362 - float(_2369);
    vec2 _2450 = mix(mix(vec2(texelFetch(IblBrdfLut, ivec3(_2369, _2371, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2374, _2371, 0).xy, 0).xy), vec2(_2381)), mix(vec2(texelFetch(IblBrdfLut, ivec3(_2369, _2377, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2374, _2377, 0).xy, 0).xy), vec2(_2381)), vec2(_2367 - float(_2371)));
    vec3 _2281 = (mix(vec3(0.039999999105930328369140625), _5699, vec3(i_material.x)) * _2450.x) + vec3(_2450.y);
    float _2457 = _2240 * _2240;
    float _2460 = clamp(1.0 - _2457, 0.0, 1.0);
    vec3 _2305 = normalize(mix(_2228, (_2228 * (2.0 * dot(_2228, _2233))) - _2233, vec3(_2460 * (sqrt(_2460) + _2457))));
    vec3 _2553 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2228.y)) + (_1606.Ibl.sh[2].xyz * _2228.z)) + (_1606.Ibl.sh[3].xyz * _2228.x)) + (_1606.Ibl.sh[4].xyz * (_2228.x * _2228.y))) + (_1606.Ibl.sh[5].xyz * (_2228.y * _2228.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2228.z) * _2228.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2228.x * _2228.z))) + (_1606.Ibl.sh[8].xyz * ((_2228.x * _2228.x) - (_2228.y * _2228.y))), vec3(0.0));
    vec3 _2640 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2305.y)) + (_1606.Ibl.sh[2].xyz * _2305.z)) + (_1606.Ibl.sh[3].xyz * _2305.x)) + (_1606.Ibl.sh[4].xyz * (_2305.x * _2305.y))) + (_1606.Ibl.sh[5].xyz * (_2305.y * _2305.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2305.z) * _2305.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2305.x * _2305.z))) + (_1606.Ibl.sh[8].xyz * ((_2305.x * _2305.x) - (_2305.y * _2305.y))), vec3(0.0));
    vec3 _5817;
    if (_1353.Globals.hasSun != 0u)
    {
        vec3 _2855 = -_1363.SunLight.direction;
        vec3 _5700;
        do
        {
            float _2889 = clamp(dot(_2228, _2855), 0.0, 1.0);
            if (_2889 <= 0.0)
            {
                _5700 = vec3(0.0);
                break;
            }
            vec3 _2898 = normalize(_2233 + _2855);
            float _2904 = max(dot(_2228, _2233), 9.9999997473787516355514526367188e-05);
            float _2909 = clamp(dot(_2228, _2898), 0.0, 1.0);
            vec3 _2920 = mix(vec3(0.039999999105930328369140625), _5699, vec3(i_material.x));
            float _2964 = clamp(1.0 - clamp(dot(_2233, _2898), 0.0, 1.0), 0.0, 1.0);
            float _2967 = _2964 * _2964;
            vec3 _2978 = _2920 + ((vec3(1.0) - _2920) * ((_2967 * _2967) * _2964));
            float _2986 = max(_2240, 0.04500000178813934326171875);
            float _2989 = _2986 * _2986;
            float _2992 = _2989 * _2989;
            float _3000 = max(((_2909 * _2909) * (_2992 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
            float _3014 = max(_2240, 0.04500000178813934326171875);
            float _3021 = ((_3014 * _3014) * _3014) * _3014;
            _5700 = ((((((vec3(1.0) - _2978) * (1.0 - i_material.x)) * _5699) * vec3(0.3183098733425140380859375)) + (_2978 * ((_2992 / ((3.1415927410125732421875 * _3000) * _3000)) * (0.5 / max((_2889 * sqrt(((_2904 * _2904) * (1.0 - _3021)) + _3021)) + (_2904 * sqrt(((_2889 * _2889) * (1.0 - _3021)) + _3021)), 9.9999997473787516355514526367188e-06))))) * (_1363.SunLight.color * _1363.SunLight.intensity)) * _2889;
            break;
        } while(false);
        float _5767;
        do
        {
            uint _5701;
            _5701 = 0u;
            for (; (_5701 < _662.Csm.cascadeCount) && (i_viewDepth > _662.Csm.cascades[_5701].splitFar); )
            {
                _5701++;
                continue;
            }
            if (_5701 >= _662.Csm.cascadeCount)
            {
                _5767 = 1.0;
                break;
            }
            float _5728;
            do
            {
                vec4 _3206 = vec4(i_worldPos + (_2228 * (_662.Csm.normalBias * _662.Csm.cascades[_5701].texelWorldSize)), 1.0) * _662.Csm.cascades[_5701].viewProj;
                if (any(greaterThan(abs(_3206.xy), vec2(1.0))) || (_3206.z > 1.0))
                {
                    _5728 = 1.0;
                    break;
                }
                float _3226 = _3206.z - (_662.Csm.depthBias / _662.Csm.cascades[_5701].depthRange);
                vec2 _3239 = vec2((_3206.x * 0.5) + 0.5, 0.5 - (_3206.y * 0.5)) * _662.Csm.shadowMapSize;
                int _3242 = int(_662.Csm.shadowMapSize) - 1;
                float _5712;
                float _5715;
                if (_662.Csm.pcfTaps >= 9u)
                {
                    float _5713;
                    _5713 = 0.0;
                    float _5726;
                    SPIRV_CROSS_UNROLL
                    for (int _5711 = -1; _5711 <= 1; _5713 = _5726, _5711++)
                    {
                        _5726 = _5713;
                        float _3276;
                        SPIRV_CROSS_UNROLL
                        for (int _5722 = -1; _5722 <= 1; _5726 = _3276, _5722++)
                        {
                            ivec2 _3268 = clamp(ivec2(floor(_3239 + vec2(float(_5722), float(_5711)))), ivec2(0), ivec2(_3242));
                            float _5724;
                            do
                            {
                                if (_5701 == 0u)
                                {
                                    _5724 = texelFetch(CsmMap0, ivec3(_3268, 0).xy, 0).x;
                                    break;
                                }
                                if (_5701 == 1u)
                                {
                                    _5724 = texelFetch(CsmMap1, ivec3(_3268, 0).xy, 0).x;
                                    break;
                                }
                                if (_5701 == 2u)
                                {
                                    _5724 = texelFetch(CsmMap2, ivec3(_3268, 0).xy, 0).x;
                                    break;
                                }
                                _5724 = texelFetch(CsmMap3, ivec3(_3268, 0).xy, 0).x;
                                break;
                            } while(false);
                            _3276 = _5726 + float(_3226 <= _5724);
                        }
                    }
                    _5715 = 9.0;
                    _5712 = _5713;
                }
                else
                {
                    bool _3288 = _662.Csm.pcfTaps >= 4u;
                    float _5714;
                    if (_3288)
                    {
                        float _5709;
                        _5709 = 0.0;
                        float _5708;
                        SPIRV_CROSS_UNROLL
                        for (int _5703 = 0; _5703 < 2; _5703++, _5709 = _5708)
                        {
                            _5708 = _5709;
                            float _3321;
                            SPIRV_CROSS_UNROLL
                            for (int _5704 = 0; _5704 < 2; _5708 = _3321, _5704++)
                            {
                                ivec2 _3313 = clamp(ivec2(floor(_3239 + (vec2(float(_5704), float(_5703)) - vec2(0.5)))), ivec2(0), ivec2(_3242));
                                float _5706;
                                do
                                {
                                    if (_5701 == 0u)
                                    {
                                        _5706 = texelFetch(CsmMap0, ivec3(_3313, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5701 == 1u)
                                    {
                                        _5706 = texelFetch(CsmMap1, ivec3(_3313, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5701 == 2u)
                                    {
                                        _5706 = texelFetch(CsmMap2, ivec3(_3313, 0).xy, 0).x;
                                        break;
                                    }
                                    _5706 = texelFetch(CsmMap3, ivec3(_3313, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3321 = _5708 + float(_3226 <= _5706);
                            }
                        }
                        _5714 = _5709;
                    }
                    else
                    {
                        ivec2 _3336 = clamp(ivec2(floor(_3239)), ivec2(0), ivec2(_3242));
                        float _5702;
                        do
                        {
                            if (_5701 == 0u)
                            {
                                _5702 = texelFetch(CsmMap0, ivec3(_3336, 0).xy, 0).x;
                                break;
                            }
                            if (_5701 == 1u)
                            {
                                _5702 = texelFetch(CsmMap1, ivec3(_3336, 0).xy, 0).x;
                                break;
                            }
                            if (_5701 == 2u)
                            {
                                _5702 = texelFetch(CsmMap2, ivec3(_3336, 0).xy, 0).x;
                                break;
                            }
                            _5702 = texelFetch(CsmMap3, ivec3(_3336, 0).xy, 0).x;
                            break;
                        } while(false);
                        _5714 = float(_3226 <= _5702);
                    }
                    _5715 = _3288 ? 4.0 : 1.0;
                    _5712 = _5714;
                }
                _5728 = _5712 / _5715;
                break;
            } while(false);
            float _3112 = _662.Csm.blendFraction * (_662.Csm.cascades[_5701].splitFar - _662.Csm.cascades[_5701].splitNear);
            float _5766;
            if ((((_5701 + 1u) < _662.Csm.cascadeCount) && (_3112 > 0.0)) && (i_viewDepth > (_662.Csm.cascades[_5701].splitFar - _3112)))
            {
                uint _3140 = _5701 + 1u;
                float _5765;
                do
                {
                    vec4 _3619 = vec4(i_worldPos + (_2228 * (_662.Csm.normalBias * _662.Csm.cascades[_3140].texelWorldSize)), 1.0) * _662.Csm.cascades[_3140].viewProj;
                    if (any(greaterThan(abs(_3619.xy), vec2(1.0))) || (_3619.z > 1.0))
                    {
                        _5765 = 1.0;
                        break;
                    }
                    float _3639 = _3619.z - (_662.Csm.depthBias / _662.Csm.cascades[_3140].depthRange);
                    vec2 _3652 = vec2((_3619.x * 0.5) + 0.5, 0.5 - (_3619.y * 0.5)) * _662.Csm.shadowMapSize;
                    int _3655 = int(_662.Csm.shadowMapSize) - 1;
                    float _5749;
                    float _5752;
                    if (_662.Csm.pcfTaps >= 9u)
                    {
                        float _5750;
                        _5750 = 0.0;
                        float _5763;
                        SPIRV_CROSS_UNROLL
                        for (int _5748 = -1; _5748 <= 1; _5750 = _5763, _5748++)
                        {
                            _5763 = _5750;
                            float _3689;
                            SPIRV_CROSS_UNROLL
                            for (int _5759 = -1; _5759 <= 1; _5763 = _3689, _5759++)
                            {
                                ivec2 _3681 = clamp(ivec2(floor(_3652 + vec2(float(_5759), float(_5748)))), ivec2(0), ivec2(_3655));
                                float _5761;
                                do
                                {
                                    if (_3140 == 0u)
                                    {
                                        _5761 = texelFetch(CsmMap0, ivec3(_3681, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3140 == 1u)
                                    {
                                        _5761 = texelFetch(CsmMap1, ivec3(_3681, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3140 == 2u)
                                    {
                                        _5761 = texelFetch(CsmMap2, ivec3(_3681, 0).xy, 0).x;
                                        break;
                                    }
                                    _5761 = texelFetch(CsmMap3, ivec3(_3681, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3689 = _5763 + float(_3639 <= _5761);
                            }
                        }
                        _5752 = 9.0;
                        _5749 = _5750;
                    }
                    else
                    {
                        bool _3701 = _662.Csm.pcfTaps >= 4u;
                        float _5751;
                        if (_3701)
                        {
                            float _5746;
                            _5746 = 0.0;
                            float _5745;
                            SPIRV_CROSS_UNROLL
                            for (int _5740 = 0; _5740 < 2; _5740++, _5746 = _5745)
                            {
                                _5745 = _5746;
                                float _3734;
                                SPIRV_CROSS_UNROLL
                                for (int _5741 = 0; _5741 < 2; _5745 = _3734, _5741++)
                                {
                                    ivec2 _3726 = clamp(ivec2(floor(_3652 + (vec2(float(_5741), float(_5740)) - vec2(0.5)))), ivec2(0), ivec2(_3655));
                                    float _5743;
                                    do
                                    {
                                        if (_3140 == 0u)
                                        {
                                            _5743 = texelFetch(CsmMap0, ivec3(_3726, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3140 == 1u)
                                        {
                                            _5743 = texelFetch(CsmMap1, ivec3(_3726, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3140 == 2u)
                                        {
                                            _5743 = texelFetch(CsmMap2, ivec3(_3726, 0).xy, 0).x;
                                            break;
                                        }
                                        _5743 = texelFetch(CsmMap3, ivec3(_3726, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _3734 = _5745 + float(_3639 <= _5743);
                                }
                            }
                            _5751 = _5746;
                        }
                        else
                        {
                            ivec2 _3749 = clamp(ivec2(floor(_3652)), ivec2(0), ivec2(_3655));
                            float _5739;
                            do
                            {
                                if (_3140 == 0u)
                                {
                                    _5739 = texelFetch(CsmMap0, ivec3(_3749, 0).xy, 0).x;
                                    break;
                                }
                                if (_3140 == 1u)
                                {
                                    _5739 = texelFetch(CsmMap1, ivec3(_3749, 0).xy, 0).x;
                                    break;
                                }
                                if (_3140 == 2u)
                                {
                                    _5739 = texelFetch(CsmMap2, ivec3(_3749, 0).xy, 0).x;
                                    break;
                                }
                                _5739 = texelFetch(CsmMap3, ivec3(_3749, 0).xy, 0).x;
                                break;
                            } while(false);
                            _5751 = float(_3639 <= _5739);
                        }
                        _5752 = _3701 ? 4.0 : 1.0;
                        _5749 = _5751;
                    }
                    _5765 = _5749 / _5752;
                    break;
                } while(false);
                _5766 = mix(_5728, _5765, clamp((i_viewDepth - (_662.Csm.cascades[_5701].splitFar - _3112)) / _3112, 0.0, 1.0));
            }
            else
            {
                _5766 = _5728;
            }
            _5767 = _5766;
            break;
        } while(false);
        _5817 = _5700 * ((i_material.z > 0.5) ? _5767 : 1.0);
    }
    else
    {
        _5817 = vec3(0.0);
    }
    uvec2 _3996 = min((uvec2(gl_FragCoord.xy) / uvec2(_1406.ClusterGridData.tileSizePx)), uvec2(_1406.ClusterGridData.gridX - 1u, _1406.ClusterGridData.gridY - 1u));
    uint _4046 = (_3996.x + (_3996.y * _1406.ClusterGridData.gridX)) + ((uint(clamp(floor((log2(max(i_viewDepth, _1406.ClusterGridData.zNear)) * _1406.ClusterGridData.sliceScale) + _1406.ClusterGridData.sliceBias), 0.0, float(_1406.ClusterGridData.gridZ - 1u))) * _1406.ClusterGridData.gridX) * _1406.ClusterGridData.gridY);
    vec3 _5816;
    _5816 = _5817;
    mat4 _5317[6];
    mat4 _5335[6];
    vec3 _6015;
    float _6043;
    float _6061;
    float _5864;
    float _5934;
    for (uint _5815 = 0u; _5815 < LightGrid_1._data[_4046].count; _5934 = _6061, _5864 = _6043, _5816 = _6015, _5815++)
    {
        uint _2756 = LightGrid_1._data[_4046].offset + _5815;
        if (LightIndexList_1._data[_2756] < _1406.ClusterGridData.numPointLights)
        {
            vec3 _4061 = PointLightsBuffer_1._data[LightIndexList_1._data[_2756]].position - i_worldPos;
            float _4063 = length(_4061);
            vec3 _4068 = _4061 / vec3(max(_4063, 9.9999997473787516355514526367188e-05));
            float _4090 = _4063 / PointLightsBuffer_1._data[LightIndexList_1._data[_2756]].radius;
            float _4093 = _4090 * _4090;
            float _4098 = clamp(1.0 - (_4093 * _4093), 0.0, 1.0);
            vec3 _5914;
            do
            {
                float _4133 = clamp(dot(_2228, _4068), 0.0, 1.0);
                if (_4133 <= 0.0)
                {
                    _5914 = vec3(0.0);
                    break;
                }
                vec3 _4142 = normalize(_2233 + _4068);
                float _4148 = max(dot(_2228, _2233), 9.9999997473787516355514526367188e-05);
                float _4153 = clamp(dot(_2228, _4142), 0.0, 1.0);
                vec3 _4164 = mix(vec3(0.039999999105930328369140625), _5699, vec3(i_material.x));
                float _4208 = clamp(1.0 - clamp(dot(_2233, _4142), 0.0, 1.0), 0.0, 1.0);
                float _4211 = _4208 * _4208;
                vec3 _4222 = _4164 + ((vec3(1.0) - _4164) * ((_4211 * _4211) * _4208));
                float _4230 = max(_2240, 0.04500000178813934326171875);
                float _4233 = _4230 * _4230;
                float _4236 = _4233 * _4233;
                float _4244 = max(((_4153 * _4153) * (_4236 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4258 = max(_2240, 0.04500000178813934326171875);
                float _4265 = ((_4258 * _4258) * _4258) * _4258;
                _5914 = ((((((vec3(1.0) - _4222) * (1.0 - i_material.x)) * _5699) * vec3(0.3183098733425140380859375)) + (_4222 * ((_4236 / ((3.1415927410125732421875 * _4244) * _4244)) * (0.5 / max((_4133 * sqrt(((_4148 * _4148) * (1.0 - _4265)) + _4265)) + (_4148 * sqrt(((_4133 * _4133) * (1.0 - _4265)) + _4265)), 9.9999997473787516355514526367188e-06))))) * ((PointLightsBuffer_1._data[LightIndexList_1._data[_2756]].color * PointLightsBuffer_1._data[LightIndexList_1._data[_2756]].intensity) * ((_4098 * _4098) / max(_4063 * _4063, 9.9999997473787516355514526367188e-05)))) * _4133;
                break;
            } while(false);
            float _5930;
            do
            {
                bool _5928;
                float _5931;
                uint _5915 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_5915 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_5915].posType.w) != 0u)
                        {
                            uint _4338 = _5915 + uint(1);
                            _5915 = _4338;
                            continue;
                        }
                        vec3 _4324 = _1055.PointShadows[_5915].posType.xyz - PointLightsBuffer_1._data[LightIndexList_1._data[_2756]].position;
                        if (dot(_4324, _4324) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _4338 = _5915 + uint(1);
                            _5915 = _4338;
                            continue;
                        }
                        float _5927;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _5927 = 1.0;
                                break;
                            }
                            _5335[0] = spvWorkaroundRowMajor(_1055.PointShadows[_5915]).vp[0];
                            _5335[1] = spvWorkaroundRowMajor(_1055.PointShadows[_5915]).vp[1];
                            _5335[2] = spvWorkaroundRowMajor(_1055.PointShadows[_5915]).vp[2];
                            _5335[3] = spvWorkaroundRowMajor(_1055.PointShadows[_5915]).vp[3];
                            _5335[4] = spvWorkaroundRowMajor(_1055.PointShadows[_5915]).vp[4];
                            _5335[5] = spvWorkaroundRowMajor(_1055.PointShadows[_5915]).vp[5];
                            vec3 _4401 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_5915]).posType.xyz;
                            uint _5916;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_5915]).posType.w < 0.5)
                            {
                                vec3 _4407 = abs(_4401);
                                uint _5917;
                                if ((_4407.x >= _4407.y) && (_4407.x >= _4407.z))
                                {
                                    _5917 = (_4401.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _5918;
                                    if (_4407.y >= _4407.z)
                                    {
                                        _5918 = (_4401.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _5918 = (_4401.z > 0.0) ? 4u : 5u;
                                    }
                                    _5917 = _5918;
                                }
                                _5916 = _5917;
                            }
                            else
                            {
                                _5916 = 0u;
                            }
                            if (_5916 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_5915]).params.y))
                            {
                                _5927 = 1.0;
                                break;
                            }
                            vec4 _4467 = vec4(i_worldPos + (_2228 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_4401)))), 1.0) * _5335[_5916];
                            if (_4467.w <= spvWorkaroundRowMajor(_1055.PointShadows[_5915]).params.x)
                            {
                                _5927 = 1.0;
                                break;
                            }
                            float _4479 = _4467.x / _4467.w;
                            float _4484 = _4467.y / _4467.w;
                            if ((abs(_4479) > 1.0) || (abs(_4484) > 1.0))
                            {
                                _5927 = 1.0;
                                break;
                            }
                            int _4496 = int(spvWorkaroundRowMajor(_1055.PointShadows[_5915]).params.z);
                            int _4508 = clamp(int(roundEven((((_4479 * 0.5) + 0.5) * float(_4496)) - 0.5)), 0, _4496 - 1);
                            int _4520 = clamp(int(roundEven(((0.5 - (_4484 * 0.5)) * float(_4496)) - 0.5)), 0, _4496 - 1);
                            int _4524 = int(_5916) * _4496;
                            int _4528 = (_4524 + _4496) - 1;
                            float _4530 = _4467.w;
                            float _4533 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _4530);
                            float _5920;
                            _5920 = 0.0;
                            float _5925;
                            SPIRV_CROSS_UNROLL
                            for (int _5919 = -1; _5919 <= 1; _5920 = _5925, _5919++)
                            {
                                _5925 = _5920;
                                float _4573;
                                SPIRV_CROSS_UNROLL
                                for (int _5921 = -1; _5921 <= 1; _5925 = _4573, _5921++)
                                {
                                    int _4549 = clamp(_4508 + _5921, 0, _4496 - 1);
                                    int _4561 = clamp(clamp(_4520 + _5919, 0, _4496 - 1) + _4524, _4524, _4528);
                                    float _5923;
                                    do
                                    {
                                        if (_5915 == 0u)
                                        {
                                            _5923 = texelFetch(PointShadowMap0, ivec3(_4549, _4561, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5915 == 1u)
                                        {
                                            _5923 = texelFetch(PointShadowMap1, ivec3(_4549, _4561, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5915 == 2u)
                                        {
                                            _5923 = texelFetch(PointShadowMap2, ivec3(_4549, _4561, 0).xy, 0).x;
                                            break;
                                        }
                                        _5923 = texelFetch(PointShadowMap3, ivec3(_4549, _4561, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _4573 = _5925 + float(_5923 >= (_4530 - _4533));
                                }
                            }
                            _5927 = _5920 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _5931 = _5927;
                        _5928 = true;
                        break;
                    }
                    else
                    {
                        _5931 = _5934;
                        _5928 = false;
                        break;
                    }
                }
                if (_5928)
                {
                    _5930 = _5931;
                    break;
                }
                _5930 = 1.0;
                break;
            } while(false);
            _6061 = _5930;
            _6043 = _5864;
            _6015 = _5816 + (_5914 * _5930);
        }
        else
        {
            uint _2801 = LightIndexList_1._data[_2756] - _1406.ClusterGridData.numPointLights;
            vec3 _4675 = SpotLightsBuffer_1._data[_2801].position - i_worldPos;
            float _4677 = length(_4675);
            vec3 _4682 = _4675 / vec3(max(_4677, 9.9999997473787516355514526367188e-05));
            float _4716 = _4677 / SpotLightsBuffer_1._data[_2801].radius;
            float _4719 = _4716 * _4716;
            float _4724 = clamp(1.0 - (_4719 * _4719), 0.0, 1.0);
            float _4741 = clamp((dot(-_4682, SpotLightsBuffer_1._data[_2801].direction) * SpotLightsBuffer_1._data[_2801].spotScale) + SpotLightsBuffer_1._data[_2801].spotOffset, 0.0, 1.0);
            vec3 _5844;
            do
            {
                float _4771 = clamp(dot(_2228, _4682), 0.0, 1.0);
                if (_4771 <= 0.0)
                {
                    _5844 = vec3(0.0);
                    break;
                }
                vec3 _4780 = normalize(_2233 + _4682);
                float _4786 = max(dot(_2228, _2233), 9.9999997473787516355514526367188e-05);
                float _4791 = clamp(dot(_2228, _4780), 0.0, 1.0);
                vec3 _4802 = mix(vec3(0.039999999105930328369140625), _5699, vec3(i_material.x));
                float _4846 = clamp(1.0 - clamp(dot(_2233, _4780), 0.0, 1.0), 0.0, 1.0);
                float _4849 = _4846 * _4846;
                vec3 _4860 = _4802 + ((vec3(1.0) - _4802) * ((_4849 * _4849) * _4846));
                float _4868 = max(_2240, 0.04500000178813934326171875);
                float _4871 = _4868 * _4868;
                float _4874 = _4871 * _4871;
                float _4882 = max(((_4791 * _4791) * (_4874 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4896 = max(_2240, 0.04500000178813934326171875);
                float _4903 = ((_4896 * _4896) * _4896) * _4896;
                _5844 = ((((((vec3(1.0) - _4860) * (1.0 - i_material.x)) * _5699) * vec3(0.3183098733425140380859375)) + (_4860 * ((_4874 / ((3.1415927410125732421875 * _4882) * _4882)) * (0.5 / max((_4771 * sqrt(((_4786 * _4786) * (1.0 - _4903)) + _4903)) + (_4786 * sqrt(((_4771 * _4771) * (1.0 - _4903)) + _4903)), 9.9999997473787516355514526367188e-06))))) * ((SpotLightsBuffer_1._data[_2801].color * SpotLightsBuffer_1._data[_2801].intensity) * (((_4724 * _4724) / max(_4677 * _4677, 9.9999997473787516355514526367188e-05)) * (_4741 * _4741)))) * _4771;
                break;
            } while(false);
            uint _2829 = LightIndexList_1._data[_2756] - _1406.ClusterGridData.numPointLights;
            float _5860;
            do
            {
                bool _5858;
                float _5861;
                uint _5845 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_5845 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_5845].posType.w) != 1u)
                        {
                            uint _4976 = _5845 + uint(1);
                            _5845 = _4976;
                            continue;
                        }
                        vec3 _4962 = _1055.PointShadows[_5845].posType.xyz - SpotLightsBuffer_1._data[_2829].position;
                        if (dot(_4962, _4962) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _4976 = _5845 + uint(1);
                            _5845 = _4976;
                            continue;
                        }
                        float _5857;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _5857 = 1.0;
                                break;
                            }
                            _5317[0] = spvWorkaroundRowMajor(_1055.PointShadows[_5845]).vp[0];
                            _5317[1] = spvWorkaroundRowMajor(_1055.PointShadows[_5845]).vp[1];
                            _5317[2] = spvWorkaroundRowMajor(_1055.PointShadows[_5845]).vp[2];
                            _5317[3] = spvWorkaroundRowMajor(_1055.PointShadows[_5845]).vp[3];
                            _5317[4] = spvWorkaroundRowMajor(_1055.PointShadows[_5845]).vp[4];
                            _5317[5] = spvWorkaroundRowMajor(_1055.PointShadows[_5845]).vp[5];
                            vec3 _5039 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_5845]).posType.xyz;
                            uint _5846;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_5845]).posType.w < 0.5)
                            {
                                vec3 _5045 = abs(_5039);
                                uint _5847;
                                if ((_5045.x >= _5045.y) && (_5045.x >= _5045.z))
                                {
                                    _5847 = (_5039.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _5848;
                                    if (_5045.y >= _5045.z)
                                    {
                                        _5848 = (_5039.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _5848 = (_5039.z > 0.0) ? 4u : 5u;
                                    }
                                    _5847 = _5848;
                                }
                                _5846 = _5847;
                            }
                            else
                            {
                                _5846 = 0u;
                            }
                            if (_5846 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_5845]).params.y))
                            {
                                _5857 = 1.0;
                                break;
                            }
                            vec4 _5105 = vec4(i_worldPos + (_2228 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_5039)))), 1.0) * _5317[_5846];
                            if (_5105.w <= spvWorkaroundRowMajor(_1055.PointShadows[_5845]).params.x)
                            {
                                _5857 = 1.0;
                                break;
                            }
                            float _5117 = _5105.x / _5105.w;
                            float _5122 = _5105.y / _5105.w;
                            if ((abs(_5117) > 1.0) || (abs(_5122) > 1.0))
                            {
                                _5857 = 1.0;
                                break;
                            }
                            int _5134 = int(spvWorkaroundRowMajor(_1055.PointShadows[_5845]).params.z);
                            int _5146 = clamp(int(roundEven((((_5117 * 0.5) + 0.5) * float(_5134)) - 0.5)), 0, _5134 - 1);
                            int _5158 = clamp(int(roundEven(((0.5 - (_5122 * 0.5)) * float(_5134)) - 0.5)), 0, _5134 - 1);
                            int _5162 = int(_5846) * _5134;
                            int _5166 = (_5162 + _5134) - 1;
                            float _5168 = _5105.w;
                            float _5171 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _5168);
                            float _5850;
                            _5850 = 0.0;
                            float _5855;
                            SPIRV_CROSS_UNROLL
                            for (int _5849 = -1; _5849 <= 1; _5850 = _5855, _5849++)
                            {
                                _5855 = _5850;
                                float _5211;
                                SPIRV_CROSS_UNROLL
                                for (int _5851 = -1; _5851 <= 1; _5855 = _5211, _5851++)
                                {
                                    int _5187 = clamp(_5146 + _5851, 0, _5134 - 1);
                                    int _5199 = clamp(clamp(_5158 + _5849, 0, _5134 - 1) + _5162, _5162, _5166);
                                    float _5853;
                                    do
                                    {
                                        if (_5845 == 0u)
                                        {
                                            _5853 = texelFetch(PointShadowMap0, ivec3(_5187, _5199, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5845 == 1u)
                                        {
                                            _5853 = texelFetch(PointShadowMap1, ivec3(_5187, _5199, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5845 == 2u)
                                        {
                                            _5853 = texelFetch(PointShadowMap2, ivec3(_5187, _5199, 0).xy, 0).x;
                                            break;
                                        }
                                        _5853 = texelFetch(PointShadowMap3, ivec3(_5187, _5199, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _5211 = _5855 + float(_5853 >= (_5168 - _5171));
                                }
                            }
                            _5857 = _5850 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _5861 = _5857;
                        _5858 = true;
                        break;
                    }
                    else
                    {
                        _5861 = _5864;
                        _5858 = false;
                        break;
                    }
                }
                if (_5858)
                {
                    _5860 = _5861;
                    break;
                }
                _5860 = 1.0;
                break;
            } while(false);
            _6061 = _5934;
            _6043 = _5860;
            _6015 = _5816 + (_5844 * _5860);
        }
    }
    vec3 _2335 = (((((((vec3(1.0) - min(_2281, vec3(1.0))) * (1.0 - i_material.x)) * _5699) * _2553) + (_2281 * _2640)) * texelFetch(AoMap, ivec3(ivec2(gl_FragCoord.xy), 0).xy, 0).x) + _5816) + i_emissive;
    _entryPointOutput = vec4(clamp((_2335 * ((_2335 * 2.5099999904632568359375) + vec3(0.02999999932944774627685546875))) / ((_2335 * ((_2335 * 2.4300000667572021484375) + vec3(0.589999973773956298828125))) + vec3(0.14000000059604644775390625)), vec3(0.0), vec3(1.0)), i_baseColor.w);
}

