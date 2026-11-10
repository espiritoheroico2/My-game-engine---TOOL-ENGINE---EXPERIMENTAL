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

float _5933;

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

layout(binding = 6, std140) uniform ObjectBuffer
{
    layout(row_major) mat4 world;
    vec4 baseColor;
    float metallic;
    float roughness;
    float receiveShadow;
    float _pad1;
    vec4 texInfo;
    vec4 emissive;
    vec4 fogColor;
    vec4 fogParams;
} _2042;

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
    vec3 _5764;
    if (i_texInfo.z > 0.5)
    {
        _5764 = i_baseColor.xyz * texelFetch(AlbedoMap, ivec3(clamp(int(fract(i_uv.x) * i_texInfo.x), 0, int(i_texInfo.x) - 1), clamp(int(fract(i_uv.y) * i_texInfo.y), 0, int(i_texInfo.y) - 1), 0).xy, 0).xyz;
    }
    else
    {
        _5764 = i_baseColor.xyz;
    }
    vec3 _2263 = normalize(i_normal);
    vec3 _2268 = normalize(i_toCam);
    float _2275 = max(i_material.y, 0.04500000178813934326171875);
    float _2427 = clamp((clamp(max(dot(_2263, _2268), 9.9999997473787516355514526367188e-05), 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    float _2432 = clamp((clamp(_2275, 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    int _2434 = int(_2427);
    int _2436 = int(_2432);
    int _2439 = min((_2434 + 1), 31);
    int _2442 = min((_2436 + 1), 31);
    float _2446 = _2427 - float(_2434);
    vec2 _2515 = mix(mix(vec2(texelFetch(IblBrdfLut, ivec3(_2434, _2436, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2439, _2436, 0).xy, 0).xy), vec2(_2446)), mix(vec2(texelFetch(IblBrdfLut, ivec3(_2434, _2442, 0).xy, 0).xy), vec2(texelFetch(IblBrdfLut, ivec3(_2439, _2442, 0).xy, 0).xy), vec2(_2446)), vec2(_2432 - float(_2436)));
    vec3 _2316 = (mix(vec3(0.039999999105930328369140625), _5764, vec3(i_material.x)) * _2515.x) + vec3(_2515.y);
    float _2522 = _2275 * _2275;
    float _2525 = clamp(1.0 - _2522, 0.0, 1.0);
    vec3 _2340 = normalize(mix(_2263, (_2263 * (2.0 * dot(_2263, _2268))) - _2268, vec3(_2525 * (sqrt(_2525) + _2522))));
    vec3 _2618 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2263.y)) + (_1606.Ibl.sh[2].xyz * _2263.z)) + (_1606.Ibl.sh[3].xyz * _2263.x)) + (_1606.Ibl.sh[4].xyz * (_2263.x * _2263.y))) + (_1606.Ibl.sh[5].xyz * (_2263.y * _2263.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2263.z) * _2263.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2263.x * _2263.z))) + (_1606.Ibl.sh[8].xyz * ((_2263.x * _2263.x) - (_2263.y * _2263.y))), vec3(0.0));
    vec3 _2705 = max((((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * _2340.y)) + (_1606.Ibl.sh[2].xyz * _2340.z)) + (_1606.Ibl.sh[3].xyz * _2340.x)) + (_1606.Ibl.sh[4].xyz * (_2340.x * _2340.y))) + (_1606.Ibl.sh[5].xyz * (_2340.y * _2340.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * _2340.z) * _2340.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (_2340.x * _2340.z))) + (_1606.Ibl.sh[8].xyz * ((_2340.x * _2340.x) - (_2340.y * _2340.y))), vec3(0.0));
    vec3 _5882;
    if (_1353.Globals.hasSun != 0u)
    {
        vec3 _2920 = -_1363.SunLight.direction;
        vec3 _5765;
        do
        {
            float _2954 = clamp(dot(_2263, _2920), 0.0, 1.0);
            if (_2954 <= 0.0)
            {
                _5765 = vec3(0.0);
                break;
            }
            vec3 _2963 = normalize(_2268 + _2920);
            float _2969 = max(dot(_2263, _2268), 9.9999997473787516355514526367188e-05);
            float _2974 = clamp(dot(_2263, _2963), 0.0, 1.0);
            vec3 _2985 = mix(vec3(0.039999999105930328369140625), _5764, vec3(i_material.x));
            float _3029 = clamp(1.0 - clamp(dot(_2268, _2963), 0.0, 1.0), 0.0, 1.0);
            float _3032 = _3029 * _3029;
            vec3 _3043 = _2985 + ((vec3(1.0) - _2985) * ((_3032 * _3032) * _3029));
            float _3051 = max(_2275, 0.04500000178813934326171875);
            float _3054 = _3051 * _3051;
            float _3057 = _3054 * _3054;
            float _3065 = max(((_2974 * _2974) * (_3057 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
            float _3079 = max(_2275, 0.04500000178813934326171875);
            float _3086 = ((_3079 * _3079) * _3079) * _3079;
            _5765 = ((((((vec3(1.0) - _3043) * (1.0 - i_material.x)) * _5764) * vec3(0.3183098733425140380859375)) + (_3043 * ((_3057 / ((3.1415927410125732421875 * _3065) * _3065)) * (0.5 / max((_2954 * sqrt(((_2969 * _2969) * (1.0 - _3086)) + _3086)) + (_2969 * sqrt(((_2954 * _2954) * (1.0 - _3086)) + _3086)), 9.9999997473787516355514526367188e-06))))) * (_1363.SunLight.color * _1363.SunLight.intensity)) * _2954;
            break;
        } while(false);
        float _5832;
        do
        {
            uint _5766;
            _5766 = 0u;
            for (; (_5766 < _662.Csm.cascadeCount) && (i_viewDepth > _662.Csm.cascades[_5766].splitFar); )
            {
                _5766++;
                continue;
            }
            if (_5766 >= _662.Csm.cascadeCount)
            {
                _5832 = 1.0;
                break;
            }
            float _5793;
            do
            {
                vec4 _3271 = vec4(i_worldPos + (_2263 * (_662.Csm.normalBias * _662.Csm.cascades[_5766].texelWorldSize)), 1.0) * _662.Csm.cascades[_5766].viewProj;
                if (any(greaterThan(abs(_3271.xy), vec2(1.0))) || (_3271.z > 1.0))
                {
                    _5793 = 1.0;
                    break;
                }
                float _3291 = _3271.z - (_662.Csm.depthBias / _662.Csm.cascades[_5766].depthRange);
                vec2 _3304 = vec2((_3271.x * 0.5) + 0.5, 0.5 - (_3271.y * 0.5)) * _662.Csm.shadowMapSize;
                int _3307 = int(_662.Csm.shadowMapSize) - 1;
                float _5777;
                float _5780;
                if (_662.Csm.pcfTaps >= 9u)
                {
                    float _5778;
                    _5778 = 0.0;
                    float _5791;
                    SPIRV_CROSS_UNROLL
                    for (int _5776 = -1; _5776 <= 1; _5778 = _5791, _5776++)
                    {
                        _5791 = _5778;
                        float _3341;
                        SPIRV_CROSS_UNROLL
                        for (int _5787 = -1; _5787 <= 1; _5791 = _3341, _5787++)
                        {
                            ivec2 _3333 = clamp(ivec2(floor(_3304 + vec2(float(_5787), float(_5776)))), ivec2(0), ivec2(_3307));
                            float _5789;
                            do
                            {
                                if (_5766 == 0u)
                                {
                                    _5789 = texelFetch(CsmMap0, ivec3(_3333, 0).xy, 0).x;
                                    break;
                                }
                                if (_5766 == 1u)
                                {
                                    _5789 = texelFetch(CsmMap1, ivec3(_3333, 0).xy, 0).x;
                                    break;
                                }
                                if (_5766 == 2u)
                                {
                                    _5789 = texelFetch(CsmMap2, ivec3(_3333, 0).xy, 0).x;
                                    break;
                                }
                                _5789 = texelFetch(CsmMap3, ivec3(_3333, 0).xy, 0).x;
                                break;
                            } while(false);
                            _3341 = _5791 + float(_3291 <= _5789);
                        }
                    }
                    _5780 = 9.0;
                    _5777 = _5778;
                }
                else
                {
                    bool _3353 = _662.Csm.pcfTaps >= 4u;
                    float _5779;
                    if (_3353)
                    {
                        float _5774;
                        _5774 = 0.0;
                        float _5773;
                        SPIRV_CROSS_UNROLL
                        for (int _5768 = 0; _5768 < 2; _5768++, _5774 = _5773)
                        {
                            _5773 = _5774;
                            float _3386;
                            SPIRV_CROSS_UNROLL
                            for (int _5769 = 0; _5769 < 2; _5773 = _3386, _5769++)
                            {
                                ivec2 _3378 = clamp(ivec2(floor(_3304 + (vec2(float(_5769), float(_5768)) - vec2(0.5)))), ivec2(0), ivec2(_3307));
                                float _5771;
                                do
                                {
                                    if (_5766 == 0u)
                                    {
                                        _5771 = texelFetch(CsmMap0, ivec3(_3378, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5766 == 1u)
                                    {
                                        _5771 = texelFetch(CsmMap1, ivec3(_3378, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_5766 == 2u)
                                    {
                                        _5771 = texelFetch(CsmMap2, ivec3(_3378, 0).xy, 0).x;
                                        break;
                                    }
                                    _5771 = texelFetch(CsmMap3, ivec3(_3378, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3386 = _5773 + float(_3291 <= _5771);
                            }
                        }
                        _5779 = _5774;
                    }
                    else
                    {
                        ivec2 _3401 = clamp(ivec2(floor(_3304)), ivec2(0), ivec2(_3307));
                        float _5767;
                        do
                        {
                            if (_5766 == 0u)
                            {
                                _5767 = texelFetch(CsmMap0, ivec3(_3401, 0).xy, 0).x;
                                break;
                            }
                            if (_5766 == 1u)
                            {
                                _5767 = texelFetch(CsmMap1, ivec3(_3401, 0).xy, 0).x;
                                break;
                            }
                            if (_5766 == 2u)
                            {
                                _5767 = texelFetch(CsmMap2, ivec3(_3401, 0).xy, 0).x;
                                break;
                            }
                            _5767 = texelFetch(CsmMap3, ivec3(_3401, 0).xy, 0).x;
                            break;
                        } while(false);
                        _5779 = float(_3291 <= _5767);
                    }
                    _5780 = _3353 ? 4.0 : 1.0;
                    _5777 = _5779;
                }
                _5793 = _5777 / _5780;
                break;
            } while(false);
            float _3177 = _662.Csm.blendFraction * (_662.Csm.cascades[_5766].splitFar - _662.Csm.cascades[_5766].splitNear);
            float _5831;
            if ((((_5766 + 1u) < _662.Csm.cascadeCount) && (_3177 > 0.0)) && (i_viewDepth > (_662.Csm.cascades[_5766].splitFar - _3177)))
            {
                uint _3205 = _5766 + 1u;
                float _5830;
                do
                {
                    vec4 _3684 = vec4(i_worldPos + (_2263 * (_662.Csm.normalBias * _662.Csm.cascades[_3205].texelWorldSize)), 1.0) * _662.Csm.cascades[_3205].viewProj;
                    if (any(greaterThan(abs(_3684.xy), vec2(1.0))) || (_3684.z > 1.0))
                    {
                        _5830 = 1.0;
                        break;
                    }
                    float _3704 = _3684.z - (_662.Csm.depthBias / _662.Csm.cascades[_3205].depthRange);
                    vec2 _3717 = vec2((_3684.x * 0.5) + 0.5, 0.5 - (_3684.y * 0.5)) * _662.Csm.shadowMapSize;
                    int _3720 = int(_662.Csm.shadowMapSize) - 1;
                    float _5814;
                    float _5817;
                    if (_662.Csm.pcfTaps >= 9u)
                    {
                        float _5815;
                        _5815 = 0.0;
                        float _5828;
                        SPIRV_CROSS_UNROLL
                        for (int _5813 = -1; _5813 <= 1; _5815 = _5828, _5813++)
                        {
                            _5828 = _5815;
                            float _3754;
                            SPIRV_CROSS_UNROLL
                            for (int _5824 = -1; _5824 <= 1; _5828 = _3754, _5824++)
                            {
                                ivec2 _3746 = clamp(ivec2(floor(_3717 + vec2(float(_5824), float(_5813)))), ivec2(0), ivec2(_3720));
                                float _5826;
                                do
                                {
                                    if (_3205 == 0u)
                                    {
                                        _5826 = texelFetch(CsmMap0, ivec3(_3746, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3205 == 1u)
                                    {
                                        _5826 = texelFetch(CsmMap1, ivec3(_3746, 0).xy, 0).x;
                                        break;
                                    }
                                    if (_3205 == 2u)
                                    {
                                        _5826 = texelFetch(CsmMap2, ivec3(_3746, 0).xy, 0).x;
                                        break;
                                    }
                                    _5826 = texelFetch(CsmMap3, ivec3(_3746, 0).xy, 0).x;
                                    break;
                                } while(false);
                                _3754 = _5828 + float(_3704 <= _5826);
                            }
                        }
                        _5817 = 9.0;
                        _5814 = _5815;
                    }
                    else
                    {
                        bool _3766 = _662.Csm.pcfTaps >= 4u;
                        float _5816;
                        if (_3766)
                        {
                            float _5811;
                            _5811 = 0.0;
                            float _5810;
                            SPIRV_CROSS_UNROLL
                            for (int _5805 = 0; _5805 < 2; _5805++, _5811 = _5810)
                            {
                                _5810 = _5811;
                                float _3799;
                                SPIRV_CROSS_UNROLL
                                for (int _5806 = 0; _5806 < 2; _5810 = _3799, _5806++)
                                {
                                    ivec2 _3791 = clamp(ivec2(floor(_3717 + (vec2(float(_5806), float(_5805)) - vec2(0.5)))), ivec2(0), ivec2(_3720));
                                    float _5808;
                                    do
                                    {
                                        if (_3205 == 0u)
                                        {
                                            _5808 = texelFetch(CsmMap0, ivec3(_3791, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3205 == 1u)
                                        {
                                            _5808 = texelFetch(CsmMap1, ivec3(_3791, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_3205 == 2u)
                                        {
                                            _5808 = texelFetch(CsmMap2, ivec3(_3791, 0).xy, 0).x;
                                            break;
                                        }
                                        _5808 = texelFetch(CsmMap3, ivec3(_3791, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _3799 = _5810 + float(_3704 <= _5808);
                                }
                            }
                            _5816 = _5811;
                        }
                        else
                        {
                            ivec2 _3814 = clamp(ivec2(floor(_3717)), ivec2(0), ivec2(_3720));
                            float _5804;
                            do
                            {
                                if (_3205 == 0u)
                                {
                                    _5804 = texelFetch(CsmMap0, ivec3(_3814, 0).xy, 0).x;
                                    break;
                                }
                                if (_3205 == 1u)
                                {
                                    _5804 = texelFetch(CsmMap1, ivec3(_3814, 0).xy, 0).x;
                                    break;
                                }
                                if (_3205 == 2u)
                                {
                                    _5804 = texelFetch(CsmMap2, ivec3(_3814, 0).xy, 0).x;
                                    break;
                                }
                                _5804 = texelFetch(CsmMap3, ivec3(_3814, 0).xy, 0).x;
                                break;
                            } while(false);
                            _5816 = float(_3704 <= _5804);
                        }
                        _5817 = _3766 ? 4.0 : 1.0;
                        _5814 = _5816;
                    }
                    _5830 = _5814 / _5817;
                    break;
                } while(false);
                _5831 = mix(_5793, _5830, clamp((i_viewDepth - (_662.Csm.cascades[_5766].splitFar - _3177)) / _3177, 0.0, 1.0));
            }
            else
            {
                _5831 = _5793;
            }
            _5832 = _5831;
            break;
        } while(false);
        _5882 = _5765 * ((i_material.z > 0.5) ? _5832 : 1.0);
    }
    else
    {
        _5882 = vec3(0.0);
    }
    uvec2 _4061 = min((uvec2(gl_FragCoord.xy) / uvec2(_1406.ClusterGridData.tileSizePx)), uvec2(_1406.ClusterGridData.gridX - 1u, _1406.ClusterGridData.gridY - 1u));
    uint _4111 = (_4061.x + (_4061.y * _1406.ClusterGridData.gridX)) + ((uint(clamp(floor((log2(max(i_viewDepth, _1406.ClusterGridData.zNear)) * _1406.ClusterGridData.sliceScale) + _1406.ClusterGridData.sliceBias), 0.0, float(_1406.ClusterGridData.gridZ - 1u))) * _1406.ClusterGridData.gridX) * _1406.ClusterGridData.gridY);
    vec3 _5881;
    _5881 = _5882;
    mat4 _5382[6];
    mat4 _5400[6];
    vec3 _6081;
    float _6109;
    float _6127;
    float _5930;
    float _6000;
    for (uint _5880 = 0u; _5880 < LightGrid_1._data[_4111].count; _6000 = _6127, _5930 = _6109, _5881 = _6081, _5880++)
    {
        uint _2821 = LightGrid_1._data[_4111].offset + _5880;
        if (LightIndexList_1._data[_2821] < _1406.ClusterGridData.numPointLights)
        {
            vec3 _4126 = PointLightsBuffer_1._data[LightIndexList_1._data[_2821]].position - i_worldPos;
            float _4128 = length(_4126);
            vec3 _4133 = _4126 / vec3(max(_4128, 9.9999997473787516355514526367188e-05));
            float _4155 = _4128 / PointLightsBuffer_1._data[LightIndexList_1._data[_2821]].radius;
            float _4158 = _4155 * _4155;
            float _4163 = clamp(1.0 - (_4158 * _4158), 0.0, 1.0);
            vec3 _5980;
            do
            {
                float _4198 = clamp(dot(_2263, _4133), 0.0, 1.0);
                if (_4198 <= 0.0)
                {
                    _5980 = vec3(0.0);
                    break;
                }
                vec3 _4207 = normalize(_2268 + _4133);
                float _4213 = max(dot(_2263, _2268), 9.9999997473787516355514526367188e-05);
                float _4218 = clamp(dot(_2263, _4207), 0.0, 1.0);
                vec3 _4229 = mix(vec3(0.039999999105930328369140625), _5764, vec3(i_material.x));
                float _4273 = clamp(1.0 - clamp(dot(_2268, _4207), 0.0, 1.0), 0.0, 1.0);
                float _4276 = _4273 * _4273;
                vec3 _4287 = _4229 + ((vec3(1.0) - _4229) * ((_4276 * _4276) * _4273));
                float _4295 = max(_2275, 0.04500000178813934326171875);
                float _4298 = _4295 * _4295;
                float _4301 = _4298 * _4298;
                float _4309 = max(((_4218 * _4218) * (_4301 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4323 = max(_2275, 0.04500000178813934326171875);
                float _4330 = ((_4323 * _4323) * _4323) * _4323;
                _5980 = ((((((vec3(1.0) - _4287) * (1.0 - i_material.x)) * _5764) * vec3(0.3183098733425140380859375)) + (_4287 * ((_4301 / ((3.1415927410125732421875 * _4309) * _4309)) * (0.5 / max((_4198 * sqrt(((_4213 * _4213) * (1.0 - _4330)) + _4330)) + (_4213 * sqrt(((_4198 * _4198) * (1.0 - _4330)) + _4330)), 9.9999997473787516355514526367188e-06))))) * ((PointLightsBuffer_1._data[LightIndexList_1._data[_2821]].color * PointLightsBuffer_1._data[LightIndexList_1._data[_2821]].intensity) * ((_4163 * _4163) / max(_4128 * _4128, 9.9999997473787516355514526367188e-05)))) * _4198;
                break;
            } while(false);
            float _5996;
            do
            {
                bool _5994;
                float _5997;
                uint _5981 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_5981 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_5981].posType.w) != 0u)
                        {
                            uint _4403 = _5981 + uint(1);
                            _5981 = _4403;
                            continue;
                        }
                        vec3 _4389 = _1055.PointShadows[_5981].posType.xyz - PointLightsBuffer_1._data[LightIndexList_1._data[_2821]].position;
                        if (dot(_4389, _4389) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _4403 = _5981 + uint(1);
                            _5981 = _4403;
                            continue;
                        }
                        float _5993;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _5993 = 1.0;
                                break;
                            }
                            _5400[0] = spvWorkaroundRowMajor(_1055.PointShadows[_5981]).vp[0];
                            _5400[1] = spvWorkaroundRowMajor(_1055.PointShadows[_5981]).vp[1];
                            _5400[2] = spvWorkaroundRowMajor(_1055.PointShadows[_5981]).vp[2];
                            _5400[3] = spvWorkaroundRowMajor(_1055.PointShadows[_5981]).vp[3];
                            _5400[4] = spvWorkaroundRowMajor(_1055.PointShadows[_5981]).vp[4];
                            _5400[5] = spvWorkaroundRowMajor(_1055.PointShadows[_5981]).vp[5];
                            vec3 _4466 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_5981]).posType.xyz;
                            uint _5982;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_5981]).posType.w < 0.5)
                            {
                                vec3 _4472 = abs(_4466);
                                uint _5983;
                                if ((_4472.x >= _4472.y) && (_4472.x >= _4472.z))
                                {
                                    _5983 = (_4466.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _5984;
                                    if (_4472.y >= _4472.z)
                                    {
                                        _5984 = (_4466.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _5984 = (_4466.z > 0.0) ? 4u : 5u;
                                    }
                                    _5983 = _5984;
                                }
                                _5982 = _5983;
                            }
                            else
                            {
                                _5982 = 0u;
                            }
                            if (_5982 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_5981]).params.y))
                            {
                                _5993 = 1.0;
                                break;
                            }
                            vec4 _4532 = vec4(i_worldPos + (_2263 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_4466)))), 1.0) * _5400[_5982];
                            if (_4532.w <= spvWorkaroundRowMajor(_1055.PointShadows[_5981]).params.x)
                            {
                                _5993 = 1.0;
                                break;
                            }
                            float _4544 = _4532.x / _4532.w;
                            float _4549 = _4532.y / _4532.w;
                            if ((abs(_4544) > 1.0) || (abs(_4549) > 1.0))
                            {
                                _5993 = 1.0;
                                break;
                            }
                            int _4561 = int(spvWorkaroundRowMajor(_1055.PointShadows[_5981]).params.z);
                            int _4573 = clamp(int(roundEven((((_4544 * 0.5) + 0.5) * float(_4561)) - 0.5)), 0, _4561 - 1);
                            int _4585 = clamp(int(roundEven(((0.5 - (_4549 * 0.5)) * float(_4561)) - 0.5)), 0, _4561 - 1);
                            int _4589 = int(_5982) * _4561;
                            int _4593 = (_4589 + _4561) - 1;
                            float _4595 = _4532.w;
                            float _4598 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _4595);
                            float _5986;
                            _5986 = 0.0;
                            float _5991;
                            SPIRV_CROSS_UNROLL
                            for (int _5985 = -1; _5985 <= 1; _5986 = _5991, _5985++)
                            {
                                _5991 = _5986;
                                float _4638;
                                SPIRV_CROSS_UNROLL
                                for (int _5987 = -1; _5987 <= 1; _5991 = _4638, _5987++)
                                {
                                    int _4614 = clamp(_4573 + _5987, 0, _4561 - 1);
                                    int _4626 = clamp(clamp(_4585 + _5985, 0, _4561 - 1) + _4589, _4589, _4593);
                                    float _5989;
                                    do
                                    {
                                        if (_5981 == 0u)
                                        {
                                            _5989 = texelFetch(PointShadowMap0, ivec3(_4614, _4626, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5981 == 1u)
                                        {
                                            _5989 = texelFetch(PointShadowMap1, ivec3(_4614, _4626, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5981 == 2u)
                                        {
                                            _5989 = texelFetch(PointShadowMap2, ivec3(_4614, _4626, 0).xy, 0).x;
                                            break;
                                        }
                                        _5989 = texelFetch(PointShadowMap3, ivec3(_4614, _4626, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _4638 = _5991 + float(_5989 >= (_4595 - _4598));
                                }
                            }
                            _5993 = _5986 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _5997 = _5993;
                        _5994 = true;
                        break;
                    }
                    else
                    {
                        _5997 = _6000;
                        _5994 = false;
                        break;
                    }
                }
                if (_5994)
                {
                    _5996 = _5997;
                    break;
                }
                _5996 = 1.0;
                break;
            } while(false);
            _6127 = _5996;
            _6109 = _5930;
            _6081 = _5881 + (_5980 * _5996);
        }
        else
        {
            uint _2866 = LightIndexList_1._data[_2821] - _1406.ClusterGridData.numPointLights;
            vec3 _4740 = SpotLightsBuffer_1._data[_2866].position - i_worldPos;
            float _4742 = length(_4740);
            vec3 _4747 = _4740 / vec3(max(_4742, 9.9999997473787516355514526367188e-05));
            float _4781 = _4742 / SpotLightsBuffer_1._data[_2866].radius;
            float _4784 = _4781 * _4781;
            float _4789 = clamp(1.0 - (_4784 * _4784), 0.0, 1.0);
            float _4806 = clamp((dot(-_4747, SpotLightsBuffer_1._data[_2866].direction) * SpotLightsBuffer_1._data[_2866].spotScale) + SpotLightsBuffer_1._data[_2866].spotOffset, 0.0, 1.0);
            vec3 _5910;
            do
            {
                float _4836 = clamp(dot(_2263, _4747), 0.0, 1.0);
                if (_4836 <= 0.0)
                {
                    _5910 = vec3(0.0);
                    break;
                }
                vec3 _4845 = normalize(_2268 + _4747);
                float _4851 = max(dot(_2263, _2268), 9.9999997473787516355514526367188e-05);
                float _4856 = clamp(dot(_2263, _4845), 0.0, 1.0);
                vec3 _4867 = mix(vec3(0.039999999105930328369140625), _5764, vec3(i_material.x));
                float _4911 = clamp(1.0 - clamp(dot(_2268, _4845), 0.0, 1.0), 0.0, 1.0);
                float _4914 = _4911 * _4911;
                vec3 _4925 = _4867 + ((vec3(1.0) - _4867) * ((_4914 * _4914) * _4911));
                float _4933 = max(_2275, 0.04500000178813934326171875);
                float _4936 = _4933 * _4933;
                float _4939 = _4936 * _4936;
                float _4947 = max(((_4856 * _4856) * (_4939 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
                float _4961 = max(_2275, 0.04500000178813934326171875);
                float _4968 = ((_4961 * _4961) * _4961) * _4961;
                _5910 = ((((((vec3(1.0) - _4925) * (1.0 - i_material.x)) * _5764) * vec3(0.3183098733425140380859375)) + (_4925 * ((_4939 / ((3.1415927410125732421875 * _4947) * _4947)) * (0.5 / max((_4836 * sqrt(((_4851 * _4851) * (1.0 - _4968)) + _4968)) + (_4851 * sqrt(((_4836 * _4836) * (1.0 - _4968)) + _4968)), 9.9999997473787516355514526367188e-06))))) * ((SpotLightsBuffer_1._data[_2866].color * SpotLightsBuffer_1._data[_2866].intensity) * (((_4789 * _4789) / max(_4742 * _4742, 9.9999997473787516355514526367188e-05)) * (_4806 * _4806)))) * _4836;
                break;
            } while(false);
            uint _2894 = LightIndexList_1._data[_2821] - _1406.ClusterGridData.numPointLights;
            float _5926;
            do
            {
                bool _5924;
                float _5927;
                uint _5911 = 0u;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_5911 < _1055.pointShadowCount)
                    {
                        if (uint(_1055.PointShadows[_5911].posType.w) != 1u)
                        {
                            uint _5041 = _5911 + uint(1);
                            _5911 = _5041;
                            continue;
                        }
                        vec3 _5027 = _1055.PointShadows[_5911].posType.xyz - SpotLightsBuffer_1._data[_2894].position;
                        if (dot(_5027, _5027) > 9.9999999392252902907785028219223e-09)
                        {
                            uint _5041 = _5911 + uint(1);
                            _5911 = _5041;
                            continue;
                        }
                        float _5923;
                        do
                        {
                            if (i_material.z < 0.5)
                            {
                                _5923 = 1.0;
                                break;
                            }
                            _5382[0] = spvWorkaroundRowMajor(_1055.PointShadows[_5911]).vp[0];
                            _5382[1] = spvWorkaroundRowMajor(_1055.PointShadows[_5911]).vp[1];
                            _5382[2] = spvWorkaroundRowMajor(_1055.PointShadows[_5911]).vp[2];
                            _5382[3] = spvWorkaroundRowMajor(_1055.PointShadows[_5911]).vp[3];
                            _5382[4] = spvWorkaroundRowMajor(_1055.PointShadows[_5911]).vp[4];
                            _5382[5] = spvWorkaroundRowMajor(_1055.PointShadows[_5911]).vp[5];
                            vec3 _5104 = i_worldPos - spvWorkaroundRowMajor(_1055.PointShadows[_5911]).posType.xyz;
                            uint _5912;
                            if (spvWorkaroundRowMajor(_1055.PointShadows[_5911]).posType.w < 0.5)
                            {
                                vec3 _5110 = abs(_5104);
                                uint _5913;
                                if ((_5110.x >= _5110.y) && (_5110.x >= _5110.z))
                                {
                                    _5913 = (_5104.x > 0.0) ? 0u : 1u;
                                }
                                else
                                {
                                    uint _5914;
                                    if (_5110.y >= _5110.z)
                                    {
                                        _5914 = (_5104.y > 0.0) ? 2u : 3u;
                                    }
                                    else
                                    {
                                        _5914 = (_5104.z > 0.0) ? 4u : 5u;
                                    }
                                    _5913 = _5914;
                                }
                                _5912 = _5913;
                            }
                            else
                            {
                                _5912 = 0u;
                            }
                            if (_5912 >= uint(spvWorkaroundRowMajor(_1055.PointShadows[_5911]).params.y))
                            {
                                _5923 = 1.0;
                                break;
                            }
                            vec4 _5170 = vec4(i_worldPos + (_2263 * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * length(_5104)))), 1.0) * _5382[_5912];
                            if (_5170.w <= spvWorkaroundRowMajor(_1055.PointShadows[_5911]).params.x)
                            {
                                _5923 = 1.0;
                                break;
                            }
                            float _5182 = _5170.x / _5170.w;
                            float _5187 = _5170.y / _5170.w;
                            if ((abs(_5182) > 1.0) || (abs(_5187) > 1.0))
                            {
                                _5923 = 1.0;
                                break;
                            }
                            int _5199 = int(spvWorkaroundRowMajor(_1055.PointShadows[_5911]).params.z);
                            int _5211 = clamp(int(roundEven((((_5182 * 0.5) + 0.5) * float(_5199)) - 0.5)), 0, _5199 - 1);
                            int _5223 = clamp(int(roundEven(((0.5 - (_5187 * 0.5)) * float(_5199)) - 0.5)), 0, _5199 - 1);
                            int _5227 = int(_5912) * _5199;
                            int _5231 = (_5227 + _5199) - 1;
                            float _5233 = _5170.w;
                            float _5236 = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * _5233);
                            float _5916;
                            _5916 = 0.0;
                            float _5921;
                            SPIRV_CROSS_UNROLL
                            for (int _5915 = -1; _5915 <= 1; _5916 = _5921, _5915++)
                            {
                                _5921 = _5916;
                                float _5276;
                                SPIRV_CROSS_UNROLL
                                for (int _5917 = -1; _5917 <= 1; _5921 = _5276, _5917++)
                                {
                                    int _5252 = clamp(_5211 + _5917, 0, _5199 - 1);
                                    int _5264 = clamp(clamp(_5223 + _5915, 0, _5199 - 1) + _5227, _5227, _5231);
                                    float _5919;
                                    do
                                    {
                                        if (_5911 == 0u)
                                        {
                                            _5919 = texelFetch(PointShadowMap0, ivec3(_5252, _5264, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5911 == 1u)
                                        {
                                            _5919 = texelFetch(PointShadowMap1, ivec3(_5252, _5264, 0).xy, 0).x;
                                            break;
                                        }
                                        if (_5911 == 2u)
                                        {
                                            _5919 = texelFetch(PointShadowMap2, ivec3(_5252, _5264, 0).xy, 0).x;
                                            break;
                                        }
                                        _5919 = texelFetch(PointShadowMap3, ivec3(_5252, _5264, 0).xy, 0).x;
                                        break;
                                    } while(false);
                                    _5276 = _5921 + float(_5919 >= (_5233 - _5236));
                                }
                            }
                            _5923 = _5916 * 0.111111111938953399658203125;
                            break;
                        } while(false);
                        _5927 = _5923;
                        _5924 = true;
                        break;
                    }
                    else
                    {
                        _5927 = _5930;
                        _5924 = false;
                        break;
                    }
                }
                if (_5924)
                {
                    _5926 = _5927;
                    break;
                }
                _5926 = 1.0;
                break;
            } while(false);
            _6127 = _6000;
            _6109 = _5926;
            _6081 = _5881 + (_5910 * _5926);
        }
    }
    vec3 _2370 = (((((((vec3(1.0) - min(_2316, vec3(1.0))) * (1.0 - i_material.x)) * _5764) * _2618) + (_2316 * _2705)) * texelFetch(AoMap, ivec3(ivec2(gl_FragCoord.xy), 0).xy, 0).x) + _5881) + i_emissive;
    vec3 _5909;
    if (_2042.fogParams.z > 0.0)
    {
        _5909 = mix(_2370, _2042.fogColor.xyz, vec3(clamp((length(i_toCam) - _2042.fogParams.x) / max(_2042.fogParams.y - _2042.fogParams.x, 0.001000000047497451305389404296875), 0.0, 1.0) * _2042.fogParams.z));
    }
    else
    {
        _5909 = _2370;
    }
    _entryPointOutput = vec4(clamp((_5909 * ((_5909 * 2.5099999904632568359375) + vec3(0.02999999932944774627685546875))) / ((_5909 * ((_5909 * 2.4300000667572021484375) + vec3(0.589999973773956298828125))) + vec3(0.14000000059604644775390625)), vec3(0.0), vec3(1.0)), i_baseColor.w);
}

