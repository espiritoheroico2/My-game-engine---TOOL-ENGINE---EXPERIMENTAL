#version 430
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

struct VSIn
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
    uint iid;
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

struct GlobalLighting
{
    uint numPointLights;
    uint numSpotLights;
    uint hasSun;
    uint qualityFlags;
    vec3 cameraPosition;
    float preExposure;
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

layout(binding = 17, std430) readonly buffer Instances
{
    layout(row_major) InstanceData _data[];
} Instances_1;

layout(binding = 6, std140) uniform BatchBuffer
{
    uvec4 batchInfo;
} _61;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 cameraPos;
    float _pad0;
} _179;

layout(binding = 1, std430) readonly buffer PointLightsBuffer
{
    PointLight _data[];
} PointLightsBuffer_1;

layout(binding = 2, std430) readonly buffer SpotLightsBuffer
{
    SpotLight _data[];
} SpotLightsBuffer_1;

layout(binding = 1, std140) uniform SunLightCB
{
    DirectionalLight SunLight;
} _313;

layout(binding = 0, std140) uniform GlobalLightCB
{
    GlobalLighting Globals;
} _317;

layout(binding = 2, std140) uniform CsmCB
{
    CsmData Csm;
} _324;

layout(binding = 7, std140) uniform PointShadowCB
{
    uint pointShadowCount;
    uint _pointShadowPad0;
    uint _pointShadowPad1;
    uint _pointShadowPad2;
    layout(row_major) PointShadowLight PointShadows[4];
} _337;

layout(binding = 5, std140) uniform ClusterGridCB
{
    ClusterGrid ClusterGridData;
} _345;

layout(binding = 8, std430) readonly buffer LightGrid
{
    ClusterCell _data[];
} LightGrid_1;

layout(binding = 9, std430) readonly buffer LightIndexList
{
    uint _data[];
} LightIndexList_1;

layout(binding = 3, std140) uniform IblCB
{
    IblData Ibl;
} _360;

layout(location = 0) in vec3 i_pos;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;
#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseInstance gl_BaseInstanceARB
#else
uniform int SPIRV_Cross_BaseInstance;
#endif
layout(location = 0) out vec3 _entryPointOutput_worldPos;
layout(location = 1) out vec3 _entryPointOutput_normal;
layout(location = 2) out vec2 _entryPointOutput_uv;
layout(location = 3) out vec3 _entryPointOutput_toCam;
layout(location = 4) out float _entryPointOutput_viewDepth;
layout(location = 5) flat out vec4 _entryPointOutput_baseColor;
layout(location = 6) flat out vec4 _entryPointOutput_material;
layout(location = 7) flat out vec4 _entryPointOutput_texInfo;
layout(location = 8) flat out vec3 _entryPointOutput_emissive;
layout(location = 9) flat out vec4 _entryPointOutput_fogColor;
layout(location = 10) flat out vec4 _entryPointOutput_fogParams;
layout(location = 11) flat out vec4 _entryPointOutput_uvXform;
layout(location = 12) flat out vec2 _entryPointOutput_normalInfo;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

vec2 UnpackNormalCutoff(float _packed)
{
    float v = floor(_packed + 0.5);
    float sq = floor(v / 1024.0);
    float cq = v - (sq * 1024.0);
    return vec2(sq * 0.00195503421127796173095703125, cq * 0.000977517105638980865478515625);
}

VSOut _VSMain(VSIn i)
{
    uint _70 = _61.batchInfo.x + i.iid;
    InstanceData d;
    d.world = Instances_1._data[_70].world;
    d.baseColor = Instances_1._data[_70].baseColor;
    d.metallic = Instances_1._data[_70].metallic;
    d.roughness = Instances_1._data[_70].roughness;
    d.receiveShadow = Instances_1._data[_70].receiveShadow;
    d._pad1 = Instances_1._data[_70]._pad1;
    d.texInfo = Instances_1._data[_70].texInfo;
    d.emissive = Instances_1._data[_70].emissive;
    d.fogColor = Instances_1._data[_70].fogColor;
    d.fogParams = Instances_1._data[_70].fogParams;
    mat4 instanceWorld = d.world;
    vec4 instanceBaseColor = d.baseColor;
    float instanceMetallic = d.metallic;
    float instanceRoughness = d.roughness;
    float instanceReceiveShadow = d.receiveShadow;
    vec4 instanceTexInfo = d.texInfo;
    vec4 instanceEmissive = d.emissive;
    vec4 instanceFogColor = d.fogColor;
    vec4 instanceFogParams = d.fogParams;
    vec4 instanceUvXform = vec4(d._pad1, d.texInfo.w, d.emissive.w, d.fogColor.w);
    vec4 wp = vec4(i.pos, 1.0) * instanceWorld;
    VSOut o;
    o.worldPos = wp.xyz;
    o.normal = normalize(i.normal * mat3(instanceWorld[0].xyz, instanceWorld[1].xyz, instanceWorld[2].xyz));
    o.uv = i.uv;
    o.clip = wp * spvWorkaroundRowMajor(_179.viewProj);
    o.toCam = _179.cameraPos - wp.xyz;
    o.viewDepth = -(wp * spvWorkaroundRowMajor(_179.view)).z;
    o.baseColor = instanceBaseColor;
    o.material = vec4(instanceMetallic, instanceRoughness, instanceReceiveShadow, 0.0);
    o.texInfo = instanceTexInfo;
    o.emissive = instanceEmissive.xyz;
    o.fogColor = instanceFogColor;
    o.fogParams = instanceFogParams;
    o.uvXform = instanceUvXform;
    float param = instanceFogParams.w;
    o.normalInfo = UnpackNormalCutoff(param);
    return o;
}

void main_inner()
{
    VSIn i;
    i.pos = i_pos;
    i.normal = i_normal;
    i.uv = i_uv;
    i.iid = uint((gl_InstanceID + SPIRV_Cross_BaseInstance));
    VSIn param = i;
    VSOut flattenTemp = _VSMain(param);
    gl_Position = flattenTemp.clip;
    _entryPointOutput_worldPos = flattenTemp.worldPos;
    _entryPointOutput_normal = flattenTemp.normal;
    _entryPointOutput_uv = flattenTemp.uv;
    _entryPointOutput_toCam = flattenTemp.toCam;
    _entryPointOutput_viewDepth = flattenTemp.viewDepth;
    _entryPointOutput_baseColor = flattenTemp.baseColor;
    _entryPointOutput_material = flattenTemp.material;
    _entryPointOutput_texInfo = flattenTemp.texInfo;
    _entryPointOutput_emissive = flattenTemp.emissive;
    _entryPointOutput_fogColor = flattenTemp.fogColor;
    _entryPointOutput_fogParams = flattenTemp.fogParams;
    _entryPointOutput_uvXform = flattenTemp.uvXform;
    _entryPointOutput_normalInfo = flattenTemp.normalInfo;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
