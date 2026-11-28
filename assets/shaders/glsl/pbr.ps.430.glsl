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

struct SurfaceData
{
    vec3 N;
    vec3 V;
    vec3 albedo;
    float metallic;
    float roughness;
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

struct DirectionalLight
{
    vec3 direction;
    float intensity;
    vec3 color;
    uint flags;
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

struct VSOut
{
    vec4 clip;
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
    vec3 toCam;
    float viewDepth;
    vec4 baseColor;
    vec4 material;
    vec4 texInfo;
    vec3 emissive;
    vec4 fogColor;
    vec4 fogParams;
    vec4 uvXform;
    vec2 normalInfo;
};

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

struct ClusterCell
{
    uint offset;
    uint count;
};

struct IblData
{
    vec4 sh[9];
    float intensity;
    float specularMaxMip;
    uint flags;
    float _pad;
};

struct InstanceData
{
    mat4 world;
    vec4 baseColor;
    float metallic;
    float roughness;
    float receiveShadow;
    float _pad1;
    vec4 texInfo;
    vec4 emissive;
    vec4 fogColor;
    vec4 fogParams;
};

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

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 cameraPos;
    float _pad0;
} _2329;

layout(binding = 17, std430) readonly buffer Instances
{
    layout(row_major) InstanceData _data[];
} Instances_1;

layout(binding = 6, std140) uniform BatchBuffer
{
    uvec4 batchInfo;
} _2338;

uniform sampler2D AlbedoMap;
uniform sampler2D NormalMap;
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
layout(location = 11) flat in vec4 i_uvXform;
layout(location = 12) flat in vec2 i_normalInfo;
layout(location = 0) out vec4 _entryPointOutput;

PointShadowLight spvWorkaroundRowMajor(PointShadowLight wrap) { return wrap; }

vec2 BrdfLutBilinear(float NoV, float rough)
{
    float fx = clamp((clamp(NoV, 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    float fy = clamp((clamp(rough, 0.0, 1.0) * 32.0) - 0.5, 0.0, 31.0);
    int x0 = int(fx);
    int y0 = int(fy);
    int x1 = min((x0 + 1), 31);
    int y1 = min((y0 + 1), 31);
    float tx = fx - float(x0);
    float ty = fy - float(y0);
    vec2 p00 = vec2(texelFetch(IblBrdfLut, ivec3(x0, y0, 0).xy, ivec3(x0, y0, 0).z).xy);
    vec2 p10 = vec2(texelFetch(IblBrdfLut, ivec3(x1, y0, 0).xy, ivec3(x1, y0, 0).z).xy);
    vec2 p01 = vec2(texelFetch(IblBrdfLut, ivec3(x0, y1, 0).xy, ivec3(x0, y1, 0).z).xy);
    vec2 p11 = vec2(texelFetch(IblBrdfLut, ivec3(x1, y1, 0).xy, ivec3(x1, y1, 0).z).xy);
    return mix(mix(p00, p10, vec2(tx)), mix(p01, p11, vec2(tx)), vec2(ty));
}

float IblDominantDirFactor(float roughness)
{
    float a = roughness * roughness;
    float s = clamp(1.0 - a, 0.0, 1.0);
    return s * (sqrt(s) + a);
}

vec3 IblEvaluateIrradiance(vec3 n)
{
    vec3 _1689 = (((((((_1606.Ibl.sh[0].xyz + (_1606.Ibl.sh[1].xyz * n.y)) + (_1606.Ibl.sh[2].xyz * n.z)) + (_1606.Ibl.sh[3].xyz * n.x)) + (_1606.Ibl.sh[4].xyz * (n.x * n.y))) + (_1606.Ibl.sh[5].xyz * (n.y * n.z))) + (_1606.Ibl.sh[6].xyz * (((3.0 * n.z) * n.z) - 1.0))) + (_1606.Ibl.sh[7].xyz * (n.x * n.z))) + (_1606.Ibl.sh[8].xyz * ((n.x * n.x) - (n.y * n.y)));
    vec3 r = _1689;
    return max(r, vec3(0.0));
}

vec3 F_Schlick(float VoH, vec3 F0)
{
    float m = clamp(1.0 - VoH, 0.0, 1.0);
    float m2 = m * m;
    return F0 + ((vec3(1.0) - F0) * ((m2 * m2) * m));
}

float D_GGX(float NoH, float roughness)
{
    float r = max(roughness, 0.04500000178813934326171875);
    float a = r * r;
    float a2 = a * a;
    float d = max(((NoH * NoH) * (a2 - 1.0)) + 1.0, 9.9999997473787516355514526367188e-06);
    return a2 / ((3.1415927410125732421875 * d) * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float roughness)
{
    float r = max(roughness, 0.04500000178813934326171875);
    float a2 = ((r * r) * r) * r;
    float ggxV = NoL * sqrt(((NoV * NoV) * (1.0 - a2)) + a2);
    float ggxL = NoV * sqrt(((NoL * NoL) * (1.0 - a2)) + a2);
    return 0.5 / max(ggxV + ggxL, 9.9999997473787516355514526367188e-06);
}

vec3 EvaluateBRDF(SurfaceData s, vec3 L, vec3 radiance)
{
    float NoL = clamp(dot(s.N, L), 0.0, 1.0);
    if (NoL <= 0.0)
    {
        return vec3(0.0);
    }
    vec3 H = normalize(s.V + L);
    float NoV = max(dot(s.N, s.V), 9.9999997473787516355514526367188e-05);
    float NoH = clamp(dot(s.N, H), 0.0, 1.0);
    float VoH = clamp(dot(s.V, H), 0.0, 1.0);
    vec3 F0 = mix(vec3(0.039999999105930328369140625), s.albedo, vec3(s.metallic));
    float param = VoH;
    vec3 param_1 = F0;
    vec3 F = F_Schlick(param, param_1);
    float param_2 = NoH;
    float param_3 = s.roughness;
    float param_4 = NoV;
    float param_5 = NoL;
    float param_6 = s.roughness;
    vec3 specular = F * (D_GGX(param_2, param_3) * V_SmithGGXCorrelated(param_4, param_5, param_6));
    vec3 kD = (vec3(1.0) - F) * (1.0 - s.metallic);
    vec3 diffuse = (kD * s.albedo) / vec3(3.1415927410125732421875);
    return ((diffuse + specular) * radiance) * NoL;
}

vec3 EvaluateDirectionalLight(DirectionalLight l, SurfaceData s)
{
    SurfaceData param = s;
    vec3 param_1 = -l.direction;
    vec3 param_2 = l.color * l.intensity;
    return EvaluateBRDF(param, param_1, param_2);
}

float CsmTexel(uint k, ivec2 p)
{
    if (k == 0u)
    {
        return texelFetch(CsmMap0, ivec3(p, 0).xy, ivec3(p, 0).z).x;
    }
    if (k == 1u)
    {
        return texelFetch(CsmMap1, ivec3(p, 0).xy, ivec3(p, 0).z).x;
    }
    if (k == 2u)
    {
        return texelFetch(CsmMap2, ivec3(p, 0).xy, ivec3(p, 0).z).x;
    }
    return texelFetch(CsmMap3, ivec3(p, 0).xy, ivec3(p, 0).z).x;
}

float CsmSampleCascade(uint k, vec3 worldPos, vec3 N)
{
    CsmCascade c;
    c.viewProj = _662.Csm.cascades[k].viewProj;
    c.splitNear = _662.Csm.cascades[k].splitNear;
    c.splitFar = _662.Csm.cascades[k].splitFar;
    c.texelWorldSize = _662.Csm.cascades[k].texelWorldSize;
    c.depthRange = _662.Csm.cascades[k].depthRange;
    vec3 p = worldPos + (N * (_662.Csm.normalBias * c.texelWorldSize));
    vec3 clip = (vec4(p, 1.0) * c.viewProj).xyz;
    if (any(greaterThan(abs(clip.xy), vec2(1.0))) || (clip.z > 1.0))
    {
        return 1.0;
    }
    float depth = clip.z - (_662.Csm.depthBias / c.depthRange);
    float size = _662.Csm.shadowMapSize;
    vec2 t = vec2((clip.x * 0.5) + 0.5, 0.5 - (clip.y * 0.5)) * size;
    int maxIdx = int(size) - 1;
    float lit = 0.0;
    float n = 1.0;
    if (_662.Csm.pcfTaps >= 9u)
    {
        n = 9.0;
        SPIRV_CROSS_UNROLL
        for (int j = -1; j <= 1; j++)
        {
            SPIRV_CROSS_UNROLL
            for (int i = -1; i <= 1; i++)
            {
                ivec2 q = clamp(ivec2(floor(t + vec2(float(i), float(j)))), ivec2(0), ivec2(maxIdx));
                uint param = k;
                ivec2 param_1 = q;
                lit += float(depth <= CsmTexel(param, param_1));
            }
        }
    }
    else
    {
        if (_662.Csm.pcfTaps >= 4u)
        {
            n = 4.0;
            SPIRV_CROSS_UNROLL
            for (int j_1 = 0; j_1 < 2; j_1++)
            {
                SPIRV_CROSS_UNROLL
                for (int i_1 = 0; i_1 < 2; i_1++)
                {
                    ivec2 q_1 = clamp(ivec2(floor(t + (vec2(float(i_1), float(j_1)) - vec2(0.5)))), ivec2(0), ivec2(maxIdx));
                    uint param_2 = k;
                    ivec2 param_3 = q_1;
                    lit += float(depth <= CsmTexel(param_2, param_3));
                }
            }
        }
        else
        {
            ivec2 q_2 = clamp(ivec2(floor(t)), ivec2(0), ivec2(maxIdx));
            uint param_4 = k;
            ivec2 param_5 = q_2;
            lit = float(depth <= CsmTexel(param_4, param_5));
        }
    }
    return lit / n;
}

float CsmVisibility(vec3 worldPos, vec3 N, float viewDepth)
{
    uint k = 0u;
    while ((k < _662.Csm.cascadeCount) && (viewDepth > _662.Csm.cascades[k].splitFar))
    {
        k++;
    }
    if (k >= _662.Csm.cascadeCount)
    {
        return 1.0;
    }
    uint param = k;
    vec3 param_1 = worldPos;
    vec3 param_2 = N;
    float vis = CsmSampleCascade(param, param_1, param_2);
    CsmCascade c;
    c.viewProj = _662.Csm.cascades[k].viewProj;
    c.splitNear = _662.Csm.cascades[k].splitNear;
    c.splitFar = _662.Csm.cascades[k].splitFar;
    c.texelWorldSize = _662.Csm.cascades[k].texelWorldSize;
    c.depthRange = _662.Csm.cascades[k].depthRange;
    float blendWidth = _662.Csm.blendFraction * (c.splitFar - c.splitNear);
    if ((((k + 1u) < _662.Csm.cascadeCount) && (blendWidth > 0.0)) && (viewDepth > (c.splitFar - blendWidth)))
    {
        float t = clamp((viewDepth - (c.splitFar - blendWidth)) / blendWidth, 0.0, 1.0);
        uint param_3 = k + 1u;
        vec3 param_4 = worldPos;
        vec3 param_5 = N;
        vis = mix(vis, CsmSampleCascade(param_3, param_4, param_5), t);
    }
    return vis;
}

uint SliceFromDepth(ClusterGrid g, float depth)
{
    float s = floor((log2(max(depth, g.zNear)) * g.sliceScale) + g.sliceBias);
    return uint(clamp(s, 0.0, float(g.gridZ - 1u)));
}

uint ClusterIndexOf(ClusterGrid g, uvec3 c)
{
    return (c.x + (c.y * g.gridX)) + ((c.z * g.gridX) * g.gridY);
}

uint ClusterIndexForPixel(ClusterGrid g, vec2 pixelPos, float depth)
{
    uvec2 tile = min((uvec2(pixelPos) / uvec2(g.tileSizePx)), uvec2(g.gridX - 1u, g.gridY - 1u));
    ClusterGrid param = g;
    float param_1 = depth;
    ClusterGrid param_2 = g;
    uvec3 param_3 = uvec3(tile, SliceFromDepth(param, param_1));
    return ClusterIndexOf(param_2, param_3);
}

float DistanceAttenuation(float d, float R)
{
    float r = d / R;
    float r2 = r * r;
    float w = clamp(1.0 - (r2 * r2), 0.0, 1.0);
    return (w * w) / max(d * d, 9.9999997473787516355514526367188e-05);
}

vec3 EvaluatePointLight(PointLight l, SurfaceData s, vec3 worldPos)
{
    vec3 toLight = l.position - worldPos;
    float d = length(toLight);
    vec3 L = toLight / vec3(max(d, 9.9999997473787516355514526367188e-05));
    float param = d;
    float param_1 = l.radius;
    vec3 radiance = (l.color * l.intensity) * DistanceAttenuation(param, param_1);
    SurfaceData param_2 = s;
    vec3 param_3 = L;
    vec3 param_4 = radiance;
    return EvaluateBRDF(param_2, param_3, param_4);
}

float PointShadowTexel(uint slot, ivec2 p)
{
    if (slot == 0u)
    {
        return texelFetch(PointShadowMap0, ivec3(p, 0).xy, ivec3(p, 0).z).x;
    }
    if (slot == 1u)
    {
        return texelFetch(PointShadowMap1, ivec3(p, 0).xy, ivec3(p, 0).z).x;
    }
    if (slot == 2u)
    {
        return texelFetch(PointShadowMap2, ivec3(p, 0).xy, ivec3(p, 0).z).x;
    }
    return texelFetch(PointShadowMap3, ivec3(p, 0).xy, ivec3(p, 0).z).x;
}

float PointShadowVisibility(uint slot, vec3 worldPos, vec3 N, float receive)
{
    if (receive < 0.5)
    {
        return 1.0;
    }
    PointShadowLight L;
    L.posType = spvWorkaroundRowMajor(_1055.PointShadows[slot]).posType;
    L.params = spvWorkaroundRowMajor(_1055.PointShadows[slot]).params;
    L.vp[0] = spvWorkaroundRowMajor(_1055.PointShadows[slot]).vp[0];
    L.vp[1] = spvWorkaroundRowMajor(_1055.PointShadows[slot]).vp[1];
    L.vp[2] = spvWorkaroundRowMajor(_1055.PointShadows[slot]).vp[2];
    L.vp[3] = spvWorkaroundRowMajor(_1055.PointShadows[slot]).vp[3];
    L.vp[4] = spvWorkaroundRowMajor(_1055.PointShadows[slot]).vp[4];
    L.vp[5] = spvWorkaroundRowMajor(_1055.PointShadows[slot]).vp[5];
    vec3 d = worldPos - L.posType.xyz;
    uint f = 0u;
    if (L.posType.w < 0.5)
    {
        vec3 a = abs(d);
        if ((a.x >= a.y) && (a.x >= a.z))
        {
            f = (d.x > 0.0) ? 0u : 1u;
        }
        else
        {
            if (a.y >= a.z)
            {
                f = (d.y > 0.0) ? 2u : 3u;
            }
            else
            {
                f = (d.z > 0.0) ? 4u : 5u;
            }
        }
    }
    if (f >= uint(L.params.y))
    {
        return 1.0;
    }
    float dist = length(d);
    vec3 pb = worldPos + (N * (0.00999999977648258209228515625 + (0.00200000009499490261077880859375 * dist)));
    vec4 c = vec4(pb, 1.0) * L.vp[f];
    if (c.w <= L.params.x)
    {
        return 1.0;
    }
    float nx = c.x / c.w;
    float ny = c.y / c.w;
    if ((abs(nx) > 1.0) || (abs(ny) > 1.0))
    {
        return 1.0;
    }
    int R = int(L.params.z);
    int cx = clamp(int(roundEven((((nx * 0.5) + 0.5) * float(R)) - 0.5)), 0, R - 1);
    int cy = clamp(int(roundEven(((0.5 - (ny * 0.5)) * float(R)) - 0.5)), 0, R - 1);
    int y0 = int(f) * R;
    int y1 = (y0 + R) - 1;
    float depth = c.w;
    float bias = 0.0199999995529651641845703125 + (0.00150000001303851604461669921875 * depth);
    float lit = 0.0;
    SPIRV_CROSS_UNROLL
    for (int dy = -1; dy <= 1; dy++)
    {
        SPIRV_CROSS_UNROLL
        for (int dx = -1; dx <= 1; dx++)
        {
            int x = clamp(cx + dx, 0, R - 1);
            int y = clamp(cy + dy, 0, R - 1) + y0;
            y = clamp(y, y0, y1);
            uint param = slot;
            ivec2 param_1 = ivec2(x, y);
            lit += float(PointShadowTexel(param, param_1) >= (depth - bias));
        }
    }
    return lit / 9.0;
}

float PointShadowForLight(vec3 lightPos, uint type, vec3 worldPos, vec3 N, float receive)
{
    SPIRV_CROSS_LOOP
    for (uint k = 0u; k < _1055.pointShadowCount; k++)
    {
        if (uint(_1055.PointShadows[k].posType.w) != type)
        {
            continue;
        }
        vec3 e = _1055.PointShadows[k].posType.xyz - lightPos;
        if (dot(e, e) > 9.9999999392252902907785028219223e-09)
        {
            continue;
        }
        uint param = k;
        vec3 param_1 = worldPos;
        vec3 param_2 = N;
        float param_3 = receive;
        return PointShadowVisibility(param, param_1, param_2, param_3);
    }
    return 1.0;
}

float SpotAttenuation(float cosAngle, float scale, float offset)
{
    float t = clamp((cosAngle * scale) + offset, 0.0, 1.0);
    return t * t;
}

vec3 EvaluateSpotLight(SpotLight l, SurfaceData s, vec3 worldPos)
{
    vec3 toLight = l.position - worldPos;
    float d = length(toLight);
    vec3 L = toLight / vec3(max(d, 9.9999997473787516355514526367188e-05));
    float cosAngle = dot(-L, l.direction);
    float param = d;
    float param_1 = l.radius;
    float param_2 = cosAngle;
    float param_3 = l.spotScale;
    float param_4 = l.spotOffset;
    float att = DistanceAttenuation(param, param_1) * SpotAttenuation(param_2, param_3, param_4);
    SurfaceData param_5 = s;
    vec3 param_6 = L;
    vec3 param_7 = (l.color * l.intensity) * att;
    return EvaluateBRDF(param_5, param_6, param_7);
}

vec3 EvaluateClusteredLights(SurfaceData s, vec3 worldPos, vec2 pixelPos, float viewDepth, float receiveShadow)
{
    vec3 total = vec3(0.0);
    if (_1353.Globals.hasSun != 0u)
    {
        DirectionalLight param;
        param.direction = _1363.SunLight.direction;
        param.intensity = _1363.SunLight.intensity;
        param.color = _1363.SunLight.color;
        param.flags = _1363.SunLight.flags;
        SurfaceData param_1 = s;
        vec3 sun = EvaluateDirectionalLight(param, param_1);
        vec3 param_2 = worldPos;
        vec3 param_3 = s.N;
        float param_4 = viewDepth;
        sun *= ((receiveShadow > 0.5) ? CsmVisibility(param_2, param_3, param_4) : 1.0);
        total += sun;
    }
    ClusterGrid param_5;
    param_5.gridX = _1406.ClusterGridData.gridX;
    param_5.gridY = _1406.ClusterGridData.gridY;
    param_5.gridZ = _1406.ClusterGridData.gridZ;
    param_5.tileSizePx = _1406.ClusterGridData.tileSizePx;
    param_5.zNear = _1406.ClusterGridData.zNear;
    param_5.zFar = _1406.ClusterGridData.zFar;
    param_5.sliceScale = _1406.ClusterGridData.sliceScale;
    param_5.sliceBias = _1406.ClusterGridData.sliceBias;
    param_5.projScale = _1406.ClusterGridData.projScale;
    param_5.screenSize = _1406.ClusterGridData.screenSize;
    param_5.numPointLights = _1406.ClusterGridData.numPointLights;
    param_5.numSpotLights = _1406.ClusterGridData.numSpotLights;
    param_5.maxLightsPerCluster = _1406.ClusterGridData.maxLightsPerCluster;
    param_5.maxIndices = _1406.ClusterGridData.maxIndices;
    vec2 param_6 = pixelPos;
    float param_7 = viewDepth;
    uint _1449 = ClusterIndexForPixel(param_5, param_6, param_7);
    ClusterCell cell;
    cell.offset = LightGrid_1._data[_1449].offset;
    cell.count = LightGrid_1._data[_1449].count;
    uint np = _1406.ClusterGridData.numPointLights;
    PointLight param_8;
    SpotLight param_16;
    for (uint i = 0u; i < cell.count; i++)
    {
        uint li = LightIndexList_1._data[cell.offset + i];
        if (li < np)
        {
            param_8.position = PointLightsBuffer_1._data[li].position;
            param_8.radius = PointLightsBuffer_1._data[li].radius;
            param_8.color = PointLightsBuffer_1._data[li].color;
            param_8.intensity = PointLightsBuffer_1._data[li].intensity;
            param_8.flags = PointLightsBuffer_1._data[li].flags;
            param_8.layerMask = PointLightsBuffer_1._data[li].layerMask;
            param_8.shadowIndex = PointLightsBuffer_1._data[li].shadowIndex;
            param_8.cullRadius = PointLightsBuffer_1._data[li].cullRadius;
            SurfaceData param_9 = s;
            vec3 param_10 = worldPos;
            vec3 c = EvaluatePointLight(param_8, param_9, param_10);
            vec3 param_11 = PointLightsBuffer_1._data[li].position;
            uint param_12 = 0u;
            vec3 param_13 = worldPos;
            vec3 param_14 = s.N;
            float param_15 = receiveShadow;
            c *= PointShadowForLight(param_11, param_12, param_13, param_14, param_15);
            total += c;
        }
        else
        {
            uint _1546 = li - np;
            param_16.position = SpotLightsBuffer_1._data[_1546].position;
            param_16.radius = SpotLightsBuffer_1._data[_1546].radius;
            param_16.color = SpotLightsBuffer_1._data[_1546].color;
            param_16.intensity = SpotLightsBuffer_1._data[_1546].intensity;
            param_16.direction = SpotLightsBuffer_1._data[_1546].direction;
            param_16.spotScale = SpotLightsBuffer_1._data[_1546].spotScale;
            param_16.spotOffset = SpotLightsBuffer_1._data[_1546].spotOffset;
            param_16.flags = SpotLightsBuffer_1._data[_1546].flags;
            param_16.layerMask = SpotLightsBuffer_1._data[_1546].layerMask;
            param_16.shadowIndex = SpotLightsBuffer_1._data[_1546].shadowIndex;
            SurfaceData param_17 = s;
            vec3 param_18 = worldPos;
            vec3 c_1 = EvaluateSpotLight(param_16, param_17, param_18);
            vec3 param_19 = SpotLightsBuffer_1._data[li - np].position;
            uint param_20 = 1u;
            vec3 param_21 = worldPos;
            vec3 param_22 = s.N;
            float param_23 = receiveShadow;
            c_1 *= PointShadowForLight(param_19, param_20, param_21, param_22, param_23);
            total += c_1;
        }
    }
    return total;
}

vec3 ToneMapACESFilm(vec3 x)
{
    return clamp((x * ((x * 2.5099999904632568359375) + vec3(0.02999999932944774627685546875))) / ((x * ((x * 2.4300000667572021484375) + vec3(0.589999973773956298828125))) + vec3(0.14000000059604644775390625)), vec3(0.0), vec3(1.0));
}

vec4 _PSMain(VSOut i)
{
    vec2 tiling = vec2((abs(i.uvXform.x) > 9.9999999747524270787835121154785e-07) ? i.uvXform.x : 1.0, (abs(i.uvXform.y) > 9.9999999747524270787835121154785e-07) ? i.uvXform.y : 1.0);
    vec2 uv = (i.uv * tiling) + i.uvXform.zw;
    vec3 dp1 = dFdx(i.worldPos);
    vec3 dp2 = dFdy(i.worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 albedo = i.baseColor.xyz;
    float texA = 1.0;
    if (i.texInfo.z > 0.5)
    {
        ivec2 tp = ivec2(clamp(int(fract(uv.x) * i.texInfo.x), 0, int(i.texInfo.x) - 1), clamp(int(fract(uv.y) * i.texInfo.y), 0, int(i.texInfo.y) - 1));
        vec4 tex = texelFetch(AlbedoMap, ivec3(tp, 0).xy, ivec3(tp, 0).z);
        albedo *= tex.xyz;
        texA = tex.w;
    }
    if ((i.normalInfo.y > 0.0) && ((i.baseColor.w * texA) < i.normalInfo.y))
    {
        discard;
    }
    float metal = i.material.x;
    float rough = i.material.y;
    float receive = i.material.z;
    vec3 N = normalize(i.normal);
    if (i.normalInfo.x > 0.0)
    {
        uvec2 sizeQueryTemp = uvec2(textureSize(NormalMap, 0));
        uint nw = sizeQueryTemp.x;
        uint nh = sizeQueryTemp.y;
        ivec2 np = ivec2(clamp(int(fract(uv.x) * float(nw)), 0, int(nw) - 1), clamp(int(fract(uv.y) * float(nh)), 0, int(nh) - 1));
        vec3 tn = (texelFetch(NormalMap, ivec3(np, 0).xy, ivec3(np, 0).z).xyz * 2.0) - vec3(1.0);
        tn.y = -tn.y;
        vec3 dp2perp = cross(dp2, N);
        vec3 dp1perp = cross(N, dp1);
        vec3 T = (dp2perp * duv1.x) + (dp1perp * duv2.x);
        vec3 B = (dp2perp * duv1.y) + (dp1perp * duv2.y);
        float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 9.9999999600419720025001879548654e-13));
        vec3 Np = normalize(((T * (tn.x * invmax)) + (B * (tn.y * invmax))) + (N * tn.z));
        N = normalize(mix(N, Np, vec3(i.normalInfo.x)));
    }
    SurfaceData s;
    s.N = N;
    s.V = normalize(i.toCam);
    s.albedo = albedo;
    s.metallic = metal;
    s.roughness = max(rough, 0.04500000178813934326171875);
    float ao = texelFetch(AoMap, ivec3(ivec2(i.clip.xy), 0).xy, ivec3(ivec2(i.clip.xy), 0).z).x;
    float NoV = max(dot(N, s.V), 9.9999997473787516355514526367188e-05);
    vec3 F0 = mix(vec3(0.039999999105930328369140625), albedo, vec3(metal));
    float param = NoV;
    float param_1 = s.roughness;
    vec2 ab = BrdfLutBilinear(param, param_1);
    vec3 Fss = (F0 * ab.x) + vec3(ab.y);
    vec3 kD = (vec3(1.0) - min(Fss, vec3(1.0))) * (1.0 - metal);
    float param_2 = s.roughness;
    vec3 R = normalize(mix(N, (N * (2.0 * dot(N, s.V))) - s.V, vec3(IblDominantDirFactor(param_2))));
    vec3 param_3 = N;
    vec3 param_4 = R;
    vec3 ambient = ((kD * albedo) * IblEvaluateIrradiance(param_3)) + (Fss * IblEvaluateIrradiance(param_4));
    vec3 color = ambient * ao;
    SurfaceData param_5 = s;
    vec3 param_6 = i.worldPos;
    vec2 param_7 = i.clip.xy;
    float param_8 = i.viewDepth;
    float param_9 = receive;
    color += EvaluateClusteredLights(param_5, param_6, param_7, param_8, param_9);
    color += i.emissive;
    if (i.fogParams.z > 0.0)
    {
        float d = length(i.toCam);
        float f = clamp((d - i.fogParams.x) / max(i.fogParams.y - i.fogParams.x, 0.001000000047497451305389404296875), 0.0, 1.0) * i.fogParams.z;
        color = mix(color, i.fogColor.xyz, vec3(f));
    }
    vec3 param_10 = color;
    return vec4(ToneMapACESFilm(param_10), i.baseColor.w);
}

void main()
{
    VSOut i;
    i.clip = gl_FragCoord;
    i.worldPos = i_worldPos;
    i.normal = i_normal;
    i.uv = i_uv;
    i.toCam = i_toCam;
    i.viewDepth = i_viewDepth;
    i.baseColor = i_baseColor;
    i.material = i_material;
    i.texInfo = i_texInfo;
    i.emissive = i_emissive;
    i.fogColor = i_fogColor;
    i.fogParams = i_fogParams;
    i.uvXform = i_uvXform;
    i.normalInfo = i_normalInfo;
    VSOut param = i;
    vec4 _2320 = _PSMain(param);
    _entryPointOutput = _2320;
}

