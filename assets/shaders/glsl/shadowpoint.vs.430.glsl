#version 430

layout(binding = 0, std140) uniform ShadowBuffer
{
    layout(row_major) mat4 lightViewProj;
    layout(row_major) mat4 world;
} _28;

layout(location = 0) in vec3 pos;
layout(location = 0) out float _entryPointOutput_depth;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    vec4 _77 = (vec4(pos, 1.0) * spvWorkaroundRowMajor(_28.world)) * spvWorkaroundRowMajor(_28.lightViewProj);
    gl_Position = _77;
    _entryPointOutput_depth = _77.w;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
