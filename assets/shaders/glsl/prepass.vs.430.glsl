#version 430

layout(binding = 6, std140) uniform ObjectBuffer
{
    layout(row_major) mat4 world;
} _32;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
} _41;

layout(location = 0) in vec3 i_pos;
layout(location = 1) in vec3 i_normal;
layout(location = 0) out vec3 _entryPointOutput_nView;
layout(location = 1) out float _entryPointOutput_depthView;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    vec4 _130 = vec4(i_pos, 1.0) * spvWorkaroundRowMajor(_32.world);
    gl_Position = _130 * spvWorkaroundRowMajor(_41.viewProj);
    _entryPointOutput_nView = (i_normal * mat3(spvWorkaroundRowMajor(_32.world)[0].xyz, spvWorkaroundRowMajor(_32.world)[1].xyz, spvWorkaroundRowMajor(_32.world)[2].xyz)) * mat3(spvWorkaroundRowMajor(_41.view)[0].xyz, spvWorkaroundRowMajor(_41.view)[1].xyz, spvWorkaroundRowMajor(_41.view)[2].xyz);
    _entryPointOutput_depthView = -(_130 * spvWorkaroundRowMajor(_41.view)).z;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
