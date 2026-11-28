#version 430

struct VSOut
{
    vec4 clip;
    vec2 uv;
    vec4 col;
    float depth;
};

struct QuadInstance
{
    vec3 center;
    float size;
    float rot;
    uint color;
    uint material;
    float temp;
};

layout(binding = 6, std140) uniform PartBuffer
{
    uvec4 flags;
    vec4 soft;
} _68;

layout(binding = 4, std140) uniform FrameBuffer
{
    layout(row_major) mat4 viewProj;
    layout(row_major) mat4 view;
    vec3 camPos;
    float pad0;
} _179;

layout(binding = 0, std430) readonly buffer Quads
{
    QuadInstance _data[];
} Quads_1;

uniform sampler2D GBuf;

layout(location = 0) in vec2 i_uv;
layout(location = 1) in vec4 i_col;
layout(location = 2) in float i_depth;
layout(location = 0) out vec4 _entryPointOutput;

float Shape(vec2 uv)
{
    float r2 = dot(uv, uv);
    float e = 0.049787066876888275146484375;
    return clamp((exp((-3.0) * r2) - e) / (1.0 - e), 0.0, 1.0) * step(r2, 1.0);
}

float SoftFade(vec4 clip, float depth)
{
    if ((_68.soft.x <= 0.0) || (_68.soft.z <= 0.0))
    {
        return 1.0;
    }
    float sceneD = texelFetch(GBuf, ivec3(ivec2(clip.xy), 0).xy, ivec3(ivec2(clip.xy), 0).z).w;
    if (sceneD <= 0.0)
    {
        return 1.0;
    }
    return clamp((sceneD - depth) / _68.soft.x, 0.0, 1.0);
}

vec3 SrgbToLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2000000476837158203125));
}

vec4 _PSAdditive(VSOut i)
{
    vec2 param = i.uv;
    vec4 param_1 = i.clip;
    float param_2 = i.depth;
    float a = (Shape(param) * i.col.w) * SoftFade(param_1, param_2);
    vec3 param_3 = i.col.xyz;
    return vec4(SrgbToLinear(param_3) * a, 0.0);
}

void main()
{
    VSOut i;
    i.clip = gl_FragCoord;
    i.uv = i_uv;
    i.col = i_col;
    i.depth = i_depth;
    VSOut param = i;
    _entryPointOutput = _PSAdditive(param);
}

