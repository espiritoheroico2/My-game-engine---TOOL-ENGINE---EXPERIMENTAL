#version 430

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
} _113;

layout(binding = 0, std140) uniform GlobalLightCB
{
    GlobalLighting Globals;
} _117;

layout(binding = 2, std140) uniform CsmCB
{
    CsmData Csm;
} _125;

layout(binding = 7, std140) uniform PointShadowCB
{
    uint pointShadowCount;
    uint _pointShadowPad0;
    uint _pointShadowPad1;
    uint _pointShadowPad2;
    layout(row_major) PointShadowLight PointShadows[4];
} _138;

layout(binding = 5, std140) uniform ClusterGridCB
{
    ClusterGrid ClusterGridData;
} _146;

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
} _161;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 cameraPos;
    float _pad0;
} _171;

layout(binding = 17, std430) readonly buffer Instances
{
    layout(row_major) InstanceData _data[];
} Instances_1;

layout(binding = 6, std140) uniform BatchBuffer
{
    uvec4 batchInfo;
} _180;

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

vec4 _EmissionPS(VSOut i)
{
    return vec4(i.emissive, 1.0);
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
    _entryPointOutput = _EmissionPS(param);
}

