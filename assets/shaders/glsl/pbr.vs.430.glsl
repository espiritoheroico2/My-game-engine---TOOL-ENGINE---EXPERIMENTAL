#version 430

struct VSIn
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

struct VSOut
{
    vec4 clip;
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
};

struct GpuLight
{
    vec3 position;
    float range;
    vec3 color;
    float intensity;
    vec3 direction;
    float spotCos;
    uint type;
    uint shadowIndex;
    float spotInnerCos;
    uint pad;
};

layout(binding = 2, std140) uniform ObjectBuffer
{
    layout(row_major) mat4 world;
    vec4 baseColor;
    float metallic;
    float roughness;
    float receiveShadow;
    float _pad1;
} _32;

layout(binding = 0, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    vec3 cameraPos;
    float _pad0;
} _67;

layout(binding = 1, std140) uniform LightBuffer
{
    GpuLight lights[64];
    vec4 ambient;
    uint lightCount;
    layout(row_major) mat4 lightViewProj;
    vec4 shadowParams;
    vec4 shadowInfo;
} _113;

layout(location = 0) in vec3 i_pos;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;
layout(location = 0) out vec3 _entryPointOutput_worldPos;
layout(location = 1) out vec3 _entryPointOutput_normal;
layout(location = 2) out vec2 _entryPointOutput_uv;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

VSOut _VSMain(VSIn i)
{
    vec4 wp = vec4(i.pos, 1.0) * spvWorkaroundRowMajor(_32.world);
    VSOut o;
    o.worldPos = wp.xyz;
    o.normal = normalize(i.normal * mat3(spvWorkaroundRowMajor(_32.world)[0].xyz, spvWorkaroundRowMajor(_32.world)[1].xyz, spvWorkaroundRowMajor(_32.world)[2].xyz));
    o.uv = i.uv;
    o.clip = wp * spvWorkaroundRowMajor(_67.viewProj);
    return o;
}

void main()
{
    VSIn i;
    i.pos = i_pos;
    i.normal = i_normal;
    i.uv = i_uv;
    VSIn param = i;
    VSOut flattenTemp = _VSMain(param);
    gl_Position = flattenTemp.clip;
    _entryPointOutput_worldPos = flattenTemp.worldPos;
    _entryPointOutput_normal = flattenTemp.normal;
    _entryPointOutput_uv = flattenTemp.uv;
    // z fica como esta: o contexto GL usa clip 0..1 (ZeroToOneNDZ em diligent_device.cpp), igual ao D3D/Vulkan.
    gl_Position.y = -gl_Position.y;
}

