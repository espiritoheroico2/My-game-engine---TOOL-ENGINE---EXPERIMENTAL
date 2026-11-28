#version 430

struct VSIn
{
    vec2 pos;
    vec2 uv;
    vec4 tint;
    vec4 shape;
    vec2 mode;
};

struct VSOut
{
    vec4 clip;
    vec2 uv;
    vec4 tint;
    vec4 shape;
    vec2 mode;
};

layout(binding = 0, std140) uniform EditorUiParams
{
    vec2 viewportSize;
    vec2 padding;
} _27;

layout(location = 0) in vec2 i_pos;
layout(location = 1) in vec2 i_uv;
layout(location = 2) in vec4 i_tint;
layout(location = 3) in vec4 i_shape;
layout(location = 4) in vec2 i_mode;
layout(location = 0) out vec2 _entryPointOutput_uv;
layout(location = 1) out vec4 _entryPointOutput_tint;
layout(location = 2) out vec4 _entryPointOutput_shape;
layout(location = 3) out vec2 _entryPointOutput_mode;

VSOut _VSMain(VSIn i)
{
    vec2 ndc = vec2(((i.pos.x / _27.viewportSize.x) * 2.0) - 1.0, 1.0 - ((i.pos.y / _27.viewportSize.y) * 2.0));
    VSOut o;
    o.clip = vec4(ndc, 0.0, 1.0);
    o.uv = i.uv;
    o.tint = i.tint;
    o.shape = i.shape;
    o.mode = i.mode;
    return o;
}

void main_inner()
{
    VSIn i;
    i.pos = i_pos;
    i.uv = i_uv;
    i.tint = i_tint;
    i.shape = i_shape;
    i.mode = i_mode;
    VSIn param = i;
    VSOut flattenTemp = _VSMain(param);
    gl_Position = flattenTemp.clip;
    _entryPointOutput_uv = flattenTemp.uv;
    _entryPointOutput_tint = flattenTemp.tint;
    _entryPointOutput_shape = flattenTemp.shape;
    _entryPointOutput_mode = flattenTemp.mode;
}


void main() { main_inner(); gl_Position.y = -gl_Position.y; }
