#version 430
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

struct ShadowInstance
{
    mat4 world;
};

layout(binding = 0, std430) readonly buffer Instances
{
    layout(row_major) ShadowInstance _data[];
} Instances_1;

layout(binding = 1, std140) uniform BatchBuffer
{
    uvec4 batchInfo;
} _31;

layout(binding = 0, std140) uniform ShadowBuffer
{
    layout(row_major) mat4 lightViewProj;
} _53;

layout(location = 0) in vec3 pos;
#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseInstance gl_BaseInstanceARB
#else
uniform int SPIRV_Cross_BaseInstance;
#endif
layout(location = 0) out float _entryPointOutput_depth;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    vec4 _109 = (vec4(pos, 1.0) * Instances_1._data[_31.batchInfo.x + uint((gl_InstanceID + SPIRV_Cross_BaseInstance))].world) * spvWorkaroundRowMajor(_53.lightViewProj);
    gl_Position = _109;
    _entryPointOutput_depth = _109.w;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
