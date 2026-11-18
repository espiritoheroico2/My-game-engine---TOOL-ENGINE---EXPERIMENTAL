#version 430
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

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

layout(binding = 17, std430) readonly buffer Instances
{
    layout(row_major) InstanceData _data[];
} Instances_1;

layout(binding = 6, std140) uniform BatchBuffer
{
    uvec4 batchInfo;
} _32;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 cameraPos;
    float _pad0;
} _140;

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

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    uint _335 = _32.batchInfo.x + uint((gl_InstanceID + SPIRV_Cross_BaseInstance));
    vec4 _383 = vec4(i_pos, 1.0) * Instances_1._data[_335].world;
    gl_Position = _383 * spvWorkaroundRowMajor(_140.viewProj);
    _entryPointOutput_worldPos = _383.xyz;
    _entryPointOutput_normal = normalize(i_normal * mat3(Instances_1._data[_335].world[0].xyz, Instances_1._data[_335].world[1].xyz, Instances_1._data[_335].world[2].xyz));
    _entryPointOutput_uv = i_uv;
    _entryPointOutput_toCam = _140.cameraPos - _383.xyz;
    _entryPointOutput_viewDepth = -(_383 * spvWorkaroundRowMajor(_140.view)).z;
    _entryPointOutput_baseColor = Instances_1._data[_335].baseColor;
    _entryPointOutput_material = vec4(Instances_1._data[_335].metallic, Instances_1._data[_335].roughness, Instances_1._data[_335].receiveShadow, 0.0);
    _entryPointOutput_texInfo = Instances_1._data[_335].texInfo;
    _entryPointOutput_emissive = Instances_1._data[_335].emissive.xyz;
    _entryPointOutput_fogColor = Instances_1._data[_335].fogColor;
    _entryPointOutput_fogParams = Instances_1._data[_335].fogParams;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
