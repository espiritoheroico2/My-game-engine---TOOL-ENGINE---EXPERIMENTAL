#version 430

layout(binding = 0, std140) uniform SpriteUiParams
{
    vec2 viewportSize;
    vec2 atlasSize;
} _27;

layout(location = 0) in vec2 i_pos;
layout(location = 1) in vec2 i_uv;
layout(location = 2) in vec4 i_tint;
layout(location = 3) in vec2 i_local;
layout(location = 4) in vec2 i_halfSize;
layout(location = 5) in float i_radius;
layout(location = 0) out vec2 _entryPointOutput_uv;
layout(location = 1) out vec4 _entryPointOutput_tint;
layout(location = 2) out vec2 _entryPointOutput_local;
layout(location = 3) out vec2 _entryPointOutput_halfSize;
layout(location = 4) out float _entryPointOutput_radius;

void main_inner()
{
    gl_Position = vec4(((i_pos.x / _27.viewportSize.x) * 2.0) - 1.0, 1.0 - ((i_pos.y / _27.viewportSize.y) * 2.0), 0.0, 1.0);
    _entryPointOutput_uv = i_uv;
    _entryPointOutput_tint = i_tint;
    _entryPointOutput_local = i_local;
    _entryPointOutput_halfSize = i_halfSize;
    _entryPointOutput_radius = i_radius;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
