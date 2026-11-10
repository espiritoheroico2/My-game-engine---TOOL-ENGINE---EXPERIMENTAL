#version 430

layout(binding = 6, std140) uniform ObjectBuffer
{
    layout(row_major) mat4 world;
    vec4 baseColor;
    float metallic;
    float roughness;
    float receiveShadow;
    float _pad1;
    vec4 texInfo;
    vec4 emissive;
    vec4 fogColor;
    vec4 fogParams;
} _32;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 cameraPos;
    float _pad0;
} _67;

layout(location = 0) in vec3 i_pos;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;
layout(location = 0) out vec3 _entryPointOutput_worldPos;
layout(location = 1) out vec3 _entryPointOutput_normal;
layout(location = 2) out vec2 _entryPointOutput_uv;
layout(location = 3) out vec3 _entryPointOutput_toCam;
layout(location = 4) out float _entryPointOutput_viewDepth;
layout(location = 5) flat out vec4 _entryPointOutput_baseColor;
layout(location = 6) flat out vec4 _entryPointOutput_material;
layout(location = 7) flat out vec4 _entryPointOutput_texInfo;
layout(location = 8) flat out vec3 _entryPointOutput_emissive;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    vec4 _255 = vec4(i_pos, 1.0) * spvWorkaroundRowMajor(_32.world);
    gl_Position = _255 * spvWorkaroundRowMajor(_67.viewProj);
    _entryPointOutput_worldPos = _255.xyz;
    _entryPointOutput_normal = normalize(i_normal * mat3(spvWorkaroundRowMajor(_32.world)[0].xyz, spvWorkaroundRowMajor(_32.world)[1].xyz, spvWorkaroundRowMajor(_32.world)[2].xyz));
    _entryPointOutput_uv = i_uv;
    _entryPointOutput_toCam = _67.cameraPos - _255.xyz;
    _entryPointOutput_viewDepth = -(_255 * spvWorkaroundRowMajor(_67.view)).z;
    _entryPointOutput_baseColor = _32.baseColor;
    _entryPointOutput_material = vec4(_32.metallic, _32.roughness, _32.receiveShadow, 0.0);
    _entryPointOutput_texInfo = _32.texInfo;
    _entryPointOutput_emissive = _32.emissive.xyz;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
