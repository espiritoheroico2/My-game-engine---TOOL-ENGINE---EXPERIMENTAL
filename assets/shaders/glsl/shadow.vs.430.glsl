#version 430
// Escrito a mao (nao ha spirv-cross no ambiente de desenvolvimento): equivale a shadow.hlsl::VSMain + o mesmo
// ajuste de clip (Y invertido) que tools/compile_shaders.py aplica aos outros shaders.

layout(binding = 0, std140) uniform ShadowBuffer
{
    layout(row_major) mat4 lightViewProj;
    layout(row_major) mat4 world;
} _23;

layout(location = 0) in vec3 pos;
layout(location = 0) out float vDepth;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main()
{
    vec4 clip = (vec4(pos, 1.0) * spvWorkaroundRowMajor(_23.world)) * spvWorkaroundRowMajor(_23.lightViewProj);
    vDepth = clip.z;
    gl_Position = clip;
    // z fica como esta: o contexto GL usa clip 0..1 (ZeroToOneNDZ em diligent_device.cpp), igual ao D3D/Vulkan.
    gl_Position.y = -gl_Position.y;
}
