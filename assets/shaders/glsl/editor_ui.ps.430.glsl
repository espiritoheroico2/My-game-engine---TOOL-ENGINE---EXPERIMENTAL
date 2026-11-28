#version 430

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
} _276;

uniform sampler2D uiTex;

layout(location = 0) in vec2 i_uv;
layout(location = 1) in vec4 i_tint;
layout(location = 2) in vec4 i_shape;
layout(location = 3) in vec2 i_mode;
layout(location = 0) out vec4 _entryPointOutput;

vec4 _PSMain(VSOut i)
{
    float cov = 1.0;
    if (i.mode.x > 0.0)
    {
        vec2 q = abs(i.shape.xy) - (i.shape.zw - vec2(i.mode.x));
        float d = (length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0)) - i.mode.x;
        cov = clamp(0.5 - d, 0.0, 1.0);
    }
    uvec2 sizeQueryTemp = uvec2(textureSize(uiTex, 0));
    uint tw = sizeQueryTemp.x;
    uint th = sizeQueryTemp.y;
    ivec2 mx = ivec2(int(tw) - 1, int(th) - 1);
    vec4 t;
    if (i.mode.y > 0.5)
    {
        vec2 p = i.uv - vec2(0.5);
        vec2 f = fract(p);
        ivec2 b = ivec2(floor(p));
        vec4 t00 = texelFetch(uiTex, ivec3(clamp(b, ivec2(0), mx), 0).xy, ivec3(clamp(b, ivec2(0), mx), 0).z);
        vec4 t10 = texelFetch(uiTex, ivec3(clamp(b + ivec2(1, 0), ivec2(0), mx), 0).xy, ivec3(clamp(b + ivec2(1, 0), ivec2(0), mx), 0).z);
        vec4 t01 = texelFetch(uiTex, ivec3(clamp(b + ivec2(0, 1), ivec2(0), mx), 0).xy, ivec3(clamp(b + ivec2(0, 1), ivec2(0), mx), 0).z);
        vec4 t11 = texelFetch(uiTex, ivec3(clamp(b + ivec2(1), ivec2(0), mx), 0).xy, ivec3(clamp(b + ivec2(1), ivec2(0), mx), 0).z);
        t = mix(mix(t00, t10, vec4(f.x)), mix(t01, t11, vec4(f.x)), vec4(f.y));
    }
    else
    {
        t = texelFetch(uiTex, ivec3(clamp(ivec2(floor(i.uv)), ivec2(0), mx), 0).xy, ivec3(clamp(ivec2(floor(i.uv)), ivec2(0), mx), 0).z);
    }
    float _231 = t.w;
    vec4 _233 = t;
    vec3 _235 = _233.xyz * _231;
    t.x = _235.x;
    t.y = _235.y;
    t.z = _235.z;
    return (t * i.tint) * cov;
}

void main()
{
    VSOut i;
    i.clip = gl_FragCoord;
    i.uv = i_uv;
    i.tint = i_tint;
    i.shape = i_shape;
    i.mode = i_mode;
    VSOut param = i;
    _entryPointOutput = _PSMain(param);
}

