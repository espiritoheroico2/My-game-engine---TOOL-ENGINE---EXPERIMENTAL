#version 430

uniform sampler2D atlasTex;

layout(location = 0) in vec2 i_uv;
layout(location = 1) in vec4 i_tint;
layout(location = 2) in vec2 i_local;
layout(location = 3) in vec2 i_halfSize;
layout(location = 4) in float i_radius;
layout(location = 0) out vec4 _entryPointOutput;

void main()
{
    float _248;
    if (i_radius > 0.0)
    {
        vec2 _162 = abs(i_local) - (i_halfSize - vec2(i_radius));
        _248 = clamp(0.5 - ((length(max(_162, vec2(0.0))) + min(max(_162.x, _162.y), 0.0)) - i_radius), 0.0, 1.0);
    }
    else
    {
        _248 = 1.0;
    }
    vec4 _197 = texelFetch(atlasTex, ivec3(ivec2(floor(i_uv)), 0).xy, 0);
    vec3 _202 = _197.xyz * _197.w;
    vec4 _243 = _197;
    _243.x = _202.x;
    _243.y = _202.y;
    _243.z = _202.z;
    _entryPointOutput = (_243 * i_tint) * _248;
}

