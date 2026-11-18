#version 430
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

struct InstanceData
{
    mat4 world;
};

layout(binding = 0, std430) readonly buffer Instances
{
    layout(row_major) InstanceData _data[];
} Instances_1;

layout(binding = 6, std140) uniform BatchBuffer
{
    uvec4 batchInfo;
} _31;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
} _60;

layout(location = 0) in vec3 i_pos;
layout(location = 1) in vec3 i_normal;
#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseInstance gl_BaseInstanceARB
#else
uniform int SPIRV_Cross_BaseInstance;
#endif
layout(location = 0) out vec3 _entryPointOutput_nView;
layout(location = 1) out float _entryPointOutput_depthView;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    uint _148 = _31.batchInfo.x + uint((gl_InstanceID + SPIRV_Cross_BaseInstance));
    vec4 _158 = vec4(i_pos, 1.0) * Instances_1._data[_148].world;
    gl_Position = _158 * spvWorkaroundRowMajor(_60.viewProj);
    _entryPointOutput_nView = (i_normal * mat3(Instances_1._data[_148].world[0].xyz, Instances_1._data[_148].world[1].xyz, Instances_1._data[_148].world[2].xyz)) * mat3(spvWorkaroundRowMajor(_60.view)[0].xyz, spvWorkaroundRowMajor(_60.view)[1].xyz, spvWorkaroundRowMajor(_60.view)[2].xyz);
    _entryPointOutput_depthView = -(_158 * spvWorkaroundRowMajor(_60.view)).z;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
