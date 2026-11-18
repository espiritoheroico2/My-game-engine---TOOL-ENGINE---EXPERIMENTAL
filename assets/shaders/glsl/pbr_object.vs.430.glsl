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
} _22;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 cameraPos;
    float _pad0;
} _101;

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
layout(location = 9) flat out vec4 _entryPointOutput_fogColor;
layout(location = 10) flat out vec4 _entryPointOutput_fogParams;

mat4 spvWorkaroundRowMajor(mat4 wrap) { return wrap; }

void main_inner()
{
    vec4 _313 = vec4(i_pos, 1.0) * spvWorkaroundRowMajor(_22.world);
    gl_Position = _313 * spvWorkaroundRowMajor(_101.viewProj);
    _entryPointOutput_worldPos = _313.xyz;
    _entryPointOutput_normal = normalize(i_normal * mat3(spvWorkaroundRowMajor(_22.world)[0].xyz, spvWorkaroundRowMajor(_22.world)[1].xyz, spvWorkaroundRowMajor(_22.world)[2].xyz));
    _entryPointOutput_uv = i_uv;
    _entryPointOutput_toCam = _101.cameraPos - _313.xyz;
    _entryPointOutput_viewDepth = -(_313 * spvWorkaroundRowMajor(_101.view)).z;
    _entryPointOutput_baseColor = _22.baseColor;
    _entryPointOutput_material = vec4(_22.metallic, _22.roughness, _22.receiveShadow, 0.0);
    _entryPointOutput_texInfo = _22.texInfo;
    _entryPointOutput_emissive = _22.emissive.xyz;
    _entryPointOutput_fogColor = _22.fogColor;
    _entryPointOutput_fogParams = _22.fogParams;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
